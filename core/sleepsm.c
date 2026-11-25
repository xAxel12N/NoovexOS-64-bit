#include "include/nvx/core/sleepsm.h"

static char* slp_cat(char* q,char* e,const char* s){ while(s&&*s&&q<e)*q++=*s++; *q=0; return q; }
static void slp_msg(struct slp_sm* s,const char* a,const char* b,const char* c){ char* q=s->msg; char* e=s->msg+sizeof(s->msg)-1; q=slp_cat(q,e,a); q=slp_cat(q,e,b); slp_cat(q,e,c); }
int slp_add(struct slp_sm* s,const struct slp_dev* d){
    if(!s||!d||!d->name)return -1;
    for(int i=0;i<s->n;i++) if(s->dev[i]==d)return i;
    if(s->n>=SLP_MAXDEV)return -1;
    s->dev[s->n]=d; return s->n++; }
static int slp_on(const struct slp_dev* d){ return !d->present||d->present(); }
int slp_enter(struct slp_sm* s){
    if(s->state==SLP_ASLEEP||s->state==SLP_SUSPEND||s->state==SLP_RESUME){ slp_msg(s,"Sleep is already in progress",0,0); return -3; }
    s->state=SLP_CHECK; s->msg[0]=0; s->live=0; s->done=0;
    for(int i=0;i<s->n;i++){ const struct slp_dev* d=s->dev[i]; if(!slp_on(d))continue; s->live|=1u<<i;
        const char* why=d->block?d->block():0; if(why&&*why){ slp_msg(s,"Sleep is not available: ",d->name," - "); { char* q=s->msg; while(*q)q++; slp_cat(q,s->msg+sizeof(s->msg)-1,why); } s->state=SLP_BLOCKED; return -1; } }
    s->state=SLP_SUSPEND;
    for(int i=0;i<s->n;i++){ if(!((s->live>>i)&1u))continue; const struct slp_dev* d=s->dev[i];
        if(d->suspend&&d->suspend()<0){
            for(int j=i-1;j>=0;j--) if(((s->live>>j)&1u)&&s->dev[j]->resume)s->dev[j]->resume();
            slp_msg(s,"Sleep stopped: ",d->name," could not be put to sleep"); s->fails++; s->state=SLP_FAILED; s->done=0; return -2; }
        s->done=i+1; }
    s->state=SLP_ASLEEP; return 0; }
int slp_leave(struct slp_sm* s){
    if(s->state!=SLP_ASLEEP)return 0;
    s->state=SLP_RESUME; int bad=0; s->msg[0]=0;
    for(int i=s->done-1;i>=0;i--){ if(!((s->live>>i)&1u))continue; const struct slp_dev* d=s->dev[i];
        if(d->resume&&d->resume()<0){ if(!bad)slp_msg(s,"Woke up, but ",d->name," did not come back"); bad++; } }
    s->resfail+=bad; s->cycles++; s->done=0; s->state=bad?SLP_FAILED:SLP_IDLE; return bad; }
int slp_pick(int enabled,int have_s3,int s3_ok,int have_lps0,int have_s1,int hwred){
    if(!enabled)return SLP_MODE_NONE;
    if(have_s3&&s3_ok)return SLP_MODE_S3;
    if(have_lps0)return SLP_MODE_S2IDLE;
    if(have_s1&&!hwred)return SLP_MODE_S1;
    return SLP_MODE_S2IDLE; }
int slp_req_due(unsigned long long now,unsigned long long wake_t,int lid_open){ if(!wake_t||now-wake_t>200)return 1; return lid_open==0?0:-1; }
const char* slp_mode_name(int m){ return m==SLP_MODE_S3?"S3 suspend to RAM":m==SLP_MODE_S1?"ACPI S1":m==SLP_MODE_S2IDLE?"light sleep (S0 idle)":"off"; }
const char* slp_state_name(int st){ static const char* const N[]={"idle","checking devices","suspending devices","asleep","resuming devices","blocked","failed"}; return st>=0&&st<=SLP_FAILED?N[st]:"?"; }
