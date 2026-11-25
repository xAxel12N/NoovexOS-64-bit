#include <stdio.h>
#include "graphics/ui/outputs.c"
static int fails, checks;
#define CK(c,msg) do{ checks++; if(!(c)){ fails++; printf("FAIL %s\n",msg); } }while(0)
int main(void){
    out_layout(1920,1080,1,1280,1024,0);
    CK(out_count()==1&&out_desk_w()==1920&&out_desk_h()==1080,"mirror: one desktop");
    out_layout(1920,1080,0,1280,1024,1);
    CK(out_count()==1,"extend without second screen falls back");
    out_layout(1920,1080,1,0,0,1);
    CK(out_count()==1,"extend with empty second mode falls back");
    out_layout(1920,1080,1,1280,1024,1);
    CK(out_count()==2&&g_out[1].x==1920&&g_out[1].y==0&&g_out[1].w==1280&&g_out[1].h==1024,"extend: second rect right of first");
    CK(out_desk_w()==3200&&out_desk_h()==1080,"desktop bounds");
    CK(out_at(0,0)==0&&out_at(1919,1079)==0&&out_at(1920,0)==1&&out_at(3199,1023)==1&&out_at(3199,1030)==-1&&out_at(-1,0)==-1&&out_at(3200,0)==-1,"hit test incl. the dead area under the shorter screen");
    CK(out_of_rect(100,100,400,300)==0&&out_of_rect(2000,100,400,300)==1&&out_of_rect(1800,100,400,300)==1&&out_of_rect(1700,100,400,300)==0,"window belongs to the screen with most overlap");
    int x=3199,y=1070; out_clamp_pt(&x,&y); CK(out_at(x,y)>=0&&x==3199&&y==1023,"point in dead area clamps onto screen 2");
    x=5000; y=-5; out_clamp_pt(&x,&y); CK(x==3199&&y==0,"far point clamps");
    x=100; y=50; CK(out_move_rect(&x,&y,400,300,0,1)==0&&x==2020&&y==50,"move keeps relative position");
    x=1800; y=900; CK(out_move_rect(&x,&y,400,300,0,1)==0&&x==2800&&y==724&&out_of_rect(x,y,400,300)==1,"move clamps inside screen 2");
    x=2800; y=724; CK(out_move_rect(&x,&y,400,300,1,0)==0&&x==880&&y==724,"move back");
    x=0; y=0; CK(out_move_rect(&x,&y,10,10,0,2)<0&&out_place(&x,&y,10,10,-1)<0,"bad screen refused");
    x=3000; y=0; CK(out_place(&x,&y,2000,2000,1)==0&&x==1920&&y==0,"window bigger than screen pinned to its origin");
    int a,b,c,d; out_max_rect(1,0,40,&a,&b,&c,&d); CK(a==1920&&b==0&&c==1280&&d==984,"maximise on screen 2");
    out_max_rect(7,24,0,&a,&b,&c,&d); CK(a==0&&b==24&&c==1920&&d==1056,"maximise bad screen uses screen 1");
    out_layout(1920,1080,1,1280,1024,0);
    CK(out_count()==1&&out_at(2000,10)==-1,"back to mirror drops screen 2");
    out_layout(1920,1080,1,1280,1024,1);
    x=1915; y=500; x+=40; out_mouse_clamp(&x,&y,1920,1080); CK(x==1955&&y==500&&out_at(x,y)==1,"mouse crosses the right edge onto screen 2");
    x=3190; y=1050; x+=40; out_mouse_clamp(&x,&y,1920,1080); CK(x==3199&&y==1023,"mouse stops at the far edge and under the shorter screen");
    x=1950; y=500; x-=60; out_mouse_clamp(&x,&y,1920,1080); CK(x==1890&&out_at(x,y)==0,"mouse comes back to screen 1");
    int nx=1700, ny=-20; out_drag_clamp(&nx,&ny,400,2000,10,1920,1080,24); CK(nx==1700&&ny==0,"drag across: window may straddle, title stays on screen");
    nx=3150; ny=1050; out_drag_clamp(&nx,&ny,400,3150,1000,1920,1080,24); CK(nx==3110&&ny==1000,"drag on screen 2 clamps to its own height and desktop width");
    CK(out_snap_ok(1918,500,2)==0&&out_snap_ok(2,500,1)==1&&out_snap_ok(900,1,3)==1&&out_snap_ok(2500,1,3)==0,"no edge snap on the shared edge or on screen 2");
    out_rect ar; out_area(2000,100,400,300,1920,1080,&ar); CK(ar.x==1920&&ar.w==1280&&ar.h==1024,"area of a screen 2 window");
    int dx,dy; CK(out_local_shift(2000,100,400,300,&dx,&dy)==1&&dx==1920&&dy==0&&out_local_shift(100,100,400,300,&dx,&dy)==0&&dx==0,"local shift only for screen 2 windows");
    g_out_arr=OUT_LEFT; out_layout(1920,1080,1,1280,1024,1);
    CK(g_out[1].x==-1280&&g_out[1].y==0&&out_desk_x0()==-1280&&out_desk_w()==3200,"left arrangement");
    x=3; y=200; x-=10; out_mouse_clamp(&x,&y,1920,1080); CK(x==-7&&out_at(x,y)==1,"mouse crosses the left edge");
    CK(out_snap_ok(1,500,1)==0&&out_snap_ok(1918,500,2)==1,"left arrangement: left edge shared, right edge snaps");
    nx=-1200; ny=10; out_drag_clamp(&nx,&ny,400,-1000,30,1920,1080,24); CK(nx==-1200&&ny==10,"drag onto a left screen");
    x=100; y=50; CK(out_move_rect(&x,&y,400,300,0,1)==0&&x==-1180&&y==50,"move to the left screen");
    g_out_arr=OUT_ABOVE; out_layout(1920,1080,1,1280,1024,1);
    CK(g_out[1].x==0&&g_out[1].y==-1024&&out_desk_h()==2104,"above arrangement");
    x=500; y=2; y-=10; out_mouse_clamp(&x,&y,1920,1080); CK(y==-8&&out_at(x,y)==1,"mouse crosses the top edge");
    x=1900; y=2; y-=10; out_mouse_clamp(&x,&y,1920,1080); CK(x==1900&&y==0,"no screen above that part of the top edge");
    CK(out_snap_ok(500,1,3)==0,"above: no maximise snap on the shared top edge");
    g_out_arr=OUT_BELOW; out_layout(1920,1080,1,1280,1024,1);
    CK(g_out[1].y==1080&&out_at(10,1085)==1,"below arrangement");
    g_out_arr=OUT_RIGHT;
    out_layout(1920,1080,1,1280,1024,0);
    x=5000; y=-3; out_mouse_clamp(&x,&y,1920,1080); CK(x==1919&&y==0,"mirror: mouse clamps to screen 1 as before");
    nx=5000; ny=5000; out_drag_clamp(&nx,&ny,400,10,10,1920,1080,24); CK(nx==1830&&ny==1056,"mirror: drag clamps as before");
    CK(out_snap_ok(1919,10,2)==1,"mirror: snapping unchanged");
    { g_out_arr=OUT_RIGHT; out_layout(1024,600,1,1280,1024,1);
      int a=2000,b=100,c=400,d=300; out_resize_win(&a,&b,&c,&d,0,0,1024,600,1920,1080); CK(a==2896&&b==100&&c==400&&d==300,"desktop resize: a window on screen 2 (right) moves with the screen, keeps its size");
      a=100,b=50,c=900,d=500; out_resize_win(&a,&b,&c,&d,0,0,1024,600,1920,1080); CK(a==187&&b==90&&c==900&&d==500,"desktop resize: a screen-1 window scales its position");
      a=1100,b=40,c=1200,d=900; out_resize_win(&a,&b,&c,&d,1,0,1024,600,1920,1080); CK(a==1996&&c==1200,"desktop resize: a maximised window on screen 2 stays there");
      int px=1500,py=10; int o=out_resize_pt(&px,&py,1024,600,1920,1080); CK(o==1&&px==2396&&py==10,"desktop resize: the mouse on screen 2 moves with it");
      out_layout(1920,1080,1,1280,1024,0); a=1700,b=900,c=1800,d=1000; out_resize_win(&a,&b,&c,&d,0,0,1920,1080,1024,600);
      CK(c==1012&&d==482&&a==6&&b==32&&a+c<=1024-6&&b+d<=600-86,"desktop resize down: windows shrink and stay on the smaller screen");
      a=900,b=500,c=3000,d=3000; out_resize_win(&a,&b,&c,&d,0,1,1920,1080,1024,600); CK(a>=0&&b>=0&&a+c<=1024&&b+d<=600-86,"desktop resize down: saved restore geometry fits the smaller screen");
      px=1919; py=1079; out_resize_pt(&px,&py,1920,1080,1024,600); out_layout(1024,600,1,1280,1024,0); out_mouse_clamp(&px,&py,1024,600); CK(px==1023&&py==599,"desktop resize down: the mouse is clamped onto the screen"); }
    { out_layout(1280,800,0,0,0,0); out_work_reserve(0,0,30); int bx,by,bw,bh; out_snap_rect(0,0,0,0,0,1280,800,&bx,&by,&bw,&bh);
      CK(bx==0&&by==0&&bw==1280&&bh==770,"work area set by the taskbar: a maximised window fills the screen down to the taskbar");
      out_snap_rect(0,0,0,0,-1,1280,800,&bx,&by,&bw,&bh); CK(bx==0&&by==0&&bw==1280&&bh==770,"hotkey snap on one screen uses the same work area");
      int a=0,b=0,c=1280,d=770; out_resize_win(&a,&b,&c,&d,1,0,1280,800,1920,1080); CK(a==0&&b==0&&c==1920&&d==1050,"maximised window keeps filling the work area after a resolution change");
      a=100; b=50; c=400; d=300; out_resize_win(&a,&b,&c,&d,0,0,1280,800,1024,600); CK(a>=0&&b>=0&&a+c<=1024&&b+d<=570,"plain windows stay inside the work area after a resolution change");
      out_work_reserve(-1,-5,-9); CK(g_out_mg==0&&g_out_top==0&&g_out_bot==0,"negative reserves clamp to zero");
      out_work_reserve(6,32,86); out_snap_rect(0,0,0,0,0,1280,800,&bx,&by,&bw,&bh); CK(bx==6&&by==32&&bw==1268&&bh==682,"default reserve restored"); }
    printf("%d checks, %s\n",checks,fails?"FAIL":"ALL PASS"); return fails!=0; }
