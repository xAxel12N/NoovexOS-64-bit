#ifndef _NVX_STDIO_H
#define _NVX_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct _NVXFILE FILE;
extern FILE* stdin; extern FILE* stdout; extern FILE* stderr;
#define EOF (-1)
#define BUFSIZ 512
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define L_tmpnam 32
#define FOPEN_MAX 8
#define _IONBF 2
#define _IOLBF 1
#define _IOFBF 0
int printf(const char*,...);
int fprintf(FILE*,const char*,...);
int snprintf(char*,size_t,const char*,...);
int sprintf(char*,const char*,...);
int vsnprintf(char*,size_t,const char*,va_list);
int vfprintf(FILE*,const char*,va_list);
int sscanf(const char*,const char*,...);
int fputs(const char*,FILE*);
int fputc(int,FILE*);
int putc(int,FILE*);
int fflush(FILE*);
int ferror(FILE*);
int feof(FILE*);
FILE* fopen(const char*,const char*);
FILE* freopen(const char*,const char*,FILE*);
int fclose(FILE*);
size_t fread(void*,size_t,size_t,FILE*);
size_t fwrite(const void*,size_t,size_t,FILE*);
int fseek(FILE*,long,int);
long ftell(FILE*);
char* fgets(char*,int,FILE*);
int getc(FILE*);
int ungetc(int,FILE*);
void clearerr(FILE*);
int setvbuf(FILE*,char*,int,size_t);
void setbuf(FILE*,char*);
int remove(const char*);
int rename(const char*,const char*);
FILE* tmpfile(void);
char* tmpnam(char*);
int fscanf(FILE*,const char*,...);
int feof(FILE*);
int fgetc(FILE*);
#endif
