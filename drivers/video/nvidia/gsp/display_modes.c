typedef struct { u32 khz; u16 hact, hfp, hsw, hbp, vact, vfp, vsw, vbp; u8 nhs, nvs; } kmode;
typedef struct { u32* pb; u32 cur, put, user, cls, inst, chid; int ok; } kchan;
#define KD_HSYNC 0xF0000000u
#define KD_HVRAM 0xF0000001u
#define KD_HFB   0xFB000000u
#define KD_ROOT  0xC5700000u
#define KD_PBMAX 1000u
static struct {
    int up; u32 gen; u32 hd; u64 inst, sync, lut, ilut, vtop; u32 wmask, sors, owned; int nobj;
    u32 ht_handle[1024]; int ht_chid[1024];
    kchan core, win[8];
    struct { u32 id; int or, win, hdmi, pend, fw; kmode m; u32 ow, oh; } head[4];
    u32 sets, timeouts; char why[112];
} KD;

static int kd_fail(const char* w){ char* q=KD.why; q=scopy(q,w); *q=0; nvl_s("  "); nvl_s(w); nvl_nl(); return 0; }
int nvk_edid_mode(const u8* e,int n,kmode* m){
    if(n<128)return 0;
    for(int b=54;b<=108;b+=18){ const u8* d=e+b; u32 pk=(u32)(d[0]|(d[1]<<8)); if(!pk)continue;
        u32 ha=(u32)(d[2]|((d[4]&0xF0)<<4)), hb=(u32)(d[3]|((d[4]&0x0F)<<8)), va=(u32)(d[5]|((d[7]&0xF0)<<4)), vb=(u32)(d[6]|((d[7]&0x0F)<<8));
        u32 ho=(u32)(d[8]|((d[11]&0xC0)<<2)), hw=(u32)(d[9]|((d[11]&0x30)<<4)), vo=(u32)((d[10]>>4)|((d[11]&0x0C)<<2)), vw=(u32)((d[10]&0x0F)|((d[11]&0x03)<<4));
        if(d[17]&0x80)continue;
        if(ha<320||va<200||ha>8192||va>8192||!hw||!vw||ho+hw>=hb||vo+vw>=vb)continue;
        m->khz=pk*10; m->hact=(u16)ha; m->hfp=(u16)ho; m->hsw=(u16)hw; m->hbp=(u16)(hb-ho-hw); m->vact=(u16)va; m->vfp=(u16)vo; m->vsw=(u16)vw; m->vbp=(u16)(vb-vo-vw);
        m->nhs=1; m->nvs=1; if((d[17]&0x18)==0x18){ m->nvs=(d[17]&0x04)?0:1; m->nhs=(d[17]&0x02)?0:1; }
        return 1; }
    return 0; }
int nvk_edid_hdmi(const u8* e,int n){
    if(n<256||e[126]==0||e[128]!=0x02)return 0; const u8* c=e+128; int dtd=c[2]; if(dtd<4||dtd>127)return 0;
    for(int i=4;i<dtd;){ int tag=c[i]>>5, len=c[i]&31; if(i+1+len>dtd)break; if(tag==3&&len>=3&&c[i+1]==0x03&&c[i+2]==0x0C&&c[i+3]==0x00)return 1; i+=1+len; }
    return 0; }
void nvk_mode_regs(const kmode* m,u32* r){
    u32 ht=(u32)m->hact+m->hfp+m->hsw+m->hbp, vt=(u32)m->vact+m->vfp+m->vsw+m->vbp;
    u32 hse=(u32)m->hsw-1, hbe=(u32)m->hsw+m->hbp-1, hbs=hbe+m->hact, vse=(u32)m->vsw-1, vbe=(u32)m->vsw+m->vbp-1, vbs=vbe+m->vact;
    r[0]=ht|(vt<<16); r[1]=hse|(vse<<16); r[2]=hbe|(vbe<<16); r[3]=hbs|(vbs<<16); }
