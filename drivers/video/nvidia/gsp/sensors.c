static struct { nvgsp_sens_t s; u64 at; u32 no; char txt[640]; } NS;
static int ns_ctrl(int bit,u32 cmd,void* p,u32 n){ if(NS.no&(1u<<bit))return 0; int r=rm_ctrl(G.hclient,G.hsubdev,cmd,p,n); if(r==0)return 1; if(r>-1000)NS.no|=1u<<bit; NS.s.refused++; return 0; }
int nvgsp_sens_up(void){ return G.state>=2&&!g_rpc_dead&&!g_nv_disabled&&G.hclient&&G.hsubdev; }
static const char* ns_down(void){ return G.state<2?"GSP-RM is not running":g_rpc_dead?"GSP-RM stopped answering":g_nv_disabled?"the NVIDIA driver is off":"GSP-RM has no subdevice yet"; }
int nvgsp_sensors(nvgsp_sens_t* o){
    if(!nvgsp_sens_up()){ u8* z=(u8*)o; for(u32 i=0;i<sizeof(*o);i++)z[i]=0; o->why=ns_down(); NS.at=0; return 0; }
    u64 t=hr_ns(); if(NS.at&&t-NS.at<1000000000ULL){ *o=NS.s; return 1; }
    nvgsp_sens_t* s=&NS.s; u32 rd=s->reads, rf=s->refused; { u8* z=(u8*)s; for(u32 i=0;i<sizeof(*s);i++)z[i]=0; } s->reads=rd+1; s->refused=rf;
    u32 b[105]; for(int i=0;i<105;i++)b[i]=0; b[0]=2; b[1]=3; b[3]=7;
    if(ns_ctrl(0,0x20801823u,b,420)){ s->link=1; s->max_gen=b[2]&0xFu; s->max_width=(b[2]>>4)&0x3Fu; s->link_gen=(b[4]>>16)&0xFu; s->link_width=(b[4]>>20)&0x3Fu; }
    u32 ps=0; if(!g_rpc_dead&&ns_ctrl(1,0x20800A81u,&ps,4)){ s->pst=1; s->aux_pstate=ps; }
    u8 hd=0; if(!g_rpc_dead&&ns_ctrl(2,0x20801708u,&hd,1)){ s->hcs=1; s->hostclk_slow_off=hd?1:0; }
    if(g_rpc_dead){ s->why=ns_down(); NS.at=0; *o=*s; return 0; }
    s->up=1; s->why=s->link||s->pst||s->hcs?"":"GSP-RM refused every sensor request"; NS.at=t; *o=*s; return 1; }
const char* nvgsp_sens_gen(u32 g){ static const char* n[7]={"?","1 (2.5 GT/s)","2 (5 GT/s)","3 (8 GT/s)","4 (16 GT/s)","5 (32 GT/s)","6 (64 GT/s)"}; return n[g<7?g:0]; }
const char* nvgsp_sens_text(void){
    nvgsp_sens_t s; int up=nvgsp_sensors(&s); char* p=NS.txt;
    p=scopy(p,"  GPU sensors through GSP-RM (read at most once a second)\n");
    if(!up){ p=scopy(p,"  not available: "); p=scopy(p,s.why); p=scopy(p,"\n"); *p=0; return NS.txt; }
    p=scopy(p,"  PCIe link            "); if(s.link){ p=scopy(p,"gen "); p=scopy(p,nvgsp_sens_gen(s.link_gen)); p=scopy(p," x"); p=udec(p,s.link_width); p=scopy(p,", card can do gen "); p=scopy(p,nvgsp_sens_gen(s.max_gen)); p=scopy(p," x"); p=udec(p,s.max_width); } else p=scopy(p,"refused by GSP-RM"); p=scopy(p,"\n");
    p=scopy(p,"  aux power state      "); if(s.pst){ *p++='P'; p=udec(p,s.aux_pstate); } else p=scopy(p,"refused by GSP-RM"); p=scopy(p,"\n");
    p=scopy(p,"  host clock slowdown  "); p=scopy(p,!s.hcs?"refused by GSP-RM":s.hostclk_slow_off?"disabled":"allowed"); p=scopy(p,"\n");
    p=scopy(p,"  temperature, clocks, fan, power: not read - NVIDIA's 535.113.01 SDK publishes no layout for those controls\n"); *p=0; return NS.txt; }
