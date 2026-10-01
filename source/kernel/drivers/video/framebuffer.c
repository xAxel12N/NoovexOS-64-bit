/* =========================================================================
   Framebuffer + back buffers
   ========================================================================= */
#define MAXW 1920
#define MAXH 1080
static u32 SCENE[MAXW*MAXH];            /* working composite (32bpp)   */
static u32 BG[MAXW*MAXH];               /* wallpaper+icons cache        */

static u8  *FBB;                        /* hardware framebuffer         */
static u32  PITCHB, W, H, SW;           /* SW = scene stride (=W)       */
static u8   BPP; static u32 BYPP;

#define rgb(r,g,b) ( ((u32)(u8)(r)<<16) | ((u32)(u8)(g)<<8) | (u32)(u8)(b) )
static inline void putpx(int x,int y,u32 c){ if((u32)x<W&&(u32)y<H) SCENE[(u32)y*SW+x]=c; }
/* ---- software 2D acceleration: SSE fill/copy (SSE on at boot; kernel is -mno-sse so xmm is untouched by codegen) ---- */
static inline void sse_fill32(u32* d,u32 c,int n){ int i=0;
    __asm__ volatile("movd %0,%%xmm7; pshufd $0,%%xmm7,%%xmm7"::"r"(c));
    for(; i+16<=n; i+=16){ __asm__ volatile("movdqu %%xmm7,(%0); movdqu %%xmm7,16(%0); movdqu %%xmm7,32(%0); movdqu %%xmm7,48(%0)"::"r"(d+i):"memory"); }
    for(; i+4<=n; i+=4){ __asm__ volatile("movdqu %%xmm7,(%0)"::"r"(d+i):"memory"); }
    for(; i<n; i++) d[i]=c;
}
static int g_accel2d=1, g_ssse3=0;   /* 2D acceleration toggle; SSSE3 (pshufb) available */
/* pack 32bpp BGRX -> 24bpp BGR, 4 pixels per pshufb. Stores 16 bytes (4-byte overrun stays
   inside the row because we require >=4 more pixels after the group), tail is scalar. */
