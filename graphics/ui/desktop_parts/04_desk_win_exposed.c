int desk_win_exposed(int i){
    if(!WN[i].open||WN[i].min||WN[i].ws!=g_ws||i==anim_hide||drag>=0||g_resize>=0||g_fdrag||g_ddrag>=0||g_marq) return 0;
    if(g_launcher||g_qs||g_clockpop||g_ctx||g_submenu||g_imgview>=0||g_alttab||g_rename||g_prop||g_wifipop||g_notifpanel||g_toast_cur>=0||toast_t) return 0;
    for(int k=0;k<NWIN;k++) if(k!=i&&WN[k].open&&!WN[k].min&&WN[k].ws==g_ws&&WN[k].z>WN[i].z) return 0;
    return 1;
}
void present_rect(int x,int y,int w,int h){
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;} if(x+w>(int)W)w=(int)W-x; if(g_tby>0&&y+h>g_tby)h=g_tby-y; if(y+h>(int)H)h=(int)H-y; if(w<=0||h<=0)return;
    if(g_nightlight) for(int r=0;r<h;r++){ u32* p=&SCENE[(u64)(y+r)*SW+x]; for(int k=0;k<w;k++){ u32 c=p[k]; u32 g=(c>>8)&255, b=c&255; p[k]=(c&0xFF0000)|((g*15/16)<<8)|(b*5/8); } }
    quiet_on();
    for(int r=0;r<h;r++) sse_copy32(SCENE_NC+(u64)(y+r)*SW+x, SCENE+(u64)(y+r)*SW+x, w);
    blit_region(x,y,w,h);
    if(cxlast>=0 && cxlast+CURBB/2>x && cxlast-CURBB/2<x+w && cylast+CURBB/2>y && cylast-CURBB/2<y+h){ draw_cursor(); blit_region(mx-CURBB/2,my-CURBB/2,CURBB,CURBB); nc_restore(mx-CURBB/2,my-CURBB/2,CURBB,CURBB); cxlast=mx; cylast=my; }
    quiet_off();
}
void nc_restore(int x,int y,int w,int h){     /* copy cursor-free cache back into SCENE[region] */
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;} if(x+w>(int)W)w=(int)W-x; if(y+h>(int)H)h=(int)H-y; if(w<=0||h<=0)return;
    g_scene_quiet++; for(int r=0;r<h;r++){ u32* d=SCENE+(u64)(y+r)*SW+x; u32* s=SCENE_NC+(u64)(y+r)*SW+x; sse_copy32(d,s,w); } g_scene_quiet--;
}
char g_fc_msg[240];
static char* fcs(char* p,const char* s){ while(*s)*p++=*s++; return p; }
static char* fcd(char* p,int v){ if(v<0){ *p++='-'; v=-v; } return udec(p,(u32)v); }
static char* fch(char* p,u32 v){ static const char X[]="0123456789ABCDEF"; for(int i=28;i>=0;i-=4)*p++=X[(v>>i)&15]; return p; }
static int fc_pass(u32* ref,int cpu,int* nd,int* bx0,int* by0,int* bx1,int* by1,int* fx,int* fy,u32* gv,u32* cv){
    extern int g_gq_mode; gfx_invalidate_all(); int m0=g_gfx, q0=g_gq_mode;
    if(cpu){ g_gfx=GFX_EMU; g_gq_mode=GQM_CPU; } else gq_log_begin(gs_scene);
    compose_build(); gfx_sync(); if(!cpu) gq_log_end();
    if(cpu){ g_gfx=m0; g_gq_mode=q0; }
    if(g_gfx==GFX_OFF) return -1;
    u32* sc=g_scene;
    if(!cpu){ for(u64 k=0;k<(u64)SW*H;k++)ref[k]=sc[k]; return 0; }
    int n=0; *bx0=1<<30; *by0=1<<30; *bx1=-1; *by1=-1;
    for(u32 y=0;y<H;y++){ u32* a=ref+(u64)y*SW; u32* b=sc+(u64)y*SW; for(u32 x=0;x<W;x++){ if(((a[x]^b[x])&0x00FFFFFFu)==0)continue;
        if(!n){ *fx=(int)x; *fy=(int)y; *gv=a[x]; *cv=b[x]; } n++; if((int)x<*bx0)*bx0=(int)x; if((int)y<*by0)*by0=(int)y; if((int)x>*bx1)*bx1=(int)x; if((int)y>*by1)*by1=(int)y; } }
    *nd=n; return 0; }
