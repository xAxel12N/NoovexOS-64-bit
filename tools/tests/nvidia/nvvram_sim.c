static int vl_eq(const u32* a,const u32* b,int n){ for(int i=0;i<n;i++) if((a[i]|0xFF000000u)!=(b[i]|0xFF000000u))return 0; return 1; }
static void nvvram_tests(void){
    g_gfx=0; nvc_set_ok(1); int keep=g_nvd_vram; g_nvd_vram=1; u64 kb=g_vl_budget; g_vl_budget=0; int st=nvd_start()==0&&g_nvd_on;
    u32 *lp=0, *dp=0, *l2=0; int L=st?nd_alloc(256,64,256,&lp):-1, D=st?nd_alloc(256,64,256,&dp):-1, L2=-1; int n=256*64;
    if(L<=0||D<=0){ chk("window layers in VRAM: test surfaces",0); goto out; }
    for(int i=0;i<n;i++)lp[i]=(u32)(i*2654435761u&0x00FFFFFFu); gq_bump(L);
    u32 t0=g_vl_up_tiles, h0=g_vl_hits; gq_overc(D,0,0,256,64,L,0,0,256); int s1=!gq_sync();
    int first=s1&&vl_eq(dp,lp,n)&&g_vl_up_tiles-t0==16&&nvl_dirty_tiles(L)==0&&g_vl_hits>h0&&VL[L].on&&vl_eq((u32*)(VR+VL[L].vram),lp,n);
    chk("window layer kept in VRAM: first use uploads all 16 tiles, the compositor's read comes from the VRAM copy",first);
    lp[5]^=0x00FFFFFFu; for(int i=0;i<n;i++)dp[i]=0; gq_overc(D,0,0,256,63,L,0,0,256); int s2=!gq_sync();
    int fromv=s2&&dp[5]==(lp[5]^0x00FFFFFFu)&&g_vl_up_tiles-t0==16; lp[5]^=0x00FFFFFFu;
    chk("an unmarked RAM change is not read: the shader reads the VRAM copy, not system RAM",fromv);
    u32 t1=g_vl_up_tiles, r1=g_vl_up_rects; lp[20*256+70]=0x00123456u; lp[40*256+200]=0x00654321u; gq_ext_mark(L,70,20,1,1); gq_ext_mark(L,200,40,1,1);
    gq_overc(D,0,0,256,62,L,0,0,256); int s3=!gq_sync();
    chk("only changed tiles are uploaded: two marked pixels send 2 tiles in 2 rectangles and the compositor sees them",s3&&g_vl_up_tiles-t1==2&&g_vl_up_rects-r1==2&&dp[20*256+70]==0x00123456u&&dp[40*256+200]==0x00654321u);
    u32 t2=g_vl_up_tiles; gq_fill(L,0,0,64,16,0x0000FF00u); gq_overc(D,0,0,256,61,L,0,0,256); int s4=!gq_sync(); int wrote=s4&&dp[0]==0x0000FF00u;
    gq_overc(D,0,0,256,60,L,0,0,256); int s5=!gq_sync();
    chk("a tile the GPU drew into is read from RAM in the same batch and uploaded before the next",wrote&&s5&&nvl_dirty_tiles(L)==0&&dp[0]==0x0000FF00u&&((u32*)(VR+VL[L].vram))[0]==0x0000FF00u);
    l2=0; L2=nd_alloc(256,64,256,&l2); for(int i=0;i<n&&l2;i++)l2[i]=(u32)i; if(L2>0)gq_bump(L2);
    g_vl_budget=VL[L].len; u32 e0=g_vl_evicts; u64 fb0=nvv_free_bytes();
    gq_overc(D,0,0,256,59,L2,0,0,256); int s6=L2>0&&!gq_sync();
    chk("under VRAM budget pressure the least recently used layer is evicted and its VRAM freed",s6&&g_vl_evicts>e0&&!VL[L].on&&VL[L2].on&&vl_eq(dp,l2,256*59)&&nvv_free_bytes()>=fb0-VL[L2].len);
    g_vl_budget=0x1000; u32 mm=g_vl_miss; gq_overc(D,0,0,256,58,L,0,0,256); int s7=!gq_sync();
    chk("a layer larger than the VRAM budget is read from RAM",s7&&g_vl_miss>mm&&!VL[L].on&&vl_eq(dp,lp,256*58));
    g_vl_budget=0; u32 m0=g_vl_miss, f0=g_vl_fail; g_ce_busy=1; lp[0]=0x00010101u; gq_bump(L); gq_overc(D,0,0,256,57,L,0,0,256); int s8=!gq_sync(); g_ce_busy=0;
    chk("a failed upload drops that VRAM copy and the op reads RAM",s8&&g_vl_miss>m0&&g_vl_fail==f0+1&&!VL[L].on&&dp[0]==0x00010101u&&vl_eq(dp,lp,256*57));
    { u64 fb1=nvv_free_bytes(); int had=VL[L2].on; gq_surf_free(L2); L2=-1; chk("freeing a surface frees its VRAM copy",had&&nvv_free_bytes()>fb1&&g_vl_n==0); }
    out:
    if(L2>0)gq_surf_free(L2); if(L>0)gq_surf_free(L); if(D>0)gq_surf_free(D); gfx_stop("test"); g_nvd_vram=keep; g_vl_budget=kb; g_vl_fail=0;
    chk("stopping GPU drawing gives every VRAM layer back",g_vl_n==0&&g_vl_bytes==0&&!g_gq_mark_hook);
    nvset_t sv=NVSE[NVSET_CARDMEM]; nvset_t* c=&NVSE[NVSET_CARDMEM]; c->on=1; c->off=0; c->skip=0; c->fails=0; c->tried=0; c->st=0; c->err[0]=0; g_vl_fail=0; nd_cm=0; nd_cm_vf=0;
    g_nvd_vram=1; g_gfx=0; nvc_set_ok(1); int on1=nvd_start()==0&&g_nvd_on; nd_cardmem(); int tr=on1&&c->tried==1&&nd_cm==1;
    g_gfx_frames+=5; nd_cardmem(); int act=tr&&!c->tried&&nvset_state(NVSET_CARDMEM)==NVSET_ST_ACTIVE;
    chk("card memory start guard: tried is saved while the first frames run, an unfinished start turns it off next boot, 5 good frames mark it working",tr&&act);
    for(int k=0;k<2;k++){ g_vl_fail++; nd_cardmem(); } int still=g_nvd_vram==1&&nvset_allowed(NVSET_CARDMEM)&&c->fails==2;
    g_vl_fail++; nd_cardmem(); int off=still&&!g_nvd_vram&&!nd_lay()&&nvset_state(NVSET_CARDMEM)==NVSET_ST_AUTO_OFF&&c->err[0];
    chk("three failed VRAM layer uploads turn card memory off with the reason in Settings, layers fall back to RAM",off);
    gfx_stop("test"); nvset_reset(NVSET_CARDMEM); g_vl_fail=0; nd_cardmem(); g_nvd_vram=1; nvc_set_ok(1); int on2=nvd_start()==0&&g_nvd_on; nd_cardmem(); int tr2=on2&&c->tried==1;
    gfx_stop("a GPU drawing batch failed"); nd_cardmem(); int can=tr2&&!c->tried&&c->fails==0&&nd_cm==0&&nvset_allowed(NVSET_CARDMEM);
    chk("GPU drawing stopping for another reason clears the card memory start guard without counting a failure",can);
    *c=sv; nd_cm=0; nd_cm_vf=0; g_vl_fail=0; g_nvd_vram=keep; }
