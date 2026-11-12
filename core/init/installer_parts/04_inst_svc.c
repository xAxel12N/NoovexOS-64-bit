const u8* g_inst_img=0; u32 g_inst_len=0; int g_inst_pid=-1, g_app_ret=ST_LIVE, g_inst_src=0;
static int g_is_step=-1, g_is_rc=0, g_is_disk=-1;
static u32 g_is_klen, g_is_bst, g_is_bsz, g_is_dst, g_is_dsz, g_is_used, g_is_pos, g_is_s2len, g_is_s2s;
static int g_is_bslot, g_is_dslot; static u8* g_is_img=0; static u64 g_is_ko1, g_is_ko2; static u8 g_is_mbr[512];
static const char* g_is_e1=""; static const char* g_is_e2="";

typedef struct { u64 sectors; int kind, cur, ok, pad; char name[48]; } inst_disk;
typedef struct { int ok, gpt, ratio, nparts; u64 sectors; int ptype[4], pok[4]; u64 pst[4], psz[4]; char name[48]; char tname[4][16]; } inst_dinfo;
typedef struct { int mode, part, fs, copy; char label[16]; } inst_params;
typedef struct { int stage, prog, done, failed; char e1[120]; char e2[120]; char stagename[48]; } inst_prog;

static int is_caller(void){ return g_inst_pid>0&&g_js_running&&g_jcur>=0&&g_jp[g_jcur].pid==g_inst_pid&&g_jp[g_jcur].uid==0; }
static int is_wok(u64 p,u64 n){ return jc_uok(p,n)&&jc_umapped_w(p,n); }
static int is_rok(u64 p,u64 n){ return jc_uok(p,n)&&jc_umapped(p,n); }
static int is_str(u64 a,int max){ for(int i=0;i<max;i++){ u64 q=a+(u64)i; if((i==0||(q&0xFFFULL)==0)&&!is_rok(q,1))return -1; if(!*(const char*)q)return i; } return -1; }
static void is_cp(char* d,const char* s,int n){ int i=0; for(;s&&s[i]&&i<n-1;i++)d[i]=s[i]; d[i]=0; }
static int is_fail(const char* a,const char* b){ g_is_e1=a; g_is_e2=b; g_is_rc=-1; if(g_is_img){ kfree(g_is_img); g_is_img=0; } g_is_step=-1; return -1; }
static void ip_nk_prog_svc(int done,int total){ static int last=-1; ip_stage=6; ip_prog=760+done*100/(total?total:1);
    int pc=total?done*100/total:0; if(pc==last)return; last=pc; int w=(int)W*pc/100; fill_rect(0,(int)H-6,(int)W,6,0x2A2A2A); fill_rect(0,(int)H-6,w,6,0x3D5A78); blit_all(); }