void nvk_fit(u32 sw,u32 sh,u32 dw,u32 dh,u32* ow,u32* oh){
    if(!sw||!sh){ *ow=dw; *oh=dh; return; }
    u64 a=(u64)dw*sh, b=(u64)dh*sw; u32 w, h;
    if(a<=b){ w=dw; h=(u32)(((u64)dw*sh)/sw); } else { h=dh; w=(u32)(((u64)dh*sw)/sh); }
    w&=~1u; h&=~1u; if(w<2)w=2; if(h<2)h=2; if(w>dw)w=dw; if(h>dh)h=dh; *ow=w; *oh=h; }

static u32 kd_hash(int chid,u32 handle){ u32 h=0; while(handle){ h^=handle&1023u; handle>>=10; } h^=(u32)chid<<6; return h&1023u; }
static int kd_ht_add(int chid,u32 handle,u64 obj){
    u32 co=kd_hash(chid,handle), ho=co;
    do{ if(KD.ht_chid[co]==chid&&KD.ht_handle[co]==handle)return 1; if(++co>=1024)co=0; }while(co!=ho);
    co=ho;
    do{ if(KD.ht_chid[co]<0){ KD.ht_chid[co]=chid; KD.ht_handle[co]=handle;
            u32 ctx=((u32)chid<<25)|(HC(2)&0x3FFFu)|((u32)(obj-KD.inst)<<9);
            nvp_wr32(KD.inst+(u64)co*8,handle); nvp_wr32(KD.inst+(u64)co*8+4,ctx); return 1; }
        if(++co>=1024)co=0; }while(co!=ho);
    return 0; }
static u64 kd_obj(u32 flags0,u64 start,u64 limit){
    if(KD.nobj>=64)return 0; u64 o=KD.inst+0x2000+(u64)KD.nobj*32; KD.nobj++;
    nvp_wr32(o,flags0); nvp_wr32(o+4,(u32)(start>>8)); nvp_wr32(o+8,(u32)(start>>40)); nvp_wr32(o+12,(u32)(limit>>8)); nvp_wr32(o+16,(u32)(limit>>40)); nvp_wr32(o+20,0); return o; }

static void km(kchan* c,u32 m,u32 n){ if(c->cur+n+1>KD_PBMAX){ c->ok=0; return; } c->pb[c->cur++]=(n<<18)|m; }
static void kv(kchan* c,u32 v){ if(c->ok&&c->cur<KD_PBMAX)c->pb[c->cur++]=v; }
static void k1(kchan* c,u32 m,u32 v){ km(c,m,1); kv(c,v); }
static int kc_idle(kchan* c,u32 ms){ u64 t=hr_ns()+(u64)ms*1000000ULL; for(;;){ if(nvr32(c->user+4)==c->put*4)return 1; if(hr_ns()>t)return 0; hr_delay_us(50); } }
static int kc_room(kchan* c,u32 need){
    if(!c->ok)return 0; if(c->cur+need+2<=KD_PBMAX)return 1;
    if(!kc_idle(c,2000))return 0;
    c->pb[c->cur]=0x20000000u; cflush(c->pb,0x1000); c->cur=0; c->put=0; nvw32(c->user,0); return kc_idle(c,2000); }
static void kc_kick(kchan* c){ if(!c->ok||c->cur==c->put)return; cflush(c->pb,0x1000); nvw32(c->user,c->cur*4); c->put=c->cur; }
static int kc_new(kchan* c,u32 cls,u32 inst,u32 user,u32 chid){
    c->ok=0; if(!c->pb){ c->pb=(u32*)sa_contig(0x1000,0x1000); if(!c->pb)return 0; }
    if(PA(c->pb)>>40)return 0;
    for(int i=0;i<1024;i++)c->pb[i]=0; c->cur=0; c->put=0; c->user=user; c->cls=cls; c->inst=inst; c->chid=chid;
    u8 p[40]; for(int i=0;i<40;i++)p[i]=0; *(u32*)p=1; *(u64*)(p+8)=PA(c->pb); *(u64*)(p+16)=0xFFF; *(u32*)(p+24)=1; *(u32*)(p+28)=cls; *(u32*)(p+32)=inst; p[36]=1;
    if(rm_ctrl(G.hclient,G.hsubdev,0x20800A58u,p,40))return 0;
    u8 a[32]; for(int i=0;i<32;i++)a[i]=0; *(u32*)a=inst;
    if(rm_alloc(HC(2),KD_ROOT,(cls<<16)|inst,cls,a,32))return 0;
    c->ok=1; return 1; }

