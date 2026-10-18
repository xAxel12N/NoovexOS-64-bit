#ifndef _SHIM_STDLIB_H
#define _SHIM_STDLIB_H
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
void*  malloc(size_t);
void   free(void*);
void*  realloc(void*,size_t);
void*  calloc(size_t,size_t);
int    atoi(const char*);
double atof(const char*);
long   strtol(const char*,char**,int);
int    abs(int);
long   labs(long);
void   exit(int);
void   abort(void);
char*  getenv(const char*);
int    system(const char*);
void   qsort(void*,size_t,size_t,int(*)(const void*,const void*));
#endif
