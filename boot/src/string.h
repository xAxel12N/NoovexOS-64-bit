#ifndef STRING_H
#define STRING_H
#include "types.h"
void* memcpy(void* d, const void* s, size_t n);
void* memmove(void* d, const void* s, size_t n);
void* memset(void* d, int c, size_t n);
int memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, size_t n);
char* strchr(const char* s, int c);
void strncpy_z(char* d, const char* s, size_t n);
int str_to_int(const char* s);
#endif
