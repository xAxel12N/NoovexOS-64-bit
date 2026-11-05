static char g_ip_e1[120], g_ip_e2[120];
static int ip_mem_verify(const u8* buf,const u8* ref,u32 len){
    u32 first=0xFFFFFFFFu, cnt=0; for(u32 i=0;i<len;i++) if(buf[i]!=ref[i]){ if(first==0xFFFFFFFFu)first=i; cnt++; }
    if(!cnt)return 0;
    u8* w=(u8*)buf; for(u32 i=0;i<len;i++)w[i]=ref[i];
    u32 now=0; for(u32 i=0;i<len;i++) if(buf[i]!=ref[i])now++;
    mt_wait(1000);
    u32 late=0, lf=0xFFFFFFFFu; for(u32 i=0;i<len;i++) if(buf[i]!=ref[i]){ if(lf==0xFFFFFFFFu)lf=i; late++; }
    u64 at=(u64)buf+first; u64 bs,be; u32 bt; int inmap=mm_known()&&mm_first_bad(at,at+1,&bs,&be,&bt);
    char* p=g_ip_e1; p=scopy(p,"Memory at "); p=hx64(p,at); p=scopy(p,now?" does not keep data":late?" was changed by something else":" was changed once"); *p=0;
    p=g_ip_e2; p=udec(p,cnt); p=scopy(p," differed"); if(late){ p=scopy(p,", "); p=udec(p,late); p=scopy(p," again at "); p=hx64(p,(u64)buf+lf); *p++=':'; for(u32 i=0;i<8&&lf+i<len;i++){ *p++=' '; p=hx(p,buf[lf+i],2); } } if(inmap){ p=scopy(p,", fw: "); p=scopy(p,mm_tname(bt)); } *p=0;
    return 1; }
typedef struct { u8* img; u32 bps,res,nf,fatsz,dstart,psec,next; int err; } ifat;
static u64 if_off(ifat* f,u32 c){ return (u64)(f->dstart+(c-2))*f->bps; }
static void if_set(ifat* f,u32 c,u32 v){ for(u32 i=0;i<f->nf;i++) *(u32*)(f->img+(u64)(f->res+i*f->fatsz)*f->bps+(u64)c*4)=v&0x0FFFFFFFu; }
static u32 if_alloc(ifat* f,u32 n){ if(!n)n=1; u32 first=f->next;
    if((u64)f->dstart+(u64)(f->next-2)+(u64)n>(u64)f->psec){ f->err=1; return 0; }
    for(u32 i=0;i<n;i++){ u32 c=f->next++; if_set(f,c,(i==n-1)?0x0FFFFFFFu:(c+1)); } return first; }
static u32 if_file(ifat* f,const u8* d,u32 len){ u32 c0=if_alloc(f,(len+f->bps-1)/f->bps); if(f->err)return 0; u8* p=f->img+if_off(f,c0); for(u32 i=0;i<len;i++)p[i]=d[i]; return c0; }
static void if_ent(ifat* f,u32 dirc,int slot,const char* nm,u8 attr,u32 clus,u32 size){
    u8* e=f->img+if_off(f,dirc)+(u64)slot*32; u8 n[11];
    if(nm[0]=='.'&&(nm[1]==0||(nm[1]=='.'&&nm[2]==0))){ for(int i=0;i<11;i++)n[i]=' '; n[0]='.'; if(nm[1])n[1]='.'; }
    else if(attr==0x08){ for(int i=0;i<11;i++){ char c=nm[i]; if(!c){ for(;i<11;i++)n[i]=' '; break; } if(c>='a'&&c<='z')c-=32; n[i]=(u8)c; } }
    else ins83(nm,n);
    for(int i=0;i<11;i++)e[i]=n[i]; e[11]=attr; *(u16*)(e+20)=(u16)(clus>>16); *(u16*)(e+26)=(u16)(clus&0xFFFF); *(u32*)(e+28)=size;
    *(u16*)(e+14)=0x6000; *(u16*)(e+16)=0x5B3B; *(u16*)(e+22)=0x6000; *(u16*)(e+24)=0x5B3B; *(u16*)(e+18)=0x5B3B; }
static int if_fits83(const char* nm){ int i=0, dot=-1; for(;nm[i];i++){ char c=nm[i]; if(c=='.'){ if(dot>=0)return 0; dot=i; continue; } if(c>='a'&&c<='z')return 0; if(c==' ')return 0; }
    if(dot<0)return i>=1&&i<=8; return dot>=1&&dot<=8&&i-dot-1>=1&&i-dot-1<=3; }
static void if_alias(const char* nm,int seq,u8 out[11]){ for(int i=0;i<11;i++)out[i]=' '; int o=0; const char* ext=0; for(const char* q=nm;*q;q++) if(*q=='.')ext=q+1;
    for(const char* q=nm;*q&&q!=ext-1&&o<6;q++){ char c=*q; if(c==' '||c=='.')continue; if(c>='a'&&c<='z')c-=32; out[o++]=(u8)c; }
    out[o++]='~'; out[o++]=(u8)('0'+(seq%10));
    if(ext){ int k=0; for(const char* q=ext;*q&&k<3;q++){ char c=*q; if(c>='a'&&c<='z')c-=32; out[8+k++]=(u8)c; } } }