static void kd_reset(void){ u8* z=(u8*)&KD; u32* keep_core=KD.core.pb; u32* keep_w[8]; for(int i=0;i<8;i++)keep_w[i]=KD.win[i].pb;
    for(u32 i=0;i<sizeof(KD);i++)z[i]=0; KD.core.pb=keep_core; for(int i=0;i<8;i++)KD.win[i].pb=keep_w[i];
    for(int i=0;i<1024;i++)KD.ht_chid[i]=-1; for(int i=0;i<4;i++){ KD.head[i].or=-1; KD.head[i].win=-1; } }
static u16 kd_fp16(u16 f){ int e=0, m=0; if(f){ while(--e&&!(f&0x8000u))f=(u16)(f<<1); m=(((int)f<<1)&0xFFC0)>>6; e+=15; } return (u16)((e<<10)|m); }
static int kd_objs(int chid,int fb){
    u64 a=kd_obj(0x00000045u,KD.sync,KD.sync+0xFFF), b=kd_obj(0x00000045u,0,KD.vtop-1), c=fb?kd_obj(0x00000005u,0,KD.vtop-1):1; if(!a||!b||!c)return 0;
    return kd_ht_add(chid,KD_HSYNC,a)&&kd_ht_add(chid,KD_HVRAM,b)&&(!fb||kd_ht_add(chid,KD_HFB,c)); }
static int kd_bail(const char* w){ if(KD.sync)nvv_free(KD.sync); if(KD.lut)nvv_free(KD.lut); if(KD.ilut)nvv_free(KD.ilut); KD.sync=0; KD.lut=0; KD.ilut=0; rm_free(HC(2),HC(2)); g_kd_live=0; return kd_fail(w); }
static int kd_init(void){
    if(KD.up&&KD.gen==g_gsp_boots)return 1;
    kd_reset(); g_kd_live=0;
    if(G.state<7)return kd_fail("the NVIDIA card is not running - run nvgsp all first");
    if(g_disp_gen!=g_gsp_boots||!g_disp_inst){ u64 v=nvv_alloc(0x10000,0x10000); if(!v)return kd_fail("no VRAM for the display instance memory");
        u8 w[24]; for(int i=0;i<24;i++)w[i]=0; *(u64*)w=v; *(u64*)(w+8)=0x10000; *(u32*)(w+16)=2; *(u32*)(w+20)=2;
        if(rm_ctrl(G.hclient,G.hsubdev,0x20800A49u,w,24)){ nvv_free(v); return kd_fail("GSP-RM refused the display instance memory (WRITE_INST_MEM)"); }
        g_disp_inst=v; g_disp_gen=g_gsp_boots; }
    KD.inst=g_disp_inst; nvp_fill(KD.inst,0x10000,0);
    if(!client_dev(2)){ return kd_bail("could not create the display client"); }
    KD.hd=0x00730000u;
    if(rm_alloc(HC(2),HDEV,KD.hd,0x0073,0,0)){ return kd_bail("GSP-RM refused NV04_DISPLAY_COMMON"); }
    { u8 s[28]; for(int i=0;i<28;i++)s[i]=0; if(rm_ctrl(G.hclient,G.hsubdev,0x20800A01u,s,28)==0)KD.wmask=*(u32*)(s+4); if(!KD.wmask)KD.wmask=0xFF; }
    { static u8 bl[4104]; for(u32 i=0;i<sizeof(bl);i++)bl[i]=0; *(u32*)bl=0x56u; rm_ctrl(G.hclient,G.hsubdev,0x20800AC6u,bl,sizeof(bl)); }
    { u32 z=0; rm_ctrl(HC(2),KD.hd,0x731365u,&z,4); }
    if(rm_alloc(HC(2),HDEV,KD_ROOT,0xC570,0,0)){ return kd_bail("GSP-RM refused the display object (class C570)"); }
    KD.sync=nvv_alloc(0x1000,0x1000); KD.lut=nvv_alloc(0x3000,0x1000); KD.ilut=nvv_alloc(0x3000,0x1000);
    if(!KD.sync||!KD.lut||!KD.ilut){ return kd_bail("no VRAM for the display notifier and colour tables"); }
    for(u32 i=0;i<1025;i++){ u32 k=i<1024?i:1023; u32 v=k<<6; u64 a=KD.lut+0x20+(u64)i*8; nvp_wr32(a,v|(v<<16)); nvp_wr32(a+4,v); u32 f=kd_fp16((u16)v); a=KD.ilut+0x20+(u64)i*8; nvp_wr32(a,f|(f<<16)); nvp_wr32(a+4,f); }
    KD.vtop=g_nvl.fb_size?g_nvl.fb_size:nv_vram_size();
    if(!kc_new(&KD.core,0xC57D,0,0x680000u,0)){ return kd_bail("GSP-RM refused the core display channel (class C57D)"); }
    if(!kd_objs(0,0)){ return kd_bail("display instance memory is full"); }
    kchan* c=&KD.core; k1(c,0x0208,KD_HSYNC);
    kc_kick(c);
    if(!c->ok||!kc_idle(c,2000)){ return kd_bail("the core display channel did not take its first commands"); }
    KD.up=1; KD.gen=g_gsp_boots; g_kd_live=1; g_kd_live_gen=g_gsp_boots; nvl_kv("display channels ready, window mask",KD.wmask); return 1; }

