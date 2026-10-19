#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <setjmp.h>
#include <errno.h>
static int cmpi(const void* a,const void* b){ int x=*(const int*)a,y=*(const int*)b; return (x>y)-(x<y); }
static jmp_buf jb; static void jumper(int n){ if(n>3)longjmp(jb,n); jumper(n+1); }
int main(void){
  const char* nums[]={"0","42","-17","  +99xyz","0x1F","0755","2147483647","2147483648","-2147483649","9223372036854775807","9223372036854775808","abc","  -0x10","1e5","077"};
  int bases[]={10,0,16,8,2,36};
  for(int i=0;i<15;i++) for(int b=0;b<6;b++){ char* e; errno=0; long v=strtol(nums[i],&e,bases[b]); printf("strtol(%s,%d)=%ld end=%ld err=%d\n",nums[i],bases[b],v,(long)(e-nums[i]),errno==ERANGE); }
  for(int i=0;i<15;i++){ char* e; unsigned long v=strtoul(nums[i],&e,0); printf("strtoul(%s)=%lu end=%ld\n",nums[i],v,(long)(e-nums[i])); }
  const char* ds[]={"0","3.14159","-2.5e-3","1e308","1e309","4.9e-324","  .5x","inf","-INF","nan","0x1p4","12abc","1.7976931348623157e308","2.2250738585072014e-308","123456789012345678901234567890"};
  for(int i=0;i<15;i++){ char* e; double v=strtod(ds[i],&e); printf("strtod(%s)=%.17g end=%ld\n",ds[i],v,(long)(e-ds[i])); }
  printf("atoi=%d %d %d atof=%.6f\n",atoi("123"),atoi("-45z"),atoi("  7"),atof("2.5e2"));
  char buf[64]; strcpy(buf,"hello world"); printf("strstr=%s strchr=%s strrchr=%s\n",strstr(buf,"wor"),strchr(buf,'o'),strrchr(buf,'o'));
  printf("strcmp=%d %d %d strncmp=%d\n",strcmp("abc","abd")<0,strcmp("b","a")>0,strcmp("x","x"),strncmp("abcdef","abcxyz",3));
  char t[64]; strcpy(t,"a,b,,c;d"); for(char* tok=strtok(t,",;");tok;tok=strtok(0,",;"))printf("[%s]",tok); printf("\n");
  char n8[8]; memset(n8,'X',8); strncpy(n8,"ab",6); for(int i=0;i<8;i++)printf("%02x",(unsigned char)n8[i]); printf("\n");
  char mv[16]="0123456789"; memmove(mv+2,mv,8); printf("memmove=%s\n",mv); memmove(mv,mv+3,5); printf("memmove2=%.10s\n",mv);
  printf("strspn=%zu strcspn=%zu strlen=%zu\n",strspn("aaabc","a"),strcspn("hello","lo"),strlen("NoovexOS"));
  int a,b2; char w[16]; double f; int n=sscanf("  42 -7 word 3.5","%d %d %15s %lf",&a,&b2,w,&f); printf("sscanf n=%d %d %d %s %.2f\n",n,a,b2,w,f);
  n=sscanf("x=0x1f,name=abc","x=%x,name=%3s",&a,w); printf("sscanf2 n=%d %d %s\n",n,a,w);
  int arr[20]; unsigned r=12345; for(int i=0;i<20;i++){ r=r*1103515245+12345; arr[i]=(int)(r>>16)%1000; } qsort(arr,20,sizeof(int),cmpi); for(int i=0;i<20;i++)printf("%d ",arr[i]); printf("\n");
  int key=arr[7]; int* hit=bsearch(&key,arr,20,sizeof(int),cmpi); printf("bsearch found=%d\n",hit&&*hit==key);
  for(int c=0;c<256;c++){ int m=(isalpha(c)?1:0)|(isdigit(c)?2:0)|(isspace(c)?4:0)|(isupper(c)?8:0)|(islower(c)?16:0)|(ispunct(c)?32:0)|(isxdigit(c)?64:0)|(isprint(c)?128:0)|(iscntrl(c)?256:0); printf("%x%s",m,(c%32==31)?"\n":","); }
  printf("toupper=%c tolower=%c\n",toupper('q'),tolower('Q'));
  char* ps[64]; int ok=1; for(int i=0;i<64;i++){ ps[i]=malloc((size_t)(i*37+1)); memset(ps[i],i,(size_t)(i*37+1)); }
  for(int i=0;i<64;i+=2){ free(ps[i]); ps[i]=0; } for(int i=1;i<64;i+=2){ ps[i]=realloc(ps[i],(size_t)(i*37+200)); for(int k=0;k<i*37+1;k++) if((unsigned char)ps[i][k]!=(unsigned char)i)ok=0; }
  for(int i=0;i<64;i++)free(ps[i]); char* z=calloc(100,4); for(int i=0;i<400;i++) if(z[i])ok=0; free(z); printf("malloc stress ok=%d\n",ok);
  FILE* fp=fopen("/tmp/nvx_ltest.txt","w"); fprintf(fp,"line one\nline two\n%d\n",12345); fclose(fp);
  fp=fopen("/tmp/nvx_ltest.txt","a"); fputs("appended\n",fp); fclose(fp);
  fp=fopen("/tmp/nvx_ltest.txt","r"); char ln[64]; while(fgets(ln,sizeof ln,fp))printf("fgets:%s",ln);
  fseek(fp,5,SEEK_SET); printf("ftell=%ld getc=%c\n",ftell(fp),getc(fp)); int uc=getc(fp); ungetc(uc,fp); printf("ungetc=%c again=%c\n",uc,getc(fp));
  fseek(fp,0,SEEK_END); printf("size=%ld\n",ftell(fp)); rewind(fp); char rb[10]={0}; size_t got=fread(rb,1,8,fp); printf("fread=%zu <%s> eof=%d\n",got,rb,feof(fp)); fclose(fp); remove("/tmp/nvx_ltest.txt");
  printf("fopen missing=%d\n",fopen("/tmp/definitely/missing","r")==0);
  time_t tt=1791158400; struct tm* g=gmtime(&tt); char tb[128]; strftime(tb,sizeof tb,"%Y-%m-%d %H:%M:%S %a %b %j %A %B %y %e|%%",g); printf("strftime=%s wday=%d yday=%d\n",tb,g->tm_wday,g->tm_yday);
  struct tm tm2={0}; tm2.tm_year=126; tm2.tm_mon=9; tm2.tm_mday=5; tm2.tm_hour=12; printf("timegm-ish via mktime fields year=%d\n",tm2.tm_year+1900);
  int jr=setjmp(jb); if(!jr){ jumper(0); printf("not reached\n"); } else printf("longjmp returned %d\n",jr);
  printf("abs=%d labs=%ld div=%d,%d\n",abs(-5),labs(-7L),div(17,5).quot,div(17,5).rem);
  return 0; }