void frame_check(void){
    char* p=g_fc_msg;
    if(g_gfx==GFX_OFF){ p=fcs(p,"frame check needs GPU drawing"); *p=0; return; }
    { extern int g_nvd_vram_n; if(g_nvd_vram_n>0){ p=fcs(p,"frame check is off while GPU work buffers are in graphics memory (the self-test of step 18 checks the GPU instead)"); *p=0; return; } }
    u32* ref=(u32*)kmalloc((unsigned long)SW*H*4UL+64UL); if(!ref){ p=fcs(p,"frame check: not enough memory"); *p=0; return; }
    extern int g_gq_serial; extern int hr_precise(void); int ser0=g_gq_serial; int irq=hr_precise(); if(irq)cpu_irq_off();
    int nd=0,bx0=0,by0=0,bx1=0,by1=0,fx=0,fy=0; u32 gv=0,cv=0; char op[96]; op[0]=0; int ok=-1, nd0=0;
    for(int at=0;at<2;at++){
        if(at==1)g_gq_serial=1;
        if(fc_pass(ref,0,0,0,0,0,0,0,0,0,0)||fc_pass(ref,1,&nd,&bx0,&by0,&bx1,&by1,&fx,&fy,&gv,&cv)){ ok=-2; break; }
        if(!nd){ ok=at; break; }
        if(at==0){ nd0=nd; gq_log_find(fx,fy,op,sizeof(op));
            p=fcs(p,"frame check: "); p=udec(p,(u32)nd); p=fcs(p," pixels differ in "); p=fcd(p,bx0); *p++=','; p=fcd(p,by0); *p++='-'; p=fcd(p,bx1+1); *p++=','; p=fcd(p,by1+1);
            p=fcs(p,"; first "); p=fcd(p,fx); *p++=','; p=fcd(p,fy); p=fcs(p," GPU "); p=fch(p,gv&0xFFFFFFu); p=fcs(p," CPU "); p=fch(p,cv&0xFFFFFFu); p=fcs(p,"; last drawn by "); p=fcs(p,op); } }
    if(irq)cpu_irq_on();
    if(ok==0){ g_gq_serial=ser0; p=g_fc_msg; p=fcs(p,"frame check: the GPU frame matches the CPU frame pixel for pixel"); }
    else if(ok==1){ p=fcs(p,"; with a wait between operations it matches, so that mode stays on"); }
    else if(ok==-2){ g_gq_serial=ser0; p=g_fc_msg; p=fcs(p,"frame check could not run: GPU drawing stopped"); }
    else { g_gq_serial=ser0; p=fcs(p,"; with waits it still differs ("); p=udec(p,(u32)nd); p=fcs(p," pixels)"); (void)nd0; }
    *p=0; kfree(ref); gfx_invalidate_all(); klog(g_fc_msg); }
int frame_check_cmd(void){ if(g_state!=ST_DESKTOP||g_gfx==GFX_OFF)return -1; frame_check(); return 0; }
u32 g_comp_us=0, g_comp_max_us=0, g_in_lag_ms=0; static u32 g_comp_max_acc=0, g_lag_acc=0; static u64 g_perf_t=0;
static void compose_present_body(void);
int g_comp_depth;
void compose_present(void){ extern u64 hr_ns(void); u64 t0=hr_ns(); g_comp_depth++; compose_present_body(); g_comp_depth--; u32 us=(u32)((hr_ns()-t0)/1000ULL); g_comp_us=us; if(us>g_comp_max_acc)g_comp_max_acc=us;
    if(g_ticks-g_perf_t>=100){ g_perf_t=g_ticks; g_comp_max_us=g_comp_max_acc; g_comp_max_acc=0; extern u32 g_in_lag_acc; g_in_lag_ms=g_in_lag_acc; g_in_lag_acc=0; } }
