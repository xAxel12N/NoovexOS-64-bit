#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
static u8 eeprom[128]; static int slave_present=1, stretch=0;
static u32 reg_val=7; static int m_scl=1, m_sda=1, s_sda=1, prev_scl=1, prev_sda=1;
static int st=0, bits=0, rw=0, ptr=0, stretch_left=0, bad=0; static u8 sh=0;
static int line_scl(void){ return m_scl&&stretch_left==0; }
static int line_sda(void){ return m_sda&&s_sda; }
static void edge(void){
    int scl=line_scl(), sda=line_sda();
    if(prev_scl&&scl&&prev_sda&&!sda){ st=1; bits=0; sh=0; s_sda=1; }
    else if(prev_scl&&scl&&!prev_sda&&sda){ st=0; s_sda=1; }
    else if(!prev_scl&&scl){
        if(st==1||st==3){ sh=(u8)((sh<<1)|sda); bits++; }
        else if(st==5){ if(sda){ st=0; s_sda=1; } else st=6; }
    }
    else if(prev_scl&&!scl){
        if(st==1&&bits==8){ if(slave_present&&(sh&0xFE)==0xA0){ rw=sh&1; s_sda=0; st=2; } else { st=0; } }
        else if(st==3&&bits==8){ ptr=sh&0x7F; s_sda=0; st=4; }
        else if(st==2||st==4){ s_sda=1; if(st==2&&rw){ st=7; bits=0; } else { st=3; bits=0; sh=0; } }
        else if(st==6){ st=7; bits=0; }
        if(st==7){ s_sda=(eeprom[ptr]>>(7-bits))&1; bits++; if(bits==8){ st=8; } }
        else if(st==8){ s_sda=1; ptr=(ptr+1)&0x7F; st=5; }
        if(stretch&&prev_scl&&!scl)stretch_left=40;
    }
    prev_scl=scl; prev_sda=sda;
}
static u32 port_addr=0xD014+4*0x20; static int writes_elsewhere=0;
static u32 nvr32(u32 r){ if(r==port_addr){ if(stretch_left>0){ stretch_left--; if(!stretch_left)edge(); } u32 v=reg_val&3; v|=(u32)line_scl()<<4; v|=(u32)line_sda()<<5; return v; } return 0xFFFFFFFFu; }
static void nvw32(u32 r,u32 v){ if(r!=port_addr){ writes_elsewhere++; return; } reg_val=v; m_scl=v&1; m_sda=(v>>1)&1; edge(); }
static void nv_udelay(u32 us){ (void)us; }
#include "NVMON_C"
static int ok=1; static void chk(const char* w,int c){ printf("  %-64s %s\n",w,c?"ok":"FAIL"); if(!c)ok=0; }
static void mk_edid(void){ memset(eeprom,0,128); static const u8 h[8]={0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0}; memcpy(eeprom,h,8);
    u16 mf=(u16)((8<<10)|(23<<5)|16); eeprom[8]=(u8)(mf>>8); eeprom[9]=(u8)mf; eeprom[10]=0x47; eeprom[11]=0x32;
    u8* d=eeprom+54; d[0]=0x02; d[1]=0x3A; d[2]=0x80; d[3]=0x18; d[4]=0x71; d[5]=0x38; d[6]=0x2D; d[7]=0x40;
    u8* n=eeprom+72; n[3]=0xFC; memcpy(n+5,"HP E232\n    ",13);
    u8 s=0; for(int i=0;i<127;i++)s=(u8)(s+eeprom[i]); eeprom[127]=(u8)(0-s); }