static int kd_win(int w){
    kchan* c=&KD.win[w]; if(c->ok)return 1;
    if(!kc_new(c,0xC57E,(u32)w,0x690000u+(u32)w*0x1000u,(u32)(1+w)))return 0;
    if(!kd_objs(1+w,1)){ c->ok=0; return 0; } return 1; }
static void kd_avi(int head,const kmode* m){
    u8 f[17]; for(int i=0;i<17;i++)f[i]=0; f[0]=0x82; f[1]=0x02; f[2]=0x0D;
    f[4]=0x10; u32 r16=(u32)m->hact*9, r4=(u32)m->hact*3; f[5]=(u8)(0x08|(r16==(u32)m->vact*16?0x20:r4==(u32)m->vact*4?0x10:0x00)); f[6]=0x08; f[7]=0x00;
    u8 s=0; for(int i=0;i<17;i++)s=(u8)(s+f[i]); f[3]=(u8)(0x100-s);
    u32 ho=(u32)head*0x400u;
    nvm32(0x6F0000u+ho,0x00000001u,0);
    nvw32(0x6F0008u+ho,(u32)f[0]|((u32)f[1]<<8)|((u32)f[2]<<16));
    nvw32(0x6F000Cu+ho,(u32)f[3]|((u32)f[4]<<8)|((u32)f[5]<<16)|((u32)f[6]<<24)); nvw32(0x6F0010u+ho,(u32)f[7]|((u32)f[8]<<8)|((u32)f[9]<<16));
    nvw32(0x6F0014u+ho,(u32)f[10]|((u32)f[11]<<8)|((u32)f[12]<<16)|((u32)f[13]<<24)); nvw32(0x6F0018u+ho,(u32)f[14]|((u32)f[15]<<8)|((u32)f[16]<<16));
    nvm32(0x6F0000u+ho,0x00000001u,1); }
static int kd_wait_ntfy(u32 ms){ u64 t=hr_ns()+(u64)ms*1000000ULL; for(;;){ if((nvp_rd32(KD.sync)>>30)==2u)return 1; if(hr_ns()>t)return 0; hr_delay_us(200); } }
static int kd_commit(u32 wmask){
    kchan* c=&KD.core; if(!kc_room(c,12))return 0;
    nvp_wr32(KD.sync,0); nvp_wr32(KD.sync+4,0); nvp_wr32(KD.sync+8,0); nvp_wr32(KD.sync+12,0);
    k1(c,0x020C,0x00001000u); km(c,0x0218,2); kv(c,0); kv(c,wmask); k1(c,0x0200,0x00000001u); k1(c,0x020C,0);
    kc_kick(c); if(!c->ok)return 0;
    if(!kd_wait_ntfy(3000)){ KD.timeouts++; return 0; }
    return kc_idle(c,2000); }