static const u8 pack24_mask[16] __attribute__((aligned(16)))={0,1,2,4,5,6,8,9,10,12,13,14,0x80,0x80,0x80,0x80};
static inline void sse_pack24(u8* d,const u32* s,int n){ int i=0;
    if(g_ssse3){ __asm__ volatile("movdqa %0,%%xmm6"::"m"(pack24_mask));
        for(; i+8<=n; i+=4){ __asm__ volatile("movdqu (%1),%%xmm0; pshufb %%xmm6,%%xmm0; movdqu %%xmm0,(%0)"::"r"(d+i*3),"r"(s+i):"memory"); } }
    for(; i<n; i++){ u32 c=s[i]; d[i*3]=(u8)c; d[i*3+1]=(u8)(c>>8); d[i*3+2]=(u8)(c>>16); }
}
static inline void sse_copy32(u32* d,const u32* s,int n){ int i=0;
    for(; i+16<=n; i+=16){ __asm__ volatile("movdqu (%1),%%xmm0; movdqu 16(%1),%%xmm1; movdqu 32(%1),%%xmm2; movdqu 48(%1),%%xmm3; movdqu %%xmm0,(%0); movdqu %%xmm1,16(%0); movdqu %%xmm2,32(%0); movdqu %%xmm3,48(%0)"::"r"(d+i),"r"(s+i):"memory"); }
    for(; i+4<=n; i+=4){ __asm__ volatile("movdqu (%1),%%xmm0; movdqu %%xmm0,(%0)"::"r"(d+i),"r"(s+i):"memory"); }
    for(; i<n; i++) d[i]=s[i];
}
static void fill_rect(int x,int y,int w,int h,u32 c){
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;} if(x+w>(int)W)w=(int)W-x; if(y+h>(int)H)h=(int)H-y; if(w<=0||h<=0)return;
    if(g_accel2d){ for(int j=0;j<h;j++) sse_fill32(SCENE+(u64)(y+j)*SW+x, c, w); }
    else { for(int j=0;j<h;j++){ u32* r=SCENE+(u64)(y+j)*SW+x; for(int i=0;i<w;i++) r[i]=c; } }
}
static inline u32  getpx(int x,int y){ return ((u32)x<W&&(u32)y<H)?SCENE[(u32)y*SW+x]:0; }
static inline u32 mix(u32 a,u32 b,int t){
    int ar=(a>>16)&255,ag=(a>>8)&255,ab=a&255,br=(b>>16)&255,bg=(b>>8)&255,bb=b&255;
    return rgb((u8)((ar*(256-t)+br*t)>>8),(u8)((ag*(256-t)+bg*t)>>8),(u8)((ab*(256-t)+bb*t)>>8));
}
static inline void pblend(int x,int y,u32 c,int a){ if((u32)x<W&&(u32)y<H){ u32*p=&SCENE[(u32)y*SW+x]; *p=mix(*p,c,a);} }
static u32 isqrt32(u32 v){ u32 r=0,b=1u<<30; while(b>v)b>>=2; while(b){ if(v>=r+b){v-=r+b;r=(r>>1)+b;} else r>>=1; b>>=2; } return r; }
static int cut(int i,int j,int w,int h,int r,int m){
    if(r<=0)return 0;
    if((m&1)&&i<r&&j<r){int dx=r-i,dy=r-j; if(dx*dx+dy*dy>r*r)return 1;}
    if((m&2)&&i>=w-r&&j<r){int dx=i-(w-1-r),dy=r-j; if(dx*dx+dy*dy>r*r)return 1;}
    if((m&4)&&i<r&&j>=h-r){int dx=r-i,dy=j-(h-1-r); if(dx*dx+dy*dy>r*r)return 1;}
    if((m&8)&&i>=w-r&&j>=h-r){int dx=i-(w-1-r),dy=j-(h-1-r); if(dx*dx+dy*dy>r*r)return 1;}
    return 0;
}
/* anti-aliased rounded rectangle */
static void fill_round(int x,int y,int w,int h,int r,int m,u32 c){
    for(int j=0;j<h;j++)for(int i=0;i<w;i++){
        int dx=-1,dy=0;
        if((m&1)&&i<r&&j<r){dx=r-i;dy=r-j;}
        else if((m&2)&&i>=w-r&&j<r){dx=i-(w-1-r);dy=r-j;}
        else if((m&4)&&i<r&&j>=h-r){dx=r-i;dy=j-(h-1-r);}
        else if((m&8)&&i>=w-r&&j>=h-r){dx=i-(w-1-r);dy=j-(h-1-r);}
        if(dx>=0){ u32 dd=isqrt32(((u32)(dx*dx+dy*dy))<<16); int cov=(int)(r*256)-(int)dd+128;
            if(cov<=0) continue; if(cov<256){ pblend(x+i,y+j,c,cov); continue; } }
        putpx(x+i,y+j,c);
    }
}
/* soft, layered drop shadow that skips the pixels the window will cover */
static void shadow_soft(int wx,int wy,int ww,int wh,int wr){
    int E[4]={12,8,5,2}, S[4]={14,10,6,3}, A[4]={6,8,10,12};
    for(int k=0;k<4;k++){ int e=E[k],s=S[k],a=A[k], x=wx-e,y=wy-e+s,w=ww+2*e,h=wh+2*e,r=wr+e;
        for(int j=0;j<h;j++)for(int i=0;i<w;i++){ int px=x+i,py=y+j;
            if(px>=wx&&px<wx+ww&&py>=wy&&py<wy+wh) continue;
            if(cut(i,j,w,h,r,15)) continue; pblend(px,py,rgb(0,0,0),a); } }
}
/* anti-aliased filled circle */
static void fill_circle(int cx,int cy,int r,u32 c){
    for(int j=-r-1;j<=r+1;j++)for(int i=-r-1;i<=r+1;i++){ int d2=i*i+j*j; if(d2>(r+1)*(r+1)) continue;
        u32 dd=isqrt32(((u32)d2)<<16); int cov=(int)(r*256)-(int)dd+128;
        if(cov<=0) continue; if(cov>=256) putpx(cx+i,cy+j,c); else pblend(cx+i,cy+j,c,cov); }
}

