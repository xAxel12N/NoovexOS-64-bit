#ifndef _NVX_STDLIB_H
#define _NVX_STDLIB_H
#include <stddef.h>
void* malloc(size_t); void* calloc(size_t,size_t); void* realloc(void*,size_t); void free(void*);
long strtol(const char*,char**,int); unsigned long strtoul(const char*,char**,int); long long strtoll(const char*,char**,int); unsigned long long strtoull(const char*,char**,int);
int atoi(const char*); long random(void); void srandom(unsigned); int rand(void); void srand(unsigned); int abs(int); long labs(long);
void qsort(void*,size_t,size_t,int(*)(const void*,const void*)); char* getenv(const char*); void exit(int); void abort(void);
#define MB_CUR_MAX 4
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 2147483647
#endif