static void compose_present_body(void){
    { extern int pm_screen_off_draw(void); if(pm_screen_off_draw())return; }
    compose_build();
    fps_cnt++; if(g_ticks-fps_t0>=100){ fps_val=fps_cnt; fps_cnt=0; fps_t0=g_ticks; }
    if(g_fps_overlay){ char b[16],*p=b; p=udec(p,(u32)fps_val); p=scopy(p," FPS"); *p=0;
        extern u32 g_gfx_frame_us, g_gfx_gpu_us, g_gfx_pres_us, g_gq_frame_syncs; extern int g_nvd_on; extern int nvgsp_present_on(void);
        char l1[96],*q=l1; q=scopy(q,g_gfx==1?(g_nvd_on?"GPU NVIDIA":"GPU"):g_gfx==2?"GPU emu":"CPU"); q=scopy(q," draw, screen copy "); q=scopy(q,nvgsp_present_on()?"copy engine":"CPU"); { extern int g_fb_wc; q=scopy(q,g_fb_wc?", screen WC":", screen UC"); } *q=0;
        char l2[96]; q=l2; q=scopy(q,"frame "); q=udec(q,g_comp_us/1000u); *q++='.'; q=udec(q,(g_comp_us/100u)%10u); q=scopy(q," ms  max "); q=udec(q,g_comp_max_us/1000u); q=scopy(q," ms  input wait "); q=udec(q,g_in_lag_ms); q=scopy(q," ms"); *q=0;
        char l3[96]; q=l3; q=scopy(q,"GPU wait "); q=udec(q,g_gfx_gpu_us/1000u); q=scopy(q," ms  present "); q=udec(q,g_gfx_pres_us/1000u); q=scopy(q," ms  syncs "); q=udec(q,g_gq_frame_syncs); *q=0;
        int bw=(int)slen(l2)*8+20; if((int)slen(l1)*8+20>bw)bw=(int)slen(l1)*8+20; int bx=(int)W-bw-10;
        fill_round(bx,8,bw,82,6,15,rgb(20,22,30)); dstr(bx+10,13,b,rgb(120,240,140),1); dstr(bx+10,31,l1,rgb(220,224,236),1); dstr(bx+10,49,l2,rgb(220,224,236),1); dstr(bx+10,67,l3,rgb(220,224,236),1);
        if(g_dmg_ok){ int x0=bx, y0=8, x1=bx+bw, y1=90; if(g_dmw>0&&g_dmh>0){ if(g_dmx<x0)x0=g_dmx; if(g_dmy<y0)y0=g_dmy; if(g_dmx+g_dmw>x1)x1=g_dmx+g_dmw; if(g_dmy+g_dmh>y1)y1=g_dmy+g_dmh; } g_dmx=x0; g_dmy=y0; g_dmw=x1-x0; g_dmh=y1-y0; } }
    int part=g_gfx&&g_dmg_ok;
    if(g_gfx){ if(!part) gq_copy(gs_nc,0,0,(int)W,(int)H,gs_scene,0,0,256,0); else if(g_dmw>0&&g_dmh>0) gq_copy(gs_nc,g_dmx,g_dmy,g_dmw,g_dmh,gs_scene,g_dmx,g_dmy,256,0); }
    else memcpy(SCENE_NC, SCENE, (u64)W*H*4);
    int skip=part&&g_dmw<=0&&cxlast==mx&&cylast==my;
    quiet_on();
    if(skip) g_frames_skipped++;
    else { draw_cursor();
        if(part){ extern void igpu_present_hint(int,int,int,int); int x0=mx-CURBB/2, y0=my-CURBB/2, x1=x0+CURBB, y1=y0+CURBB;
            if(cxlast>=0){ int a=cxlast-CURBB/2, b=cylast-CURBB/2; if(a<x0)x0=a; if(b<y0)y0=b; if(a+CURBB>x1)x1=a+CURBB; if(b+CURBB>y1)y1=b+CURBB; }
            if(g_dmw>0&&g_dmh>0){ if(g_dmx<x0)x0=g_dmx; if(g_dmy<y0)y0=g_dmy; if(g_dmx+g_dmw>x1)x1=g_dmx+g_dmw; if(g_dmy+g_dmh>y1)y1=g_dmy+g_dmh; }
            igpu_present_hint(x0,y0,x1-x0,y1-y0); g_frames_part++; }
        blit_all(); }
    if(g_gfx) gfx_frame_end();
    if(!skip) nc_restore(mx-CURBB/2,my-CURBB/2,CURBB,CURBB);
    cxlast=mx; cylast=my;
    quiet_off(); if(g_gfx) gfx_scene_settle();
}
/* fast path: only the cursor moved -> repaint just the old + new cursor rectangles */

