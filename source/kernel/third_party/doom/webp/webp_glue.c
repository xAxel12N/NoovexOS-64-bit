/* webp_glue.c - SSE-init stubs (we use WebP's C reference paths) + a simple
   decode wrapper that the NoovexOS kernel calls.  Compiled into the blob. */
#include <stdint.h>
#include <stddef.h>
#include "webp/decode.h"

/* The C dsp files reference the SSE2 initializers; provide empty stubs so the
   plain C implementations stay in place (no SIMD accel; fine for a one-shot decode). */
void VP8DspInitSSE2(void){}
void VP8FiltersInitSSE2(void){}
void VP8LDspInitSSE2(void){}
void WebPInitAlphaProcessingSSE2(void){}
void WebPInitConvertARGBToYUVSSE2(void){}
void WebPInitSamplersSSE2(void){}
void WebPInitUpsamplersSSE2(void){}
void WebPInitYUV444ConvertersSSE2(void){}
void WebPRescalerDspInitSSE2(void){}
/* SSE4.1 / AVX variants, in case any dispatcher references them */
void VP8DspInitSSE41(void){}
void VP8EncDspInitSSE2(void){}
void VP8EncDspInitSSE41(void){}
void WebPInitAlphaProcessingSSE41(void){}
void WebPInitConvertARGBToYUVSSE41(void){}
void VP8LEncDspInitSSE2(void){}
void VP8LEncDspInitSSE41(void){}

/* Decode a WebP image (BGRA, matching the kernel's 0x00RRGGBB pixel byte order)
   into 'out'.  Returns 0 on success and sets *ow/*oh; negative on failure. */
int webp_decode(const uint8_t* data, int len, uint8_t* out, int outcap, int* ow, int* oh){
    int w=0,h=0;
    if(len<=0 || !WebPGetInfo(data,(size_t)len,&w,&h)) return -1;
    if(w<=0 || h<=0) return -1;
    long need=(long)w*(long)h*4;
    if(need>(long)outcap) return -2;              /* too big for the buffer */
    if(!WebPDecodeBGRAInto(data,(size_t)len,out,(size_t)outcap,w*4)) return -3;
    /* force opaque (kernel ignores alpha; keep high byte clean) */
    uint32_t* p=(uint32_t*)out; for(long i=0;i<(long)w*h;i++) p[i]&=0x00FFFFFFu;
    *ow=w; *oh=h; return 0;
}