static int is_step0(void){
    u32 klen=*(volatile u32*)(u64)(0x100000ULL+24); if(klen<4096||klen>KSNAP_MAX)return is_fail("The kernel size stamp is invalid.","");
    if(!g_kimg_len||klen!=g_kimg_len)return is_fail("The kernel image could not be captured at boot.","Run kcheck in the terminal and send a photo of the result.");
    if(!kimg_full())return is_fail("Not enough memory to prepare the kernel image.","");
    { int io=-1,fo=-1; int kr=kimg_check(g_kimg_snap,g_kimg_len,&io,&fo);
      if(kr<0||(kr==0&&(!io||!fo)))return is_fail(kr<0?"The kernel image could not be captured at boot.":!io?"The kernel code loaded at boot is not the original build.":"The kernel data loaded at boot is not the original build.","Run kcheck in the terminal and send a photo of the result."); }
    if(mm_area_conflicts())return is_fail("The firmware uses memory that NoovexOS also uses.","Run memmap in the terminal and send a photo of the result.");
    { extern int disk_ok; extern volatile int g_fs_wb_pending; disk_ok=0; g_fs_wb_pending=0; }
    g_is_klen=klen; g_is_bsz=INST_BOOTP;
    if(ip_mode==0){ g_is_bst=2048u; g_is_dst=g_is_bst+g_is_bsz; g_is_dsz=INST_DATAP; g_is_bslot=0; g_is_dslot=1;
        if(g_dksectors>g_is_dst+INST_DATAP+4096u){ u32 avail=g_dksectors-g_is_dst-4096u; u32 cap=g_ip_fs==0?0x8000000u:g_ip_fs==3?4186112u:g_ip_fs==2?0x4000000u:0xFFFFF000u; g_is_dsz=avail<cap?avail:cap; } }
    else { if(!ip_part_ok(ip_part))return is_fail("The chosen partition can't be used.","");
        g_is_bst=ip_pst[ip_part]; g_is_dst=g_is_bst+g_is_bsz; g_is_dsz=ip_psz[ip_part]-g_is_bsz; g_is_bslot=ip_part; g_is_dslot=ip_free_slot(ip_part); }
    if(g_is_dsz%dk_ratio())g_is_dsz-=g_is_dsz%dk_ratio();
    if((u64)g_is_dst+g_is_dsz>g_dksectors)return is_fail("The disk is too small.","Choose a larger disk.");
    if(g_ip_fs==3&&g_is_dsz>4186112u)g_is_dsz=4186112u;
    if(g_ip_fs==3&&g_is_dsz<8400u)return is_fail("The partition is too small for FAT16.","FAT16 needs at least 4 MB.");
    if(g_ip_fs==1&&g_is_dsz<32768u)return is_fail("The partition is too small for NTFS.","NTFS needs at least 16 MB - choose FAT16 instead.");
    if(g_ip_fs==2&&g_is_dsz<66600u)return is_fail("The partition is too small for FAT32.","FAT32 needs at least 33 MB - choose FAT16 instead.");
    u32 R=dk_ratio(), bps=512*R, fz=0, dso=0, cap=inst_fs_layout(bps,g_is_bsz/R,klen,&fz,&dso);
    if(cap>g_is_bsz/R)return is_fail("The kernel does not fit in the boot partition.","");
    g_is_img=(u8*)kmalloc((u64)cap*bps); if(!g_is_img)return is_fail("Not enough memory to build the boot partition.","");
    u32 usedn=0; int br=inst_build_fs(g_is_img,bps,g_is_bst/R,g_is_bsz/R,cap,g_dm_name,g_kimg_snap,klen,&usedn,&g_is_ko1,&g_is_ko2);
    if(br)return is_fail("The boot partition image could not be built.",br==-2?"The partition is too small for FAT32 on this disk.":"");
    if(ip_mem_verify(g_is_img+g_is_ko1,g_kimg_snap,klen)||ip_mem_verify(g_is_img+g_is_ko2,g_kimg_snap,klen))return is_fail(g_ip_e1,g_ip_e2);
    g_is_used=usedn*R; ip_stage=2; ip_prog=90; return 1; }

static int is_step1(void){
    u32 R=dk_ratio(); u8* mbr=g_is_mbr; for(int i=0;i<512;i++)mbr[i]=ip_mode==0?0:ip_mbr[i];
    int s1=(int)(_binary_inst_boot_bin_end-_binary_inst_boot_bin_start); for(int i=0;i<446;i++)mbr[i]=i<s1?_binary_inst_boot_bin_start[i]:0;
    for(int e=0;e<4;e++)mbr[446+e*16]=0;
    { u8* pe=mbr+446+g_is_bslot*16; for(int i=0;i<16;i++)pe[i]=0; pe[0]=0x80; pe[2]=2; pe[4]=0x0C; pe[5]=0xFE; pe[6]=0xFF; pe[7]=0xFF; *(u32*)(pe+8)=g_is_bst/R; *(u32*)(pe+12)=g_is_bsz/R; }
    { u8* pe=mbr+446+g_is_dslot*16; for(int i=0;i<16;i++)pe[i]=0; pe[2]=2; pe[4]=g_ip_fs==1?0x07:g_ip_fs==2?0x0C:g_ip_fs==3?0x0E:NVX_PTYPE; pe[5]=0xFE; pe[6]=0xFF; pe[7]=0xFF; *(u32*)(pe+8)=g_is_dst/R; *(u32*)(pe+12)=g_is_dsz/R; }
    if(ip_mode==0){ *(u32*)(mbr+440)=0x4E565849u^(u32)g_ticks; } mbr[510]=0x55; mbr[511]=0xAA;
    if(gdisk_write(0,mbr))return is_fail("Writing the partition table failed.","The disk reported an error.");
    if(ip_mode==0){ u8 z[512]; for(int i=0;i<512;i++)z[i]=0; for(u32 q=1;q<34u*R;q++)gdisk_write(q,z); if(g_dksectors>34u*R) for(u32 q=g_dksectors-34u*R;q<g_dksectors;q++)gdisk_write(q,z); }
    g_is_s2len=(u32)(_binary_inst_stage2_bin_end-_binary_inst_stage2_bin_start); g_is_s2s=(g_is_s2len+511)/512; if(R>1)g_is_s2s=0;
    for(u32 s=0;s<g_is_s2s;s++){ u8 sec[512]; for(int i=0;i<512;i++){ u32 o=s*512+i; sec[i]=o<g_is_s2len?_binary_inst_stage2_bin_start[o]:0; } if(gdisk_write(1+s,sec))return is_fail("Writing the boot loader failed.",""); }
    g_is_pos=0; ip_stage=4; ip_prog=140; return 1; }