static void if_add(ifat* f,u32 dirc,int* slot,const char* nm,u8 attr,u32 clus,u32 size){
    if(if_fits83(nm)){ if_ent(f,dirc,*slot,nm,attr,clus,size); (*slot)++; return; }
    static int seq=1; u8 sn[11]; if_alias(nm,seq++,sn); u8 ck=0; for(int i=0;i<11;i++)ck=(u8)(((ck&1)?0x80:0)+(ck>>1)+sn[i]);
    int len=0; while(nm[len])len++; int nl=(len+12)/13;
    for(int e=nl;e>=1;e--){ u8* d=f->img+if_off(f,dirc)+(u64)(*slot)*32; for(int i=0;i<32;i++)d[i]=0;
        d[0]=(u8)(e|(e==nl?0x40:0)); d[11]=0x0F; d[13]=ck;
        static const u8 pos[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};
        for(int k=0;k<13;k++){ int ci=(e-1)*13+k; u16 v=ci<len?(u16)(u8)nm[ci]:ci==len?0:0xFFFF; d[pos[k]]=(u8)v; d[pos[k]+1]=(u8)(v>>8); }
        (*slot)++; }
    u8* e=f->img+if_off(f,dirc)+(u64)(*slot)*32; for(int i=0;i<11;i++)e[i]=sn[i]; e[11]=attr; *(u16*)(e+20)=(u16)(clus>>16); *(u16*)(e+26)=(u16)(clus&0xFFFF); *(u32*)(e+28)=size;
    *(u16*)(e+14)=0x6000; *(u16*)(e+16)=0x5B3B; *(u16*)(e+22)=0x6000; *(u16*)(e+24)=0x5B3B; *(u16*)(e+18)=0x5B3B; (*slot)++; }
static u32 if_mkdir(ifat* f,u32 parent,int* pslot,const char* nm,u8 attr){ u32 d=if_alloc(f,2); if(f->err)return 0; if_add(f,parent,pslot,nm,attr,d,0); if_ent(f,d,0,".",0x10,d,0); if_ent(f,d,1,"..",0x10,parent==2?0:parent,0); return d; }
static const char IF_INFO[]="NoovexOS 64 v10.60\r\n\r\nThis disk was set up by the NoovexOS installer.\r\n\r\nEFI\\BOOT\\BOOTX64.EFI       UEFI boot loader (started by the firmware)\r\nEFI\\NoovexOS\\              copy of the boot loader and its settings\r\nNoovexOS\\System\\          system files and drivers\r\nNoovexOS\\Boot\\            boot settings\r\nNoovexOS\\Logs\\            log files\r\nProgram Files\\            installed programs\r\nProgramData\\              settings shared by all users\r\nUsers\\                    user folders\r\nboot\\kernel64.bin, KERNEL64.BIN, boot.cfg   hidden system files used to start NoovexOS\r\n";
static u32 inst_fs_layout(u32 bps,u32 psec,u32 klen,u32* fatsz,u32* dstart){
    u32 res=32, clus=psec-res; u32 fs=((clus+2)*4+bps-1)/bps; u32 ds=res+2*fs;
    u32 cfglen=(u32)(_binary_inst_boot_cfg_end-_binary_inst_boot_cfg_start), efilen=(u32)(_binary_inst_efi_bin_end-_binary_inst_efi_bin_start);
    u32 need=3+((klen+bps-1)/bps)+2*((cfglen+bps-1)/bps)+2*((efilen+bps-1)/bps)+2+3+60+8;
    *fatsz=fs; *dstart=ds; return ds+need; }