/* cheap idle update: redraw only the top bar (clock/battery) instead of the whole screen */
void present_topbar(void){
    int ty=g_tby>0?g_tby:(int)H-64, th=(int)H-ty;
    if(g_gfx){ gfx_sync(); gq_ext_mark(gs_scene,0,ty,(int)W,th); } g_scene_quiet++;
    for(int y=ty;y<ty+th;y++){ u32* d=SCENE+(u64)y*SW; u32* s=BG+(u64)y*SW; sse_copy32(d,s,(int)W); }
    draw_topbar(); night_tint(ty,ty+th);
    for(int y=ty;y<ty+th;y++){ u32* d=SCENE_NC+(u64)y*SW; u32* s=SCENE+(u64)y*SW; sse_copy32(d,s,(int)W); }
    blit_region(0,ty,(int)W,th);
    if(cxlast>=0 && cylast > ty-CURBB) present_cursor();                                             /* re-overlay cursor if it's up there */
    if(g_gfx)gfx_sync(); g_scene_quiet--;
}
void present_cursor(void){
    quiet_on();
    if(cxlast>=0) blit_region(cxlast-CURBB/2, cylast-CURBB/2, CURBB, CURBB);   /* erase old (SCENE is cursor-free) */
    draw_cursor();                                                             /* draw into SCENE at mx,my */
    blit_region(mx-CURBB/2, my-CURBB/2, CURBB, CURBB);                         /* show new */
    nc_restore(mx-CURBB/2, my-CURBB/2, CURBB, CURBB);                          /* restore SCENE to cursor-free */
    cxlast=mx; cylast=my;
    quiet_off();
}
static u64 g_pump_last=0; static int g_pump_hooked=0;
volatile int g_busy_esc=0;
static int pump_building(void){ extern int g_gfx, g_rt, gs_scene; return g_gfx&&g_rt!=gs_scene; }
void busy_pump(void);
void desk_nested_step(void){
    extern int g_pi_depth;
    static int in=0; if(in||g_state!=ST_DESKTOP||g_comp_depth)return; in=1;
    { extern volatile u32 g_ml_beat; g_ml_beat++; }
    extern void tp_poll(void); extern void etp_poll(void); extern void syn_poll(void); extern void vm_mouse_poll(void); extern void acpi_poll(void); extern void snd_poll(void); extern void media_tick(void); extern int process_input(void);
    tp_poll(); etp_poll(); syn_poll(); vm_mouse_poll(); if(xhid_n) xhci_mouse_poll();
    { static u64 ap=0; if(g_ticks-ap>=2){ ap=g_ticks; acpi_poll(); snd_poll(); } }
    media_tick();
    if(kdown[0x01]&&focus==W_BROWSER)g_busy_esc=1;
    if(g_pi_depth){ int dx,dy; extern void in_mq_rebase_locked(int,int); cpu_irq_off(); dx=mouse_dx; dy=mouse_dy; mouse_dx=0; mouse_dy=0; in_mq_rebase_locked(dx,dy); cpu_irq_on(); if(dx||dy){ mx+=dx; my-=dy; if(mx<0)mx=0; if(my<0)my=0; if(mx>=(int)W)mx=W-1; if(my>=(int)H)my=H-1; g_cursor_moved=1; } }
    else process_input();
    compose_present(); g_cursor_moved=0;
    in=0; }
void busy_pump(void){
    { extern volatile u32 g_ml_beat; g_ml_beat++; }
    static int in=0; static u64 last=0, lastp=~0ULL; if(in||g_state!=ST_DESKTOP||g_ticks==lastp)return; lastp=g_ticks; in=1;
    if(xhid_n) xhci_mouse_poll();
    int dx,dy; extern void in_mq_rebase_locked(int,int); cpu_irq_off(); dx=mouse_dx; dy=mouse_dy; mouse_dx=0; mouse_dy=0; in_mq_rebase_locked(dx,dy); cpu_irq_on();
    if((dx||dy)&&g_mouse_accel){ int m=(g_mouse_accel==1)?3:5; if(dx>6||dx<-6)dx=dx*m/2; if(dy>6||dy<-6)dy=dy*m/2; }
    if(dx||dy){ mx+=dx; my-=dy; if(mx<0)mx=0; if(my<0)my=0; if(mx>=(int)W)mx=W-1; if(my>=(int)H)my=H-1; g_cursor_moved=1; }
    if(kdown[0x01])g_busy_esc=1;
    if((cxlast!=mx||cylast!=my)&&g_ticks!=last&&!pump_building()){ last=g_ticks; present_cursor(); }
    { extern unsigned long web_busy_ms(void); static u64 bdraw=0; unsigned long bm=web_busy_ms();
      if(bm>3000&&!pump_building()&&g_ticks-bdraw>=100&&WN[W_BROWSER].open){ bdraw=g_ticks; extern int g_round_off; int sv=g_round_off; g_round_off=1;
        const char* m1="This page is busy."; const char* m2="Press Esc to stop it, or wait."; int bw=300, bh=58, bx=WN[W_BROWSER].x+WN[W_BROWSER].w/2-bw/2, by=WN[W_BROWSER].y+WN[W_BROWSER].h/2-bh/2;
        fill_rect(bx,by,bw,bh,rgb(212,208,200)); ui_bevel(bx,by,bw,bh,0); dstr(bx+14,by+10,m1,rgb(0,0,0),1); dstr(bx+14,by+32,m2,rgb(0,0,0),1);
        { char t[16],*p=t; p=udec(p,(u32)(bm/1000)); p=scopy(p," s"); *p=0; dstr(bx+bw-14-slen(t)*8,by+10,t,rgb(64,64,64),1); }
        g_round_off=sv; present_rect(bx,by,bw,bh); } }
    in=0; }
