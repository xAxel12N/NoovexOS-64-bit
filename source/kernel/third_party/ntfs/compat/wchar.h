#ifndef _NVX_WCHAR_H
#define _NVX_WCHAR_H
#include <stddef.h>
typedef struct { unsigned int count; unsigned int value; } mbstate_t;
size_t mbrtowc(wchar_t*,const char*,size_t,mbstate_t*); size_t wcrtomb(char*,wchar_t,mbstate_t*); int mbsinit(const mbstate_t*);
size_t mbsrtowcs(wchar_t*,const char**,size_t,mbstate_t*); size_t wcsrtombs(char*,const wchar_t**,size_t,mbstate_t*); int mbtowc(wchar_t*,const char*,size_t); int wctomb(char*,wchar_t);
size_t mbstowcs(wchar_t*,const char*,size_t); size_t wcstombs(char*,const wchar_t*,size_t); size_t wcslen(const wchar_t*);
#endif
