#ifndef _NVX_STDLIB_H
#define _NVX_STDLIB_H
#include <stddef.h>
void* malloc(size_t); void* realloc(void*,size_t); void free(void*); void* calloc(size_t,size_t);
int abs(int); long labs(long);
double strtod(const char*,char**);
long strtol(const char*,char**,int);
unsigned long strtoul(const char*,char**,int);
long long strtoll(const char*,char**,int);
unsigned long long strtoull(const char*,char**,int);
int atoi(const char*); long atol(const char*); double atof(const char*);
void abort(void); void exit(int);
int rand(void); void srand(unsigned);
char* getenv(const char*);
int system(const char*);
void qsort(void*,size_t,size_t,int(*)(const void*,const void*));
void* bsearch(const void*,const void*,size_t,size_t,int(*)(const void*,const void*));
#define RAND_MAX 0x7fffffff
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#endif
