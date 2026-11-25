#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/net/stack/firewall.h"

extern volatile u64 g_ticks;
int ux_read_file(const char* p,char* out,int cap);
int ux_write_text(const char* path,const char* body,int len,unsigned mode);
int svcu_count(void);
int svcu_state(int i,const char** name,const char** desc,const char** state,int* pid,int* enabled);

int g_fw_on=1;
u32 g_fw_dropped, g_fw_passed;
struct fw_rule g_fw_rule[FW_RULES];
static struct { u16 lport, rport; u32 rip; u64 t; u8 used; } fw_ct[FW_CT];
static int fw_loaded;

static int fw_eq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }
static void fw_cp(char* d,const char* s,int cap){ int i=0; for(;s&&s[i]&&i<cap-1;i++)d[i]=s[i]; d[i]=0; }
int fw_proto(const char* s){ return fw_eq(s,"tcp")?6:fw_eq(s,"udp")?17:0; }

static int fw_add(int proto,int port,const char* svc){
    if((proto!=6&&proto!=17)||port<1||port>65535)return -1;
    for(int i=0;i<FW_RULES;i++){ struct fw_rule* r=&g_fw_rule[i]; if(r->used&&r->proto==proto&&r->port==port){ fw_cp(r->svc,svc,24); return i; } }
    for(int i=0;i<FW_RULES;i++){ struct fw_rule* r=&g_fw_rule[i]; if(r->used)continue; r->used=1; r->proto=(u8)proto; r->port=(u16)port; fw_cp(r->svc,svc,24); return i; }
    return -1;
}
void fw_defaults(void){
    g_fw_on=1; for(int i=0;i<FW_RULES;i++)g_fw_rule[i].used=0; fw_add(6,22,"sshd");
}
static const char* fw_word(const char* p,const char* e,char* w,int cap){
    int n=0; while(p<e&&(*p==' '||*p=='\t'))p++;
    while(p<e&&*p!=' '&&*p!='\t'&&*p!='\n'&&*p!='\r'){ if(n<cap-1)w[n++]=*p; p++; } w[n]=0; return p;
}
int fw_parse(const char* t,int n){
    g_fw_on=1; for(int i=0;i<FW_RULES;i++)g_fw_rule[i].used=0;
    const char* p=t; const char* end=t+n; int rules=0;
    while(p<end){ const char* le=p; while(le<end&&*le!='\n')le++;
        char w[24],pr[8],po[8],sv[24]; const char* q=fw_word(p,le,w,24);
        if(fw_eq(w,"enabled=0"))g_fw_on=0;
        else if(fw_eq(w,"allow")){ q=fw_word(q,le,pr,8); q=fw_word(q,le,po,8); fw_word(q,le,sv,24); int v=0,d=0; for(int i=0;po[i];i++){ if(po[i]<'0'||po[i]>'9'){ d=0; break; } v=v*10+(po[i]-'0'); d=1; if(v>65535)break; }
            if(d&&fw_add(fw_proto(pr),v,sv)>=0)rules++; }
        p=le<end?le+1:end; }
    return rules;
}
int fw_format(char* out,int cap){
    char* o=out; if(cap<64)return -1;
    o=scopy(o,g_fw_on?"enabled=1\n":"enabled=0\n");
    for(int i=0;i<FW_RULES;i++){ struct fw_rule* r=&g_fw_rule[i]; if(!r->used)continue; if(o-out>cap-48)break;
        o=scopy(o,"allow "); o=scopy(o,r->proto==6?"tcp ":"udp "); o=udec(o,r->port); if(r->svc[0]){ *o++=' '; o=scopy(o,r->svc); } *o++='\n'; }
    *o=0; return (int)(o-out);
}
void fw_load(void){
    if(fw_loaded)return;
    fw_loaded=1; fw_defaults();
    static char b[1024]; int n=ux_read_file(FW_CONF,b,(int)sizeof b); if(n>0)fw_parse(b,n);
}
int fw_save(void){ static char b[1024]; int n=fw_format(b,(int)sizeof b); if(n<0)return -1; return ux_write_text(FW_CONF,b,n,0100644u)<0?-1:0; }
int fw_set(int on){ fw_load(); g_fw_on=on?1:0; klog(on?"firewall: on, inbound connections blocked except replies and allowed services":"firewall: OFF, all inbound connections allowed"); return fw_save(); }
int fw_allow(int proto,int port,const char* svc){ fw_load(); if(fw_add(proto,port,svc)<0)return -1; return fw_save(); }
int fw_deny(int proto,int port){
    fw_load(); int hit=0; for(int i=0;i<FW_RULES;i++){ struct fw_rule* r=&g_fw_rule[i]; if(r->used&&r->proto==proto&&r->port==port){ r->used=0; hit=1; } }
    if(!hit)return -1;
    return fw_save();
}
int fw_rule_open(const struct fw_rule* r){
    int n=svcu_count(); if(!r->used)return 0;
    if(!r->svc[0])return 1;
    for(int i=0;i<n;i++){ const char *nm,*ds,*st; int pid,en; if(!svcu_state(i,&nm,&ds,&st,&pid,&en))continue; if(fw_eq(nm,r->svc))return fw_eq(st,"running")||fw_eq(st,"starting"); }
    return 0;
}
void fw_note_out(int proto,u16 lport,u32 rip,u16 rport){
    int k=0; if(proto!=17)return;
    for(int i=0;i<FW_CT;i++){ if(fw_ct[i].used&&fw_ct[i].lport==lport&&fw_ct[i].rip==rip&&fw_ct[i].rport==rport){ fw_ct[i].t=g_ticks; return; } if(!fw_ct[i].used){ if(fw_ct[k].used)k=i; } else if(fw_ct[k].used&&fw_ct[i].t<fw_ct[k].t)k=i; }
    fw_ct[k].used=1; fw_ct[k].lport=lport; fw_ct[k].rip=rip; fw_ct[k].rport=rport; fw_ct[k].t=g_ticks;
}
int fw_allow_in(int proto,u32 sip,u16 sport,u16 dport,int local){
    if(local)return 1;
    fw_load(); if(!g_fw_on){ g_fw_passed++; return 1; }
    if(proto==17) for(int i=0;i<FW_CT;i++){ if(!fw_ct[i].used||fw_ct[i].lport!=dport||fw_ct[i].rport!=sport)continue; if(g_ticks-fw_ct[i].t>12000u){ fw_ct[i].used=0; continue; }
        if(fw_ct[i].rip==sip||fw_ct[i].rip==0xFFFFFFFFu||(fw_ct[i].rip&0xFFu)==0xFFu){ g_fw_passed++; return 1; } }
    for(int i=0;i<FW_RULES;i++){ struct fw_rule* r=&g_fw_rule[i]; if(r->used&&r->proto==proto&&r->port==dport&&fw_rule_open(r)){ g_fw_passed++; return 1; } }
    g_fw_dropped++; return 0;
}
int fw_status(char* out,int cap){
    char* o=out; fw_load(); if(cap<200)return -1;
    o=scopy(o,g_fw_on?"firewall: on - inbound connections are blocked except replies and these:\n":"firewall: OFF - every inbound connection is allowed\n");
    for(int i=0;i<FW_RULES;i++){ struct fw_rule* r=&g_fw_rule[i]; if(!r->used)continue; if(o-out>cap-96)break;
        o=scopy(o,"  "); o=scopy(o,r->proto==6?"tcp ":"udp "); o=udec(o,r->port);
        if(r->svc[0]){ o=scopy(o,"  only while "); o=scopy(o,r->svc); o=scopy(o," runs"); }
        o=scopy(o,fw_rule_open(r)?"  (open)\n":"  (closed)\n"); }
    o=scopy(o,"  blocked "); o=udec(o,g_fw_dropped); o=scopy(o,", allowed "); o=udec(o,g_fw_passed); *o++='\n'; *o=0; return (int)(o-out);
}
