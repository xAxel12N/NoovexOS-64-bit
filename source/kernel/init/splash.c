/* =========================================================================
   Boot splash  (synthwave)
   ========================================================================= */
static void spin(int n){ for(volatile int i=0;i<n;i++) __asm__ volatile("pause"); }
static int isqrt_i(int v){ if(v<=0)return 0; int a=1; while(a*a<=v)a++; return a-1; }
/* 8-bit coverage wordmark, scaled to width tw, centred horizontally at cx, top-left y=top, colour col */
static void draw_wordmark(int cx,int top,int tw,u32 col){
    int th=tw*WM_H/WM_W; if(tw<=0||th<=0)return; int x0=cx-tw/2;
    for(int y=0;y<th;y++){ int sy=y*WM_H/th; const unsigned char* srow=wordmark_a+(u64)sy*WM_W;
        for(int x=0;x<tw;x++){ int sx=x*WM_W/tw; int a=srow[sx]; if(a<6)continue;
            int dx=x0+x, dy=top+y; if((u32)dx<W&&(u32)dy<H){ u32*p=&SCENE[(u64)dy*SW+dx]; *p=mix(*p,col,a); } } }
}
/* purple gradient + perspective grid + retro sun (scanline gaps) + wordmark */
static void synthwave_scene(void){
    for(u32 y=0;y<H;y++){ u32 c=mix(rgb(26,6,42),rgb(120,22,124),(int)((y*256)/H)); for(u32 x=0;x<W;x++)SCENE[(u64)y*SW+x]=c; }
    int hor=(int)H*58/100; u32 grid=rgb(240,60,185);
    int yy=hor, step=4; while(yy<(int)H){ for(u32 x=0;x<W;x++){ u32*px=&SCENE[(u64)yy*SW+x]; *px=mix(*px,grid,150); } step+=3; yy+=step; }
    int vp=(int)W/2; for(int k=-12;k<=12;k++){ int bx=vp+k*((int)W/10); line_bl(vp,hor,bx,(int)H,grid,80,1); }
    int scx=(int)W/2, scy=(int)H*30/100, R=(int)H*20/100;
    for(int dy=-R;dy<=R;dy++){ int yv=scy+dy; if(yv<0||yv>=(int)H)continue; int hw=isqrt_i(R*R-dy*dy);
        if(dy>0 && (dy%6) >= (6-(dy*4)/R)) continue;                 /* scanline gaps, growing downward */
        u32 c=mix(rgb(255,238,120),rgb(255,40,150),(dy+R)*256/(2*R));
        for(int x=scx-hw;x<=scx+hw;x++)putpx(x,yv,c); }
    int wmw=(int)H*34/100, wmh=wmw*WM_H/WM_W; draw_wordmark(scx, scy-wmh/2, wmw, rgb(255,255,255));   /* logo centred on the sun */
}
static void splash_boot(void){
    /* just the logo, straight in (no fade), held ~5 seconds */
    for(u32 y=0;y<H;y++)for(u32 x=0;x<W;x++)SCENE[(u64)y*SW+x]=rgb(0,0,0);   /* black */
    int wmw=(int)H*52/100, wmh=wmw*WM_H/WM_W;
    draw_wordmark((int)W/2,(int)H/2 - wmh/2, wmw, rgb(255,255,255));         /* centred white wordmark */
    blit_all();
    u8 last=cmos(0); int elapsed=0;                                          /* RTC-timed 5-second hold */
    while(elapsed<5){ while(cmos(0x0A)&0x80){} u8 s=cmos(0); if(s!=last){ last=s; elapsed++; } spin(40000); }
}