static int inst_build_fs(u8* img,u32 bps,u32 plba,u32 psec,u32 cap,const char* label,const u8* k,u32 klen,u32* used,u64* ko1,u64* ko2){
    ifat F; F.img=img; F.bps=bps; F.res=32; F.nf=2; F.psec=cap; F.next=3; F.err=0;
    u32 ds=0; inst_fs_layout(bps,psec,klen,&F.fatsz,&ds); F.dstart=ds;
    if(psec-F.dstart<65525u)return -2;
    for(u64 i=0;i<(u64)cap*bps;i++)img[i]=0;
    u8* bs=img; bs[0]=0xEB;bs[1]=0x58;bs[2]=0x90; { const char* o="NOOVEXOS"; for(int i=0;i<8;i++)bs[3+i]=(u8)o[i]; }
    *(u16*)(bs+11)=(u16)bps; bs[13]=1; *(u16*)(bs+14)=(u16)F.res; bs[16]=(u8)F.nf; bs[21]=0xF8; *(u16*)(bs+24)=63; *(u16*)(bs+26)=255;
    *(u32*)(bs+28)=plba; *(u32*)(bs+32)=psec; *(u32*)(bs+36)=F.fatsz; *(u32*)(bs+44)=2; *(u16*)(bs+48)=1; *(u16*)(bs+50)=6;
    bs[64]=0x80; bs[66]=0x29; *(u32*)(bs+67)=0x4E56584Fu^(u32)g_ticks;
    { int e=0; for(int i=0;i<11;i++){ char c=(!e&&label&&label[i])?label[i]:(e=1,' '); if(c>='a'&&c<='z')c-=32; bs[71+i]=(u8)c; } }
    { const char* ft="FAT32   "; for(int i=0;i<8;i++)bs[82+i]=(u8)ft[i]; } *(u16*)(bs+510)=0xAA55; if(bps>512)*(u16*)(bs+bps-2)=0xAA55;
    u8* fsi=img+bps; *(u32*)fsi=0x41615252u; *(u32*)(fsi+484)=0x61417272u; *(u32*)(fsi+488)=0xFFFFFFFFu; *(u32*)(fsi+492)=0xFFFFFFFFu; *(u32*)(fsi+508)=0xAA550000u;
    if_set(&F,0,0x0FFFFFF8u); if_set(&F,1,0x0FFFFFFFu); if_set(&F,2,0x0FFFFFFFu);
    const u8* cfg=_binary_inst_boot_cfg_start; u32 cfglen=(u32)(_binary_inst_boot_cfg_end-_binary_inst_boot_cfg_start);
    const u8* efi=_binary_inst_efi_bin_start; u32 efilen=(u32)(_binary_inst_efi_bin_end-_binary_inst_efi_bin_start);
    { u32 rx=if_alloc(&F,3); if(F.err||rx!=3)return -1; if_set(&F,2,rx); }
    u32 dboot=if_alloc(&F,1), defi=if_alloc(&F,2), defib=if_alloc(&F,1);
    u32 ck=if_file(&F,k,klen), cc=if_file(&F,cfg,cfglen), ce=efilen?if_file(&F,efi,efilen):0;
    if(F.err)return -1;
    char vl[12]; { int i=0; for(;i<11&&label&&label[i];i++)vl[i]=label[i]; vl[i]=0; if(!i){ vl[0]='N';vl[1]='O';vl[2]='O';vl[3]='V';vl[4]='E';vl[5]='X';vl[6]=0; } }
    if_ent(&F,2,0,vl,0x08,0,0);
    if_ent(&F,2,1,"boot",0x16,dboot,0); if_ent(&F,2,2,"boot.cfg",0x26,cc,cfglen);
    int rs=3;
    if(efilen){ if_ent(&F,2,rs++,"EFI",0x10,defi,0); }
    if_ent(&F,dboot,0,".",0x10,dboot,0); if_ent(&F,dboot,1,"..",0x10,0,0); if_ent(&F,dboot,2,"kernel64.bin",0x20,ck,klen);
    if(efilen){ if_ent(&F,defi,0,".",0x10,defi,0); if_ent(&F,defi,1,"..",0x10,0,0); if_ent(&F,defi,2,"BOOT",0x10,defib,0);
        if_ent(&F,defib,0,".",0x10,defib,0); if_ent(&F,defib,1,"..",0x10,defi,0); if_ent(&F,defib,2,"BOOTX64.EFI",0x20,ce,efilen);
        int es=3; u32 dnv=if_mkdir(&F,defi,&es,"NoovexOS",0x10); int ns=2; u32 ce2=if_file(&F,efi,efilen), cc2=if_file(&F,cfg,cfglen);
        if(!F.err&&dnv){ if_add(&F,dnv,&ns,"BOOTX64.EFI",0x20,ce2,efilen); if_add(&F,dnv,&ns,"boot.cfg",0x20,cc2,cfglen); } }
    { u32 ilen=sizeof(IF_INFO)-1;
      u32 dsys=if_mkdir(&F,2,&rs,"NoovexOS",0x10); int ss=2;
      u32 dsy2=if_mkdir(&F,dsys,&ss,"System",0x10); { int q=2; if_mkdir(&F,dsy2,&q,"Drivers",0x10); if_mkdir(&F,dsy2,&q,"Config",0x10); u32 ci=if_file(&F,(const u8*)IF_INFO,ilen); if_add(&F,dsy2,&q,"version.txt",0x21,ci,ilen); }
      { u32 dbt=if_mkdir(&F,dsys,&ss,"Boot",0x10); int q=2; u32 cc3=if_file(&F,cfg,cfglen); if_add(&F,dbt,&q,"boot.cfg",0x20,cc3,cfglen); }
      if_mkdir(&F,dsys,&ss,"Fonts",0x10); if_mkdir(&F,dsys,&ss,"Logs",0x10); if_mkdir(&F,dsys,&ss,"Temp",0x10);
      if_mkdir(&F,2,&rs,"Program Files",0x10); if_mkdir(&F,2,&rs,"ProgramData",0x10);
      u32 du=if_mkdir(&F,2,&rs,"Users",0x10); int us=2; if_mkdir(&F,du,&us,"Public",0x10);
      u32 dd=if_mkdir(&F,du,&us,"Default",0x10); { int q=2; const char* sub[6]={"Desktop","Documents","Downloads","Pictures","Music","Videos"}; for(int k2=0;k2<6;k2++)if_mkdir(&F,dd,&q,sub[k2],0x10); }
      u32 cr=if_file(&F,(const u8*)IF_INFO,ilen); if_add(&F,2,&rs,"README.txt",0x21,cr,ilen); }
    if(F.err)return -1;
    { u32 tot=psec-F.dstart; *(u32*)(fsi+488)=tot>(F.next-2)?tot-(F.next-2):0; *(u32*)(fsi+492)=F.next; }
    for(u32 i=0;i<2*bps;i++)img[6*bps+i]=img[i];
    *used=F.dstart+(F.next-2); *ko1=if_off(&F,ck); *ko2=*ko1;
    return 0;
}

enum { IP_WELCOME, IP_TARGET, IP_CONFIRM, IP_RUN, IP_DONE, IP_ERROR, IP_FS };
static int g_ip_fs=0;
static int ip_page=IP_WELCOME, ip_mode=0, ip_part=-1, ip_copy=1, ip_ack=0, ip_ret=ST_BOOTMENU, ip_stage=-1, ip_prog=0;
static const char* ip_err=""; static const char* ip_err2="";
static u8 ip_mbr[512]; static int ip_ptype[4]; static u32 ip_pst[4], ip_psz[4]; static int ip_gpt=0;
static int ip_bx[24], ip_by[24], ip_bw[24], ip_bh[24], ip_bid[24], ip_bn=0;
static const char* IP_STAGES[8]={"Checking the kernel image","Preparing the boot partition","Writing the partition table","Installing the boot loader","Writing system files","Creating the data partition","Copying your files","Verifying the installation"};
static void ip_btnreg(int x,int y,int w,int h,int id){ if(ip_bn<24){ ip_bx[ip_bn]=x; ip_by[ip_bn]=y; ip_bw[ip_bn]=w; ip_bh[ip_bn]=h; ip_bid[ip_bn]=id; ip_bn++; } }
void ip_load(void){
    gdisk_ensure(); for(int i=0;i<512;i++)ip_mbr[i]=0; ip_gpt=0;
    if(g_dktype!=DK_NONE)gdisk_read(0,1,ip_mbr);
    int valid=(ip_mbr[510]==0x55&&ip_mbr[511]==0xAA);
    for(int e=0;e<4;e++){ u8* pe=ip_mbr+446+e*16; ip_ptype[e]=valid?pe[4]:0; ip_pst[e]=*(u32*)(pe+8)*dk_ratio(); ip_psz[e]=*(u32*)(pe+12)*dk_ratio();
        if(!ip_psz[e])ip_ptype[e]=0; if(ip_ptype[e]==0xEE)ip_gpt=1; }
}
static int g_ip_focus=2, g_ip_lastpg=-1;
void dm_open(int ret){ dm_load(); g_dm_open=1; g_dm_sel=-1; g_dm_editname=0; g_dm_msg=0; g_dm_ret=ret; g_state=ST_DISKMGR; }
void ip_open(int ret){ dm_load(); g_ip_lastpg=-1; g_ip_focus=2; ip_ret=ret; ip_page=IP_FS; g_ip_fs=0; ip_mode=0; ip_part=-1; ip_ack=0; ip_copy=1; ip_stage=-1; ip_prog=0; ip_load(); g_state=ST_INSTALL; }
static u32 ip_need(void){ return INST_BOOTP+INST_DATAP; }
static int ip_disk_ok(void){ return g_dktype!=DK_NONE&&g_dksectors>=2048u+ip_need()+2048u&&!(g_dktype==DK_NVME&&!g_nvme_bsz); }
static int ip_free_slot(int except){ for(int e=0;e<4;e++) if(e!=except&&!ip_ptype[e])return e; return -1; }
static int ip_part_ok(int e){ if(e<0||e>3||!ip_ptype[e]||ip_gpt)return 0; if(ip_psz[e]<ip_need())return 0; if(ip_pst[e]<64)return 0; if(ip_free_slot(e)<0)return 0;
    for(int q=0;q<4;q++) if(q!=e&&ip_ptype[q]&&ip_pst[q]<64)return 0; return 1; }
