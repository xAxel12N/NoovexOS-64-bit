#include "string.h"

void* memcpy(void* d, const void* s, size_t n){
    uint8_t* dd = (uint8_t*)d;
    const uint8_t* ss = (const uint8_t*)s;
    while(n--) *dd++ = *ss++;
    return d;
}

void* memmove(void* d, const void* s, size_t n){
    uint8_t* dd = (uint8_t*)d;
    const uint8_t* ss = (const uint8_t*)s;
    if(dd < ss){ while(n--) *dd++ = *ss++; }
    else { dd += n; ss += n; while(n--) *--dd = *--ss; }
    return d;
}

void* memset(void* d, int c, size_t n){
    uint8_t* dd = (uint8_t*)d;
    while(n--) *dd++ = (uint8_t)c;
    return d;
}

int memcmp(const void* a, const void* b, size_t n){
    const uint8_t* x = (const uint8_t*)a;
    const uint8_t* y = (const uint8_t*)b;
    while(n--){ if(*x != *y) return (int)*x - (int)*y; x++; y++; }
    return 0;
}

size_t strlen(const char* s){
    size_t n = 0;
    while(s[n]) n++;
    return n;
}

int strcmp(const char* a, const char* b){
    while(*a && (*a == *b)){ a++; b++; }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

int strncmp(const char* a, const char* b, size_t n){
    while(n && *a && (*a == *b)){ a++; b++; n--; }
    if(n == 0) return 0;
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

char* strchr(const char* s, int c){
    while(*s){ if(*s == (char)c) return (char*)s; s++; }
    return (char)c ? NULL : (char*)s;
}

void strncpy_z(char* d, const char* s, size_t n){
    size_t i = 0;
    if(n == 0) return;
    while(i < n - 1 && s[i]){ d[i] = s[i]; i++; }
    d[i] = 0;
}

int str_to_int(const char* s){
    int r = 0, neg = 0;
    while(*s == ' ' || *s == '\t') s++;
    if(*s == '-'){ neg = 1; s++; }
    while(*s >= '0' && *s <= '9'){ r = r*10 + (*s - '0'); s++; }
    return neg ? -r : r;
}