static int kd_own(int h,int w){
    if(KD.owned&(1u<<w))return 1; kchan* c=&KD.core; if(!kc_room(c,16))return 0; u32 i=(u32)w;
    km(c,0x1004+i*0x80,2); kv(c,0x0000000Fu); kv(c,0); k1(c,0x1010+i*0x80,0x7FFFu|(1u<<16)|(1u<<20)); k1(c,0x1000+i*0x80,(u32)h);
    if(!c->ok||!kd_commit(0))return 0; KD.owned|=1u<<w; return 1; }
static int kd_head_on(int h,nvgsp_mon_t* mon,const kmode* m,int hdmi,int link,u32 excl,int byfw){
    int w=h*2; if(!(KD.wmask&(1u<<w)))return kd_fail("the card has no window for this head");
    u64 fbpa=*(volatile u64*)(NV_BOOTINFO); u32 pitch=*(volatile u32*)(NV_BOOTINFO+8); u32 fw=*(volatile u16*)(NV_BOOTINFO+12), fh=*(volatile u16*)(NV_BOOTINFO+14);
    if(!G.bar1||fbpa<G.bar1||fbpa>=G.bar1+G.bar1_len)return kd_fail("the desktop picture is not in NVIDIA video memory (another GPU drives the first screen)");
    u64 fbv=fbpa-G.bar1; if((pitch&63u)||(fbv&0xFFu)||!fw||!fh||fw>8192||fh>8192||pitch<fw*4)return kd_fail("the desktop picture has a line length the display engine cannot scan out directly");
    u8 a[80]; for(int i=0;i<80;i++)a[i]=0; *(u32*)(a+4)=mon->id; a[8]=(u8)excl;
    if(rm_ctrl(HC(2),KD.hd,0x731152u,a,80))return kd_fail("GSP-RM did not assign an output (ASSIGN_SOR)");
    int or=-1; for(int i=0;i<4;i++) if(*(u32*)(a+40+i*8)&mon->id){ or=i; break; }
    if(or<0)return kd_fail("GSP-RM returned no output for this monitor");
    if(!kd_win(w))return kd_fail("GSP-RM refused the window channel (class C57E)");
    if(!kd_own(h,w))return kd_fail("the display engine did not take the window assignment for this head");
    if(hdmi){ u8 e[12]; for(int i=0;i<12;i++)e[i]=0; *(u32*)(e+4)=mon->id; e[8]=1; if(rm_ctrl(HC(2),KD.hd,0x730273u,e,12))hdmi=0; else { u8 sc[12]; for(int i=0;i<12;i++)sc[i]=0; *(u32*)(sc+4)=mon->id; rm_ctrl(HC(2),KD.hd,0x730293u,sc,12); } }
    u32 ow, oh; nvk_fit(fw,fh,m->hact,m->vact,&ow,&oh); u32 r[4]; nvk_mode_regs(m,r); u32 ho=(u32)h*0x400u;
    kchan* c=&KD.core; if(!kc_room(c,64))return kd_fail("the core display channel is stuck"); u32 c0=c->cur;
    k1(c,0x204C+ho,fw|(fh<<16)); k1(c,0x2058+ho,ow|(oh<<16));
    km(c,0x2064+ho,4); kv(c,r[0]); kv(c,r[1]); kv(c,r[2]); kv(c,r[3]);
    k1(c,0x2074+ho,0x00000001u); k1(c,0x2008+ho,0);
    k1(c,0x200C+ho,m->khz*1000u); k1(c,0x2028+ho,m->khz*1000u);
    k1(c,0x2030+ho,0x00000004u|(1u<<4)|(1u<<8)|(1u<<12));
    km(c,0x2280+ho,4); kv(c,0x00000001u|(2u<<2)|(1029u<<8)); kv(c,0xFFFFFFFFu); kv(c,KD_HVRAM); kv(c,(u32)(KD.lut>>8));
    k1(c,0x2000+ho,0); k1(c,0x2018+ho,0);
    k1(c,0x2004+ho,((u32)m->nhs<<2)|((u32)m->nvs<<3)|(4u<<4)|(0x3Fu<<26));
    k1(c,0x2020+ho,mon->id);
    k1(c,0x0300+(u32)or*0x20,(1u<<h)|((u32)(link==2?2:1)<<8));
    if(!c->ok)return kd_fail("display command buffer overflow");
    kchan* q=&KD.win[w]; if(!kc_room(q,48)){ if(c->put<=c0)c->cur=c0; return kd_fail("the window channel is stuck"); }
    k1(q,0x0308,0x00000001u);
    km(q,0x0224,4); kv(q,fw|(fh<<16)); kv(q,0x00000010u); kv(q,0x000000CFu); kv(q,pitch>>6);
    km(q,0x0240,1); kv(q,KD_HFB); k1(q,0x0260,(u32)(fbv>>8)); k1(q,0x0290,0); k1(q,0x0298,fw|(fh<<16)); k1(q,0x02A4,fw|(fh<<16));
    km(q,0x02EC,7); kv(q,255u<<4); kv(q,255u); kv(q,2u|(2u<<4)|(4u<<8)|(4u<<12)); kv(q,0xFFFF0000u); kv(q,0xFFFF0000u); kv(q,0xFFFF0000u); kv(q,0xFFFF0000u);
    km(q,0x0400,12); for(int i=0;i<12;i++)kv(q,(i==0||i==5||i==10)?0x00010000u:0);
    km(q,0x0440,3); kv(q,(2u<<2)|(1029u<<8)); kv(q,KD_HVRAM); kv(q,(u32)(KD.ilut>>8));
    km(q,0x0370,2); kv(q,0x00000001u); kv(q,1u<<w); k1(q,0x0200,0x00000001u);
    if(!q->ok){ if(c->put<=c0)c->cur=c0; return kd_fail("window command buffer overflow"); }
    kc_kick(q);
    if(hdmi)kd_avi(h,m);
    KD.sors|=1u<<or; KD.head[h].id=mon->id; KD.head[h].or=or; KD.head[h].win=w; KD.head[h].hdmi=hdmi; KD.head[h].m=*m; KD.head[h].ow=ow; KD.head[h].oh=oh; KD.head[h].pend=1; KD.head[h].fw=byfw;
    if(!kd_commit(1u<<w))return kd_fail("the display engine did not finish the mode set within 3 s");
    KD.head[h].pend=0; KD.sets++;
    mon->head=h; mon->act_w=m->hact; mon->act_h=m->vact; mon->lit=2;
    nvl_s("  head "); nvl_d((u64)h); nvl_s(" -> output "); nvl_x(mon->id); nvl_s(" (SOR "); nvl_d((u64)or); nvl_s(hdmi?", HDMI": ", DVI signalling"); nvl_s("): "); nvl_d(m->hact); nvl_s("x"); nvl_d(m->vact); nvl_s(", pixel clock "); nvl_d(m->khz); nvl_s(" kHz, desktop "); nvl_d(fw); nvl_s("x"); nvl_d(fh); nvl_s(" shown as "); nvl_d(ow); nvl_s("x"); nvl_d(oh); nvl_nl();
    return 1; }
