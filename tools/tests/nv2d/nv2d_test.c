#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "drivers/video/nvidia/nv2dq.c"
static u32 MEM[4][256*64]; static u32 R[0x400]; static int fails=0, bound=0;
static u32* sp(u64 va,u32 pb,int x,int y){ int i=(int)(va>>20)-1; if(i<0||i>3)return 0; return &MEM[i][(u64)y*(pb/4)+(u32)x]; }
static void run(const u32* pb,int n){ for(int i=0;i<n;){ u32 h=pb[i++]; u32 c=(h>>16)&0x1FFF, m=(h&0xFFF)<<2, sub=(h>>13)&7; if((h>>29)!=1||sub!=3){ fails++; return; }
    for(u32 k=0;k<c;k++){ u32 mm=m+k*4, v=pb[i++]; R[mm/4]=v; if(mm==0)bound=v==0x902Du;
        if(mm==0x60C&&R[0x580/4]==4&&R[0x2AC/4]==3&&R[0x204/4]==1&&R[0x200/4]==0xCF){ u64 va=((u64)R[0x220/4]<<32)|R[0x224/4]; for(u32 y=R[0x604/4];y<v;y++) for(u32 x=R[0x600/4];x<R[0x608/4];x++)*sp(va,R[0x214/4],(int)x,(int)y)=R[0x588/4]; }
        if(mm==0x8DC&&R[0x2AC/4]==3&&R[0x8C0/4]==0&&R[0x8C4/4]==1&&R[0x8C8/4]==0&&R[0x8CC/4]==1){ u64 dv=((u64)R[0x220/4]<<32)|R[0x224/4], sv=((u64)R[0x250/4]<<32)|R[0x254/4];
            for(u32 j=0;j<R[0x8BC/4];j++) for(u32 x=0;x<R[0x8B8/4];x++)*sp(dv,R[0x214/4],(int)(R[0x8B0/4]+x),(int)(R[0x8B4/4]+j))=*sp(sv,R[0x244/4],(int)(R[0x8D4/4]+x),(int)(v+j)); } } } }
static u32 CPU[4][256*64];
static u32 bl(u32 d,u32 s,u32 a){ u32 ia=256-a; u32 rb=(((s&0x00FF00FFu)*a+(d&0x00FF00FFu)*ia)>>8)&0x00FF00FFu; u32 ag=((((s>>8)&0x00FF00FFu)*a+((d>>8)&0x00FF00FFu)*ia))&0xFF00FF00u; return rb|ag; }
static void chk(const char* t,int ok){ printf("  %s %s\n",t,ok?"ok":"FAIL"); if(!ok)fails++; }
int main(void){
    u32 rs=0x12345u; for(int s=0;s<4;s++) for(int i=0;i<256*64;i++){ rs^=rs<<13; rs^=rs>>17; rs^=rs<<5; MEM[s][i]=CPU[s][i]=rs; }
    nv2qs S[4]; for(int s=0;s<4;s++){ S[s].va=(u64)(s+1)<<20; S[s].pitchb=1024; S[s].w=200+s*10; S[s].h=64; }
    nv2q q; nv2q_reset(&q); u32 pb[8192]; int n=0, ops=0, rej=0;
    for(int t=0;t<300;t++){ rs^=rs<<13; rs^=rs>>17; rs^=rs<<5; int d=rs&3, s=(rs>>2)&3, k=(rs>>4)&1; int w=1+(int)((rs>>5)%80u), h=1+(int)((rs>>12)%40u), x=(int)((rs>>18)%220u), y=(int)((rs>>8)%70u); int sx=(int)((rs>>3)%220u), sy=(int)((rs>>20)%70u); u32 c=rs*2654435761u;
        int r=k?nv2q_copy(&q,pb+n,&S[d],x,y,w,h,&S[s],sx,sy,s==d,t&1):nv2q_fill(&q,pb+n,&S[d],x,y,w,h,c,t&1);
        if(r<0||r>NV2Q_OPDW){ fails++; break; } if(!r){ rej++; continue; } n+=r; ops++;
        if(k){ for(int j=0;j<h;j++) for(int i=0;i<w;i++) CPU[d][(y+j)*256+x+i]=bl(CPU[d][(y+j)*256+x+i],CPU[s][(sy+j)*256+sx+i],256); }
        else for(int j=0;j<h;j++) for(int i=0;i<w;i++) CPU[d][(y+j)*256+x+i]=c;
        if(n>7000){ run(pb,n); n=0; } }
    run(pb,n);
    chk("902D object bound on subchannel 3 and every method word well formed",bound);
    int same=1; for(int s=0;s<4;s++) if(memcmp(MEM[s],CPU[s],sizeof MEM[s]))same=0;
    printf("  %d operations encoded, %d refused (out of bounds or overlapping copy)\n",ops,rej);
    chk("300 random fills and copies: simulated 2D engine result is pixel-exact against the CPU renderer",same&&ops>50&&rej>0);
    { nv2qs bad=S[0]; bad.pitchb=1000; chk("surfaces the 2D engine cannot address (pitch not 64-byte aligned) are refused, so the op stays on the compute/CPU path",!nv2q_fill(&q,pb,&bad,0,0,4,4,1,0)); }
    { nv2q_reset(&q); int a=nv2q_fill(&q,pb,&S[0],0,0,4,4,1,0), b=nv2q_fill(&q,pb,&S[0],0,0,4,4,1,0); chk("destination state is sent once per batch, repeated fills to the same surface are 11 words each",a>b&&b==11); }
    static nv2qa A; static u32 AT[NV2Q_GH*NV2Q_GW]; static unsigned char G[95][112]; for(int g=0;g<95;g++) for(int i=0;i<112;i++)G[g][i]=(unsigned char)((g*37+i*11)&255);
    nv2qa_reset(&A); int ok=1, ax, ay, bx, by;
    for(int g=0;g<95;g++) for(int s=1;s<=2;s++) if(nv2qa_get(&A,AT,NV2Q_GW,G[g],g+32,0,s,&ax,&ay))ok=0;
    nv2qa_get(&A,AT,NV2Q_GW,G[5],37,0,2,&ax,&ay); nv2qa_get(&A,AT,NV2Q_GW,G[5],37,0,2,&bx,&by);
    chk("glyph atlas: 190 glyphs cached once, repeated lookups hit the same cell",ok&&A.n==190&&ax==bx&&ay==by&&A.miss==190&&A.hits>=2);
    { u32 dc[16*28], dr[16*28], m[16*28]; for(int i=0;i<16*28;i++)dc[i]=dr[i]=0x00336699u+(u32)i; u32 col=0x00FFEE10u;
      for(int r=0;r<14;r++) for(int c=0;c<8;c++){ int a=G[5][r*8+c]; if(a<6)continue; u32 cov=a>=250?256u:(u32)a; for(int dy=0;dy<2;dy++) for(int dx=0;dx<2;dx++){ int i=(r*2+dy)*16+c*2+dx; dc[i]=bl(dc[i],col,cov); } }
      for(int j=0;j<28;j++) for(int i=0;i<16;i++){ m[j*16+i]=AT[(u64)(ay+j)*NV2Q_GW+(u32)(ax+i)]; dr[j*16+i]=bl(dr[j*16+i],col,(m[j*16+i]*256u)>>8); }
      chk("a glyph drawn from the atlas by the mask shader op is pixel-exact against the CPU text renderer",!memcmp(dc,dr,sizeof dc)); }
    printf(fails?"FAILURES\n":"ALL PASS\n"); return fails?1:0; }