static int is_step_write(void){
    extern int gdisk_writen(u32,int,const u8*);
    u32 end=g_is_pos+4096u; if(end>g_is_used)end=g_is_used;
    while(g_is_pos<end){ u32 c=end-g_is_pos; if(c>128)c=128; if(gdisk_writen(g_is_bst+g_is_pos,(int)c,g_is_img+(u64)g_is_pos*512))return is_fail("Writing the boot partition failed.","The disk reported an error."); g_is_pos+=c; }
    ip_prog=140+(int)((u64)g_is_pos*560/(g_is_used?g_is_used:1)); if(g_is_pos<g_is_used)return 0;
    ip_stage=5; ip_prog=700; return 1; }

static int is_step_format(void){
    { char mk[96],*q=mk; q=scopy(q,"installed="); q=scopy(q,(char*)g_buildsig.version); q=scopy(q,"\n"); *q=0; nvx_mark_file("/etc/nvx-installed",mk); k_unlink("/etc/nvx-setup-done"); }
    if(g_ip_fs){
        const rfs_ops* fo=g_ip_fs==1?&g_rfs_ntfs:g_ip_fs==2?&g_rfs_fat32:&g_rfs_fat16;
        int nr=nk_install(fo,(unsigned long long)g_is_dst,(unsigned long long)g_is_dsz,ip_copy,ip_nk_prog_svc);
        if(nr)return is_fail(nr==-1?"Formatting the partition failed.":"Writing files to the new partition failed.",fo->name);
        g_nvx_note=g_ip_fs==1?"NoovexOS NTFS partition":g_ip_fs==2?"NoovexOS FAT32 partition":"NoovexOS FAT16 partition";
    } else {
        extern int disk_ok; g_nvx_generic=1; g_nvx_base=g_is_dst; g_nvx_secs=g_is_dsz; g_rootfs=0; g_n4_nres=0; disk_ok=1;
        if(n4_format(g_is_dsz)){ disk_ok=0; return is_fail("Formatting the data partition failed.",g_n4_err); }
        g_nvx_note="NoovexOS data partition"; }
    ip_stage=7; ip_prog=870; return 1; }

static int is_step_verify_head(void){
    u8 rb[512];
    if(gdisk_read(0,1,rb))return is_fail("Verification failed - the partition table could not be read.","");
    for(int i=0;i<512;i++) if(rb[i]!=g_is_mbr[i])return is_fail("Verification failed - the partition table differs.","");
    for(u32 s=0;s<g_is_s2s;s++){ if(gdisk_read(1+s,1,rb))return is_fail("Verification failed - the boot loader could not be read.",""); for(int i=0;i<512;i++){ u32 o=s*512+i; if(rb[i]!=(o<g_is_s2len?_binary_inst_stage2_bin_start[o]:0))return is_fail("Verification failed - the boot loader differs.",""); } }
    g_is_pos=0; return 1; }