static int kd_head_off(int h){
    if(KD.head[h].or<0||KD.head[h].win<0)return 1; int w=KD.head[h].win; u32 ho=(u32)h*0x400u;
    u32 il=0; kchan* q=&KD.win[w]; if(q->ok&&kc_room(q,12)){ k1(q,0x0308,0); k1(q,0x0240,0); km(q,0x0370,2); kv(q,1); kv(q,1u<<w); k1(q,0x0200,1); if(q->ok){ kc_kick(q); il=1u<<w; } }
    kchan* c=&KD.core; if(!kc_room(c,16))return 0;
    k1(c,0x0300+(u32)KD.head[h].or*0x20,0); k1(c,0x2020+ho,0); k1(c,0x2288+ho,0);
    int ok=kd_commit(il); if(!ok)return 0;
    if(KD.head[h].hdmi)nvm32(0x6F0000u+ho,1,0);
    KD.sors&=~(1u<<KD.head[h].or); for(int i=0;i<g_mon_n&&i<8;i++) if(g_mon[i].head==h&&g_mon[i].lit==2){ g_mon[i].head=-1; g_mon[i].lit=0; }
    KD.head[h].or=-1; KD.head[h].win=-1; KD.head[h].id=0; KD.head[h].pend=0; KD.head[h].fw=0; return ok; }

