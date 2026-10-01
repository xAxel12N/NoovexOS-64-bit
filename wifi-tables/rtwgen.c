#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef uint32_t u32;

#define BRANCH_IF   0
#define BRANCH_ELIF 1
#define BRANCH_ELSE 2
#define BRANCH_ENDIF 3

static u32 g_cut=0, g_pkg=0, g_intf=1, g_rfe=0;

static int cond_pos(u32 w){ return (w>>31)&1; }
static int cond_neg(u32 w){ return (w>>30)&1; }
static int cond_branch(u32 w){ return (w>>28)&3; }
static u32 cond_cut(u32 w){ return (w>>20)&0xF; }
static u32 cond_intf(u32 w){ return (w>>16)&0xF; }
static u32 cond_pkg(u32 w){ return (w>>12)&0xF; }
static u32 cond_rfe(u32 w){ return (w>>0)&0xFF; }

static int check_positive(u32 c){
    if(cond_cut(c) && cond_cut(c)!=g_cut) return 0;
    if(cond_pkg(c) && cond_pkg(c)!=g_pkg) return 0;
    if(cond_intf(c) && cond_intf(c)!=g_intf) return 0;
    if(cond_rfe(c) != g_rfe) return 0;
    return 1;
}

static u32* load_array(const char* file,const char* name,long* outn){
    FILE* f=fopen(file,"r"); if(!f){perror(file);exit(1);}
    char* buf; long sz;
    fseek(f,0,SEEK_END); sz=ftell(f); fseek(f,0,SEEK_SET);
    buf=malloc(sz+1); fread(buf,1,sz,f); buf[sz]=0; fclose(f);
    char pat[128]; snprintf(pat,sizeof pat,"%s[] = {",name);
    char* p=strstr(buf,pat); if(!p){fprintf(stderr,"array %s not found\n",name);exit(1);}
    p=strchr(p,'{')+1;
    char* end=strstr(p,"};"); if(!end){fprintf(stderr,"no end for %s\n",name);exit(1);}
    *end=0;
    u32* v=malloc(sizeof(u32)*200000); long n=0;
    char* s=p;
    while(s<end){
        while(*s && (*s==' '||*s=='\t'||*s=='\n'||*s=='\r'||*s==',')) s++;
        if(s>=end||!*s) break;
        if(s[0]=='0'&&(s[1]=='x'||s[1]=='X')){
            v[n++]=(u32)strtoul(s,&s,16);
        } else { while(*s && *s!=','&&*s!='\n') s++; }
    }
    *outn=n; free(buf); return v;
}

static void emit_flat(const char* file,const char* name,int size,const char* out){
    long n; u32* v=load_array(file,name,&n);
    FILE* o=fopen(out,"w"); if(!o){perror(out);exit(1);}
    fprintf(o,"# %s  rfe=0x%02X cut=%u pkg=%u intf=%u\n",name,g_rfe,g_cut,g_pkg,g_intf);
    long i=0, emitted=0;
    int is_matched=1, is_skipped=0;
    u32 pos_cond=0;
    while(i+1<n){
        u32 a=v[i], d=v[i+1];
        if(cond_pos(a)){
            int br=cond_branch(a);
            if(br==BRANCH_ENDIF){ is_matched=1; is_skipped=0; }
            else if(br==BRANCH_ELSE){ is_matched = is_skipped?0:1; }
            else { pos_cond=a; }
            i+=2; continue;
        } else if(cond_neg(a)){
            if(!is_skipped){
                if(check_positive(pos_cond)){ is_matched=1; is_skipped=1; }
                else { is_matched=0; is_skipped=0; }
            } else is_matched=0;
            i+=2; continue;
        } else if(is_matched){
            fprintf(o,"%X %d %X\n",a,size,d);
            emitted++;
        }
        i+=2;
    }
    fclose(o);
    printf("  %-22s -> %-14s %ld writes\n",name,out,emitted);
    free(v);
}

int main(int argc,char** argv){
    if(argc<2){ fprintf(stderr,"usage: rtwgen <rtw8821c_table.c> [rfe=NN] [cut=N] [pkg=N] [intf=N]\n"); return 1; }
    const char* src=argv[1];
    for(int i=2;i<argc;i++){
        if(!strncmp(argv[i],"rfe=",4)) g_rfe=(u32)strtoul(argv[i]+4,0,0);
        else if(!strncmp(argv[i],"cut=",4)) g_cut=(u32)strtoul(argv[i]+4,0,0);
        else if(!strncmp(argv[i],"pkg=",4)) g_pkg=(u32)strtoul(argv[i]+4,0,0);
        else if(!strncmp(argv[i],"intf=",5)) g_intf=(u32)strtoul(argv[i]+5,0,0);
    }
    printf("resolving for rfe=0x%02X cut=%u pkg=%u intf=%u (intf 1=PCIe)\n",g_rfe,g_cut,g_pkg,g_intf);
    emit_flat(src,"rtw8821c_mac",  1,"RTWMAC.TXT");
    emit_flat(src,"rtw8821c_bb",   4,"RTWBB.TXT");
    emit_flat(src,"rtw8821c_agc",  4,"RTWAGC.TXT");
    emit_flat(src,"rtw8821c_rf_a", 5,"RTWRFA.TXT");
    printf("done. size column: 1/2/4 = MMIO write of that width, 5 = RF SIPI write (path A).\n");
    printf("BB/AGC delay opcodes (addr 0xF9-0xFE) and RF delay opcodes (0xFE,0xFFE) are preserved as-is.\n");
    return 0;
}
