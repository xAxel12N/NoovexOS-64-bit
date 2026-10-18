#include <stdio.h>
#include <string.h>
#include <stdint.h>
static uint64_t s=0xD1B54A32D192ED03ull; static uint64_t rnd(void){ s^=s<<13; s^=s>>7; s^=s<<17; return s; }
static double rdbl(void){ uint64_t k=rnd()%10; union{double d;uint64_t u;}u;
  if(k<4){ u.u=rnd(); return u.d; }
  if(k==4){ static const double sp[]={0.0,-0.0,0.5,1.5,2.5,-2.5,0.125,1e300,1e-300,4.9e-324,2.2250738585072014e-308,1.7976931348623157e308,2.675,1.005,0.1,123456.785,9.9999995,99.5,1e15+0.5,1e22}; return sp[rnd()%20]*(rnd()%2?1:-1); }
  if(k==5){ return (double)((int64_t)(rnd()%2000001)-1000000)/(double)(1u<<(rnd()%12)); }
  if(k==6){ return 1.0/0.0*(rnd()%2?1:-1); }
  return (double)(int64_t)rnd()/(double)(rnd()|1)*(double)(rnd()%1000);
}
int main(void){
  static const char* fl[]={"","-","+"," ","#","0","-+","0+","# ","-0","+#0"," 0#"};
  static const char* ic="diuxXo"; static const char* fc="fFeEgGaA";
  static const char* lens[]={"","hh","h","l","ll","z","j"};
  char fmt[64], out[2048];
  for(int i=0;i<100000;i++){
    int kind=(int)(rnd()%5); char* p=fmt; *p++='%'; strcpy(p,fl[rnd()%12]); p+=strlen(p);
    if(rnd()%2){ p+=sprintf(p,"%d",(int)(rnd()%30)); }
    int hp=(int)(rnd()%3); if(hp==1){ p+=sprintf(p,".%d",(int)(rnd()%(kind==1?25:20))); } else if(hp==2){ *p++='.'; }
    int r;
    if(kind==0){ const char* L=lens[rnd()%7]; strcpy(p,L); p+=strlen(p); *p++=ic[rnd()%6]; *p=0;
      long long v=(long long)rnd(); if(rnd()%3==0)v%=1000; if(!strcmp(L,"ll")||!strcmp(L,"j")||!strcmp(L,"z")||!strcmp(L,"l")) r=snprintf(out,sizeof out,fmt,v); else r=snprintf(out,sizeof out,fmt,(int)v); }
    else if(kind==1){ *p++=fc[rnd()%8]; *p=0; double d=rdbl(); r=snprintf(out,sizeof out,fmt,d); }
    else if(kind==2){ *p++='s'; *p=0; static const char* ss[]={"","a","hello","NoovexOS is here","tab\there"}; r=snprintf(out,sizeof out,fmt,ss[rnd()%5]); }
    else if(kind==3){ *p++='c'; *p=0; r=snprintf(out,sizeof out,fmt,(int)('A'+rnd()%26)); }
    else { strcpy(fmt,"%%|%5d|%-5d|%05.1f|%s|%p"); r=snprintf(out,sizeof out,fmt,-42,42,3.25,"x",(void*)(uintptr_t)(rnd()%2?0x1234abcdULL:0)); }
    printf("%d [%s] r=%d <%s>\n",i,fmt,r,out);
  }
  char small[8]; int r2=snprintf(small,sizeof small,"%s","truncated string"); printf("trunc r=%d <%s>\n",r2,small);
  return 0; }