/* ---- text into SCENE ---- */
static int slen(const char*s){ int n=0; while(s[n])n++; return n; }
static void dchar(int x,int y,char ch,u32 c,int s){
    int idx=(u8)ch-32; if(idx<0||idx>=95)return; const u8*g=FONT814[idx];
    for(int r=0;r<14;r++){ u8 b=g[r]; for(int col=0;col<8;col++) if(b&(1u<<col)){
        if(s==1)putpx(x+col,y+r,c); else fill_rect(x+col*s,y+r*s,s,s,c);} }
}
static int dstr(int x,int y,const char*s,u32 c,int sc){ while(*s){dchar(x,y,*s,c,sc);x+=8*sc;s++;} return x; }
static void dstr_ctr(int cx,int y,const char*s,u32 c,int sc){ dstr(cx-(slen(s)*8*sc)/2,y,s,c,sc); }
/* ---- proportional text (variable width from the 8x14 bitmap ink bounds) ---- */
static int g_gl[95], g_gw[95], g_gm_ready=0;
static void gmetrics(void){ for(int idx=0;idx<95;idx++){ const unsigned char*g=fontaa[idx]; int lo=8,hi=-1;
        for(int r=0;r<14;r++){ for(int c=0;c<8;c++){ if(g[r*8+c]>40){ if(c<lo)lo=c; if(c>hi)hi=c; } } }
        if(hi<0){ g_gl[idx]=0; g_gw[idx]=0; } else { g_gl[idx]=lo; g_gw[idx]=hi-lo+1; } } g_gm_ready=1; }
static int gadv(char ch,int s){ int idx=(u8)ch-32; if(idx<0||idx>=95)return 4*s; if(g_gw[idx]==0)return 4*s; return (g_gw[idx]+1)*s; }
static int pw_sub(const char*str,int a,int b,int s){ int w=0; for(int i=a;str[i]&&i<b;i++)w+=gadv(str[i],s); return w; }
static int pw_str(const char*str,int s){ int w=0; for(int i=0;str[i];i++)w+=gadv(str[i],s); return w; }
static void pdch(int x,int y,char ch,u32 c,int s){ int idx=(u8)ch-32; if(idx<0||idx>=95||g_gw[idx]==0)return; const unsigned char*gp=fontaa[idx]; int ox=x-g_gl[idx]*s;
    for(int r=0;r<14;r++){ for(int col=0;col<8;col++){ int a=gp[r*8+col]; if(a<6)continue;
        if(s==1){ int px=ox+col,py=y+r; if((u32)px<W&&(u32)py<H){ u32*p=&SCENE[(u64)py*SW+px]; *p=mix(*p,c,a); } }
        else { for(int dy=0;dy<s;dy++)for(int dx=0;dx<s;dx++){ int px=ox+col*s+dx,py=y+r*s+dy; if((u32)px<W&&(u32)py<H){ u32*p=&SCENE[(u64)py*SW+px]; *p=mix(*p,c,a); } } } } } }
static int pdstr(int x,int y,const char*str,u32 c,int s){ for(int i=0;str[i];i++){ pdch(x,y,str[i],c,s); x+=gadv(str[i],s); } return x; }
static int pdsub(int x,int y,const char*str,int a,int b,u32 c,int s){ for(int i=a;str[i]&&i<b;i++){ pdch(x,y,str[i],c,s); x+=gadv(str[i],s); } return x; }
static void pdstr_ctr(int cx,int y,const char*s,u32 c,int sc){ pdstr(cx-pw_str(s,sc)/2,y,s,c,sc); }

