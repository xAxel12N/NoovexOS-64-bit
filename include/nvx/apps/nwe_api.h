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
#define NWE_OP_DROPFILE 16
#define NWE_OP_PERF 17

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
    char nav[4096];
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
    char final_url[4096];
    char* hdrs;
    int hlen;
} nwk_resp;
extern const char* g_nwk_xhdr;
extern int g_nwk_kind;

int nwk_fetch(const char* url, const char* method, const char* ctype, const char* body, int blen, const char* referer, nwk_resp* r);
typedef struct { int type, id, status, ok; const char* ctype; const char* url; const char* hdrs; int hlen; const unsigned char* data; int dlen; } nwk_netev;
void nwk_net_keep(int id);
int nwk_net_start(long owner, const char* url, const char* method, const char* ctype, const char* body, int blen, const char* xhdr, const char* referer);
int nwk_net_event(long owner, nwk_netev* ev);
void nwk_net_abort(long owner, int id);
void nwk_net_cancel_owner(long owner);
int nwk_net_active(long owner);
void nwk_free(void* p);
int nwk_image(const unsigned char* data, int len, unsigned int** argb, int* w, int* h);
int nwk_emoji_has(int cp);
int nwk_emoji(int cp, unsigned int** argb, int* w, int* h);
int nwk_cookie_get(const char* url, int forjs, char* out, int cap);
void nwk_cookie_set(const char* url, const char* line, int fromjs);
int nwk_store_get(const char* origin, int session, const char* key, char* out, int cap);
void nwk_store_set(const char* origin, int session, const char* key, const char* val);
void nwk_store_clear(const char* origin, int session);
int nwk_store_key(const char* origin, int session, int idx, char* out, int cap);
int nwk_store_len(const char* origin, int session);
void nwk_log(const char* s);
void nwk_error(const char* s);
void nwk_pending_set(const char* s, int len);
int nwk_frame_info(void);
void nwk_frame_fail(const char* why);
int nwk_popup_open(const char* url, int w, int h);
int nwk_popup_info(void);
void nwk_popup_close(void);
int nwk_perm_get(const char* origin, const char* name);
int nwk_perm_ask(const char* origin, const char* name);
int nwk_perm_poll(int id);
int nwk_wk_ok(void);
int nwk_wk_new(void);
int nwk_wk_send(int wid, int type, const void* d, int n);
int nwk_wk_recv(int* wid, int* type, void* buf, int cap);
int nwk_wk_busy(void);
int nwk_tz_index(void);
void nwk_frame_slot(int s);
void nwk_frame_switch(void);
void nwk_status(const char* s);
void nwk_alert(const char* s);
unsigned long nwk_ms(void);
int nwk_clip_get(char* out, int cap);
void nwk_clip_set(const char* s, int n);
unsigned long long nwk_us(void);
typedef struct { unsigned long long requests, reused, conns, handshakes, bytes, dns_us, connect_us, tls_us, wait_us, body_us, fetch_us, cache_hits, disk_hits, pump_us, pumps, prefetched, syn_resends; } nwk_netstat;
typedef struct { unsigned ms, dns, tcp, tls; char host[48]; } nwk_slow;
extern nwk_slow g_nwk_slow;
typedef struct { unsigned long long js_us, compile_us, style_us, layout_us, paint_us, scripts, code_hits, code_disk, load_us, net_us, after_us, after_net_us, after_ops, after_scripts, after_js_us, tick_us, event_us, longest_us, mem_used, mem_size, js_mem; } nwe_perf;
unsigned long long nwk_net_us(void);
void nwk_prefetch(const char** urls, int n, const char* referer);
void nwk_prefetch_more(const char** urls, int n, const char* referer);
int nwk_busy(void);
long long nwk_epoch_ms(void);
int nwk_tz_min(void);
void nwk_pump(void);
int nwk_ws_open(const char* url, const char* protocols, const char* origin);
int nwk_ws_send(int id, const unsigned char* data, int len, int binary);
int nwk_ws_recv(int id, unsigned char* out, int cap, int* binary);
int nwk_ws_info(int id, int* code, char* proto, int pcap, char* reason, int rcap);
void nwk_ws_close(int id, int code, int forget);
void nwk_ws_close_all(void);
int nwk_frame_status(const char* url, const char* srcdoc, char* out, int cap);
int nwk_frame_paint(const char* url, const char* srcdoc, int x, int y, int w, int h, int cx0, int cy0, int cx1, int cy1, unsigned int* fb, int stride);
int nwk_frame_post(const char* msg, int len);
int nwk_frame_recv(char* out, int cap);
int nwk_frame_is(void);
void nwk_frame_reset(void);
void nwk_frame_open(const char* url);
void nwk_frame_sandbox(int flags);
int nwk_doc_policy(char* csp, int cap, char* ro, int rcap);
int nwk_mods(void);
void nwk_notify(const char* t, const char* b);
void nwk_badge(int n);
void nwk_frame_open_doc(const char* html);
void* nwk_alloc_arena(unsigned long n);
int nwk_code_get(unsigned long long key, int srclen, const unsigned char** data, int* len);
void nwk_code_put(unsigned long long key, int srclen, const unsigned char* data, int len);
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
