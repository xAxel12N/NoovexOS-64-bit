/* ================= Disk Manager (graphical partition editor + installer) ================= */
static int g_dm_open=0, g_dm_sel=-1, g_dm_editname=0, g_dm_ret=ST_BOOTMENU; static void ip_open_preset(int ret,int mode,int part); static char g_dm_name[16]="NOOVEXOS";
static u8 g_dm_mbr[512]; static int g_dm_msg=0; static const char* g_dm_msgtext="";
static int dm_rowy[4], dm_bx[6], dm_by[6], dm_bw[6];   /* partition rows + buttons */
typedef struct { int used; u8 type; u32 start, size; } dmpart;
static dmpart g_dm_p[4];
static void dm_load(void){
    gdisk_ensure();
    for(int i=0;i<512;i++)g_dm_mbr[i]=0;
    if(g_dktype!=DK_NONE) gdisk_read(0,1,g_dm_mbr);
    int valid=(g_dm_mbr[510]==0x55&&g_dm_mbr[511]==0xAA);
    for(int e=0;e<4;e++){ u8* pe=g_dm_mbr+446+e*16; u8 ty=valid?pe[4]:0; u32 st=*(u32*)(pe+8)*dk_ratio(), sz=*(u32*)(pe+12)*dk_ratio();
        g_dm_p[e].used=(valid&&ty!=0&&sz!=0); g_dm_p[e].type=ty; g_dm_p[e].start=st; g_dm_p[e].size=sz; }
}
static u32 dm_free_start(void){    /* first free LBA after existing partitions (aligned to 2048) */
    u32 top=2048; for(int e=0;e<4;e++) if(g_dm_p[e].used){ u32 end=g_dm_p[e].start+g_dm_p[e].size; if(end>top)top=end; }
    return (top+2047u)&~2047u;
}
static void dm_create(void){    /* create a partition in the first empty slot, filling free space */
    int slot=-1; for(int e=0;e<4;e++) if(!g_dm_p[e].used){ slot=e; break; } if(slot<0){ g_dm_msg=1; g_dm_msgtext="No free partition slots (max 4)."; return; }
    u32 st=dm_free_start(); if(st>=g_dksectors){ g_dm_msg=1; g_dm_msgtext="No free space on the disk."; return; }
    u32 sz=g_dksectors-st; sz-=sz%dk_ratio(); g_dm_p[slot].used=1; g_dm_p[slot].type=0x0C; g_dm_p[slot].start=st; g_dm_p[slot].size=sz;
    u8* pe=g_dm_mbr+446+slot*16; for(int i=0;i<16;i++)pe[i]=0; pe[4]=0x0C; pe[5]=0xFE;pe[6]=0xFF;pe[7]=0xFF; *(u32*)(pe+8)=st/dk_ratio(); *(u32*)(pe+12)=sz/dk_ratio();
    g_dm_mbr[510]=0x55; g_dm_mbr[511]=0xAA; gdisk_write(0,g_dm_mbr); dm_load(); g_dm_sel=slot;
    g_dm_msg=1; g_dm_msgtext="Partition created.";
}
static void dm_delete(void){
    if(g_dm_sel<0||g_dm_sel>3||!g_dm_p[g_dm_sel].used){ g_dm_msg=1; g_dm_msgtext="Select a partition to delete."; return; }
    u8* pe=g_dm_mbr+446+g_dm_sel*16; for(int i=0;i<16;i++)pe[i]=0; g_dm_mbr[510]=0x55; g_dm_mbr[511]=0xAA;
    gdisk_write(0,g_dm_mbr); dm_load(); g_dm_msg=1; g_dm_msgtext="Partition deleted."; g_dm_sel=-1;
}
static void dm_present(void){
    for(u32 yy=0;yy<H;yy++)for(u32 xx=0;xx<W;xx++)SCENE[(u64)yy*SW+xx]=rgb(18,26,44);
    dstr((int)W*8/100,(int)H*7/100,"Disk Manager",rgb(240,246,252),3);
    char b[110],*p;
    p=b;p=scopy(p,"Disk: ");p=scopy(p,(char*)gdisk_name()); if(g_dksectors){u64 mb=(u64)g_dksectors*512/1048576ULL;p=scopy(p,"   "); if(mb>=1024){p=udec(p,(u32)(mb/1024));*p++='.';p=udec(p,(u32)((mb%1024)*10/1024));p=scopy(p," GB");}else{p=udec(p,(u32)mb);p=scopy(p," MB");}}else p=scopy(p,"  (none detected)"); *p=0;
    dstr((int)W*8/100,(int)H*7/100+40,b,rgb(150,175,215),1);
    int lx=(int)W*8/100, ly=(int)H*20/100, lw=(int)W*84/100;
    dstr(lx,ly-24,"Partitions:",rgb(150,175,215),1);
    for(int e=0;e<4;e++){ int ry=ly+e*54; dm_rowy[e]=ry; int selr=(g_dm_sel==e);
        int hov=(mx>=lx&&mx<lx+lw&&my>=ry&&my<ry+46)||selr;
        fill_round(lx,ry,lw,46,8,15, selr?rgb(40,90,150):hov?rgb(30,44,72):rgb(26,36,58));
        p=b;p=scopy(p,"Partition ");p=udec(p,(u32)(e+1));p=scopy(p,":  ");
        if(g_dm_p[e].used){
            char vol[13]=""; if((g_dm_p[e].type==0x0C||g_dm_p[e].type==0x0B)&&g_dktype!=DK_NONE){ u8 bs2[512];
                if(gdisk_read(g_dm_p[e].start,1,bs2)==0){ for(int k=0;k<11;k++)vol[k]=(char)bs2[71+k]; vol[11]=0; int z=10; while(z>=0&&(vol[z]==' ')){vol[z]=0;z--;} } }
            p=scopy(p,g_dm_p[e].type==0x0C||g_dm_p[e].type==0x0B?"FAT32":g_dm_p[e].type==0x07?"NTFS":g_dm_p[e].type==0x83?"Linux":"used");
            if(vol[0]){ p=scopy(p,"  \""); p=scopy(p,vol); *p++='"'; }
            p=scopy(p,"   "); u64 mb=(u64)g_dm_p[e].size*512/1048576ULL;
            if(mb>=1024){p=udec(p,(u32)(mb/1024));*p++='.';p=udec(p,(u32)((mb%1024)*10/1024));p=scopy(p," GB");}else{p=udec(p,(u32)mb);p=scopy(p," MB");}
            p=scopy(p,"   @ LBA ");p=udec(p,g_dm_p[e].start); if(g_dm_mbr[446+e*16]&0x80)p=scopy(p,"  [active]"); }
        else p=scopy(p,"(empty)");
        *p=0; dstr(lx+16,ry+15,b, g_dm_p[e].used?rgb(232,240,252):rgb(120,132,152),1);
    }
    /* volume name field */
    int ny=ly+4*54+16; dstr(lx,ny,"Volume name:",rgb(150,175,215),1);
    int nfx=lx+130, nfw=260; fill_round(nfx,ny-6,nfw,28,6,15, g_dm_editname?rgb(255,255,255):rgb(40,52,80));
    p=b;p=scopy(p,g_dm_name); if(g_dm_editname&&((g_ticks/50)&1))*p++='_'; *p=0; dstr(nfx+10,ny,b, g_dm_editname?rgb(20,28,44):rgb(232,240,252),1);
    dm_bx[5]=nfx;dm_by[5]=ny-6;dm_bw[5]=nfw;   /* name field click */
    /* buttons */
    const char* bl[5]={"Create partition","Delete partition","Install to selected","Install whole disk","Back"};
    u32 bcol[5]={rgb(60,120,80),rgb(150,70,70),rgb(50,110,190),rgb(150,110,50),rgb(60,70,92)};
    int byb=ny+50, bxs=lx;
    for(int k=0;k<5;k++){ int bw2=(k==2||k==3)?210:(k==4?110:170); int hov=(mx>=bxs&&mx<bxs+bw2&&my>=byb&&my<byb+38);
        fill_round(bxs,byb,bw2,38,8,15, hov?mix(bcol[k],rgb(255,255,255),50):bcol[k]); dstr_ctr(bxs+bw2/2,byb+11,(char*)bl[k],rgb(255,255,255),1);
        dm_bx[k]=bxs; dm_by[k]=byb; dm_bw[k]=bw2; bxs+=bw2+14; if(bxs>lx+lw-120){ bxs=lx; byb+=50; } }
    if(g_dm_msg){ int pw=460,ph=120,px=(int)W/2-pw/2,py=(int)H/2-ph/2; shadow_soft(px,py,pw,ph,14); fill_round(px,py,pw,ph,14,15,rgb(250,251,254));
        dstr_ctr((int)W/2,py+34,(char*)g_dm_msgtext,rgb(30,36,54),1); int okw=120,okx=(int)W/2-okw/2,oky=py+ph-46;
        fill_round(okx,oky,okw,32,8,15,ACC); dstr_ctr(okx+okw/2,oky+9,"OK",rgb(255,255,255),1); dm_bx[4]=okx;dm_by[4]=oky;dm_bw[4]=okw; }
    draw_cursor(); blit_all();
}
static void dm_do_install(int whole){
    u32 lba,sz; int idx;
    if(whole){ lba=2048; sz=(g_dksectors>2048)?(g_dksectors-2048):0; idx=0; }
    else { if(g_dm_sel<0||!g_dm_p[g_dm_sel].used){ g_dm_msg=1; g_dm_msgtext="Select a partition first."; return; } lba=g_dm_p[g_dm_sel].start; sz=g_dm_p[g_dm_sel].size; idx=g_dm_sel; }
    (void)lba; (void)sz;
    ip_open_preset(g_dm_ret==ST_INSTALL?ST_BOOTMENU:g_dm_ret,whole?0:1,idx);
}
static int dm_input(void){
    int changed=0;
    if(mouse_evt){ int dx,dy,btn; __asm__ volatile("cli"); dx=mouse_dx;dy=mouse_dy;mouse_dx=0;mouse_dy=0;btn=mouse_btn;mouse_evt=0; __asm__ volatile("sti");
        if(dx||dy){ mx+=dx;my-=dy; if(mx<0)mx=0;if(my<0)my=0;if(mx>=(int)W)mx=W-1;if(my>=(int)H)my=H-1; changed=1; }
        if(btn&&!mouse_btn_prev){ changed=1;
            if(g_dm_msg){ if(in_rect(mx,my,dm_bx[4],dm_by[4],dm_bw[4],32)) g_dm_msg=0; }
            else { g_dm_editname=0;
                for(int e=0;e<4;e++) if(in_rect(mx,my,(int)W*8/100,dm_rowy[e],(int)W*84/100,46)){ g_dm_sel=e; }
                if(in_rect(mx,my,dm_bx[5],dm_by[5],dm_bw[5],28)) g_dm_editname=1;
                else if(in_rect(mx,my,dm_bx[0],dm_by[0],dm_bw[0],38)) dm_create();
                else if(in_rect(mx,my,dm_bx[1],dm_by[1],dm_bw[1],38)) dm_delete();
                else if(in_rect(mx,my,dm_bx[2],dm_by[2],dm_bw[2],38)) dm_do_install(0);
                else if(in_rect(mx,my,dm_bx[3],dm_by[3],dm_bw[3],38)) dm_do_install(1);
                else if(in_rect(mx,my,dm_bx[4],dm_by[4],dm_bw[4],38)){ g_dm_open=0; if(g_dm_ret==ST_INSTALL){ ip_load(); g_state=ST_INSTALL; } else g_state=g_dm_ret; }
            } }
        mouse_btn_prev=btn;
    }
    int c; while((c=kpop())>=0){ changed=1;
        if(g_dm_msg){ g_dm_msg=0; continue; }
        if(g_dm_editname){ int L=0; while(g_dm_name[L])L++;
            if(c=='\n'){ g_dm_editname=0; } else if(c=='\b'){ if(L>0)g_dm_name[L-1]=0; }
            else if(L<11 && ((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-')){ g_dm_name[L]=(char)c; g_dm_name[L+1]=0; }
            continue; }
        if(c==27){ g_dm_open=0; if(g_dm_ret==ST_INSTALL){ ip_load(); g_state=ST_INSTALL; } else g_state=g_dm_ret; }
    }
    return changed;
}
extern const unsigned char _binary_inst_efi_bin_start[], _binary_inst_efi_bin_end[];
#define INST_BOOTP (dk_ratio()>1?557056u:131072u)
#define INST_DATAP (((NVX_REGION_SECS)+2047u)&~2047u)
static u8 g_inst_datasnap[0x60000]; static u32 g_inst_datalen=0;
static void inst_snapshot_data(void){
    extern char __immutable_end[], __file_end[];
    u64 a=(u64)__immutable_end, b=(u64)__file_end; g_inst_datalen=0;
    if(b<=a||b-a>sizeof(g_inst_datasnap))return;
    for(u64 i=0;i<b-a;i++)g_inst_datasnap[i]=((const u8*)a)[i];
    g_inst_datalen=(u32)(b-a);
}
static int inst_kernel_image(u8* out,u32 klen){
    extern char __immutable_end[];
    u32 imm=(u32)((u64)__immutable_end-0x100000ULL); if(imm>klen)return -1;
    const u8* k=(const u8*)0x100000ULL; for(u32 i=0;i<imm;i++)out[i]=k[i];
    u32 rest=klen-imm; if(rest>g_inst_datalen)return -1;
    for(u32 i=0;i<rest;i++)out[imm+i]=g_inst_datasnap[i];
    u32 so=(u32)((u64)&g_buildsig-0x100000ULL), ho=so+32, fo=so+88;
    int zero=1; for(int i=0;i<32;i++) if(g_buildsig.sha256_full[i])zero=0; if(zero)return 1;
    u8 sh[32],sf[32]; for(int i=0;i<32;i++){ sh[i]=out[ho+i]; sf[i]=out[fo+i]; out[ho+i]=0; out[fo+i]=0; }
    u8 d[32]; sha256_hash(out,(int)klen,d);
    for(int i=0;i<32;i++){ out[ho+i]=sh[i]; out[fo+i]=sf[i]; }
    for(int i=0;i<32;i++) if(d[i]!=g_buildsig.sha256_full[i])return 2;
    return 0;
}
typedef struct { u8* img; u32 bps,res,nf,fatsz,dstart,psec,next; int err; } ifat;
static u64 if_off(ifat* f,u32 c){ return (u64)(f->dstart+(c-2))*f->bps; }
static void if_set(ifat* f,u32 c,u32 v){ for(u32 i=0;i<f->nf;i++) *(u32*)(f->img+(u64)(f->res+i*f->fatsz)*f->bps+(u64)c*4)=v&0x0FFFFFFFu; }
static u32 if_alloc(ifat* f,u32 n){ if(!n)n=1; u32 first=f->next;
    if((u64)(f->dstart+(f->next-2)+n)>f->psec){ f->err=1; return 0; }
    for(u32 i=0;i<n;i++){ u32 c=f->next++; if_set(f,c,(i==n-1)?0x0FFFFFFFu:(c+1)); } return first; }
static u32 if_file(ifat* f,const u8* d,u32 len){ u32 c0=if_alloc(f,(len+f->bps-1)/f->bps); if(f->err)return 0; u8* p=f->img+if_off(f,c0); for(u32 i=0;i<len;i++)p[i]=d[i]; return c0; }
static void if_ent(ifat* f,u32 dirc,int slot,const char* nm,u8 attr,u32 clus,u32 size){
    u8* e=f->img+if_off(f,dirc)+(u64)slot*32; u8 n[11];
    if(nm[0]=='.'&&(nm[1]==0||(nm[1]=='.'&&nm[2]==0))){ for(int i=0;i<11;i++)n[i]=' '; n[0]='.'; if(nm[1])n[1]='.'; }
    else if(attr==0x08){ for(int i=0;i<11;i++){ char c=nm[i]; if(!c){ for(;i<11;i++)n[i]=' '; break; } if(c>='a'&&c<='z')c-=32; n[i]=(u8)c; } }
    else ins83(nm,n);
    for(int i=0;i<11;i++)e[i]=n[i]; e[11]=attr; *(u16*)(e+20)=(u16)(clus>>16); *(u16*)(e+26)=(u16)(clus&0xFFFF); *(u32*)(e+28)=size;
    *(u16*)(e+14)=0x6000; *(u16*)(e+16)=0x5B3B; *(u16*)(e+22)=0x6000; *(u16*)(e+24)=0x5B3B; *(u16*)(e+18)=0x5B3B; }
static u32 inst_fs_layout(u32 bps,u32 psec,u32 klen,u32* fatsz,u32* dstart){
    u32 res=32, clus=psec-res; u32 fs=((clus+2)*4+bps-1)/bps; u32 ds=res+2*fs;
    u32 cfglen=(u32)(_binary_inst_boot_cfg_end-_binary_inst_boot_cfg_start), efilen=(u32)(_binary_inst_efi_bin_end-_binary_inst_efi_bin_start);
    u32 need=3+2*((klen+bps-1)/bps)+(cfglen+bps-1)/bps+(efilen+bps-1)/bps+2;
    *fatsz=fs; *dstart=ds; return ds+need; }
static int inst_build_fs(u8* img,u32 bps,u32 plba,u32 psec,u32 cap,const char* label,const u8* k,u32 klen,u32* used){
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
    u32 dboot=if_alloc(&F,1), defi=if_alloc(&F,1), defib=if_alloc(&F,1);
    u32 ck=if_file(&F,k,klen), ck2=if_file(&F,k,klen), cc=if_file(&F,cfg,cfglen), ce=efilen?if_file(&F,efi,efilen):0;
    if(F.err)return -1;
    char vl[12]; { int i=0; for(;i<11&&label&&label[i];i++)vl[i]=label[i]; vl[i]=0; if(!i){ vl[0]='N';vl[1]='O';vl[2]='O';vl[3]='V';vl[4]='E';vl[5]='X';vl[6]=0; } }
    if_ent(&F,2,0,vl,0x08,0,0);
    if_ent(&F,2,1,"boot",0x10,dboot,0); if_ent(&F,2,2,"boot.cfg",0x20,cc,cfglen); if_ent(&F,2,3,"KERNEL64.BIN",0x20,ck2,klen);
    if(efilen)if_ent(&F,2,4,"EFI",0x10,defi,0);
    if_ent(&F,dboot,0,".",0x10,dboot,0); if_ent(&F,dboot,1,"..",0x10,0,0); if_ent(&F,dboot,2,"kernel64.bin",0x20,ck,klen);
    if(efilen){ if_ent(&F,defi,0,".",0x10,defi,0); if_ent(&F,defi,1,"..",0x10,0,0); if_ent(&F,defi,2,"BOOT",0x10,defib,0);
        if_ent(&F,defib,0,".",0x10,defib,0); if_ent(&F,defib,1,"..",0x10,defi,0); if_ent(&F,defib,2,"BOOTX64.EFI",0x20,ce,efilen); }
    { u32 tot=psec-F.dstart; *(u32*)(fsi+488)=tot>(F.next-2)?tot-(F.next-2):0; *(u32*)(fsi+492)=F.next; }
    for(u32 i=0;i<2*bps;i++)img[6*bps+i]=img[i];
    *used=F.dstart+(F.next-2);
    return 0;
}

enum { IP_WELCOME, IP_TARGET, IP_CONFIRM, IP_RUN, IP_DONE, IP_ERROR };
static int ip_page=IP_WELCOME, ip_mode=0, ip_part=-1, ip_copy=1, ip_ack=0, ip_ret=ST_BOOTMENU, ip_stage=-1, ip_prog=0;
static const char* ip_err=""; static const char* ip_err2="";
static u8 ip_mbr[512]; static int ip_ptype[4]; static u32 ip_pst[4], ip_psz[4]; static int ip_gpt=0;
static int ip_bx[24], ip_by[24], ip_bw[24], ip_bh[24], ip_bid[24], ip_bn=0;
static const char* IP_STAGES[8]={"Checking the kernel image","Preparing the boot partition","Writing the partition table","Installing the boot loader","Writing system files","Creating the data partition","Copying your files","Verifying the installation"};
static void ip_btnreg(int x,int y,int w,int h,int id){ if(ip_bn<24){ ip_bx[ip_bn]=x; ip_by[ip_bn]=y; ip_bw[ip_bn]=w; ip_bh[ip_bn]=h; ip_bid[ip_bn]=id; ip_bn++; } }
static void ip_load(void){
    gdisk_ensure(); for(int i=0;i<512;i++)ip_mbr[i]=0; ip_gpt=0;
    if(g_dktype!=DK_NONE)gdisk_read(0,1,ip_mbr);
    int valid=(ip_mbr[510]==0x55&&ip_mbr[511]==0xAA);
    for(int e=0;e<4;e++){ u8* pe=ip_mbr+446+e*16; ip_ptype[e]=valid?pe[4]:0; ip_pst[e]=*(u32*)(pe+8)*dk_ratio(); ip_psz[e]=*(u32*)(pe+12)*dk_ratio();
        if(!ip_psz[e])ip_ptype[e]=0; if(ip_ptype[e]==0xEE)ip_gpt=1; }
}
static void ip_open(int ret){ ip_ret=ret; ip_page=IP_WELCOME; ip_mode=0; ip_part=-1; ip_ack=0; ip_copy=1; ip_stage=-1; ip_prog=0; ip_load(); g_state=ST_INSTALL; }
static u32 ip_need(void){ return INST_BOOTP+INST_DATAP; }
static int ip_disk_ok(void){ return g_dktype!=DK_NONE&&g_dksectors>=2048u+ip_need()+2048u&&!(g_dktype==DK_NVME&&!g_nvme_bsz); }
static int ip_free_slot(int except){ for(int e=0;e<4;e++) if(e!=except&&!ip_ptype[e])return e; return -1; }
static int ip_part_ok(int e){ if(e<0||e>3||!ip_ptype[e]||ip_gpt)return 0; if(ip_psz[e]<ip_need())return 0; if(ip_pst[e]<64)return 0; if(ip_free_slot(e)<0)return 0;
    for(int q=0;q<4;q++) if(q!=e&&ip_ptype[q]&&ip_pst[q]<64)return 0; return 1; }
static const char* ip_tname(int t){ switch(t){ case 0x0B: case 0x0C: return "FAT32"; case 0x07: return "NTFS/exFAT"; case 0x83: return "Linux"; case 0x82: return "Linux swap"; case 0xEE: return "GPT"; case 0xEF: return "EFI"; case NVX_PTYPE: return "NoovexOS data"; case 0x05: case 0x0F: return "Extended"; default: return "Other"; } }
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
static void ip_present(void){
    ip_bn=0;
    for(u32 y=0;y<H;y++){ u32 c=mix(rgb(12,18,36),rgb(38,22,70),(int)(y*200/H)); u32* r=&SCENE[(u64)y*SW]; for(u32 x=0;x<W;x++)r[x]=c; }
    radial((int)W*18/100,(int)H*22/100,(int)H/3,rgb(60,90,200),70); radial((int)W*86/100,(int)H*84/100,(int)H*2/5,rgb(150,70,210),60);
    int cw=(int)W-80; if(cw>900)cw=900; int ch=(int)H-80; if(ch>560)ch=560; int cx=((int)W-cw)/2, cy=((int)H-ch)/2;
    shadow_soft(cx,cy,cw,ch,18); fill_round(cx,cy,cw,ch,18,15,rgb(250,251,253));
    int rw=230; fill_round(cx,cy,rw,ch,18,1|4,rgb(22,28,50)); fill_rect(cx+rw-18,cy,18,ch,rgb(22,28,50));
    draw_logo(cx+rw/2,cy+34,46,rgb(255,255,255),255);
    dstr_ctr(cx+rw/2,cy+100,"NoovexOS Setup",rgb(236,240,250),1);
    { const char* st[5]={"Welcome","Choose disk","Confirm","Install","Finish"}; int cur=ip_page>=IP_ERROR?3:ip_page;
      for(int k=0;k<5;k++){ int y=cy+150+k*52; int done=k<cur||(ip_page==IP_DONE&&k<=4);
          u32 cc=done?rgb(90,200,130):(k==cur?ACC:rgb(60,68,94)); fill_circle(cx+44,y+10,13,cc);
          if(done&&!(k==cur&&ip_page!=IP_DONE)){ for(int t=0;t<4;t++){ putpx(cx+38+t,y+10+t,rgb(255,255,255)); putpx(cx+38+t,y+11+t,rgb(255,255,255)); } for(int t=0;t<7;t++){ putpx(cx+42+t,y+14-t,rgb(255,255,255)); putpx(cx+42+t,y+15-t,rgb(255,255,255)); } }
          else { char n[2]={(char)('1'+k),0}; dstr_ctr(cx+44,y+3,n,rgb(255,255,255),1); }
          if(k<4)fill_rect(cx+43,y+25,2,26,k<cur?rgb(90,200,130):rgb(52,60,86));
          dstr(cx+68,y+3,st[k],k==cur?rgb(255,255,255):rgb(150,160,186),1); } }
    { char b[64],*p=b; p=scopy(p,gdisk_name()); if(g_dksectors){ p=scopy(p,"  "); p=ip_size(p,g_dksectors); } *p=0; dstr(cx+20,cy+ch-30,b,rgb(120,130,160),1); }
    int x0=cx+rw+36, y0=cy+34, w0=cw-rw-72, by=cy+ch-66;
    if(ip_page==IP_WELCOME){
        dstr(x0,y0,"Install NoovexOS",INK,3); y0+=58;
        dstr(x0,y0,"Setup copies the running system onto a disk so it starts",rgb(90,100,120),1); y0+=20;
        dstr(x0,y0,"without the ISO or USB stick. It works with BIOS and UEFI",rgb(90,100,120),1); y0+=20;
        dstr(x0,y0,"computers and creates a data partition for your files.",rgb(90,100,120),1); y0+=40;
        fill_round(x0,y0,w0,116,12,15,rgb(241,244,249));
        fill_round(x0+18,y0+26,56,40,6,15,rgb(120,130,150)); fill_round(x0+24,y0+32,44,14,3,15,rgb(70,80,100)); fill_circle(x0+30,y0+58,3,rgb(120,220,140));
        if(g_dktype==DK_NONE||!g_dksectors){ dstr(x0+92,y0+30,"No disk found",INK,2); dstr(x0+92,y0+62,"Attach an IDE, SATA or NVMe disk and restart.",rgb(120,128,146),1); }
        else { char b[80],*p=b; p=scopy(p,gdisk_name()); p=scopy(p," disk  -  "); p=ip_size(p,g_dksectors); *p=0; dstr(x0+92,y0+22,b,INK,2);
            if(g_dktype==DK_NVME&&g_nvme_bsz>512){ char q[24],*r=q; r=udec(r,g_nvme_bsz); r=scopy(r,"-byte sectors"); *r=0; dstr(x0+w0-slen(q)*8-18,y0+8,q,rgb(120,128,146),1); }
            int np=0; for(int e=0;e<4;e++) if(ip_ptype[e])np++;
            p=b; if(ip_gpt)p=scopy(p,"GPT disk (other systems installed)"); else if(!np)p=scopy(p,"Empty disk - ready for NoovexOS"); else { p=udec(p,(u32)np); p=scopy(p,np==1?" partition":" partitions"); } *p=0; dstr(x0+92,y0+58,b,rgb(120,128,146),1);
            ip_bar(x0+92,y0+82,w0-112,14,0); }
        y0+=130;
        if(g_dktype==DK_NVME&&!g_nvme_bsz){ dstr(x0,y0,"This NVMe drive uses a metadata format Setup can't write.",rgb(200,70,70),1); }
        else if(g_dksectors&&!ip_disk_ok()){ char b[80],*p=b; p=scopy(p,"The disk is too small. NoovexOS needs at least "); p=ip_size(p,ip_need()+4096); *p=0; dstr(x0,y0,b,rgb(200,70,70),1); }
        ip_button(x0,by,120,"Cancel",1,0,1); ip_button(x0+w0-160,by,160,"Continue",2,1,ip_disk_ok());
    }
    else if(ip_page==IP_TARGET){
        dstr(x0,y0,"Where should NoovexOS go?",INK,2); y0+=46;
        const char* tt[2]={"Erase disk and install","Install into a partition"};
        const char* td[2]={"Uses this disk for NoovexOS. Everything on it is deleted.","Keeps the other partitions. Pick one below to replace."};
        for(int m=0;m<2;m++){ int on=(ip_mode==m), en=(m==0)||!ip_gpt; int hov=en&&in_rect(mx,my,x0,y0,w0,64);
            fill_round(x0,y0,w0,64,12,15,on?mix(ACC,rgb(255,255,255),215):hov?rgb(240,243,248):rgb(246,248,251));
            if(on)fill_round(x0,y0+14,4,36,2,15,ACC);
            fill_circle(x0+28,y0+32,10,on?ACC:rgb(200,206,218)); fill_circle(x0+28,y0+32,on?4:7,rgb(255,255,255));
            dstr(x0+52,y0+14,tt[m],en?INK:rgb(170,176,190),1); dstr(x0+52,y0+36,(m==1&&ip_gpt)?"Not available on GPT disks.":td[m],rgb(120,128,146),1);
            if(en)ip_btnreg(x0,y0,w0,64,10+m); y0+=76; }
        y0+=4; ip_bar(x0,y0,w0,18,0); y0+=32;
        if(ip_mode==1){ int any=0;
            for(int e=0;e<4;e++) if(ip_ptype[e]){ any=1; int ok=ip_part_ok(e), on=(ip_part==e), hov=ok&&in_rect(mx,my,x0,y0,w0,30);
                if(on)fill_round(x0,y0,w0,30,8,15,mix(ACC,rgb(255,255,255),200)); else if(hov)fill_round(x0,y0,w0,30,8,15,rgb(238,241,247));
                fill_round(x0+10,y0+9,12,12,3,15,ip_pcol(ip_ptype[e]));
                char b[96],*p=b; p=scopy(p,"Partition "); p=udec(p,(u32)(e+1)); p=scopy(p,"   "); p=scopy(p,ip_tname(ip_ptype[e])); p=scopy(p,"   "); p=ip_size(p,ip_psz[e]); *p=0;
                dstr(x0+32,y0+8,b,ok?INK:rgb(170,176,190),1);
                if(!ok){ const char* why=ip_psz[e]<ip_need()?"too small":ip_free_slot(e)<0?"no free slot":"unsupported"; dstr(x0+w0-slen(why)*8-12,y0+8,why,rgb(190,120,120),1); }
                if(ok)ip_btnreg(x0,y0,w0,30,20+e); y0+=34; }
            if(!any)dstr(x0,y0,"This disk has no partitions. Use \"Erase disk\" instead.",rgb(120,128,146),1); }
        ip_button(x0,by,120,"Back",3,0,1); ip_button(x0+132,by,170,"Partition editor",6,0,1);
        ip_button(x0+w0-160,by,160,"Continue",4,1,ip_mode==0||(ip_part>=0&&ip_part_ok(ip_part)));
    }
    else if(ip_page==IP_CONFIRM){
        dstr(x0,y0,"Ready to install",INK,2); y0+=44;
        ip_bar(x0,y0,w0,22,1); y0+=36;
        { char b[96],*p=b; u32 bl=INST_BOOTP, dl=(ip_mode==1&&ip_part>=0)?ip_psz[ip_part]-INST_BOOTP:INST_DATAP;
          fill_round(x0,y0+3,12,12,3,15,ACC); p=scopy(p,"Boot partition   FAT32   "); p=ip_size(p,bl); p=scopy(p,dk_ratio()>1?"   UEFI loader, kernel (4K drive)":"   BIOS + UEFI loaders, kernel"); *p=0; dstr(x0+22,y0,b,INK,1); y0+=24;
          fill_round(x0,y0+3,12,12,3,15,rgb(110,190,120)); p=b; p=scopy(p,"Data partition   NVXFS   "); p=ip_size(p,dl); p=scopy(p,"   your files and folders"); *p=0; dstr(x0+22,y0,b,INK,1); y0+=36; }
        { int nf=0,nd=0; for(int k=0;k<FS_N;k++)if(FS[k].used)nf++; for(int i=1;i<DN_MAX;i++)if(DT[i].used)nd++;
          char b[80],*p=b; p=scopy(p,"Copy my files ("); p=udec(p,(u32)nf); p=scopy(p," files, "); p=udec(p,(u32)nd); p=scopy(p," folders)"); *p=0; ip_check(x0,y0,ip_copy,b,30,INK); y0+=40; }
        fill_round(x0,y0,w0,68,10,15,rgb(253,238,238));
        fill_circle(x0+24,y0+34,12,rgb(216,64,64)); dstr(x0+21,y0+27,"!",rgb(255,255,255),1);
        if(ip_mode==0){ char b[96],*p=b; p=scopy(p,"Everything on this "); p=ip_size(p,g_dksectors); p=scopy(p," disk will be erased."); *p=0; dstr(x0+48,y0+16,b,rgb(150,40,40),1);
            dstr(x0+48,y0+38,"This can't be undone.",rgb(170,80,80),1); }
        else { char b[96],*p=b; p=scopy(p,"Partition "); p=udec(p,(u32)(ip_part+1)); p=scopy(p," will be erased. Other partitions are kept,"); *p=0; dstr(x0+48,y0+16,b,rgb(150,40,40),1);
            dstr(x0+48,y0+38,"but the disk's boot code is replaced by NoovexOS's.",rgb(170,80,80),1); }
        y0+=84; ip_check(x0,y0,ip_ack,"I understand - erase and install",31,INK);
        ip_button(x0,by,120,"Back",5,0,1); ip_button(x0+w0-180,by,180,"Install now",7,2,ip_ack);
    }
    else if(ip_page==IP_RUN){
        dstr(x0,y0,"Installing NoovexOS",INK,2); y0+=34; dstr(x0,y0,"Please keep the computer on. This takes about a minute.",rgb(120,128,146),1); y0+=36;
        for(int s=0;s<8;s++){ int done=s<ip_stage, cur=s==ip_stage; u32 cc=done?rgb(90,200,130):cur?ACC:rgb(214,219,229);
            fill_circle(x0+10,y0+8,8,cc);
            if(done){ for(int t=0;t<3;t++){ putpx(x0+6+t,y0+8+t,rgb(255,255,255)); } for(int t=0;t<6;t++){ putpx(x0+9+t,y0+10-t,rgb(255,255,255)); } }
            dstr(x0+30,y0+1,IP_STAGES[s],done?rgb(90,100,120):cur?INK:rgb(170,176,190),1); y0+=26; }
        y0+=14; fill_round(x0,y0,w0,14,7,15,rgb(226,230,238)); int pw=(int)((long)w0*ip_prog/1000); if(pw>8)fill_round(x0,y0,pw,14,7,15,ACC);
        { char b[16],*p=b; p=udec(p,(u32)(ip_prog/10)); *p++='%'; *p=0; dstr(x0+w0-slen(b)*8,y0+22,b,rgb(120,128,146),1); }
    }
    else if(ip_page==IP_DONE){
        fill_circle(x0+40,y0+40,36,rgb(90,200,130)); for(int t=0;t<12;t++){ for(int q=0;q<3;q++){ putpx(x0+24+t,y0+40+t+q-2,rgb(255,255,255)); } } for(int t=0;t<24;t++){ for(int q=0;q<3;q++){ putpx(x0+35+t,y0+51-t+q-2,rgb(255,255,255)); } }
        dstr(x0+100,y0+18,"NoovexOS is installed",INK,2); dstr(x0+100,y0+50,"Everything was written and verified.",rgb(90,100,120),1); y0+=110;
        const char* L[3]={"1.  Shut down the computer.","2.  Remove the ISO or USB stick.","3.  Start it again - NoovexOS boots from the disk."};
        for(int k=0;k<3;k++){ dstr(x0,y0,L[k],INK,1); y0+=26; }
        y0+=10; dstr(x0,y0,"In VirtualBox: Settings > Storage, remove the ISO, then start the VM.",rgb(120,128,146),1);
        ip_button(x0,by,160,ip_ret==ST_DESKTOP?"Back to desktop":"Boot menu",8,0,1); ip_button(x0+w0-160,by,160,"Restart now",9,1,1);
    }
    else {
        fill_circle(x0+40,y0+40,36,rgb(216,64,64)); dstr_ctr(x0+40,y0+26,"!",rgb(255,255,255),2);
        dstr(x0+100,y0+18,"Installation stopped",INK,2); dstr(x0+100,y0+52,ip_err,rgb(150,40,40),1); dstr(x0+100,y0+72,ip_err2,rgb(120,128,146),1);
        ip_button(x0,by,160,"Back",3,0,1);
    }
    draw_cursor(); blit_all();
}
static void ip_fail(const char* a,const char* b){ ip_err=a; ip_err2=b; ip_page=IP_ERROR; ip_present(); }
static void ip_stage_go(int s,int prog){ ip_stage=s; ip_prog=prog; ip_present(); }
static void ip_run(void){
    ip_page=IP_RUN; ip_stage_go(0,0);
    u32 klen=*(volatile u32*)(u64)(0x100000ULL+24); if(klen<4096||klen>0x800000u){ ip_fail("The kernel size stamp is invalid.",""); return; }
    u8* kimg=(u8*)kmalloc(klen); if(!kimg){ ip_fail("Not enough memory for the kernel image.",""); return; }
    int kv=inst_kernel_image(kimg,klen);
    if(kv<0||kv==2){ kfree(kimg); ip_fail("The kernel in memory doesn't match the original build.","Boot the ISO again and run Setup before other programs."); return; }
    u32 bst,bsz=INST_BOOTP,dst,dsz; int bslot,dslot;
    if(ip_mode==0){ bst=2048u; dst=bst+bsz; dsz=INST_DATAP; bslot=0; dslot=1; }
    if(dsz%dk_ratio())dsz-=dsz%dk_ratio();
    else { if(!ip_part_ok(ip_part)){ kfree(kimg); ip_fail("The chosen partition can't be used.",""); return; }
        bst=ip_pst[ip_part]; dst=bst+bsz; dsz=ip_psz[ip_part]-bsz; bslot=ip_part; dslot=ip_free_slot(ip_part); }
    if((u64)dst+dsz>g_dksectors){ kfree(kimg); ip_fail("The disk is too small.",""); return; }
    ip_stage_go(1,40);
    u32 R=dk_ratio(), bps=512*R, fz=0, dso=0, cap=inst_fs_layout(bps,bsz/R,klen,&fz,&dso);
    u8* img=(u8*)kmalloc((u64)cap*bps); if(!img){ kfree(kimg); ip_fail("Not enough memory to build the boot partition.",""); return; }
    u32 usedn=0; int br=inst_build_fs(img,bps,bst/R,bsz/R,cap,g_dm_name,kimg,klen,&usedn);
    if(br){ kfree(img); kfree(kimg); ip_fail("The boot partition image could not be built.",br==-2?"The partition is too small for FAT32 on this disk.":""); return; }
    kfree(kimg); u32 used=usedn*R;
    ip_stage_go(2,90);
    u8 mbr[512]; for(int i=0;i<512;i++)mbr[i]=ip_mode==0?0:ip_mbr[i];
    int s1=(int)(_binary_inst_boot_bin_end-_binary_inst_boot_bin_start); for(int i=0;i<446;i++)mbr[i]=i<s1?_binary_inst_boot_bin_start[i]:0;
    for(int e=0;e<4;e++)mbr[446+e*16]=0;
    { u8* pe=mbr+446+bslot*16; for(int i=0;i<16;i++)pe[i]=0; pe[0]=0x80; pe[2]=2; pe[4]=0x0C; pe[5]=0xFE; pe[6]=0xFF; pe[7]=0xFF; *(u32*)(pe+8)=bst/R; *(u32*)(pe+12)=bsz/R; }
    { u8* pe=mbr+446+dslot*16; for(int i=0;i<16;i++)pe[i]=0; pe[2]=2; pe[4]=NVX_PTYPE; pe[5]=0xFE; pe[6]=0xFF; pe[7]=0xFF; *(u32*)(pe+8)=dst/R; *(u32*)(pe+12)=dsz/R; }
    if(ip_mode==0){ *(u32*)(mbr+440)=0x4E565849u^(u32)g_ticks; } mbr[510]=0x55; mbr[511]=0xAA;
    if(gdisk_write(0,mbr)){ kfree(img); ip_fail("Writing the partition table failed.","The disk reported an error."); return; }
    if(ip_mode==0&&ip_gpt){ u8 z[512]; for(int i=0;i<512;i++)z[i]=0; for(u32 q=0;q<R;q++)gdisk_write(g_dksectors-R+q,z); }
    ip_stage_go(3,110);
    u32 s2len=(u32)(_binary_inst_stage2_bin_end-_binary_inst_stage2_bin_start), s2s=(s2len+511)/512;
    if(R>1)s2s=0;
    for(u32 s=0;s<s2s;s++){ u8 sec[512]; for(int i=0;i<512;i++){ u32 o=s*512+i; sec[i]=o<s2len?_binary_inst_stage2_bin_start[o]:0; } if(gdisk_write(1+s,sec)){ kfree(img); ip_fail("Writing the boot loader failed.",""); return; } }
    ip_stage_go(4,140);
    for(u32 s=0;s<used;s++){ if(gdisk_write(bst+s,img+(u64)s*512)){ kfree(img); ip_fail("Writing the boot partition failed.","The disk reported an error."); return; }
        if((s&127)==0){ ip_prog=140+(int)((u64)s*560/used); ip_present(); } }
    ip_stage_go(5,700);
    g_nvx_generic=1; g_nvx_base=dst; disk_ok=1; nvx_format();
    ip_stage_go(6,760);
    if(ip_copy){ int nf=0,tot=0; for(int k=0;k<FS_N;k++)if(FS[k].used)tot++;
        for(int k=0;k<FS_N;k++) if(FS[k].used){ nvx_write(FS[k].name,(const u8*)FS[k].data,FS[k].len,FS[k].folder); nf++; ip_prog=760+nf*100/(tot?tot:1); ip_present(); } }
    dt_save(); g_nvx_note="NoovexOS data partition";
    ip_stage_go(7,870);
    { u8 rb[512]; int bad=0;
      if(gdisk_read(0,1,rb)){ bad=1; } else for(int i=0;i<512;i++) if(rb[i]!=mbr[i]){ bad=1; break; }
      for(u32 s=0;s<s2s&&!bad;s++){ if(gdisk_read(1+s,1,rb)){ bad=1; break; } for(int i=0;i<512;i++){ u32 o=s*512+i; if(rb[i]!=(o<s2len?_binary_inst_stage2_bin_start[o]:0)){ bad=1; break; } } }
      for(u32 s=0;s<used&&!bad;s++){ if(gdisk_read(bst+s,1,rb)){ bad=1; break; } const u8* w=img+(u64)s*512; for(int i=0;i<512;i++) if(rb[i]!=w[i]){ bad=1; break; }
          if((s&255)==0){ ip_prog=870+(int)((u64)s*130/used); ip_present(); } }
      kfree(img);
      if(bad){ ip_fail("Verification failed - the disk returned different data.","Try again, or use another disk."); return; } }
    ip_prog=1000; ip_stage=8; ip_page=IP_DONE; ip_load(); ip_present();
}
static void ip_act(int id){
    if(id==1){ g_state=ip_ret; return; }
    if(id==2){ if(ip_disk_ok())ip_page=IP_TARGET; return; }
    if(id==3){ ip_page=(ip_page==IP_ERROR)?IP_TARGET:IP_WELCOME; ip_load(); return; }
    if(id==4){ if(ip_mode==0||(ip_part>=0&&ip_part_ok(ip_part))){ ip_page=IP_CONFIRM; ip_ack=0; } return; }
    if(id==5){ ip_page=IP_TARGET; return; }
    if(id==6){ dm_load(); g_dm_open=1; g_dm_sel=-1; g_dm_ret=ST_INSTALL; g_state=ST_DISKMGR; return; }
    if(id==7){ if(ip_ack)ip_run(); return; }
    if(id==8){ g_state=ip_ret; return; }
    if(id==9){ outb(0x64,0xFE); for(;;)__asm__ volatile("hlt"); }
    if(id==10||id==11){ ip_mode=id-10; if(ip_mode==0)ip_part=-1; return; }
    if(id>=20&&id<24){ ip_part=id-20; return; }
    if(id==30){ ip_copy=!ip_copy; return; }
    if(id==31){ ip_ack=!ip_ack; return; }
}
static int ip_input(void){
    int changed=0;
    if(mouse_evt){ int dx,dy,btn; __asm__ volatile("cli"); dx=mouse_dx;dy=mouse_dy;mouse_dx=0;mouse_dy=0;btn=mouse_btn;mouse_evt=0; __asm__ volatile("sti");
        if(dx||dy){ mx+=dx;my-=dy; if(mx<0)mx=0;if(my<0)my=0;if(mx>=(int)W)mx=W-1;if(my>=(int)H)my=H-1; changed=1; }
        if(btn&&!mouse_btn_prev&&ip_page!=IP_RUN){ changed=1; for(int b=ip_bn-1;b>=0;b--) if(in_rect(mx,my,ip_bx[b],ip_by[b],ip_bw[b],ip_bh[b])){ ip_act(ip_bid[b]); break; } }
        mouse_btn_prev=btn; }
    if(xhid_n)xhci_mouse_poll();
    int c; while((c=kpop())>=0){ changed=1;
        if(c==27){ if(ip_page==IP_WELCOME||ip_page==IP_DONE)ip_act(1); else if(ip_page==IP_TARGET)ip_act(3); else if(ip_page==IP_CONFIRM)ip_act(5); else if(ip_page==IP_ERROR)ip_act(3); }
        else if(c=='\n'){ if(ip_page==IP_WELCOME)ip_act(2); else if(ip_page==IP_TARGET)ip_act(4); else if(ip_page==IP_CONFIRM)ip_act(7); else if(ip_page==IP_DONE)ip_act(9); }
        else if(ip_page==IP_TARGET&&(c==19||c==20)){ ip_mode=(c==20)?1:0; if(ip_gpt)ip_mode=0; } }
    return changed;
}
static void ip_open_preset(int ret,int mode,int part){ ip_open(ret); if(!ip_disk_ok())return; ip_mode=mode; ip_part=part; ip_page=(mode==0||ip_part_ok(part))?IP_CONFIRM:IP_TARGET; }
static void bootmenu_present(void){
    synthwave_scene();
    if(g_bootcfg){
        int pw=540, ph=310, px=(int)W/2-pw/2, py=(int)H*38/100; shadow_soft(px,py,pw,ph,16); fill_round(px,py,pw,ph,16,15,rgb(250,251,254));
        int x=px+30,y=py+26; dstr(x,y,"Boot Configuration",INK,2); y+=46;
        char b[96],*p; p=b;p=scopy(p,"Resolution:  ");p=udec(p,W);*p++='x';p=udec(p,H);p=scopy(p," @ ");p=udec(p,BPP);p=scopy(p,"-bit");*p=0; dstr(x,y,b,INK,1); y+=26;
        p=b;p=scopy(p,"CPU:  ");p=scopy(p,g_brand[0]?g_brand:"x86-64");*p=0; dstr(x,y,b,INK,1); y+=26;
        { int k=nic_index(); p=b;p=scopy(p,"Network:  ");p=scopy(p,k>=0?(char*)nic_name(pci_ven[k],pci_dev[k]):"none detected");*p=0; dstr(x,y,b,INK,1); y+=34; }
        dstr(x,y+7,"Safe mode (plain wallpaper, no animations)",INK,1);
        cfg_tgx=px+pw-76; cfg_tgy=y; cfg_tgw=48; cfg_tgh=28; fill_round(cfg_tgx,cfg_tgy,cfg_tgw,cfg_tgh,7,15,g_safemode?ACC:rgb(224,228,238));
        dstr_ctr(cfg_tgx+cfg_tgw/2,cfg_tgy+7,g_safemode?"ON":"OFF",g_safemode?rgb(255,255,255):rgb(120,128,146),1);
        cfg_bkx=x; cfg_bky=py+ph-54; cfg_bkw=130; cfg_bkh=38; fill_round(cfg_bkx,cfg_bky,cfg_bkw,cfg_bkh,8,15,ACC); dstr_ctr(cfg_bkx+cfg_bkw/2,cfg_bky+9,"Back",rgb(255,255,255),1);
        draw_cursor(); blit_all(); return;
    }
    bm_bw=380; bm_bh=36; bm_bx=(int)W/2-bm_bw/2; int y0=(int)H*60/100;
    for(int k=0;k<5;k++){ int by=y0+k*(bm_bh+10); bm_by[k]=by; int sel=(k==g_bootsel);
        int hov=(mx>=bm_bx&&mx<bm_bx+bm_bw&&my>=by&&my<by+bm_bh);
        if(sel||hov){ shadow_soft(bm_bx,by,bm_bw,bm_bh,10); fill_round(bm_bx,by,bm_bw,bm_bh,10,15,rgb(250,250,255)); dstr(bm_bx+22,by+10,BM_ITEMS[k],rgb(28,18,42),1); }
        else { fill_round(bm_bx,by,bm_bw,bm_bh,10,15,mix(rgb(58,20,80),rgb(255,255,255),44)); dstr(bm_bx+22,by+10,BM_ITEMS[k],rgb(232,216,242),1); } }
    dstr_ctr((int)W/2,(int)H-42,"Up / Down to choose, Enter to select - or click",rgb(212,182,226),1);
    if(g_bootmsg){ int pw=470,ph=160,px=(int)W/2-pw/2,py=(int)H/2-ph/2; shadow_soft(px,py,pw,ph,14); fill_round(px,py,pw,ph,14,15,rgb(250,251,254));
        dstr_ctr((int)W/2,py+30,"Disk installer not available",INK,2);
        dstr_ctr((int)W/2,py+70,"This is a read-only build - your disks are untouched.",rgb(90,100,120),1);
        msg_okw=110; msg_okx=(int)W/2-msg_okw/2; msg_oky=py+ph-48; msg_okh=34; fill_round(msg_okx,msg_oky,msg_okw,msg_okh,8,15,ACC); dstr_ctr(msg_okx+msg_okw/2,msg_oky+8,"OK",rgb(255,255,255),1); }
    draw_cursor(); blit_all();
}
static void bootmenu_activate(void){
    if(g_bootsel==0){ g_state=ST_SETUP; }
    else if(g_bootsel==1){ g_safemode=1; g_state=ST_SETUP; }
    else if(g_bootsel==2){ g_state=ST_RECOVERY; g_recmode=0; g_recsel=0; }
    else if(g_bootsel==3){ ip_open(ST_BOOTMENU); }
    else { g_bootcfg=1; }
}
static int bootmenu_input(void){
    int changed=0;
    if(mouse_evt){ int dx,dy,btn; __asm__ volatile("cli"); dx=mouse_dx;dy=mouse_dy;mouse_dx=0;mouse_dy=0;btn=mouse_btn;mouse_evt=0; __asm__ volatile("sti");
        if(dx||dy){ mx+=dx;my-=dy; if(mx<0)mx=0;if(my<0)my=0;if(mx>=(int)W)mx=W-1;if(my>=(int)H)my=H-1; changed=1; }
        if(btn&&!mouse_btn_prev){ changed=1;
            if(g_bootmsg){ if(in_rect(mx,my,msg_okx,msg_oky,msg_okw,msg_okh)||1) g_bootmsg=0; }
            else if(g_bootcfg){ if(in_rect(mx,my,cfg_tgx,cfg_tgy,cfg_tgw,cfg_tgh)) g_safemode^=1;
                else if(in_rect(mx,my,cfg_bkx,cfg_bky,cfg_bkw,cfg_bkh)) g_bootcfg=0; }
            else { for(int k=0;k<5;k++) if(in_rect(mx,my,bm_bx,bm_by[k],bm_bw,bm_bh)){ g_bootsel=k; bootmenu_activate(); break; } } }
        mouse_btn_prev=btn; }
    int c; while((c=kpop())>=0){ changed=1;
        if(g_bootmsg){ g_bootmsg=0; continue; }
        if(g_bootcfg){ if(c=='\n'||c==27)g_bootcfg=0; else if(c==' ')g_safemode^=1; continue; }
        if(c==19){ g_bootsel=(g_bootsel+4)%5; } else if(c==20){ g_bootsel=(g_bootsel+1)%5; }
        else if(c=='\n'){ bootmenu_activate(); } }
    static int lb=-1; int bp=(int)((g_ticks/50)&1); if(bp!=lb){lb=bp;changed=1;}
    return changed;
}

