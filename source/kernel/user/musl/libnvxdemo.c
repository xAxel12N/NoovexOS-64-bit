#include <stdio.h>
#include <string.h>
#include <stdlib.h>
int demo_counter;
static char g_name[64];
__attribute__((constructor)) static void demo_init(void){ demo_counter=42; }
int demo_add(int a,int b){ return a+b; }
const char* demo_name(void){ snprintf(g_name,sizeof g_name,"libnvxdemo strlen=%zu",strlen("noovex")); return g_name; }
void* demo_alloc(size_t n){ char* p=malloc(n); if(p)memset(p,7,n); return p; }
