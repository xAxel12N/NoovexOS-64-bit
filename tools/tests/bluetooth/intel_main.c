#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#define INTELBT_HOST
#include "drivers/bluetooth/firmware/intelbt_fw.c"
static int fails, checks;
#define CHECK(c,msg) do{ checks++; if(c)printf("  %-60s ok\n",msg); else { fails++; printf("  %-60s FAIL (line %d)\n",msg,__LINE__); } }while(0)
static int put32(u8* p,u32 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24); return 4; }
static int tlv(u8* p,int t,int l,const u8* v){ p[0]=(u8)t; p[1]=(u8)l; memcpy(p+2,v,(size_t)l); return l+2; }
static int tlv32(u8* p,int t,u32 v){ u8 b[4]; put32(b,v); return tlv(p,t,4,b); }
static int tlv8(u8* p,int t,u8 v){ return tlv(p,t,1,&v); }
typedef struct { int n; u8 type[4096]; int off[4096]; int len[4096]; const u8* base; } fr_t;
static int rec(u8 type,const u8* d,int n,void* ctx){ fr_t* f=ctx; if(f->n>=4096)return -1; f->type[f->n]=type; f->off[f->n]=(int)(d-f->base); f->len[f->n]=n; f->n++; return 0; }
static void tlv_tests(void){
    u8 r[255]; int n=0; static const u8 bd[6]={1,2,3,4,5,6}, odd[3]={9,9,9};
    r[n++]=0;
    n+=tlv32(r+n,0x10,0x00400410); n+=tlv32(r+n,0x11,0x00400410); n+=tlv32(r+n,0x12,0x00173700); n+=tlv32(r+n,0x13,0x02001700);
    { u8 b[2]={0x34,0x12}; n+=tlv(r+n,0x16,2,b); }
    n+=tlv8(r+n,0x1C,0x01); { u8 b[2]={0x25,0x14}; n+=tlv(r+n,0x1D,2,b); } n+=tlv8(r+n,0x1E,0x01); n+=tlv32(r+n,0x1F,0x00004321);
    n+=tlv8(r+n,0x28,1); n+=tlv8(r+n,0x2E,0); n+=tlv8(r+n,0x2F,1); n+=tlv(r+n,0x99,3,odd); n+=tlv(r+n,0x30,6,bd);
    intelbt_tlv_t v;
    CHECK(intelbt_parse_tlv(r,n,&v)==0,"synthetic AX210-style TLV version reply parses");
    CHECK(v.cnvi_top==0x00400410&&v.cnvr_top==0x00400410&&v.cnvi_bt==0x00173700&&v.cnvr_bt==0x02001700,"CNVi/CNVr top and BT words");
    CHECK(v.dev_rev_id==0x1234&&v.img_type==1&&v.timestamp==0x1425&&v.build_type==1&&v.build_num==0x4321,"rev id, image type, timestamp, build");
    CHECK(v.secure_boot==1&&v.limited_cce==0&&v.sbe_type==1&&!memcmp(v.bdaddr,bd,6),"secure boot, CCE, SBE type, OTP address; unknown type skipped");
    CHECK(intelbt_identify(r,n,&v)==INTELBT_9260,"variant 0x17 identifies as the 9260/TLV family");
    char nm[64]; intelbt_name_tlv(&v,"sfi",nm,(int)sizeof nm);
    CHECK(!strcmp(nm,"ibt-0041-0041.sfi"),"TLV firmware name ibt-0041-0041.sfi");
    v.cnvi_top=0x0A0001F2; intelbt_name_tlv(&v,"ddc",nm,(int)sizeof nm);
    CHECK(!strcmp(nm,"ibt-2a1f-0041.ddc"),"CNVx top packing (step/type bits) and .ddc suffix");
    CHECK(intelbt_parse_tlv(r,n-3,&v)<0,"truncated TLV is rejected");
    r[0]=1; CHECK(intelbt_parse_tlv(r,n,&v)<0,"non-zero status is rejected"); r[0]=0;
    { u8 b[8]={0,0x10,2,0x10,0x04,0,0,0}; CHECK(intelbt_parse_tlv(b,6,&v)<0,"wrong TLV length for a 32-bit field is rejected"); }
    { u8 b[32]; int m=0; b[m++]=0; m+=tlv32(b+m,0x12,0x00123700); CHECK(intelbt_identify(b,m,&v)==INTELBT_8260,"TLV variant 0x12 identifies as the 8260 family"); }
    { u8 b[32]; int m=0; b[m++]=0; m+=tlv32(b+m,0x12,0x00173800); CHECK(intelbt_identify(b,m,&v)==0,"TLV hw_platform other than 0x37 is not supported"); }
    { u8 b[32]; int m=0; b[m++]=0; m+=tlv32(b+m,0x12,0x00173700); CHECK(m!=10&&intelbt_identify(b,m,&v)==INTELBT_9260,"short TLV reply is not mistaken for the legacy layout"); }
}
static void legacy_tests(void){
    char nm[64]; intelbt_tlv_t v;
    u8 a[10]={0,0x37,0x0b,0x01,0x06,0x02,0x10,0x30,0x15,0x00};
    CHECK(intelbt_identify(a,10,&v)==INTELBT_8260,"legacy reply, variant 0x0b -> 8260");
    intelbt_name_legacy(a,16,"sfi",0,nm,(int)sizeof nm); CHECK(!strcmp(nm,"ibt-11-16.sfi"),"8260/8265 name ibt-11-16.sfi");
    a[2]=0x12; a[3]=0x10; a[5]=0x02;
    intelbt_name_legacy(a,0,"sfi",0,nm,(int)sizeof nm); CHECK(!strcmp(nm,"ibt-18-16-2.sfi"),"9260/9560 name ibt-18-16-2.sfi");
    u8 b[10]={0,0x37,0x08,0x10,0x01,0x80,0x01,0x2d,0x0d,0x00};
    CHECK(intelbt_identify(b,10,&v)==INTELBT_7260,"legacy reply, variant 0x08 -> 7260");
    intelbt_name_legacy(b,0,"bseq",0,nm,(int)sizeof nm); CHECK(!strcmp(nm,"ibt-hw-37.8.10-fw-1.80.1.2d.d.bseq"),"7265 patch name");
    intelbt_name_legacy(b,0,"bseq",1,nm,(int)sizeof nm); CHECK(!strcmp(nm,"ibt-hw-37.8.bseq"),"7265 fallback patch name");
    b[2]=0x20; CHECK(intelbt_identify(b,10,&v)==0&&intelbt_name_legacy(b,0,"sfi",0,nm,(int)sizeof nm)==0,"unknown legacy variant is refused");
    CHECK(intelbt_match(0x8087,0x0032)==INTELBT_9260&&intelbt_match(0x8087,0x0aa7)==INTELBT_7260&&intelbt_match(0x8087,0x0029)==INTELBT_8260,"Intel USB IDs match");
    CHECK(!intelbt_match(0x0BDA,0xC123)&&!intelbt_match(0x8087,0x1234)&&!intelbt_match(0,0),"Realtek 0BDA:C123 and unknown IDs are inert");
}
static int mkfw(u8* f,int ecdsa,int* cmdend,u32 boot){
    memset(f,0xA5,2000); int o;
    put32(f+8,0x00010000);
    if(ecdsa){ f[644]=0x06; put32(f+644+8,0x00020000); o=964; } else o=644;
    srand(7);
    for(int i=0;i<300;i++){ int l=rand()%40; if(i==150){ f[o]=0x0e; f[o+1]=0xfc; f[o+2]=4; put32(f+o+3,boot); o+=7; continue; } f[o]=(u8)rand(); f[o+1]=0xfc; f[o+2]=(u8)l; o+=3; for(int k=0;k<l;k++)f[o++]=(u8)rand(); }
    *cmdend=o; return o;
}
static void frag_tests(void){
    static u8 f[64000]; static fr_t fr; int end; u32 boot=0;
    int len=mkfw(f,0,&end,0x12345678);
    memset(&fr,0,sizeof fr); fr.base=f;
    int nf=intelbt_send_fw(f,len,0x12,0,&boot,rec,&fr);
    CHECK(nf==fr.n&&nf>5,"RSA image is sent");
    CHECK(fr.type[0]==0&&fr.type[1]==3&&fr.type[2]==3&&fr.type[3]==2&&fr.type[4]==2,"RSA header: CSS, key x2, signature x2");
    CHECK(fr.off[0]==0&&fr.off[1]==128&&fr.off[2]==256&&fr.off[3]==388&&fr.off[4]==516&&fr.len[4]==128,"RSA header offsets skip the 4-byte exponent");
    int ok=1, pos=644, al=1; for(int i=5;i<fr.n;i++){ if(fr.type[i]!=1||fr.off[i]!=pos||fr.len[i]>252)ok=0; if(fr.len[i]<252&&fr.len[i]%4)al=0; pos+=fr.len[i]; }
    CHECK(ok,"command buffer is sent contiguously in <=252-byte fragments");
    CHECK(al,"partial fragments are 4-byte aligned");
    int bound=0; { int o=644; while(o<end){ if(o==pos){ bound=1; break; } o+=3+f[o+2]; } if(o==pos)bound=1; }
    CHECK(bound&&pos<=end&&end-pos<300,"download stops on a command boundary");
    CHECK(boot==0x12345678,"boot address taken from the 0xfc0e command");
    len=mkfw(f,1,&end,0xCAFEF00D); memset(&fr,0,sizeof fr); fr.base=f; boot=0;
    nf=intelbt_send_fw(f,len,0x17,1,&boot,rec,&fr);
    CHECK(nf>3&&fr.type[0]==0&&fr.off[0]==644&&fr.len[0]==128&&fr.type[1]==3&&fr.off[1]==772&&fr.len[1]==96&&fr.type[2]==2&&fr.off[2]==868&&fr.len[2]==96,"ECDSA header fragments");
    CHECK(fr.off[3]==964&&boot==0xCAFEF00D,"ECDSA command buffer starts at 964");
    CHECK(intelbt_send_fw(f,len,0x17,0,&boot,rec,&fr)>0,"variant 0x17 with RSA engine accepted (ECDSA CSS present)");
    CHECK(intelbt_send_fw(f,len,0x12,1,&boot,rec,&fr)<0,"RSA-only variant with ECDSA engine refused");
    CHECK(intelbt_send_fw(f,len,0x15,0,&boot,rec,&fr)<0,"variant 0x15/0x16 refused (no header layout)");
    CHECK(intelbt_send_fw(f,600,0x12,0,&boot,rec,&fr)<0,"image shorter than the RSA header refused");
    f[644]=0x05; CHECK(intelbt_send_fw(f,len,0x17,1,&boot,rec,&fr)<0,"missing ECDSA CSS marker refused");
    put32(f+8,0x00030000); CHECK(intelbt_send_fw(f,len,0x12,0,&boot,rec,&fr)<0,"bad RSA CSS version refused");
    f[0]=0; put32(f+8,0x00010000); f[644]=0xfc; f[645]=0xfc; f[646]=0xff; memset(&fr,0,sizeof fr); fr.base=f;
    CHECK(intelbt_send_fw(f,700,0x12,0,&boot,rec,&fr)>=0&&fr.n>=5,"record running past the end does not over-read");
}
int main(void){
    printf("Intel Bluetooth firmware loader host test\n");
    tlv_tests(); legacy_tests(); frag_tests();
    printf("%d/%d checks passed\n",checks-fails,checks);
    if(!fails)printf("INTEL BT: ALL PASS\n");
    return fails?1:0;
}
