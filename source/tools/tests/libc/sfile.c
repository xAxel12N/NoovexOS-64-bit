#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
static char line[2000];
int main(void){ FILE* f=fopen(getenv("STRTOD_INPUTS")?getenv("STRTOD_INPUTS"):"strtod_inputs.txt","r"); if(!f)return 1;
  while(fgets(line,sizeof line,f)){ size_t n=strlen(line); if(n&&line[n-1]=='\n')line[n-1]=0; char* e; double v=strtod(line,&e); union{double d; unsigned long long u;}u={v}; printf("%016llx %ld\n",u.u,(long)(e-line)); }
  fclose(f); return 0; }
