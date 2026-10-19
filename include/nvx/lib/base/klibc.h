#ifndef NVX_LIB_KLIBC_H
#define NVX_LIB_KLIBC_H

#include "include/nvx/base.h"

unsigned long strlen(const char*s);
int strcmp(const char*a,const char*b);
int strncmp(const char*a,const char*b,unsigned long n);
char* strcpy(char*d,const char*s);
char* strncpy(char*d,const char*s,unsigned long n);
char* strcat(char*d,const char*s);
char* strchr(const char*s,int c);
char* strrchr(const char*s,int c);
char* strstr(const char*h,const char*n);
void* memmove(void*d,const void*s,unsigned long n);
int memcmp(const void*a,const void*b,unsigned long n);
void* memchr(const void*s,int c,unsigned long n);
int atoi(const char*s);
int abs(int v);

#endif