static int is_step_verify(void){
    extern int gdisk_readn(u32,int,u8*); static char e2[120];
    u8* vb=(u8*)kmalloc(128*512); if(!vb)return is_fail("Not enough memory to verify the installation.","");
    u32 end=g_is_pos+4096u; if(end>g_is_used)end=g_is_used;
    while(g_is_pos<end){ u32 c=end-g_is_pos; if(c>128)c=128;
        if(gdisk_readn(g_is_bst+g_is_pos,(int)c,vb)){ kfree(vb); char* p=e2; p=scopy(p,"Reading sector "); p=udec(p,g_is_bst+g_is_pos); p=scopy(p," failed ("); p=scopy(p,gdisk_name()); *p++=')'; *p=0; return is_fail("Verification failed - the disk returned an error.",e2); }
        const u8* w=g_is_img+(u64)g_is_pos*512; for(u32 i=0;i<c*512u;i++) if(vb[i]!=w[i]){ kfree(vb); char* p=e2; p=scopy(p,"Sector "); p=udec(p,g_is_bst+g_is_pos+i/512u); p=scopy(p," differs ("); p=scopy(p,gdisk_name()); *p++=')'; *p=0; return is_fail("Verification failed - the disk returned different data.",e2); }
        g_is_pos+=c; }
    kfree(vb); ip_prog=870+(int)((u64)g_is_pos*100/(g_is_used?g_is_used:1)); return g_is_pos>=g_is_used; }

static int is_step_finish(void){
    u32 R=dk_ratio();
    if(ip_mem_verify(g_is_img+g_is_ko1,g_kimg_snap,g_is_klen)||ip_mem_verify(g_is_img+g_is_ko2,g_kimg_snap,g_is_klen))return is_fail(g_ip_e1,g_ip_e2);
    kfree(g_is_img); g_is_img=0;
    if(gdisk_flush())return is_fail("The disk did not confirm that the data was saved.","Try again, or use another disk.");
    if(sysroot_base_ok()&&upd_put_sysroot_part((unsigned long long)(g_is_bst/R),(unsigned long long)(g_is_bsz/R)))return is_fail("Writing the Linux files to the boot partition failed.","The disk reported an error.");
    { char mk[96],*q=mk; q=scopy(q,"installed="); q=scopy(q,(char*)g_buildsig.version); q=scopy(q,"\n"); *q=0; nvx_mark_file("/etc/nvx-installed",mk); }
    klog("install: done by the installer program (INSTALL.NVX)");
    ip_stage=8; ip_prog=1000; return 1; }

static int is_run_step(void){
    extern volatile int g_frz_hold; int hold=g_frz_hold; g_frz_hold=1; int r=0;
    switch(g_is_step){
      case 0: r=is_step0(); if(r>0)g_is_step=1; break;
      case 1: r=is_step1(); if(r>0)g_is_step=2; break;
      case 2: r=is_step_write(); if(r>0)g_is_step=3; break;
      case 3: r=is_step_format(); if(r>0)g_is_step=4; break;
      case 4: r=is_step_verify_head(); if(r>0)g_is_step=5; break;
      case 5: r=is_step_verify(); if(r>0)g_is_step=6; break;
      case 6: r=is_step_finish(); if(r>0){ g_is_step=7; g_is_rc=1; } break;
      default: break; }
    g_frz_hold=hold; return r; }

static void is_dinfo(inst_dinfo* o){
    for(u32 i=0;i<sizeof(*o);i++)((u8*)o)[i]=0;
    o->ok=ip_disk_ok(); o->gpt=ip_gpt; o->ratio=(int)dk_ratio(); o->sectors=g_dksectors; is_cp(o->name,gdisk_name(),48);
    for(int e=0;e<4;e++){ o->ptype[e]=ip_ptype[e]; o->pst[e]=ip_pst[e]; o->psz[e]=ip_psz[e]; o->pok[e]=ip_part_ok(e); if(ip_ptype[e]){ o->nparts++; is_cp(o->tname[e],ip_tname(ip_ptype[e]),16); } } }