int nvgsp_mode(int all){
    nvl_s("[20] second screen through GSP-RM: display object, core and window channels, native mode from EDID, the desktop picture mirrored"); nvl_nl();
    KD.why[0]=0;
    if(G.state<7)return kd_fail("the NVIDIA card is not running - run nvgsp all first");
    if(!mon_scan())return kd_fail(g_mon_why[0]?g_mon_why:"the monitors could not be read");
    if(!kd_init())return 0;
    for(int h=0;h<4;h++) if(KD.head[h].or>=0&&KD.head[h].pend){ if(!kd_head_off(h))return kd_fail("an earlier mode set is still unfinished in the display engine"); }
    for(int h=0;h<4;h++) if(KD.head[h].or>=0&&!KD.head[h].fw){ int here=0; for(int i=0;i<g_mon_n&&i<8;i++) if(g_mon[i].connected&&g_mon[i].id==KD.head[h].id)here=1; if(!here){ nvl_s("  the monitor on head "); nvl_d((u64)h); nvl_s(" is gone - head turned off"); nvl_nl(); kd_head_off(h); } }
    nvgsp_mode_marks(); int mor[8]; for(int i=0;i<8;i++)mor[i]=-1;
    u32 act[4]={0,0,0,0}; u32 hmask=g_mon_hmask?g_mon_hmask:0x3;
    for(int h=0;h<4;h++){ if(!(hmask&(1u<<h)))continue; if(KD.head[h].or>=0){ act[h]=KD.head[h].id; continue; }
        u32 g[4]={0,(u32)h,0,0}; if(rm_ctrl(HC(2),KD.hd,0x730126u,g,16)==0)act[h]=g[3]; }
    u32 fwsors=0;
    for(int i=0;i<g_mon_n&&i<8;i++){ nvgsp_mon_t* m=&g_mon[i]; if(m->lit==2)continue; m->head=-1; m->lit=0;
        for(int h=0;h<4;h++) if(act[h]&&act[h]==m->id&&KD.head[h].or<0){ m->head=h; m->lit=1;
            u8 o[56]; for(int k=0;k<56;k++)o[k]=0; *(u32*)(o+4)=m->id; if(rm_ctrl(HC(2),KD.hd,0x73028Bu,o,56)==0&&*(u32*)(o+8)<4){ mor[i]=(int)*(u32*)(o+8); fwsors|=1u<<mor[i]; } } }
    int done=0, tried=0, con=0;
    for(int i=0;i<g_mon_n&&i<8;i++){ nvgsp_mon_t* m=&g_mon[i]; if(!m->connected)continue; con++;
        if(m->lit==2)continue; if(m->lit==1&&!all)continue;
        if(m->or_type!=2||!(m->proto==1||m->proto==2)){ nvl_s("  output "); nvl_x(m->id); nvl_s(": "); nvl_s(nvgsp_mon_sig(m)); nvl_s(" is not driven yet (only HDMI and single-link DVI are)"); nvl_nl(); continue; }
        kmode md; if(!m->edid_ok||!nvk_edid_mode(g_mon_edid[i],m->edid_len>256?256:m->edid_len,&md)){ nvl_s("  output "); nvl_x(m->id); nvl_s(": no usable native mode in the EDID"); nvl_nl(); continue; }
        int hdmi=nvk_edid_hdmi(g_mon_edid[i],m->edid_len>256?256:m->edid_len);
        if(md.khz>(hdmi?340000u:165000u)){ nvl_s("  output "); nvl_x(m->id); nvl_s(": native mode needs a "); nvl_d(md.khz/1000); nvl_s(" MHz pixel clock, above what this link type carries"); nvl_nl(); continue; }
        int h=-1; if(m->lit==1)h=m->head; else for(int k=0;k<4;k++) if((hmask&(1u<<k))&&!act[k]){ h=k; break; }
        if(h<0){ nvl_s("  output "); nvl_x(m->id); nvl_s(": no free display head"); nvl_nl(); continue; }
        u32 excl=(KD.sors|fwsors)&0xFu; if(m->lit==1&&mor[i]>=0)excl&=~(1u<<mor[i]);
        tried++; if(kd_head_on(h,m,&md,hdmi,(int)m->proto,excl,m->lit==1)){ act[h]=m->id; done++; } }
    nvl_s("  "); nvl_d((u64)con); nvl_s(" monitor(s) connected, "); nvl_d((u64)done); nvl_s(" set up now"); nvl_nl();
    if(tried&&!done)return 0;
    if(!tried&&!done){ int lit=0, fwl=0; for(int i=0;i<g_mon_n&&i<8;i++){ if(g_mon[i].lit==2)lit++; if(g_mon[i].lit==1&&g_mon[i].connected)fwl++; } if(lit){ nvl_s("  the second screen is already on"); nvl_nl(); return 1; }
        if(fwl>=2){ nvl_s("  both monitors already show a picture set by the firmware (nvgsp mode all sets their native modes)"); nvl_nl(); return 1; } return kd_fail(con<2?"only one monitor is connected to the NVIDIA card":"no second monitor could be set up (see the lines above)"); }
    return 1; }
