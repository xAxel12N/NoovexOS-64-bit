#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "drivers/mmc/mmc_blk.h"
struct mmc_host* g_mmc_hosts[MMC_MAX_HOSTS]; int g_mmc_nhosts=0;
static u8* img; static u64 nsec; static int maxn, fail_at=-1, calls;
int mmc_read_blocks(struct mmc_host* h,u64 lba,u32 n,void* buf){ (void)h; calls++; if((int)n>maxn)maxn=(int)n; if(fail_at>=0&&lba<=(u64)fail_at&&lba+n>(u64)fail_at)return -3; if(lba+n>nsec)return -5; memcpy(buf,img+lba*512,(size_t)n*512); return 0; }
int mmc_write_blocks(struct mmc_host* h,u64 lba,u32 n,const void* buf){ (void)h; calls++; if((int)n>maxn)maxn=(int)n; if(lba+n>nsec)return -5; memcpy(img+lba*512,buf,(size_t)n*512); return 0; }
static int fails=0, checks=0;
#define CK(c,m) do{ checks++; if(!(c)){ fails++; printf("FAIL %s\n",m); } }while(0)
int main(int argc,char** argv){
    nsec=argc>1?0:65536; FILE* f=argc>1?fopen(argv[1],"rb"):0; if(f){ fseek(f,0,SEEK_END); nsec=(u64)ftell(f)/512; fseek(f,0,SEEK_SET); }
    img=calloc(nsec,512); if(f){ if(fread(img,512,nsec,f)!=nsec)return 2; fclose(f); }
    struct mmc_host h; memset(&h,0,sizeof h); h.name="sim-sdhci"; h.present=1; h.ready=1; h.card.type=MMC_TYPE_SD; h.card.hc=1; h.card.sectors=nsec; memcpy(h.card.name,"SIM32",6);
    g_mmc_hosts[0]=&h; g_mmc_nhosts=1;
    CK(mmcblk_count()==0,"empty at start");
    mmcblk_sync_hosts(); CK(mmcblk_count()==1,"sync attaches ready card"); mmcblk_sync_hosts(); CK(mmcblk_count()==1,"sync is idempotent");
    CK(!strcmp(g_mmcblk[0].name,"mmcblk0")&&strstr(g_mmcblk[0].model,"SIM32")&&g_mmcblk[0].kind==MMCBLK_SDHC,"name/model/kind");
    u8* b=malloc(512*300); CK(!mmcblk_read(0,0,1,b),"read sector 0");
    if(f){ CK(b[510]==0x55&&b[511]==0xAA,"MBR signature through block layer"); u32 st=b[454]|(b[455]<<8)|(b[456]<<16)|((u32)b[457]<<24); CK(st>0&&st<nsec,"partition start sane");
        CK(!mmcblk_read(0,st,1,b)&&b[510]==0x55&&(!memcmp(b+54,"FAT",3)||!memcmp(b+82,"FAT",3)),"FAT boot sector on partition"); }
    maxn=0; calls=0; CK(!mmcblk_read(0,10,300,b)&&maxn<=MMCBLK_CHUNK&&calls==5,"300-sector read split into 64-sector chunks");
    CK(mmcblk_read(0,nsec-1,2,b)==-1,"read past end refused"); CK(mmcblk_read(0,nsec,1,b)==-1,"read at end refused"); CK(mmcblk_read(0,(u64)-1,1,b)==-1,"overflowing lba refused");
    memset(b,0xA5,512); CK(!mmcblk_write(0,nsec-1,1,b)&&img[(nsec-1)*512]==0xA5,"write last sector");
    fail_at=20; CK(mmcblk_read(0,0,64,b)==-1&&g_mmcblk[0].nerr==1,"host error propagates and counts"); fail_at=-1;
    char s[512]; mmcblk_status(s,sizeof s); CK(strstr(s,"mmcblk0")&&strstr(s,"SDHC"),"status text");
    mmcblk_status(s,8); CK(strlen(s)==7,"status truncates safely");
    CK(!mmcblk_eject(0)&&mmcblk_count()==0&&mmcblk_read(0,0,1,b)==-1,"eject blocks IO"); CK(mmcblk_eject(0)==-1,"double eject refused");
    h.present=0; mmcblk_sync_hosts(); CK(!g_mmcblk[0].used,"card removal detaches");
    h.present=1; h.card.ro=1; mmcblk_sync_hosts(); CK(mmcblk_count()==1&&mmcblk_write(0,0,1,b)==-2,"write-protected card refuses writes");
    for(int i=0;i<8;i++){ mmcblk_add(MMCBLK_SD,"x",8,0,0,(mmcblk_rd_fn)0,0); } CK(mmcblk_count()==1,"null read op refused");
    static int dummy; int k=0; for(int i=0;i<8;i++) if(mmcblk_add(MMCBLK_SD,0,8,0,&dummy,(mmcblk_rd_fn)mmc_read_blocks,0)>=0)k++; CK(k==MMCBLK_MAX-1,"slot table bounded");
    free(b); free(img); printf("%d checks, %d failures\n%s\n",checks,fails,fails?"FAILED":"ALL PASS"); fflush(stdout); return fails?1:0; }