static void is_event(int* o){
    o[0]=-1; o[3]=0;
    if(mouse_evt){ int dx,dy,btn; cpu_irq_off(); dx=mouse_dx; dy=mouse_dy; mouse_dx=0; mouse_dy=0; btn=mouse_btn; mouse_evt=0; cpu_irq_on();
        if(dx||dy){ mx+=dx; my-=dy; if(mx<0)mx=0; if(my<0)my=0; if(mx>=(int)W)mx=W-1; if(my>=(int)H)my=H-1; o[3]=1; }
        if(btn&&!mouse_btn_prev)o[3]|=2; mouse_btn_prev=btn; }
    o[0]=kpop(); o[1]=mx; o[2]=my; }

long inst_syscall(u64 n,u64 a,u64 b,u64 c){
    if(!is_caller())return -1;
    switch(n){
      case 200: g_state=ST_APP; return 1;
      case 201: { if(!is_wok(a,sizeof(int)*4))return -1; int* o=(int*)a; o[0]=(int)W; o[1]=(int)H; o[2]=g_lockdown; o[3]=g_inst_src; return 0; }
      case 202: { extern int gdisk_count(void); extern int gdisk_kind_of(int); extern u32 gdisk_size_of(int); extern const char* gdisk_name_of(int); extern int gdisk_current(void);
          int cap=(int)b; if(cap<0||cap>8)cap=8; if(!is_wok(a,sizeof(inst_disk)*(u64)(cap?cap:1)))return -1; inst_disk* d=(inst_disk*)a; int k=gdisk_count(); if(k>cap)k=cap;
          for(int i=0;i<k;i++){ for(u32 q=0;q<sizeof(inst_disk);q++)((u8*)&d[i])[q]=0; d[i].sectors=gdisk_size_of(i); d[i].kind=gdisk_kind_of(i); d[i].cur=(i==gdisk_current()); is_cp(d[i].name,gdisk_name_of(i),48); d[i].ok=d[i].sectors>=2048u+ip_need()+2048u; }
          return k; }
      case 203: { if(!is_wok(b,sizeof(inst_dinfo)))return -1; if(g_is_step>0&&g_is_rc==0)return -1; ip_pick_disk((int)a); g_is_disk=(int)a; is_dinfo((inst_dinfo*)b); return 0; }
      case 204: { if(!is_rok(a,sizeof(inst_params)))return -1; if(g_is_step>0&&g_is_rc==0)return -1; inst_params pr=*(inst_params*)a;
          if(pr.fs<0||pr.fs>3||pr.mode<0||pr.mode>1)return -2; if(!ip_disk_ok())return -3; if(pr.mode==1&&!ip_part_ok(pr.part))return -4;
          ip_mode=pr.mode; ip_part=pr.mode?pr.part:-1; g_ip_fs=pr.fs; ip_copy=pr.copy?1:0; pr.label[15]=0; if(pr.label[0])is_cp(g_dm_name,pr.label,16);
          g_is_step=0; g_is_rc=0; g_is_e1=""; g_is_e2=""; ip_stage=0; ip_prog=0; klog("install: started by the installer program"); return 0; }
      case 205: { if(!is_wok(a,sizeof(inst_prog)))return -1; inst_prog* o=(inst_prog*)a;
          if(g_is_step>=0&&g_is_step<7)is_run_step();
          o->stage=ip_stage; o->prog=ip_prog; o->done=g_is_rc==1; o->failed=g_is_rc<0; is_cp(o->e1,g_is_e1,120); is_cp(o->e2,g_is_e2,120);
          is_cp(o->stagename,ip_stage>=0&&ip_stage<8?IP_STAGES[ip_stage]:"",48); return 0; }
      case 206: { int l=is_str(c,256); if(l<0)return -1; int sc=(int)(b>>32); if(sc<1)sc=1; if(sc>4)sc=4; dstr((int)(a&0xFFFFFFFF),(int)(a>>32),(const char*)c,(u32)b,sc); return 0; }
      case 207: draw_cursor(); blit_all(); return 0;
      case 208: { if(!is_wok(a,sizeof(int)*4))return -1; is_event((int*)a); return 0; }
      case 209: { if(a==1){ if(g_is_rc==1||g_is_step<0)power_reboot(); return -1; } if(g_is_step>0&&g_is_step<7&&g_is_rc==0)return -1; g_state=g_app_ret; { extern void fb_shadow_invalidate(void); fb_shadow_invalidate(); } return 0; }
      case 210: { int l=is_str(a,120); if(l<0)return -1; klog((const char*)a); return 0; }
    }
    return -1; }

