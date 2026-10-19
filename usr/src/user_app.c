/* A real NoovexOS user-space app: runs in ring 3, uses the libc + syscalls.
   Draws a window with graphics, uses malloc + printf, then waits for a key. */
#include "user.h"

int main(void){
    int W = screen_w(), H = screen_h();

    /* console output via printf + malloc */
    printf("NoovexOS user app (ring 3), screen %dx%d\n", W, H);
    int* nums = (int*)malloc(16*sizeof(int));
    for(int i=0;i<16;i++) nums[i]=i*i;
    printf("malloc ok: squares 0..15, last = %d\n", nums[15]);

    /* draw a little UI to the framebuffer via syscalls */
    fb_clear(rgb(24,28,40));
    fb_rect(W/2-200, H/2-130, 400, 260, rgb(38,44,64));
    fb_rect(W/2-200, H/2-130, 400, 34,  rgb(52,110,200));
    fb_text(W/2-188, H/2-122, "Hello from a real user-space app!", rgb(255,255,255));
    fb_text(W/2-188, H/2-70,  "This program runs in ring 3, isolated,", rgb(210,218,235));
    fb_text(W/2-188, H/2-50,  "talking to NoovexOS only via syscalls.", rgb(210,218,235));
    /* a row of colored bars sized by our malloc'd data */
    for(int i=0;i<16;i++){
        int bh = 4 + (nums[i]*70)/225;
        fb_rect(W/2-180 + i*22, H/2+90 - bh, 16, bh, rgb(90+i*8, 200-i*6, 150));
    }
    fb_text(W/2-188, H/2+100, "Press any key to exit...", rgb(150,160,185));
    fb_present();

    waitkey();
    return 0;
}
