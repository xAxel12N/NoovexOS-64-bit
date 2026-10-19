#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/auxv.h>
#include <link.h>
#include "libnvxdemo_so.h"

static int fails, passes;
static void out(const char* s){
    fputs(s,stdout); fflush(stdout);
    int k=open("/dev/kmsg",O_WRONLY); if(k>=0){ if(write(k,s,strlen(s))<0){} close(k); }
}
static void res(const char* name,int ok,const char* detail){
    char b[300]; snprintf(b,sizeof b,"dyntest: %s %s%s%s\n",ok?"ok  ":"FAIL",name,detail&&*detail?" - ":"",detail?detail:""); out(b);
    if(ok)passes++; else fails++;
}
static int nlib;
static int each(struct dl_phdr_info* i,size_t sz,void* d){ (void)sz; char* b=d; size_t L=strlen(b); if(L<200)snprintf(b+L,256-L,"%s%s",L?",":"",i->dlpi_name&&*i->dlpi_name?i->dlpi_name:"(main)"); nlib++; return 0; }
static void* th(void* a){ return (void*)((long)a*2); }
int main(void){
    char d[256];
    out("dyntest: start\n");
    unsigned long base=getauxval(AT_BASE), ent=getauxval(AT_ENTRY);
    snprintf(d,sizeof d,"AT_BASE=0x%lx AT_ENTRY=0x%lx",base,ent); res("started through the dynamic linker",base!=0&&ent!=0,d);
    d[0]=0; nlib=0; dl_iterate_phdr(each,d); res("loaded objects",nlib>=2,d);
    char* m=malloc(100000); strcpy(m,"heap"); res("malloc from the shared libc",m&&!strcmp(m,"heap"),0); free(m);
    pthread_t t; void* rv=0; int pc=pthread_create(&t,0,th,(void*)21); if(!pc)pthread_join(t,&rv);
    res("threads in a dynamic program",pc==0&&(long)rv==42,0);
    const char* path="/tmp/libnvxdemo.so";
    mkdir("/tmp",0777);
    int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0755); int w=-1; if(fd>=0){ w=(int)write(fd,libnvxdemo_so,libnvxdemo_so_len); close(fd); }
    snprintf(d,sizeof d,"wrote %d of %u bytes",w,libnvxdemo_so_len); res("write a shared library to disk",w==(int)libnvxdemo_so_len,d);
    void* h=dlopen(path,RTLD_NOW);
    snprintf(d,sizeof d,"%s",h?"loaded":dlerror()); res("dlopen",h!=0,d);
    if(h){
        int (*add)(int,int)=(int(*)(int,int))dlsym(h,"demo_add");
        const char* (*name)(void)=(const char*(*)(void))dlsym(h,"demo_name");
        int* ctr=(int*)dlsym(h,"demo_counter");
        void* (*al)(size_t)=(void*(*)(size_t))dlsym(h,"demo_alloc");
        res("dlsym finds the functions",add&&name&&ctr&&al,0);
        if(add&&name&&ctr&&al){
            snprintf(d,sizeof d,"demo_add(40,2)=%d",add(40,2)); res("call into the library",add(40,2)==42,d);
            snprintf(d,sizeof d,"%s",name()); res("library calls back into libc",!strcmp(name(),"libnvxdemo strlen=6"),d);
            snprintf(d,sizeof d,"demo_counter=%d",*ctr); res("library constructor ran",*ctr==42,d);
            char* p=al(64); res("memory from the library's malloc can be freed by the program",p&&p[63]==7,0); free(p); }
        d[0]=0; nlib=0; dl_iterate_phdr(each,d); res("dl_iterate_phdr sees the library",strstr(d,"libnvxdemo")!=0,d);
        res("dlclose",dlclose(h)==0,0); }
    snprintf(d,sizeof d,"dyntest: done %d passed, %d failed\n",passes,fails); out(d);
    return fails?1:0;
}