/* ---- present SCENE -> hardware framebuffer (honours pitch + bpp) ---- */
static int g_panel_oled; static volatile u64 g_ticks;   /* fwd: OLED setting + PIT ticks */
static void blit_all(void){
    /* OLED care: shift the whole image by 1px through 4 positions, changing every ~30s */
    int shx=0,shy=0; if(g_panel_oled){ int ph=(int)((g_ticks/3000ULL)&3); shx=(ph==1)?1:(ph==3)?-1:0; shy=(ph==2)?1:0; }
    for(u32 y=0;y<H;y++){ int sr=(int)y-shy; if(sr<0)sr=0; if(sr>=(int)H)sr=(int)H-1; u32*s=SCENE+(u64)sr*SW;
        if(BYPP==4){ u32*d=(u32*)(FBB+(u64)y*PITCHB);
            if(!g_accel2d){ for(u32 x=0;x<W;x++){ int sx=(int)x-shx; if(sx<0)sx=0; if(sx>=(int)W)sx=(int)W-1; d[x]=s[sx]; } }
            else if(shx==0) sse_copy32(d,s,(int)W);
            else if(shx==1){ d[0]=s[0]; sse_copy32(d+1,s,(int)W-1); }
            else { sse_copy32(d,s+1,(int)W-1); d[W-1]=s[W-1]; } }
        else { u8*d=FBB+(u64)y*PITCHB;
            if(g_accel2d && shx==0) sse_pack24(d,s,(int)W);
            else for(u32 x=0;x<W;x++){ int sx=(int)x-shx; if(sx<0)sx=0; if(sx>=(int)W)sx=(int)W-1; u32 c=s[sx]; d[0]=(u8)c;d[1]=(u8)(c>>8);d[2]=(u8)(c>>16); d+=3; } } }
}

/* =========================================================================
   Small helpers / CPU / RTC
   ========================================================================= */