int nvgsp_mode_off(void){
    nvl_s("[20] second screen off"); nvl_nl(); KD.why[0]=0;
    if(!KD.up||KD.gen!=g_gsp_boots)return kd_fail("no screen was set up by this driver");
    int n=0, ok=1; for(int h=0;h<4;h++) if(KD.head[h].or>=0&&!KD.head[h].fw){ n++; if(!kd_head_off(h))ok=0; }
    if(!n)return kd_fail("no second screen was set up by this driver");
    if(!ok)return kd_fail("the display engine did not finish turning the screen off");
    nvl_s("  "); nvl_d((u64)n); nvl_s(" screen(s) turned off"); nvl_nl(); return 1; }
void nvgsp_mode_marks(void){ int live=KD.up&&KD.gen==g_gsp_boots; for(int i=0;i<g_mon_n&&i<8;i++){ g_mon[i].head=-1; g_mon[i].lit=0; g_mon[i].act_w=0; g_mon[i].act_h=0; if(!live)continue;
        for(int h=0;h<4;h++) if(KD.head[h].or>=0&&!KD.head[h].pend&&KD.head[h].id==g_mon[i].id){ g_mon[i].head=h; g_mon[i].lit=2; g_mon[i].act_w=KD.head[h].m.hact; g_mon[i].act_h=KD.head[h].m.vact; } } }
void nvgsp_mode_lost(void){ g_kd_live=0; KD.up=0; for(int h=0;h<4;h++){ KD.head[h].or=-1; KD.head[h].win=-1; } for(int i=0;i<8;i++){ g_mon[i].head=-1; g_mon[i].lit=0; } }
int nvgsp_mode_info(nvgsp_mode_t* o){
    u8* z=(u8*)o; for(u32 i=0;i<sizeof(*o);i++)z[i]=0; o->up=KD.up&&KD.gen==g_gsp_boots&&G.state>=7; o->sets=KD.sets; o->timeouts=KD.timeouts; o->why=KD.why;
    if(!o->up)return 0; int n=0;
    for(int h=0;h<4;h++) if(KD.head[h].or>=0&&!KD.head[h].pend&&!KD.head[h].fw){ o->h[n].head=h; o->h[n].id=KD.head[h].id; o->h[n].sor=KD.head[h].or; o->h[n].hdmi=KD.head[h].hdmi; o->h[n].w=KD.head[h].m.hact; o->h[n].h=KD.head[h].m.vact; o->h[n].khz=KD.head[h].m.khz; o->h[n].ow=KD.head[h].ow; o->h[n].oh=KD.head[h].oh; n++; }
    o->n=n; return n; }
