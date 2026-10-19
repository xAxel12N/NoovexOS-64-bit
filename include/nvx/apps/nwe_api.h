#ifndef NVX_NWE_API_H
#define NVX_NWE_API_H

#define NWE_OP_INIT 1
#define NWE_OP_LOAD 2
#define NWE_OP_LAYOUT 3
#define NWE_OP_PAINT 4
#define NWE_OP_SCROLL 5
#define NWE_OP_MOVE 6
#define NWE_OP_DOWN 7
#define NWE_OP_UP 8
#define NWE_OP_KEY 9
#define NWE_OP_TICK 10
#define NWE_OP_FIND 11
#define NWE_OP_EVAL 12
#define NWE_OP_INFO 13
#define NWE_OP_WHEEL 14
#define NWE_OP_MEDIA 15

#define NWE_K_UP 19
#define NWE_K_DOWN 20
#define NWE_K_LEFT 21
#define NWE_K_RIGHT 22
#define NWE_K_PGUP 23
#define NWE_K_PGDN 24
#define NWE_K_HOME 25
#define NWE_K_END 26

typedef struct {
    int op;
    const char* url;
    const char* data;
    int len;
    int x, y, w, h, btn, key;
    unsigned int* fb;
    int stride, fbw, fbh;
    int ret;
    int scroll, doch, viewh, cursor, dirty;
    char nav[1024];
    char navmethod[8];
    char* navbody;
    int navblen;
    char href[512];
    char title[128];
    char out[2048];
} nwe_req;

long nwe_entry(long op, void* req);
void* nwe_stack_top(void);

typedef struct {
    char* body;
    int len;
    int status;
    char ctype[96];
    char final_url[1024];
} nwk_resp;

int nwk_fetch(const char* url, const char* method, const char* ctype, const char* body, int blen, const char* referer, nwk_resp* r);
void nwk_free(void* p);
int nwk_image(const unsigned char* data, int len, unsigned int** argb, int* w, int* h);
int nwk_cookie_get(const char* url, int forjs, char* out, int cap);
void nwk_cookie_set(const char* url, const char* line, int fromjs);
int nwk_store_get(const char* origin, int session, const char* key, char* out, int cap);
void nwk_store_set(const char* origin, int session, const char* key, const char* val);
void nwk_store_clear(const char* origin, int session);
int nwk_store_key(const char* origin, int session, int idx, char* out, int cap);
int nwk_store_len(const char* origin, int session);
void nwk_log(const char* s);
void nwk_status(const char* s);
void nwk_alert(const char* s);
unsigned long nwk_ms(void);
int nwk_busy(void);
long long nwk_epoch_ms(void);
int nwk_tz_min(void);
void nwk_pump(void);
void* nwk_alloc_arena(unsigned long n);
int nwk_inflate(const unsigned char* src, int slen, unsigned char* dst, int dcap);
int nwk_media_new(const char* url, int split, int w, int h, long long start_ms, int paused);
void nwk_media_cmd(int id, int cmd, long long a, long long b);
int nwk_media_status(int id, long long* v);
int nwk_media_append(int id, int stream, const unsigned char* d, int len);
void nwk_media_end(int id, int stream);
int nwk_media_restart(int id, long long start_ms, int keep);
void nwk_media_free(int id);
long long nwk_media_len(int id, int stream);
const unsigned* nwk_media_frame(int id, int* w, int* h, unsigned long long* seq);

#endif