static inline void cpuid(u32 lf,u32*a,u32*b,u32*c,u32*d){ __asm__ volatile("cpuid":"=a"(*a),"=b"(*b),"=c"(*c),"=d"(*d):"a"(lf),"c"(0)); }
static inline u64 rdmsr(u32 m){ u32 lo,hi; __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(m)); return ((u64)hi<<32)|lo; }
static char *e4(char*p,u32 r){ for(int j=0;j<4;j++)*p++=(char)(r>>(j*8)); return p; }
static char *scopy(char*p,const char*s){ while(*s)*p++=*s++; return p; }
static char *udec(char*p,u32 v){ char t[12];int n=0; if(!v){*p++='0';return p;} while(v){t[n++]='0'+v%10;v/=10;} while(n)*p++=t[--n]; return p; }
static char *u2(char*p,u32 v){ if(v<10)*p++='0'; return udec(p,v); }
static u32 g_glasstmp[MAXW];
static void glass_panel(int x,int y,int w,int h,int r,u32 tint,int tintamt,int blurR,int hairline){
    int x0=x<0?0:x, y0=y<0?0:y, x1=x+w>(int)W?(int)W:x+w, y1=y+h>(int)H?(int)H:y+h;
    if(x1<=x0||y1<=y0)return; int bw=x1-x0;
    if(blurR>0){
        for(int pass=0;pass<2;pass++){
            for(int j=y0;j<y1;j++){ u32* row=&SCENE[(u64)j*SW]; int sr=0,sg=0,sb=0,n=0;
                for(int i=-blurR;i<=blurR;i++){ int xi=x0+i; if(xi<x0)xi=x0; if(xi>=x1)xi=x1-1; u32 c=row[xi]; sr+=(c>>16)&255; sg+=(c>>8)&255; sb+=c&255; n++; }
                for(int i=0;i<bw;i++){ if(!cut(i,j-y,w,h,r,15))g_glasstmp[i]=rgb(sr/n,sg/n,sb/n);
                    int ad=x0+i+blurR+1; if(ad>=x1)ad=x1-1; int rm=x0+i-blurR; if(rm<x0)rm=x0;
                    u32 ca=row[ad],cr=row[rm]; sr+=((ca>>16)&255)-((cr>>16)&255); sg+=((ca>>8)&255)-((cr>>8)&255); sb+=(ca&255)-(cr&255); }
                for(int i=0;i<bw;i++) if(!cut(i,j-y,w,h,r,15))row[x0+i]=g_glasstmp[i]; }
        }
    }
    int a=tintamt*256/100; if(a>256)a=256;
    for(int j=y0;j<y1;j++){ u32* row=&SCENE[(u64)j*SW]; int gy=j-y;
        for(int i=x0;i<x1;i++){ int gx=i-x; if(cut(gx,gy,w,h,r,15))continue;
            u32 t=tint; int extra=0;
            if(gy<h/2){ int f=(h/2-gy)*36/(h/2); t=mix(t,rgb(255,255,255),255-f); extra=f; }
            row[i]=mix(row[i],t,a); if(extra)row[i]=mix(row[i],rgb(255,255,255),255-extra/2); }
    }
    if(hairline){ u32 hl=rgb(255,255,255);
        for(int i=0;i<w;i++){ if(!cut(i,0,w,h,r,15)&&(u32)(x+i)<W&&(u32)y<H){ u32* p=&SCENE[(u64)y*SW+x+i]; *p=mix(*p,hl,150); } }
        for(int j=0;j<h;j++){ int gx=0; while(gx<w&&cut(gx,j,w,h,r,15))gx++; if(gx<w&&(u32)(x+gx)<W&&(u32)(y+j)<H){ u32* p=&SCENE[(u64)(y+j)*SW+x+gx]; *p=mix(*p,hl,180); } }
    }
}
static void glass_liquid(int x,int y,int w,int h,int r,u32 tint,int tintamt,int blurR){
    int x0=x<0?0:x, y0=y<0?0:y, x1=x+w>(int)W?(int)W:x+w, y1=y+h>(int)H?(int)H:y+h;
    if(x1<=x0||y1<=y0)return; int bw=x1-x0;
    if(blurR>0) for(int pass=0;pass<3;pass++){
        for(int j=y0;j<y1;j++){ u32* row=&SCENE[(u64)j*SW]; int sr=0,sg=0,sb=0,n=0;
            for(int i=-blurR;i<=blurR;i++){ int xi=x0+i; if(xi<x0)xi=x0; if(xi>=x1)xi=x1-1; u32 c=row[xi]; sr+=(c>>16)&255; sg+=(c>>8)&255; sb+=c&255; n++; }
            for(int i=0;i<bw;i++){ if(!cut(i,j-y,w,h,r,15))g_glasstmp[i]=rgb(sr/n,sg/n,sb/n);
                int ad=x0+i+blurR+1; if(ad>=x1)ad=x1-1; int rm=x0+i-blurR; if(rm<x0)rm=x0;
                u32 ca=row[ad],cr=row[rm]; sr+=((ca>>16)&255)-((cr>>16)&255); sg+=((ca>>8)&255)-((cr>>8)&255); sb+=(ca&255)-(cr&255); }
            for(int i=0;i<bw;i++) if(!cut(i,j-y,w,h,r,15))row[x0+i]=g_glasstmp[i]; } }
    int a=tintamt*256/100; if(a>256)a=256;
    for(int j=y0;j<y1;j++){ u32* row=&SCENE[(u64)j*SW]; int gy=j-y;
        for(int i=x0;i<x1;i++){ int gx=i-x; if(cut(gx,gy,w,h,r,15))continue;
            u32 base=mix(row[i],tint,a);
            int top=(gy<h*45/100); if(top){ int f=(h*45/100-gy)*70/(h*45/100); base=mix(base,rgb(255,255,255),255-f); }
            int edge=gx; if(w-1-gx<edge)edge=w-1-gx; if(gy<edge)edge=gy; if(h-1-gy<edge)edge=h-1-gy;
            if(edge<3){ base=mix(base,rgb(255,255,255),150); }
            int bot=(gy>h*80/100); if(bot){ int f=(gy-h*80/100)*40/(h-h*80/100+1); base=mix(base,rgb(10,14,26),255-f); }
            row[i]=base; }
    }
    /* top specular sweep — gentle */
    for(int gy=1;gy<h*22/100;gy++){ int j=y+gy; if((u32)j>=H)continue; u32* row=&SCENE[(u64)j*SW];
        int str=110-gy*110/(h*22/100);
        for(int i=x0;i<x1;i++){ int gx=i-x; if(cut(gx,gy,w,h,r,15))continue; row[i]=mix(row[i],rgb(255,255,255),255-str); } }
    /* crisp 1px rim highlight */
    for(int gx=0;gx<w;gx++){ int i=x+gx; if((u32)i>=W)continue;
        for(int gy=0;gy<h;gy++){ if(cut(gx,gy,w,h,r,15))continue; int j=y+gy; if((u32)j<H){ u32* p=&SCENE[(u64)j*SW+i]; *p=mix(*p,rgb(255,255,255),150); } break; }
        for(int gy=h-1;gy>=0;gy--){ if(cut(gx,gy,w,h,r,15))continue; int j=y+gy; if((u32)j<H){ u32* p=&SCENE[(u64)j*SW+i]; *p=mix(*p,rgb(150,160,185),200); } break; } }
}
static void soft_ring(int cx,int cy,int rr,int thick,u32 c,int amt){
    for(int y=-rr-thick;y<=rr+thick;y++)for(int x=-rr-thick;x<=rr+thick;x++){ int d2=x*x+y*y; int lo=(rr-thick)*(rr-thick),hi=(rr+thick)*(rr+thick);
        if(d2>=lo&&d2<=hi){ int px=cx+x,py=cy+y; if((u32)px<W&&(u32)py<H){ u32* p=&SCENE[(u64)py*SW+px]; *p=mix(*p,c,amt); } } }
}