static const u8* is_load_usb(u32* n){
    if(!(msd_ok&&ufat_ok))return 0;
    for(int i=0;i<ufs_n;i++){ const char* a=ufs[i].name; const char* b="INSTALL.NVX"; int k=0; while(a[k]&&b[k]&&((a[k]>='a'&&a[k]<='z')?a[k]-32:a[k])==b[k])k++; if(a[k]||b[k])continue;
        u32 sz=ufs[i].size; if(sz<256||sz>(8u<<20))return 0; u8* d=(u8*)kmalloc(sz); if(!d)return 0; int got=ufat_read(i,(char*)d,(int)sz); if(got!=(int)sz){ kfree(d); return 0; } *n=sz; return d; }
    return 0; }

int inst_launch(int ret){
    static u8* own=0; u32 n=0, body=0; const u8* d=0; const char* why="";
    if(g_state==ST_APP||g_inst_pid>0)return 0;
    if(g_uid!=0){ toast("Installing NoovexOS needs administrator rights"); return -1; }
    u8* u=(u8*)is_load_usb(&n);
    if(u){ int r=secboot_check_file(u,n,&body); if(r==0){ if(own)kfree(own); own=u; d=u; g_inst_src=1; } else { why=secboot_file_why(r); kfree(u); klog2("install: INSTALL.NVX on the USB stick was refused: ",why); } }
    if(!d){ u32 sl=0; int sx=0; const u8* s=sysroot_get("/usr/lib/noovex/install.nvx",&sl,&sx); if(s&&sl>256&&secboot_check_file(s,sl,&body)==0){ d=s; n=sl; g_inst_src=2; } }
    if(!d){ klog("install: no valid installer program found - using the built-in installer"); toast("Installer program not found - using the built-in installer"); ip_open(ret); return -1; }
    g_inst_img=d; g_inst_len=body;
    int j=jc_create("nvx-install",0,0); g_inst_img=0; if(j<0){ klog("install: the installer program could not be started - using the built-in installer"); ip_open(ret); return -1; }
    g_inst_pid=g_jp[j].pid; g_app_ret=ret; g_state=ST_APP; g_is_step=-1; g_is_rc=0;
    klog(g_inst_src==1?"install: started INSTALL.NVX from the USB stick (signature OK)":"install: started the installer program from the Linux files (signature OK)");
    return 1; }

int inst_app_tick(void){
    if(g_inst_pid<=0)return 0;
    int j=jc_idx(g_inst_pid);
    if(j>=0&&g_jp[j].state!=S_ZOMB)return 0;
    if(j>=0)g_jp[j].state=S_FREE;
    if(g_is_step>0&&g_is_step<7&&g_is_rc==0){ klog("install: the installer program stopped in the middle - the disk will not start"); toast("Installation interrupted - the disk will not start. Run install again."); }
    g_is_step=-1;
    g_inst_pid=-1; g_inst_img=0; if(g_is_img){ kfree(g_is_img); g_is_img=0; }
    klog("install: installer program ended");
    if(g_state==ST_APP){ g_state=g_app_ret; { extern void fb_shadow_invalidate(void); fb_shadow_invalidate(); } }
    return 1; }
void inst_app_keys(void){
    static int held=0; extern volatile unsigned char kdown[128];
    if(g_state!=ST_APP||g_inst_pid<=0){ held=0; return; }
    int on=kdown[0x1D]&&kdown[0x38]&&kdown[0x01];
    if(on&&!held&&!(g_is_step>0&&g_is_step<7&&g_is_rc==0)){ int j=jc_idx(g_inst_pid); if(j>=0){ extern void jc_signal_proc(int,int); jc_signal_proc(j,9); klog("install: installer program closed with Ctrl+Alt+Esc"); } }
    held=on; }