static const char* ip_tname(int t){ switch(t){ case 0x0B: case 0x0C: return "FAT32"; case 0x07: return "NTFS/exFAT"; case 0x83: return "Linux"; case 0x82: return "Linux swap"; case 0xEE: return "GPT"; case 0xEF: return "EFI"; case NVX_PTYPE: return "NoovexOS"; case 0x05: case 0x0F: return "Extended"; default: return "Other"; } }
static char* ip_size(char* p,u64 secs){ u64 mb=secs*512/1048576ULL; if(mb>=1024){ p=udec(p,(u32)(mb/1024)); *p++='.'; p=udec(p,(u32)((mb%1024)*10/1024)); return scopy(p," GB"); } p=udec(p,(u32)mb); return scopy(p," MB"); }
static u32 ip_pcol(int t){ if(t==0x0B||t==0x0C)return rgb(80,140,230); if(t==0x07)return rgb(90,170,220); if(t==0x83)return rgb(230,150,60); if(t==NVX_PTYPE)return rgb(110,190,120); if(t==0xEE)return rgb(150,120,200); return rgb(160,168,184); }
static void ip_bar(int x,int y,int w,int h,int preview){
    fill_round(x,y,w,h,8,15,rgb(226,230,238)); if(!g_dksectors)return; u64 tot=g_dksectors;
    if(preview){ u32 bs=2048u,bl=INST_BOOTP,ds=bs+bl,dl=INST_DATAP;
        if(ip_mode==1&&ip_part>=0){ bs=ip_pst[ip_part]; ds=bs+bl; dl=ip_psz[ip_part]-bl;
            for(int e=0;e<4;e++) if(ip_ptype[e]&&e!=ip_part){ int sx=x+(int)((u64)ip_pst[e]*w/tot), sw=(int)((u64)ip_psz[e]*w/tot); if(sw<3)sw=3; fill_round(sx,y,sw,h,4,15,ip_pcol(ip_ptype[e])); } }
        int sx=x+(int)((u64)bs*w/tot), sw=(int)((u64)bl*w/tot); if(sw<6)sw=6; fill_round(sx,y,sw,h,4,15,ACC);
        int dx=x+(int)((u64)ds*w/tot), dw=(int)((u64)dl*w/tot); if(dw<6)dw=6; if(dx<sx+sw)dx=sx+sw; fill_round(dx,y,dw,h,4,15,rgb(110,190,120)); return; }
    for(int e=0;e<4;e++) if(ip_ptype[e]){ int sx=x+(int)((u64)ip_pst[e]*w/tot), sw=(int)((u64)ip_psz[e]*w/tot); if(sw<4)sw=4; if(sx+sw>x+w)sw=x+w-sx;
        fill_round(sx,y,sw,h,4,15,(ip_mode==1&&ip_part==e)?ACC:ip_pcol(ip_ptype[e]));
}
}
static void ip_button(int x,int y,int w,const char* t,int id,int kind,int enabled){
    int hov=enabled&&in_rect(mx,my,x,y,w,40); u32 bg,fg;
    if(kind==1){ bg=enabled?(hov?ACC2:ACC):rgb(196,204,222); fg=rgb(255,255,255); }
    else if(kind==2){ bg=enabled?(hov?rgb(196,52,52):rgb(216,64,64)):rgb(226,190,190); fg=rgb(255,255,255); }
    else { bg=hov?rgb(226,230,238):rgb(238,241,246); fg=enabled?INK:rgb(170,176,190); }
    fill_round(x,y,w,40,10,15,bg); dstr_ctr(x+w/2,y+13,t,fg,1); if(enabled)ip_btnreg(x,y,w,40,id); }
static void ip_check(int x,int y,int on,const char* t,int id,u32 col){
    int hov=in_rect(mx,my,x,y,slen(t)*8+34,24); fill_round(x,y+2,20,20,5,15,on?ACC:(hov?rgb(214,220,232):rgb(226,230,238)));
    if(on){ for(int k=0;k<5;k++){ putpx(x+5+k,y+11+k,rgb(255,255,255)); putpx(x+5+k,y+12+k,rgb(255,255,255)); } for(int k=0;k<8;k++){ putpx(x+10+k,y+15-k,rgb(255,255,255)); putpx(x+10+k,y+16-k,rgb(255,255,255)); } }
    dstr(x+30,y+6,t,col,1); ip_btnreg(x,y,slen(t)*8+34,24,id); }