static char *uhex(char*p,u64 v,int dg){ static const char*H="0123456789ABCDEF"; *p++='0';*p++='x'; for(int i=dg-1;i>=0;i--)*p++=H[(v>>(i*4))&0xF]; return p; }
static char *hx(char*p,u32 v,int dg){ static const char*H="0123456789ABCDEF"; for(int i=dg-1;i>=0;i--)*p++=H[(v>>(i*4))&0xF]; return p; }
static int seq(const char*a,const char*b){ while(*a&&*b){if(*a!=*b)return 0;a++;b++;} return *a==*b; }

static u8 cmos(u8 r){ outb(0x70,r); return inb(0x71); }
static int bcd(u8 v){ return (v&0x0F)+((v>>4)*10); }
static void read_clock(int*hh,int*mm,int*ss,int*yr,int*mo,int*dy){
    while(cmos(0x0A)&0x80){}
    u8 s=cmos(0),mi=cmos(2),h=cmos(4),d=cmos(7),mon=cmos(8),y=cmos(9),rb=cmos(0x0B);
    int pm=(!(rb&0x02))&&(h&0x80); h&=0x7F;
    if(!(rb&0x04)){s=(u8)bcd(s);mi=(u8)bcd(mi);h=(u8)bcd(h);d=(u8)bcd(d);mon=(u8)bcd(mon);y=(u8)bcd(y);}
    if(!(rb&0x02)){ if(pm&&h!=12)h+=12; if(!pm&&h==12)h=0; }
    *ss=s;*mm=mi;*hh=h;*dy=d;*mo=mon;*yr=2000+y;
}
static char g_brand[52]; static int g_lma;
static void get_brand(void){
    u32 a,b,c,d; cpuid(0x80000000,&a,&b,&c,&d); g_brand[0]=0;
    if(a>=0x80000004){ char*q=g_brand;
        cpuid(0x80000002,&a,&b,&c,&d);q=e4(q,a);q=e4(q,b);q=e4(q,c);q=e4(q,d);
        cpuid(0x80000003,&a,&b,&c,&d);q=e4(q,a);q=e4(q,b);q=e4(q,c);q=e4(q,d);
        cpuid(0x80000004,&a,&b,&c,&d);q=e4(q,a);q=e4(q,b);q=e4(q,c);q=e4(q,d);*q=0;
        char*s=g_brand; while(*s==' ')s++; if(s!=g_brand){char*w=g_brand;while(*s)*w++=*s++;*w=0;}
    } else scopy(g_brand,"x86-64 CPU");
    g_lma=(int)((rdmsr(0xC0000080)>>10)&1);
}

