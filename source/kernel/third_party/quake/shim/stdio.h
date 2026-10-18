#ifndef _SHIM_STDIO_H
#define _SHIM_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct _DFILE FILE;
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define EOF (-1)
#define BUFSIZ 8192
extern FILE* stdin; extern FILE* stdout; extern FILE* stderr;
FILE* fopen(const char*,const char*);
int   fclose(FILE*);
size_t fread(void*,size_t,size_t,FILE*);
size_t fwrite(const void*,size_t,size_t,FILE*);
int   fseek(FILE*,long,int);
long  ftell(FILE*);
int   fflush(FILE*);
int   feof(FILE*);
char* fgets(char*,int,FILE*);
int   fputs(const char*,FILE*);
int   fputc(int,FILE*);
int   fgetc(FILE*);
int   ungetc(int,FILE*);
int   puts(const char*);
int   putchar(int);
int   getchar(void);
int   remove(const char*);
int   rename(const char*,const char*);
int   printf(const char*,...);
int   fprintf(FILE*,const char*,...);
int   sprintf(char*,const char*,...);
int   snprintf(char*,size_t,const char*,...);
int   vsnprintf(char*,size_t,const char*,va_list);
int   vfprintf(FILE*,const char*,va_list);
int   vprintf(const char*,va_list);
int   fscanf(FILE*,const char*,...);
int   sscanf(const char*,const char*,...);
#endif