void ui_pump(void){
    if(!g_pump_hooked){ g_wpa_pump=ui_pump; g_pump_hooked=1; }
    if(xhid_n) xhci_mouse_poll();
    if(g_ticks-g_pump_last>=2&&!pump_building()){ g_pump_last=g_ticks; present_cursor(); }
}

void win_anim(int i,int opening){ (void)i; (void)opening; return;   /* animations off: windows open/close instantly (smoother) */ }
static void win_anim_unused(int i,int opening){
    if(g_safemode) return;
    int wx=WN[i].x,wy=WN[i].y,ww=WN[i].w,wh=WN[i].h;
    if(ww<=0||wh<=0||ww>ANIMW||wh>ANIMH||wx<0||wy<0||wx+ww>(int)W||wy+wh>(int)H) return;   /* skip if off-screen/too big */
    anim_hide=-1; compose_build();
    for(int r=0;r<wh;r++){ u32* s=&SCENE[(u64)(wy+r)*SW+wx]; u32* d=&WANIM[r*ANIMW]; for(int cc=0;cc<ww;cc++)d[cc]=s[cc]; }
    int frames=7;
    for(int f=0;f<=frames;f++){ int t=opening?f:(frames-f); int sc=800+200*t/frames; int al=40+216*t/frames;
        anim_hide=i; compose_build();
        int sw=ww*sc/1000, sh=wh*sc/1000; if(sw<1)sw=1; if(sh<1)sh=1; int ox=wx+(ww-sw)/2, oy=wy+(wh-sh)/2;
        for(int r=0;r<sh;r++){ int dy=oy+r; if(dy<0||dy>=(int)H)continue; int srcr=r*wh/sh; u32* srow=&WANIM[srcr*ANIMW];
            for(int cc=0;cc<sw;cc++){ int dx=ox+cc; if(dx<0||dx>=(int)W)continue; int srcc=cc*ww/sw; u32* dd=&SCENE[(u64)dy*SW+dx]; *dd=mix(*dd,srow[srcc],al); } }
        draw_cursor(); blit_all(); spin(1300000);
    }
    anim_hide=-1;
}


void snd_apply_volume(int v){ if(g_snd_state)g_snd.volume(v); }
void desk_nv_progress(int step,const char* what){
    { extern volatile u32 g_ml_beat; g_ml_beat++; }
    if(g_gfx||step<=0||g_state!=ST_DESKTOP)return;
    int w=560, h=50, x=((int)W-w)/2, y=44; if(x<0)x=0;
    fill_round(x,y,w,h,10,15,rgb(24,28,40));
    char b[72],*p=b; p=scopy(p,"Starting the NVIDIA GPU - step "); p=udec(p,(u32)step); p=scopy(p," of 19"); *p=0; dstr(x+16,y+9,b,rgb(232,236,246),1);
    char t[66]; int i=0; while(what&&what[i]&&i<64){ t[i]=what[i]; i++; } t[i]=0; dstr(x+16,y+27,t,rgb(150,160,182),1);
    int bw=(w-32)*(step>19?19:step)/19; fill_rect(x+16,y+h-7,w-32,3,rgb(60,66,84)); fill_rect(x+16,y+h-7,bw,3,ACC);
    blit_region(x,y,w,h); }