int main(void){
    static u8 v[0x400]; memset(v,0,sizeof v); v[0]=0x55; v[1]=0xAA; v[0x36]=0x00; v[0x37]=0x01;
    u8* dcb=v+0x100; dcb[0]=0x40; dcb[1]=0x1B; dcb[2]=4; dcb[3]=8; dcb[6]=0xCB; dcb[7]=0xBD; dcb[8]=0xDC; dcb[9]=0x4E; dcb[4]=0x00; dcb[5]=0x02; dcb[0x14]=0x00; dcb[0x15]=0x03;
    u32 ents[4]={ 2u|(1u<<4)|(0xFu<<8)|(0u<<12)|(1u<<24), 6u|(2u<<4)|(0xFu<<8)|(1u<<12)|(2u<<24), 2u|(3u<<4)|(0xFu<<8)|(2u<<12)|(4u<<24), 0xEu };
    for(int i=0;i<4;i++) memcpy(dcb+0x1B+i*8,&ents[i],4);
    u8* it=v+0x200; it[0]=0x41; it[1]=5; it[2]=4; it[3]=4; u32 ie[4]={0x1F,4,7,2}; for(int i=0;i<4;i++) memcpy(it+5+i*4,&ie[i],4);
    u8* ct=v+0x300; ct[0]=0x40; ct[1]=5; ct[2]=3; ct[3]=4; ct[5]=0x60; ct[9]=0x46; ct[13]=0x31;
    printf("== VBIOS output table (DCB + I2C + connector tables) ==\n");
    nvm_out_t o[8]; int n=nvm_outputs(v,sizeof v,0x100,o,8);
    chk("3 outputs, stops at the end-of-list entry", n==3);
    chk("output 0: HDMI socket, TMDS, encoder 0, DDC index 1 -> port 4", n>0&&o[0].conntype==0x60&&o[0].type==2&&o[0].sor==0&&o[0].port==4);
    chk("output 1: DisplayPort socket, DP signal, DDC index 2 -> port 7", n>1&&o[1].conntype==0x46&&o[1].type==6&&o[1].port==7);
    chk("output 2: DVI-D socket", n>2&&o[2].conntype==0x31);
    chk("encoder masks 1,2,4 decode to encoders 0,1,2", n>2&&o[0].sor==0&&o[1].sor==1&&o[2].sor==2);
    chk("connector names (0x61 is full-size HDMI, 0x63 mini HDMI)", !strcmp(nvm_conn(0x60),"HDMI")&&!strcmp(nvm_conn(0x61),"HDMI")&&!strcmp(nvm_conn(0x63),"mini HDMI")&&!strcmp(nvm_conn(0x46),"DisplayPort"));
    chk("DCB encoder field is a mask: 1 -> encoder 0, 2 -> encoder 1", nvm_sor_index(1)==0&&nvm_sor_index(2)==1&&nvm_sor_index(4)==2&&nvm_sor_index(0)==-1);
    chk("an unused DDC entry maps to 'no port'", nvm_i2c_port(v,sizeof v,0x100,0)==-1);
    printf("== EDID decoding ==\n");
    mk_edid(); nvm_edid_t m; int r=nvm_edid_parse(eeprom,&m);
    chk("valid EDID accepted", r==0&&m.ok);
    chk("maker HWP, name 'HP E232'", !strcmp(m.maker,"HWP")&&!strcmp(m.name,"HP E232"));
    chk("native mode 1920x1080 @ 60 Hz (148.5 MHz)", m.hact==1920&&m.vact==1080&&m.hz==60&&m.pclk_khz==148500);
    eeprom[20]^=1; chk("corrupted EDID rejected (checksum)", nvm_edid_parse(eeprom,&m)!=0); eeprom[20]^=1;
    printf("== display engine state decoding ==\n");
    nvm_head_t hd; nvm_decode_head((1125u<<16)|2200u,0,(41u<<16)|192u,(1121u<<16)|2112u,148500000u,&hd);
    chk("head timings -> 1920x1080 @ 60 Hz, active", hd.active&&hd.hact==1920&&hd.vact==1080&&hd.hz==60);
    nvm_decode_head(0,0,0,0,0,&hd); chk("an unused head reads as inactive", !hd.active);
    nvm_decode_head(0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,&hd); chk("an unreadable engine (all 1s) reads as inactive", !hd.active);
    printf("== bit-banged DDC/I2C against a simulated monitor EEPROM ==\n");
    u8 e[128]; memset(e,0,128); r=nvm_read_edid(4,e);
    chk("EDID read over I2C succeeds", r==1);
    chk("all 128 bytes byte-exact", !memcmp(e,eeprom,128));
    chk("only the DDC port register was written", writes_elsewhere==0);
    chk("bus released afterwards (SCL and SDA high)", line_scl()&&line_sda()&&st==0);
    stretch=1; memset(e,0,128); r=nvm_read_edid(4,e); stretch=0;
    chk("works when the monitor stretches the clock", r==1&&!memcmp(e,eeprom,128));
    slave_present=0; r=nvm_read_edid(4,e); slave_present=1;
    chk("no monitor connected -> clean failure", r==0&&line_scl()&&line_sda());
    printf("%s\n",ok?"ALL PASS":"FAILURES"); return !ok; }
