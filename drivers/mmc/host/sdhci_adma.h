#ifndef NVX_SDHCI_ADMA_H
#define NVX_SDHCI_ADMA_H
#include "include/nvx/base.h"
#define SDHCI_ADMA_VALID 0x01
#define SDHCI_ADMA_END 0x02
#define SDHCI_ADMA_TRAN 0x20
#define SDHCI_ADMA_MAXSEG 65536u
static inline int sdhci_adma2_fill(u32* tab,int max,u64 addr,u32 len){
    if(!tab||max<1||!len||(addr&3)||(len&3)||addr+(u64)len>0x100000000ULL)return -1;
    int n=0; while(len){ if(n>=max)return -1; u32 k=len>SDHCI_ADMA_MAXSEG?SDHCI_ADMA_MAXSEG:len; len-=k;
        tab[2*n]=((k==SDHCI_ADMA_MAXSEG?0u:k)<<16)|SDHCI_ADMA_TRAN|SDHCI_ADMA_VALID|(len?0u:SDHCI_ADMA_END); tab[2*n+1]=(u32)addr; addr+=k; n++; }
    return n; }
#endif
