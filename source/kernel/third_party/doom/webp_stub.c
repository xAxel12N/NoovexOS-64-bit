/* WebP stub (real WebP source was lost in a sandbox reset). PNG/JPEG still work. */
int is_webp(const unsigned char* d, int n){ (void)d; (void)n; return 0; }
int webp_decode(const unsigned char* d, int n, unsigned char* out, int cap, int* w, int* h){
    (void)d;(void)n;(void)out;(void)cap; if(w)*w=0; if(h)*h=0; return -1;
}