/* =========================================================================
   PCI enumeration (read-only config-space scan; safe, no MMIO/DMA)
   ========================================================================= */
#define PCI_MAX 24
static u16 pci_ven[PCI_MAX], pci_dev[PCI_MAX]; static u8 pci_cls[PCI_MAX], pci_sub[PCI_MAX], pci_bus[PCI_MAX], pci_slot[PCI_MAX]; static int pci_n=0;
static u32 pci_cfg(int bus,int slot,int fn,int off){ u32 a=(1u<<31)|((u32)bus<<16)|((u32)slot<<11)|((u32)fn<<8)|(off&0xFC); outl(0xCF8,a); return inl(0xCFC); }
static void pci_cfgw(int bus,int slot,int fn,int off,u32 v){ u32 a=(1u<<31)|((u32)bus<<16)|((u32)slot<<11)|((u32)fn<<8)|(off&0xFC); outl(0xCF8,a); outl(0xCFC,v); }
/* find a device by class/subclass across ALL 256 buses + all functions (NVMe/AHCI often live on high buses) */
static int pci_find(int cls,int sub,int prog,int* obus,int* oslot,int* ofn){
    for(int bus=0;bus<256;bus++) for(int slot=0;slot<32;slot++){
        u32 id0=pci_cfg(bus,slot,0,0); if((id0&0xFFFF)==0xFFFF)continue;
        int nf=(pci_cfg(bus,slot,0,0x0C)&0x800000)?8:1;   /* multifunction? */
        for(int fn=0;fn<nf;fn++){ u32 id=pci_cfg(bus,slot,fn,0); if((id&0xFFFF)==0xFFFF)continue;
            u32 cl=pci_cfg(bus,slot,fn,0x08); int c=(cl>>24)&0xFF, s=(cl>>16)&0xFF, pr=(cl>>8)&0xFF;
            if(c==cls && s==sub && (prog<0||pr==prog)){ if(obus)*obus=bus; if(oslot)*oslot=slot; if(ofn)*ofn=fn; return 1; } }
    }
    return 0;
}
static void pci_scan(void){ pci_n=0;
    for(int bus=0;bus<4;bus++) for(int slot=0;slot<32;slot++){ u32 id=pci_cfg(bus,slot,0,0); u16 v=id&0xFFFF; if(v==0xFFFF)continue;
        u32 cl=pci_cfg(bus,slot,0,0x08);
        if(pci_n<PCI_MAX){ pci_ven[pci_n]=v; pci_dev[pci_n]=(u16)(id>>16); pci_cls[pci_n]=(cl>>24)&0xFF; pci_sub[pci_n]=(cl>>16)&0xFF; pci_bus[pci_n]=(u8)bus; pci_slot[pci_n]=(u8)slot; pci_n++; } }
}
static const char* nic_name(u16 v,u16 d){
    if(v==0x8086&&(d==0x100E||d==0x100F||d==0x10D3)) return "Intel e1000";
    if(v==0x10EC&&(d==0x8168||d==0x8169||d==0x8161||d==0x8136)) return "Realtek RTL8168";
    if(v==0x10EC&&d==0x8139) return "Realtek RTL8139";
    if(v==0x1AF4&&(d==0x1000||d==0x1041)) return "VirtIO-net";
    if(v==0x1022&&d==0x2000) return "AMD PCnet";
    return "unknown NIC";
}
static int nic_index(void){ for(int k=0;k<pci_n;k++) if(pci_cls[k]==2) return k; return -1; }
