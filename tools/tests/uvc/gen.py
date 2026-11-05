import re,sys
src=open(sys.argv[1]).read()
def func(sig):
    i=src.index(sig); j=src.index('{',i); d=0; k=j
    while True:
        c=src[k]
        if c=='{': d+=1
        elif c=='}':
            d-=1
            if d==0: return src[i:k+1]
        k+=1
def line(prefix):
    i=src.index(prefix); j=src.index('\n',i); return src[i:j]
out=[]
out.append('#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\ntypedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64;\n')
out.append('static char* scopy(char* p,const char* s){ while(*s)*p++=*s++; *p=0; return p; }\nstatic void klog(const char* s){ (void)s; }\nstatic u32 uvc_r32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }\nstatic char* udec(char* p,u32 v){ char t[12]; int n=0; if(!v)t[n++]=48; while(v){ t[n++]=(char)(48+v%10); v/=10; } while(n)*p++=t[--n]; *p=0; return p; }\n')
out.append(line('typedef struct { u8 alt, ep, intv, burst, mult; u16 mps; u32 esit; } uvc_alt_t;'))
out.append(line('typedef struct { u8 kind, fi, ri; u16 w, h; u32 defint; } uvc_fr_t;'))
out.append(line('static struct { int ok, ctl, reg, slot, speed, root, vc_if, vs_if'))
out.append('int uvc_state=0, uvc_w=0, uvc_h=0, uvc_kind=0; u32 uvc_frames=0,uvc_short=0,uvc_seq=0,uvc_bytes=0,uvc_badframes=0,uvc_jlen=0,uvc_maxpay=0; u32* uvc_rgb; u8* uvc_jpg; volatile int uvc_jnew; static u8* uvc_fb; static u32 uvc_fill=0,uvc_fcap=0; static int uvc_fbad=0,uvc_lfid=-1; const u8 uvc_std_dht[]={0}; const int uvc_std_dht_len=1;\nstatic void uvc_aud_share(int f){(void)f;}\nvoid uvc_stop(void){}\n')
for s in ['static int uvc_fourcc(const u8* g){','const char* uvc_kind_name(int k){','static int uvc_has_color(void){']: out.append(func(s))
out.append('static void uvc_parse(const u8* c,int n,int target);')
for s in ['static void uvc_choose(const u8* c,int n,int want){','int uvc_streams(void){','int uvc_stream_cur(void){','int uvc_select_stream(int k){','static void uvc_parse(const u8* c,int n,int target){','static u32 uvc_px(int y,int u,int v){','static void uvc_frame_done(void){','static void uvc_append(const u8* s,int dn){','static void uvc_payload(const u8* b,int n){','static void uvc_bulk_chunk(const u8* b,int got,int tl){']: out.append(func(s))
out.append(line('typedef struct { u8 ifn, alt, ep, intv, ch, sub, bits, nrate, cont, v2; u16 mps; u32 rate[8]; } uac_alt_t;'))
out.append(line('static struct { int ok, ctl, reg, slot, speed, root, ac_if, nalt, nfu, pb, cp'))
out.append(line('typedef struct { volatile u32* ring; u8* bufs; u32 enq, deq, cyc, out, ntd, bsz, dci, mps, ivus, rate, ch, acc, dirin, bs, bcap; } aud_ep_t;'))
out.append('static aud_ep_t apb, acp; static u32 *aud_cpq; static u32 aud_cpw=0,aud_cpr=0; \n#define ACP_N 16384u\n')
for s in ['static int aud_has_audio(const u8* c,int n){','static void aud_parse(const u8* c,int n){','static u32 aud_pick_rate(const uac_alt_t* a){','static int aud_pick(int in){','static int aud_s16(const u8* p,u32 bs){','static void aud_take_cp(const u8* d,u32 n){','static int uvc_has_video(const u8* c,int n){']: out.append(func(s))
open(sys.argv[2],'w').write('\n'.join(out)+'\n'+open(sys.argv[3]).read())
