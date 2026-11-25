#define UVC_XV(X) X(cam) X(uvc_state) X(uvc_w) X(uvc_h) X(uvc_kind) X(uvc_fi) X(uvc_ri) X(uvc_altsel) X(uvc_lastcc) X(uvc_cfgcc) X(uvc_maxintr) \
    X(uvc_hdrbad) X(uvc_restarts) X(uvc_maxpay) X(uvc_maxfs) X(uvc_interval) X(uvc_events) X(uvc_tds) X(uvc_tderr) X(uvc_frames) X(uvc_badframes) X(uvc_short) X(uvc_bytes) X(uvc_seq) X(uvc_fps10) X(uvc_jlen) \
    X(uvc_vid) X(uvc_pid) X(uvc_name) X(uvc_msg) X(uvc_rgb) X(uvc_jpg) X(uvc_jnew) X(uvc_fb) X(uvc_bufs) X(uvc_ermem) X(uvc_ring) X(uvc_er) X(uvc_erst) X(uvc_iri) \
    X(uvc_enq) X(uvc_deq) X(uvc_cyc) X(uvc_out) X(uvc_ev1) X(uvc_ev1c) X(uvc_fill) X(uvc_fcap) X(uvc_fbad) X(uvc_lfid) X(uvc_ft0) X(uvc_fc0) X(uvc_cc) X(uvc_er_done) X(uvc_lasthd) X(uvc_inrst) X(uvc_wd_t) X(uvc_wd_f) X(uvc_reg_hold)
#define UVC_XF(v) __typeof__(v) v;
#define UVC_XS(v) __builtin_memcpy((void*)&uvc_sl[i].v,(const void*)&v,sizeof(v));
#define UVC_XL(v) __builtin_memcpy((void*)&v,(const void*)&uvc_sl[i].v,sizeof(v));
static struct { UVC_XV(UVC_XF) int used; } uvc_sl[UVC_NCAM];
static int uvc_cur=0;
static u32 uvc_bw_budget(int speed){ return speed>=4?56250u:speed==3?6000u:1350u; }
static int uvc_bw_class(int speed){ return speed>=4?2:speed==3?1:0; }
static u32 uvc_bw_left(int speed,u32 used){ u32 b=uvc_bw_budget(speed); return used>=b?0u:b-used; }
static void uvc_sv(int i){ UVC_XV(UVC_XS) uvc_sl[i].used=1; }
static void uvc_ld(int i){ if(!uvc_sl[i].used){ u8* z=(u8*)&uvc_sl[i]; for(u32 k=0;k<sizeof(uvc_sl[i]);k++)z[k]=0; uvc_sl[i].uvc_cyc=1; uvc_sl[i].uvc_ev1c=1; uvc_sl[i].uvc_lfid=-1; uvc_sl[i].uvc_reg_hold=-1; uvc_sl[i].uvc_iri=i?2+i:1; uvc_sl[i].cam.vs_if=-1; uvc_sl[i].used=1; } UVC_XV(UVC_XL) }
int uvc_selected(void){ return uvc_cur; }
int uvc_select(int i){ if(i<0||i>=UVC_NCAM)return -1; if(i==uvc_cur)return 0; uvc_sv(uvc_cur); uvc_ld(i); uvc_cur=i; return 0; }
int uvc_cam_ok(int i){ if(i<0||i>=UVC_NCAM)return 0; return i==uvc_cur?cam.ok:(uvc_sl[i].used&&uvc_sl[i].cam.ok); }
int uvc_count(void){ int n=0; for(int i=0;i<UVC_NCAM;i++)n+=uvc_cam_ok(i)?1:0; return n; }
static int uvc_st(int i){ return i==uvc_cur?(cam.ok&&uvc_state==2):(uvc_sl[i].used&&uvc_sl[i].cam.ok&&uvc_sl[i].uvc_state==2); }
int uvc_streaming_n(void){ int n=0; for(int i=0;i<UVC_NCAM;i++)n+=uvc_st(i)?1:0; return n; }
static int uvc_held(int r){ if(r<0)return 0; for(int i=0;i<UVC_NCAM;i++){ int h=i==uvc_cur?uvc_reg_hold:(uvc_sl[i].used?uvc_sl[i].uvc_reg_hold:-1); if(h==r)return 1; } return 0; }
static int uvc_free_slot(void){ for(int i=0;i<UVC_NCAM;i++) if(i!=uvc_cur&&!uvc_cam_ok(i)){ uvc_select(i); return 1; } return 0; }
static u32 uvc_bwleft(void){ u32 used=0; for(int i=0;i<UVC_NCAM;i++){ if(i==uvc_cur||!uvc_st(i))continue; if(uvc_sl[i].cam.isbulk||uvc_sl[i].cam.ctl!=cam.ctl||uvc_bw_class(uvc_sl[i].cam.speed)!=uvc_bw_class(cam.speed))continue; used+=(u32)uvc_sl[i].cam.esit; } return uvc_bw_left(cam.speed,used); }
void uvc_poll(void){ int other=0; for(int i=0;i<UVC_NCAM;i++) if(i!=uvc_cur&&uvc_st(i))other=1; if(!other){ uvc_poll1(); return; } int sv=uvc_cur; for(int i=0;i<UVC_NCAM;i++) if(uvc_st(i)){ uvc_select(i); uvc_poll1(); } uvc_select(sv); }
static int uvc_rest=-1;
static int uvc_attach(const usbloc_t* L,int d,const u8* full,int wtot,u8 cfgval,u16 vid,u16 pid,const char* nm){ int sv=uvc_cur, was=cam.ok; int r=uvc_attach1(L,d,full,wtot,cfgval,vid,pid,nm); if(was&&uvc_cur!=sv){ if(r)uvc_rest=sv; else uvc_select(sv); } return r; }
static void uvc_attach_done(void){ if(uvc_rest>=0){ uvc_select(uvc_rest); uvc_rest=-1; } }