static int ip_btns(int* o){
    if(ip_page==IP_FS){ o[0]=2; return 1; }
    if(ip_page==IP_WELCOME||ip_page==IP_TARGET){ o[0]=2; o[1]=6; return 2; }
    if(ip_page==IP_CONFIRM){ o[0]=1; o[1]=2; return 2; }
    if(ip_page==IP_DONE){ o[0]=5; return 1; }
    if(ip_page==IP_ERROR){ o[0]=3; return 1; }
    return 0;
}
static void ip_focus_move(int dir){ int b[4]; int n=ip_btns(b); if(n<2)return; int k=0; for(int i=0;i<n;i++) if(b[i]==g_ip_focus)k=i; k=(k+dir+n)%n; g_ip_focus=b[k]; }
static void ip_cbtn(int cx,int by,const char* t,int id){ int w=slen(t)*8+32; int x=cx-w/2;
    int foc=(id==g_ip_focus), hov=in_rect(mx,my,x,by,w,20);
    if(foc){ fill_rect(x,by,w,20,hov?rgb(230,80,90):rgb(210,60,70)); }
    u32 c=foc?rgb(255,255,255):(hov?rgb(255,120,130):rgb(210,60,70));
    dstr(x+4,by+3,"<",c,1); dstr(x+16,by+3,t,c,1); dstr(x+16+slen(t)*8+4,by+3,">",c,1); ip_btnreg(x,by,w,20,id); }
