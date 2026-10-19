#ifndef _NVX_STDIO_H
#define _NVX_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct _nvx_file { int fd; } FILE;
extern FILE* stdin; extern FILE* stdout; extern FILE* stderr;
#define EOF (-1)
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif
#define BUFSIZ 1024
int printf(const char*,...); int fprintf(FILE*,const char*,...); int sprintf(char*,const char*,...); int snprintf(char*,size_t,const char*,...);
int vprintf(const char*,va_list); int vfprintf(FILE*,const char*,va_list); int vsprintf(char*,const char*,va_list); int vsnprintf(char*,size_t,const char*,va_list);
int puts(const char*); int fputs(const char*,FILE*); int fputc(int,FILE*); int putchar(int); int fflush(FILE*);
FILE* fopen(const char*,const char*); int fclose(FILE*); char* fgets(char*,int,FILE*); size_t fread(void*,size_t,size_t,FILE*); size_t fwrite(const void*,size_t,size_t,FILE*);
int getc(FILE*); int fgetc(FILE*); int feof(FILE*); int ferror(FILE*); void perror(const char*); int sscanf(const char*,const char*,...);
#define putc fputc
#endif
