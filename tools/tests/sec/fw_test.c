#include <stdio.h>
#include <string.h>
#include "include/nvx/base.h"
volatile u64 g_ticks;
static char disk[1024]; static int disk_n=-1, writes, sshd_run;
char* scopy(char* p,const char* s){ while(*s)*p++=*s++; *p=0; return p; }
char* udec(char* p,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*p++=t[--n]; *p=0; return p; }
void klog(const char* m){ (void)m; }
int ux_read_file(const char* p,char* out,int cap){ if(strcmp(p,"/etc/firewall.conf")||disk_n<0)return -1; int n=disk_n<cap-1?disk_n:cap-1; memcpy(out,disk,n); out[n]=0; return n; }
int ux_write_text(const char* p,const char* b,int n,unsigned m){ (void)m; if(strcmp(p,"/etc/firewall.conf")||n>=(int)sizeof disk)return -1; memcpy(disk,b,n); disk[n]=0; disk_n=n; writes++; return n; }
int svcu_count(void){ return 2; }
int svcu_state(int i,const char** name,const char** desc,const char** state,int* pid,int* en){ *desc=""; *pid=0; *en=0; if(i==0){ *name="cron"; *state="running"; return 1; } *name="sshd"; *state=sshd_run?"running":"inactive"; return 1; }
#include "net/stack/firewall.c"
static int fails;
#define CK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
int main(void){
    u32 peer=0xC0A80105u, other=0xC0A80109u;
    CK(fw_allow_in(6,peer,40000,8080,0)==0); CK(g_fw_on==1); CK(g_fw_dropped==1);
    CK(fw_allow_in(6,peer,40000,8080,1)==1);
    CK(fw_allow_in(6,peer,40000,22,0)==0);
    sshd_run=1; CK(fw_allow_in(6,peer,40000,22,0)==1); CK(fw_allow_in(17,peer,40000,22,0)==0); sshd_run=0; CK(fw_allow_in(6,peer,40000,22,0)==0);
    CK(fw_allow_in(17,0x08080808u,53,41000,0)==0);
    fw_note_out(17,41000,0x08080808u,53); CK(fw_allow_in(17,0x08080808u,53,41000,0)==1); CK(fw_allow_in(17,other,53,41000,0)==0); CK(fw_allow_in(17,0x08080808u,54,41000,0)==0); CK(fw_allow_in(17,0x08080808u,53,41001,0)==0);
    g_ticks+=12001; CK(fw_allow_in(17,0x08080808u,53,41000,0)==0);
    fw_note_out(17,5353,0xC0A801FFu,5353); CK(fw_allow_in(17,other,5353,5353,0)==1);
    for(int i=0;i<FW_CT+5;i++){ g_ticks++; fw_note_out(17,(u16)(30000+i),peer,123); } CK(fw_allow_in(17,peer,123,30000+FW_CT+4,0)==1); CK(fw_allow_in(17,peer,123,30000,0)==0);
    CK(fw_allow(6,8080,"")==0); CK(fw_allow_in(6,other,1,8080,0)==1); CK(strstr(disk,"allow tcp 8080\n")!=0); CK(strstr(disk,"allow tcp 22 sshd\n")!=0); CK(strstr(disk,"enabled=1\n")==disk);
    CK(fw_allow(6,0,"")<0); CK(fw_allow(6,70000,"")<0); CK(fw_allow(1,80,"")<0);
    CK(fw_deny(6,8080)==0); CK(fw_allow_in(6,other,1,8080,0)==0); CK(fw_deny(6,8080)<0);
    CK(fw_set(0)==0); CK(fw_allow_in(6,other,1,9999,0)==1); CK(strstr(disk,"enabled=0\n")==disk);
    fw_loaded=0; CK(fw_allow_in(6,other,1,9999,0)==1); CK(g_fw_on==0);
    CK(fw_set(1)==0); fw_loaded=0; CK(fw_allow_in(6,other,1,9999,0)==0);
    strcpy(disk,"garbage\nallow tcp 99999\nallow icmp 5\nallow udp 51820\nallow tcp 2x2\n"); disk_n=(int)strlen(disk); fw_loaded=0;
    CK(fw_allow_in(17,other,1,51820,0)==1); CK(g_fw_on==1); sshd_run=1; CK(fw_allow_in(6,other,1,22,0)==0);
    disk_n=-1; fw_loaded=0; CK(fw_allow_in(6,other,1,22,0)==1); CK(g_fw_on==1);
    for(int i=0;i<FW_RULES+3;i++)fw_allow(17,1000+i,"");
    { int n=0; for(int i=0;i<FW_RULES;i++) n+=g_fw_rule[i].used; CK(n==FW_RULES); }
    { char b[2048]; CK(fw_status(b,sizeof b)>0); CK(strstr(b,"tcp 22  only while sshd runs  (open)")!=0); CK(strstr(b,"firewall: on")!=0); }
    { char b[64]; CK(fw_status(b,sizeof b)<0); char c[32]; CK(fw_format(c,sizeof c)<0); }
    if(fails){ printf("%d failures\n",fails); return 1; }
    printf("firewall: all ok\n"); return 0;
}