void ip_box(int* ox,int* oy,int* ow,int bw,int bh,const char* title){
    int bx=((int)W-bw)/2, by=((int)H-bh)/2;
    fill_rect(bx+10,by+10,bw,bh,rgb(0,0,0));                 /* hard black shadow */
    fill_rect(bx,by,bw,bh,rgb(248,249,251));                /* white box */
    fill_rect(bx+3,by+3,bw-6,2,rgb(20,24,34)); fill_rect(bx+3,by+bh-5,bw-6,2,rgb(20,24,34));
    fill_rect(bx+3,by+3,2,bh-6,rgb(20,24,34)); fill_rect(bx+bw-5,by+3,2,bh-6,rgb(20,24,34));
    { int tw=slen(title)*8; int full=tw+32; int fx=bx+(bw-full)/2;   /* "[ " + title + " ]" */
      for(int gx=bx+10;gx<fx-4;gx+=8)dstr(gx,by+6,"-",rgb(20,24,34),1);
      for(int gx=fx+full+4;gx<bx+bw-10;gx+=8)dstr(gx,by+6,"-",rgb(20,24,34),1);
      dstr(fx,by+6,"[",rgb(20,24,34),1); dstr(fx+16,by+6,(char*)title,rgb(210,60,70),1); dstr(fx+16+tw,by+6,"]",rgb(20,24,34),1); }
    *ox=bx+18; *oy=by+30; *ow=bw-36;
}
int g_ip_curmoved=0;
static int ip_hover(void){ for(int b=ip_bn-1;b>=0;b--) if(in_rect(mx,my,ip_bx[b],ip_by[b],ip_bw[b],ip_bh[b])) return b; return -1; }
void ip_present(void){
    if(ip_page==IP_RUN){ static u64 lt=0; static int lp=-1; if(ip_prog/10==lp/10 && g_ticks-lt<10 && ip_prog<1000) return; lt=g_ticks; lp=ip_prog; }
    g_ip_curmoved=0;
    if(ip_page!=g_ip_lastpg){ g_ip_lastpg=ip_page; g_ip_focus=(ip_page==IP_DONE)?5:(ip_page==IP_ERROR)?3:2; }
    ip_bn=0;
    fill_rect(0,0,(int)W,(int)H,rgb(72,130,200));
    int ox,oy,ow;
    if(ip_page==IP_FS){
        ip_box(&ox,&oy,&ow,560,216,"Select File System");
        dstr(ox,oy,"Choose the file system for NoovexOS:",rgb(20,24,34),1); oy+=28;
        { const char* nm[4]={"NoovexFS","NTFS","FAT32","FAT16"}; const char* ds[4]={"native, recommended","also readable by Windows","readable almost everywhere","classic, partitions up to 2 GB"};
          for(int i=0;i<4;i++){ int sel=(g_ip_fs==i); fill_rect(ox,oy-2,ow,20,sel?rgb(20,24,34):rgb(248,249,251));
            char b[80],*p=b; p=scopy(p,sel?"(*) ":"( ) "); p=scopy(p,nm[i]); while(p-b<16)*p++=' '; p=scopy(p,ds[i]); *p=0;
            dstr(ox+8,oy+2,b,sel?rgb(255,255,255):rgb(20,24,34),1); ip_btnreg(ox,oy-2,ow,20,40+i); oy+=22; } }
        oy+=18;
        ip_cbtn(ox+ow/2, oy, "Continue", 2);
    }
    else if(ip_page==IP_WELCOME || ip_page==IP_TARGET){
        extern int gdisk_count(void); extern int gdisk_current(void); extern u32 gdisk_size_of(int); extern const char* gdisk_name_of(int);
        int nd=gdisk_count(); if(nd<1)nd=1; if(nd>6)nd=6;
        ip_box(&ox,&oy,&ow,660,172+(nd-1)*24,"Select Target Disk");
        dstr(ox,oy,"Choose the disk to install NoovexOS to (click it or use Up/Down):",rgb(20,24,34),1); oy+=18;
        dstr(ox,oy,"Or Skip to set up NoovexOS without installing.  Press P to edit partitions.",rgb(110,118,135),1); oy+=28;
        for(int i=0;i<nd;i++){ int sel=(i==gdisk_current()); int bx2=ox; int rww=ow;
          fill_rect(bx2,oy-2,rww,20, sel?rgb(20,24,34):rgb(238,240,244));
          char b[80],*p=b; p=scopy(p,sel?"(*) ":"( ) "); p=scopy(p,gdisk_count()?(char*)gdisk_name_of(i):"/dev/hda"); p=scopy(p,"   -   "); u32 sz=gdisk_count()?gdisk_size_of(i):0; if(sz)p=ip_size(p,sz); else p=scopy(p,"no disk found"); *p=0;
          dstr(bx2+8,oy+2,b, sel?rgb(255,255,255):rgb(20,24,34),1);
          ip_btnreg(bx2,oy-2,rww,20,60+i); oy+=24; }
        if(g_dm_np>0){ char b[200],*p=b; int any=0; p=scopy(p,"Partitions: ");
            for(int i=0;i<g_dm_np&&i<4;i++){ if(!g_dm_p[i].used)continue; if(any)p=scopy(p,", "); any=1; p=scopy(p,g_dm_p[i].name); p=scopy(p," "); p=dm_size(p,g_dm_p[i].size); if(g_dm_p[i].fs[0]){ p=scopy(p," "); int k=0; while(g_dm_p[i].fs[k]&&k<8){ *p++=g_dm_p[i].fs[k]; k++; } } }
            if(!any)p=scopy(p,"none (empty disk)"); *p=0; dstr(ox,oy,b,rgb(70,78,95),1); }
        if(!gdisk_count()){ extern const char* g_vmd_note; extern u32 g_vmd_dev; char b[160],*p=b; if(g_vmd_dev){ p=scopy(p,g_vmd_note); if(nvme_found&&!nvme_ok){ p=scopy(p," - NVMe start failed at stage "); p=udec(p,(u32)nvme_failstage); } } else p=scopy(p,"No disk found. Run disks in the terminal and send a photo."); *p=0; dstr(ox,oy,b,rgb(190,60,50),1); }
        oy+=16;
        ip_cbtn(ox+ow/3, oy, "Continue", 2); ip_cbtn(ox+ow*2/3, oy, "Skip", 6);
    }
    else if(ip_page==IP_CONFIRM){
        ip_box(&ox,&oy,&ow,560,182,"Confirm Installation");
        dstr(ox,oy,"NoovexOS will be installed to:",rgb(20,24,34),1); oy+=24;
        { char b[80],*p=b; p=scopy(p,"  "); p=scopy(p,gdisk_name()[0]?gdisk_name():"/dev/hda"); *p=0; dstr(ox,oy,b,rgb(20,24,34),1); } oy+=22;
        { char b[80],*p=b; p=scopy(p,"File system: "); { const char* fsn[4]={"NoovexFS","NTFS","FAT32","FAT16"}; p=scopy(p,fsn[g_ip_fs&3]); } *p=0; dstr(ox,oy,b,rgb(20,24,34),1); } oy+=24;
        dstr(ox,oy,"All data on this disk will be erased.",rgb(190,40,50),1); oy+=36;
        ip_cbtn(ox+ow/3, oy, "Back", 1); ip_cbtn(ox+ow*2/3, oy, "Install", 2);
    }
    else if(ip_page==IP_RUN){
        ip_box(&ox,&oy,&ow,560,130,"Installing");
        { const char* st[8]={"Preparing disk","Partitioning","Formatting","Copying system","Writing bootloader","Configuring","Syncing","Finishing"}; int si=ip_stage; if(si<0)si=0; if(si>7)si=7;
          dstr(ox,oy,(char*)st[si],rgb(20,24,34),1); } oy+=28;
        int bw2=ow; fill_rect(ox,oy,bw2,18,rgb(220,222,228)); int fw=bw2*ip_prog/1000; fill_rect(ox,oy,fw,18,rgb(40,120,80));
        for(int t=0;t<1;t++){ fill_rect(ox,oy,bw2,1,rgb(20,24,34)); fill_rect(ox,oy+17,bw2,1,rgb(20,24,34)); fill_rect(ox,oy,1,18,rgb(20,24,34)); fill_rect(ox+bw2-1,oy,1,18,rgb(20,24,34)); }
        { char b[8],*p=b; p=udec(p,(u32)(ip_prog/10)); *p++='%'; *p=0; dstr_ctr(ox+bw2/2,oy+3,b,rgb(255,255,255),1); }
    }
    else if(ip_page==IP_DONE){
        ip_box(&ox,&oy,&ow,520,130,"Installation Complete");
        dstr(ox,oy,"NoovexOS has been installed successfully.",rgb(20,24,34),1); oy+=24;
        dstr(ox,oy,"Remove the install media and reboot.",rgb(90,100,120),1); oy+=36;
        ip_cbtn(ox+ow/2, oy, "Reboot", 5);
    }
    else { /* IP_ERROR */
        { int bw=560, l1=ip_err?slen(ip_err):0, l2=ip_err2?slen(ip_err2):0; int lm=l1>l2?l1:l2; if(lm*8+60>bw)bw=lm*8+60; if(bw>(int)W-20)bw=(int)W-20; ip_box(&ox,&oy,&ow,bw,150,"Error"); }
        dstr(ox,oy,ip_err?ip_err:"Installation failed.",rgb(190,40,50),1); oy+=24;
        if(ip_err2)dstr(ox,oy,ip_err2,rgb(90,100,120),1); oy+=36;
        ip_cbtn(ox+ow/2, oy, "Back", 3);
    }
    memcpy(SCENE_NC,SCENE,(u64)W*H*4); draw_cursor(); blit_all();
    nc_restore(mx-CURBB/2,my-CURBB/2,CURBB,CURBB); cxlast=mx; cylast=my;
}
static void ip_fail(const char* a,const char* b){ ip_err=a; ip_err2=b; ip_page=IP_ERROR; ip_present(); }
static void ip_stage_go(int s,int prog){ ip_stage=s; ip_prog=prog; ip_present(); }
static void ip_nk_prog(int done,int total){ ip_stage=6; ip_prog=760+done*100/(total?total:1); ip_present(); }
static void ip_run2(void);
static u64 g_ip_t0=0;
static void ip_run(void){ extern volatile int g_frz_hold; g_frz_hold=1; ip_run2(); g_frz_hold=0; }
static void ip_run2(void){
    ip_page=IP_RUN; g_ip_t0=g_ticks; ip_stage_go(0,0);
    u32 klen=*(volatile u32*)(u64)(0x100000ULL+24); if(klen<4096||klen>KSNAP_MAX){ ip_fail("The kernel size stamp is invalid.",""); return; }
    if(!g_kimg_len||klen!=g_kimg_len){ ip_fail("The kernel image could not be captured at boot.","Run kcheck in the terminal and send a photo of the result."); return; }
    if(!kimg_full()){ ip_fail("Not enough memory to prepare the kernel image.",""); return; }
    { int io=-1,fo=-1; int kr=kimg_check(g_kimg_snap,g_kimg_len,&io,&fo);
      if(kr<0||(kr==0&&(!io||!fo))){ ip_fail(kr<0?"The kernel image could not be captured at boot.":!io?"The kernel code loaded at boot is not the original build.":"The kernel data loaded at boot is not the original build.","Run kcheck in the terminal and send a photo of the result."); return; } }
    if(mm_area_conflicts()){ ip_fail("The firmware uses memory that NoovexOS also uses.","Run memmap in the terminal and send a photo of the result."); return; }
    const u8* kimg=g_kimg_snap;
    { extern int disk_ok; extern volatile int g_fs_wb_pending; disk_ok=0; g_fs_wb_pending=0; }
    u32 bst,bsz=INST_BOOTP,dst,dsz; int bslot,dslot;
    if(ip_mode==0){ bst=2048u; dst=bst+bsz; dsz=INST_DATAP; bslot=0; dslot=1;
        if(g_dksectors>dst+INST_DATAP+4096u){ u32 avail=g_dksectors-dst-4096u; u32 cap=g_ip_fs==0?0x8000000u:g_ip_fs==3?4186112u:g_ip_fs==2?0x4000000u:0xFFFFF000u; dsz=avail<cap?avail:cap; } }
    else { if(!ip_part_ok(ip_part)){ ip_fail("The chosen partition can't be used.",""); return; }
        bst=ip_pst[ip_part]; dst=bst+bsz; dsz=ip_psz[ip_part]-bsz; bslot=ip_part; dslot=ip_free_slot(ip_part); }
    if(dsz%dk_ratio())dsz-=dsz%dk_ratio();
    if((u64)dst+dsz>g_dksectors){ char* p=g_ip_e2; p=scopy(p,gdisk_name()); p=scopy(p," disk: "); p=udec(p,(u32)((u64)g_dksectors*512/1048576ULL)); p=scopy(p," MB, needed: "); p=udec(p,(u32)(((u64)dst+dsz)*512/1048576ULL)); p=scopy(p," MB. Use Next disk in the Disk Manager (press P) if this is the wrong disk."); *p=0; ip_fail("The disk is too small.",g_ip_e2); return; }
    if(g_ip_fs==3&&dsz>4186112u){ dsz=4186112u; }
    if(g_ip_fs==3&&dsz<8400u){ ip_fail("The partition is too small for FAT16.","FAT16 needs at least 4 MB."); return; }
    if(g_ip_fs==1&&dsz<32768u){ ip_fail("The partition is too small for NTFS.","NTFS needs at least 16 MB - choose FAT16 instead."); return; }
    if(g_ip_fs==2&&dsz<66600u){ ip_fail("The partition is too small for FAT32.","FAT32 needs at least 33 MB - choose FAT16 instead."); return; }
    ip_stage_go(1,40);
    u32 R=dk_ratio(), bps=512*R, fz=0, dso=0, cap=inst_fs_layout(bps,bsz/R,klen,&fz,&dso);
    if(cap>bsz/R){ ip_fail("The kernel does not fit in the boot partition.",""); return; }
    u8* img=(u8*)kmalloc((u64)cap*bps); if(!img){ ip_fail("Not enough memory to build the boot partition.",""); return; }
    u32 usedn=0; u64 ko1=0,ko2=0; int br=inst_build_fs(img,bps,bst/R,bsz/R,cap,g_dm_name,kimg,klen,&usedn,&ko1,&ko2);
    if(br){ kfree(img); ip_fail("The boot partition image could not be built.",br==-2?"The partition is too small for FAT32 on this disk.":""); return; }
    if(ip_mem_verify(img+ko1,kimg,klen)||ip_mem_verify(img+ko2,kimg,klen)){ kfree(img); ip_fail(g_ip_e1,g_ip_e2); return; }
    u32 used=usedn*R;
    ip_stage_go(2,90);
    u8 mbr[512]; for(int i=0;i<512;i++)mbr[i]=ip_mode==0?0:ip_mbr[i];
    int s1=(int)(_binary_inst_boot_bin_end-_binary_inst_boot_bin_start); for(int i=0;i<446;i++)mbr[i]=i<s1?_binary_inst_boot_bin_start[i]:0;
    for(int e=0;e<4;e++)mbr[446+e*16]=0;
    { u8* pe=mbr+446+bslot*16; for(int i=0;i<16;i++)pe[i]=0; pe[0]=0x80; pe[2]=2; pe[4]=0x0C; pe[5]=0xFE; pe[6]=0xFF; pe[7]=0xFF; *(u32*)(pe+8)=bst/R; *(u32*)(pe+12)=bsz/R; }
    { u8* pe=mbr+446+dslot*16; for(int i=0;i<16;i++)pe[i]=0; pe[2]=2; pe[4]=g_ip_fs==1?0x07:g_ip_fs==2?0x0C:g_ip_fs==3?0x0E:NVX_PTYPE; pe[5]=0xFE; pe[6]=0xFF; pe[7]=0xFF; *(u32*)(pe+8)=dst/R; *(u32*)(pe+12)=dsz/R; }
    if(ip_mode==0){ *(u32*)(mbr+440)=0x4E565849u^(u32)g_ticks; } mbr[510]=0x55; mbr[511]=0xAA;
    if(gdisk_write(0,mbr)){ kfree(img); ip_fail("Writing the partition table failed.","The disk reported an error."); return; }
    if(ip_mode==0){ u8 z[512]; for(int i=0;i<512;i++)z[i]=0; for(u32 q=1;q<34u*R;q++)gdisk_write(q,z); if(g_dksectors>34u*R) for(u32 q=g_dksectors-34u*R;q<g_dksectors;q++)gdisk_write(q,z); }
    ip_stage_go(3,110);
    u32 s2len=(u32)(_binary_inst_stage2_bin_end-_binary_inst_stage2_bin_start), s2s=(s2len+511)/512;
    if(R>1)s2s=0;
    for(u32 s=0;s<s2s;s++){ u8 sec[512]; for(int i=0;i<512;i++){ u32 o=s*512+i; sec[i]=o<s2len?_binary_inst_stage2_bin_start[o]:0; } if(gdisk_write(1+s,sec)){ kfree(img); ip_fail("Writing the boot loader failed.",""); return; } }
    ip_stage_go(4,140);
    { extern int gdisk_writen(u32,int,const u8*);
      for(u32 s=0;s<used;){ u32 c=used-s; if(c>128)c=128; if(gdisk_writen(bst+s,(int)c,img+(u64)s*512)){ kfree(img); ip_fail("Writing the boot partition failed.","The disk reported an error."); return; }
        s+=c; ip_prog=140+(int)((u64)s*560/used); ip_present(); } }
    ip_stage_go(5,700);
    { char mk[96],*q=mk; q=scopy(q,"installed="); q=scopy(q,(char*)g_buildsig.version); q=scopy(q,"\n"); *q=0; nvx_mark_file("/etc/nvx-installed",mk); k_unlink("/etc/nvx-setup-done"); }
    if(g_ip_fs){
        const rfs_ops* fo=g_ip_fs==1?&g_rfs_ntfs:g_ip_fs==2?&g_rfs_fat32:&g_rfs_fat16;
        int nr=nk_install(fo,(unsigned long long)dst,(unsigned long long)dsz,ip_copy,ip_nk_prog);
        if(nr){ kfree(img); ip_fail(nr==-1?"Formatting the partition failed.":"Writing files to the new partition failed.",fo->name); return; }
        g_nvx_note=g_ip_fs==1?"NoovexOS NTFS partition":g_ip_fs==2?"NoovexOS FAT32 partition":"NoovexOS FAT16 partition";
    } else {
    g_nvx_generic=1; g_nvx_base=dst; g_nvx_secs=dsz; g_rootfs=0; g_n4_nres=0; disk_ok=1;
    ip_stage_go(6,760);
    if(n4_format(dsz)){ disk_ok=0; kfree(img); ip_fail("Formatting the data partition failed.",g_n4_err); return; }
    g_nvx_note="NoovexOS data partition";
    }
    ip_stage_go(7,870);
    { u8 rb[512]; int bad=0;
      if(gdisk_read(0,1,rb)){ bad=1; } else for(int i=0;i<512;i++) if(rb[i]!=mbr[i]){ bad=1; break; }
      for(u32 s=0;s<s2s&&!bad;s++){ if(gdisk_read(1+s,1,rb)){ bad=1; break; } for(int i=0;i<512;i++){ u32 o=s*512+i; if(rb[i]!=(o<s2len?_binary_inst_stage2_bin_start[o]:0)){ bad=1; break; } } }
      u32 bads=0;
      { extern int gdisk_readn(u32,int,u8*); u8* vb=(u8*)kmalloc(128*512);
        for(u32 s=0;s<used&&!bad;){ u32 c=used-s; if(c>128)c=128;
          if(!vb){ if(gdisk_read(bst+s,1,rb)){ bad=2; bads=s; break; } const u8* w=img+(u64)s*512; for(int i=0;i<512;i++) if(rb[i]!=w[i]){ bad=1; bads=s; break; } s++; continue; }
          if(gdisk_readn(bst+s,(int)c,vb)){ bad=2; bads=s; break; }
          const u8* w=img+(u64)s*512; for(u32 i=0;i<c*512u;i++) if(vb[i]!=w[i]){ bad=1; bads=s+i/512u; break; }
          s+=c; ip_prog=870+(int)((u64)s*130/used); ip_present(); }
        if(vb)kfree(vb); }
      if(!bad&&(ip_mem_verify(img+ko1,kimg,klen)||ip_mem_verify(img+ko2,kimg,klen))){ kfree(img); ip_fail(g_ip_e1,g_ip_e2); return; }
      kfree(img);
      if(gdisk_flush()&&!bad){ ip_fail("The disk did not confirm that the data was saved.","Try again, or use another disk."); return; }
      if(!bad&&sysroot_base_ok()&&upd_put_sysroot_part((unsigned long long)(bst/R),(unsigned long long)(bsz/R))){ ip_fail("Writing the Linux files to the boot partition failed.","The disk reported an error."); return; }
      if(bad){ char* p=g_ip_e2; p=scopy(p,bad==2?"Reading sector ":"Sector "); p=udec(p,bst+bads); p=scopy(p,bad==2?" failed (":" differs ("); p=scopy(p,gdisk_name()); if(g_dktype==DK_NVME&&nvme_lasterr){ p=scopy(p,nvme_lasterr==2?", controller fault":", timeout"); } *p++=')'; *p=0;
          ip_fail("Verification failed - the disk returned different data.",g_ip_e2); return; } }
    { char mk[96],*q=mk; q=scopy(q,"installed="); q=scopy(q,(char*)g_buildsig.version); q=scopy(q,"\n"); *q=0; nvx_mark_file("/etc/nvx-installed",mk); }
    { extern u32 g_nvme_cmds, g_nvme_multi; char lb[120],*p=lb; p=scopy(p,"install: done in "); p=udec(p,(u32)((g_ticks-g_ip_t0)*10)); p=scopy(p," ms, "); p=udec(p,used); p=scopy(p," boot sectors, disk "); p=scopy(p,gdisk_name()); p=scopy(p,", nvme cmds "); p=udec(p,g_nvme_cmds); p=scopy(p," multi "); p=udec(p,g_nvme_multi); *p=0; klog(lb); }
    ip_prog=1000; ip_stage=8; ip_page=IP_DONE; ip_load(); ip_present();
}
