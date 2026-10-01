/* FAT state */
static int   fat_ok=0, fat_is32=0;
static u32   fat_part_lba, fat_fatstart, fat_datastart, fat_rootlba, fat_rootclus;
static int   fat_spc, fat_rootents, fat_bps;
static u8    fat_sec[512];
static u32 fr16(const u8*p){ return p[0]|(p[1]<<8); }
static u32 fr32(const u8*p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static int fat_mount(void){
    fat_ok=0; if(g_ata_drv<0 && !ata_detect())return 0;
    if(ata_read(0,1,fat_sec)<0)return 0;
    u32 vbr=0;                                               /* find FAT: MBR partition or superfloppy */
    if(fat_sec[510]==0x55&&fat_sec[511]==0xAA){
        int mbr=(fat_sec[0]==0xEB||fat_sec[0]==0xE9)?0:1;    /* boot jump => it's a VBR (superfloppy) */
        if(mbr){ for(int e=0;e<4;e++){ const u8* pe=fat_sec+446+e*16; u8 ty=pe[4]; if(ty==0x0B||ty==0x0C||ty==0x04||ty==0x06||ty==0x0E||ty==0x01){ vbr=fr32(pe+8); break; } } }
    }
    if(ata_read(vbr,1,fat_sec)<0)return 0;
    const u8* b=fat_sec;
    fat_bps=fr16(b+11); fat_spc=b[13]; if(fat_bps!=512||fat_spc==0)return 0;
    int resv=fr16(b+14), nfat=b[16]; fat_rootents=fr16(b+17);
    u32 fsz16=fr16(b+22), fsz32=fr32(b+36); u32 fatsz=fsz16?fsz16:fsz32;
    u32 tot=fr16(b+19); if(!tot)tot=fr32(b+32);
    u32 rootsec=((u32)fat_rootents*32 + 511)/512;
    fat_part_lba=vbr; fat_fatstart=vbr+resv;
    fat_datastart=vbr+resv+nfat*fatsz+rootsec;
    u32 dataclus=(tot-(resv+nfat*fatsz+rootsec))/fat_spc;
    fat_is32=(fat_rootents==0)||(dataclus>=65525);
    if(fat_is32){ fat_rootclus=fr32(b+44); fat_rootlba=0; } else { fat_rootlba=vbr+resv+nfat*fatsz; fat_rootclus=0; }
    fat_ok=1; return 1;
}
static u32 fat_next(u32 clus){
    if(fat_is32){ u32 off=clus*4; if(ata_read(fat_fatstart+off/512,1,fat_sec)<0)return 0x0FFFFFFF; return fr32(fat_sec+off%512)&0x0FFFFFFF; }
    u32 off=clus*2; if(ata_read(fat_fatstart+off/512,1,fat_sec)<0)return 0xFFFF; return fr16(fat_sec+off%512);
}
static int fat_eoc(u32 c){ return fat_is32?(c>=0x0FFFFFF8):(c>=0xFFF8); }
static void fat_name83(const char* in,char* out){ for(int i=0;i<11;i++)out[i]=' '; int i=0,o=0; while(in[i]&&in[i]!='.'&&o<8){ char c=in[i++]; if(c>='a'&&c<='z')c-=32; out[o++]=c; } while(in[i]&&in[i]!='.')i++; if(in[i]=='.')i++; o=8; while(in[i]&&o<11){ char c=in[i++]; if(c>='a'&&c<='z')c-=32; out[o++]=c; } }
/* iterate root directory; cb(name8.3-raw11, size, attr) */
static void fat_listdir(void (*cb)(const u8* ent)){
    if(!fat_ok && !fat_mount())return;
    if(!fat_is32){ u32 sec=fat_rootlba; int ents=fat_rootents; for(int s=0; s*16<ents; s++){ if(ata_read(sec+s,1,fat_sec)<0)return; for(int e=0;e<16;e++){ const u8* ent=fat_sec+e*32; if(ent[0]==0)return; if(ent[0]==0xE5||(ent[11]&0x0F)==0x0F||(ent[11]&0x08))continue; cb(ent); } } }
    else { u32 c=fat_rootclus; int guard=0; while(!fat_eoc(c)&&c>=2&&guard++<4096){ u32 base=fat_datastart+(c-2)*fat_spc; for(int s=0;s<fat_spc;s++){ if(ata_read(base+s,1,fat_sec)<0)return; for(int e=0;e<16;e++){ const u8* ent=fat_sec+e*32; if(ent[0]==0)return; if(ent[0]==0xE5||(ent[11]&0x0F)==0x0F||(ent[11]&0x08))continue; cb(ent); } } c=fat_next(c); } }
}
static int fat_read_file(const char* name,u8* out,int cap){    /* returns bytes read or -1 */
    if(!fat_ok && !fat_mount())return -1;
    char want[11]; fat_name83(name,want);
    u32 fclus=0; int fsize=-1;
    /* scan root for the entry */
    if(!fat_is32){ u32 sec=fat_rootlba; int ents=fat_rootents; for(int s=0; s*16<ents && fsize<0; s++){ if(ata_read(sec+s,1,fat_sec)<0)return -1; for(int e=0;e<16;e++){ const u8* ent=fat_sec+e*32; if(ent[0]==0){fsize=-2;break;} if(ent[0]==0xE5||(ent[11]&0x0F)==0x0F)continue; int m=1; for(int q=0;q<11;q++)if(ent[q]!=want[q]){m=0;break;} if(m){ fclus=(fr16(ent+20)<<16)|fr16(ent+26); fsize=(int)fr32(ent+28); break; } } } }
    else { u32 c=fat_rootclus; int g=0; while(!fat_eoc(c)&&c>=2&&g++<4096&&fsize<0){ u32 base=fat_datastart+(c-2)*fat_spc; for(int s=0;s<fat_spc&&fsize<0;s++){ if(ata_read(base+s,1,fat_sec)<0)return -1; for(int e=0;e<16;e++){ const u8* ent=fat_sec+e*32; if(ent[0]==0){fsize=-2;break;} if(ent[0]==0xE5||(ent[11]&0x0F)==0x0F)continue; int m=1; for(int q=0;q<11;q++)if(ent[q]!=want[q]){m=0;break;} if(m){ fclus=(fr16(ent+20)<<16)|fr16(ent+26); fsize=(int)fr32(ent+28); break; } } } c=fat_next(c); } }
    if(fsize<0)return -1;
    int got=0; u32 c=fclus; int guard=0;
    while(!fat_eoc(c)&&c>=2&&got<fsize&&guard++<1000000){ u32 base=fat_datastart+(c-2)*fat_spc;
        for(int s=0;s<fat_spc&&got<fsize;s++){ u8 tmp[512]; if(ata_read(base+s,1,tmp)<0)return got; int n=fsize-got; if(n>512)n=512; if(got+n>cap)n=cap-got; if(n<=0)return got; for(int q=0;q<n;q++)out[got+q]=tmp[q]; got+=n; if(got>=cap)return got; }
        c=fat_next(c); }
    return got;
}

