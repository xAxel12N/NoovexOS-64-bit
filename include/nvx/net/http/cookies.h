#ifndef NVX_NET_COOKIES_H
#define NVX_NET_COOKIES_H
#include "include/nvx/base.h"
void cookie_set(const char* url, const char* line, int fromjs);
int cookie_header(const char* url, int forjs, char* out, int cap);
void cookie_clear(void);
int cookie_count(void);
int cookie_list(int idx, char* domain, int dcap, char* name, int ncap, char* value, int vcap, u64* expires, int* flags);
void cookie_delete_domain(const char* dom);
int webstore_get(const char* origin, int session, const char* key, char* out, int cap);
void webstore_set(const char* origin, int session, const char* key, const char* val);
void webstore_clear(const char* origin, int session);
int webstore_key(const char* origin, int session, int idx, char* out, int cap);
int webstore_len(const char* origin, int session);
void webstore_session_reset(void);
void cookies_flush(int force);
#endif
