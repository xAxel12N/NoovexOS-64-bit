#define FXV_MAX 256
typedef struct { char name[64]; int dir; unsigned long long size; } fxv_ent;
static fxv_ent* fxv_e; static int fxv_n=0, fxv_sel=-1, fxv_li=-1, fxv_scroll=0, fxv_vol=-1, fxv_trunc=0; static u64 fxv_lt=0; static char fxv_cwd[256]="/";
int fxv_active(void){ return fxv_vol>=0&&volfs_kind()&&volfs_vol()==fxv_vol; }
static void fxv_cb(const char* name,int isdir,unsigned long long size,void* ctx){ (void)ctx; if(fxv_n>=FXV_MAX){ fxv_trunc=1; return; } if(name[0]=='.'&&(!name[1]||(name[1]=='.'&&!name[2])))return;
    fxv_ent* e=&fxv_e[fxv_n]; int i=0; for(;name[i]&&i<63;i++)e->name[i]=name[i]; if(name[i])return; e->name[i]=0; e->dir=isdir; e->size=size; fxv_n++; }
static int fxv_lt_ent(const fxv_ent* a,const fxv_ent* b){ if(a->dir!=b->dir)return a->dir>b->dir; const char* p=a->name; const char* q=b->name; for(;*p&&*q;p++,q++){ char x=*p,y=*q; if(x>='A'&&x<='Z')x+=32; if(y>='A'&&y<='Z')y+=32; if(x!=y)return x<y; } return !*p&&*q; }
static void fxv_load(void){ if(!fxv_e)fxv_e=(fxv_ent*)kmalloc(sizeof(fxv_ent)*FXV_MAX); fxv_n=0; fxv_trunc=0; fxv_sel=-1; fxv_scroll=0; if(!fxv_e)return;
    if(volfs_ls(fxv_cwd,fxv_cb,0)){ toast("Could not read this folder"); return; }
    for(int i=1;i<fxv_n;i++){ fxv_ent t=fxv_e[i]; int j=i-1; while(j>=0&&fxv_lt_ent(&t,&fxv_e[j])){ fxv_e[j+1]=fxv_e[j]; j--; } fxv_e[j+1]=t; } }
void fxv_enter(int vi){ int r=volfs_open(vi); if(r){ toast(volfs_why(r)); fxv_vol=-1; return; } fxv_vol=vi; scopy(fxv_cwd,"/")[0]=0; fxv_load();
    toast(volfs_unclean()?volfs_why(VOLFS_E_RO):volfs_ro()?"Opened read-only - files can be copied out but not changed":"Opened - you can copy, rename and delete files here"); }
void fxv_leave(void){ fxv_vol=-1; fxv_n=0; fxv_sel=-1; }
static int fxv_up(void){ int n=slen(fxv_cwd); if(n<=1)return 0; while(n>1&&fxv_cwd[n-1]!='/')n--; if(n>1)n--; fxv_cwd[n]=0; if(!n){ fxv_cwd[0]='/'; fxv_cwd[1]=0; } fxv_load(); return 1; }
static int fxv_path(char* o,const char* name){ return volfs_join(o,256,fxv_cwd,name)<0?-1:0; }
static void fxv_uniq(char* nm,const char* src){ scopy(nm,src)[0]=0;
    for(int t=2;t<100&&fs_find_name(nm)>=0;t++){ char* p=nm; int i=0; for(;src[i]&&src[i]!='.'&&i<40;i++)*p++=src[i]; p=scopy(p," ("); p=udec(p,(u32)t); *p++=')'; for(const char* e=src+i;*e&&p<nm+62;)*p++=*e++; *p=0; } }
static int fxv_copy_out(int k,int open){ if(k<0||k>=fxv_n||fxv_e[k].dir){ toast("Select a file first"); return -1; }
    char pth[256]; if(fxv_path(pth,fxv_e[k].name)){ toast("Path too long"); return -1; }
    long sz=volfs_size(pth); if(sz<0){ toast("Could not read the file"); return -1; } if(sz>(64L<<20)){ toast("Files over 64 MB cannot be copied from here yet"); return -1; }
    char* buf=(char*)kmalloc((u64)sz+1); if(!buf){ toast("Not enough memory"); return -1; }
    long n=volfs_read(pth,buf,sz); if(n!=sz){ kfree(buf); toast("Could not read the file from the disk"); return -1; }
    char nm[64]; fxv_uniq(nm,fxv_e[k].name); int s=fs_save_bin(nm,(const u8*)buf,(int)n,1); kfree(buf);
    if(s<0){ toast("Could not copy the file"); return -1; }
    { char b[96],*p=b; p=scopy(p,open?"Opened ":"Copied "); p=scopy(p,nm); p=scopy(p," (in Downloads)"); *p=0; toast(b); }
    if(open){ fxv_leave(); fx_mode=0; fx_open_file(s); } return s; }
static void fxv_paste(void){ if(volfs_ro()){ toast(volfs_why(VOLFS_E_RO)); return; }
    if(fx_clip<0||!FS[fx_clip].used){ toast("Copy a file in Files first, then paste it here"); return; }
    char pth[256]; if(fxv_path(pth,FS[fx_clip].name)){ toast("Path too long"); return; }
    if(volfs_size(pth)>=0){ toast("A file with that name already exists here"); return; }
    long r=volfs_write(pth,fsd(fx_clip),FS[fx_clip].len); if(r<0){ toast(volfs_why((int)r)); return; } toast("Copied to the disk"); fxv_load(); }
static void fxv_delete(void){ if(volfs_ro()){ toast(volfs_why(VOLFS_E_RO)); return; } if(fxv_sel<0||fxv_sel>=fxv_n){ toast("Select a file or folder first"); return; }
    char pth[256]; if(fxv_path(pth,fxv_e[fxv_sel].name))return; int r=volfs_delete(pth); if(r){ toast(fxv_e[fxv_sel].dir?"Could not delete - is the folder empty?":volfs_why(r)); return; } toast("Deleted from the disk"); fxv_load(); }
void fxv_input_done(int mode,const char* n){
    if(!fxv_active()||volfs_ro()){ toast(volfs_why(VOLFS_E_RO)); return; } char to[256]; if(fxv_path(to,n)){ toast("Invalid name"); return; }
    if(mode==11){ int r=volfs_mkdir(to); toast(r?volfs_why(r):"Folder created"); fxv_load(); return; }
    if(fxv_sel<0||fxv_sel>=fxv_n)return; char from[256]; if(fxv_path(from,fxv_e[fxv_sel].name))return; if(seq(fxv_e[fxv_sel].name,(char*)n))return;
    int r=volfs_rename(from,to); toast(r?"Could not rename - the name may be taken":"Renamed"); fxv_load(); }
static void fxv_open(int k){ if(k<0||k>=fxv_n)return;
    if(fxv_e[k].dir){ char p[256]; if(fxv_path(p,fxv_e[k].name)){ toast("Path too long"); return; } scopy(fxv_cwd,p)[0]=0; fxv_load(); return; }
    fxv_copy_out(k,1); }
void fxv_do(int id,int val){
    if(!fxv_active()){ fxv_leave(); return; }
    switch(id){
    case 110: if(!fxv_up())fxv_leave(); break;
    case 111: fxv_open(fxv_sel); break;
    case 112: fxv_copy_out(fxv_sel,0); break;
    case 113: fxv_paste(); break;
    case 114: if(volfs_ro())toast(volfs_why(VOLFS_E_RO)); else fx_start_input(11,"New folder",-1,-1); break;
    case 115: if(volfs_ro())toast(volfs_why(VOLFS_E_RO)); else if(fxv_sel<0||fxv_sel>=fxv_n)toast("Select a file or folder first"); else fx_start_input(10,fxv_e[fxv_sel].name,-1,-1); break;
    case 116: fxv_delete(); break;
    case 117: { int v=fxv_vol, usb=v>=0&&v<g_vol_n&&g_vol[v].kind==VOL_USB; fxv_leave(); int r=volfs_eject(v); toast(r?volfs_why(r):usb?"USB stick ejected - you can remove it now":"SD card ejected - you can remove it now"); tpc_sel=-1; tpc_props=-1; } break;
    case 118: { extern u64 g_click_t; if(val==fxv_li&&g_click_t-fxv_lt<45){ fxv_open(val); fxv_lt=0; } else { fxv_sel=val; fxv_li=val; fxv_lt=g_click_t; } } break;
    case 119: case 121: fxv_scroll+=id==119?-10:10; if(fxv_scroll>fxv_n-1)fxv_scroll=fxv_n-1; if(fxv_scroll<0)fxv_scroll=0; break;
    case 120: fxv_load(); break;
    } }
void fxv_draw(int x0,int y0,int w,int h){
    fill_rect(x0,y0,w,h,rgb(255,255,255)); int ro=volfs_ro(), sd=fxv_vol>=0&&fxv_vol<g_vol_n&&(g_vol[fxv_vol].kind==VOL_SD||g_vol[fxv_vol].kind==VOL_USB), has=fxv_sel>=0&&fxv_sel<fxv_n, hf=has&&!fxv_e[fxv_sel].dir;
    int x=x0+10, y=y0+8;
    fx_toolbtn(x,y,44,26,"Up",110,0,1); x+=50; fx_toolbtn(x,y,54,26,"Open",111,0,has); x+=60; fx_toolbtn(x,y,124,26,"Copy to Downloads",112,0,hf); x+=130;
    fx_toolbtn(x,y,94,26,"Paste here",113,0,!ro&&fx_clip>=0); x+=100; fx_toolbtn(x,y,94,26,"New folder",114,0,!ro); x+=100; fx_toolbtn(x,y,70,26,"Rename",115,0,!ro&&has); x+=76;
    fx_toolbtn(x,y,64,26,"Delete",116,0,!ro&&has); x+=70; if(sd&&x+64<x0+w)fx_toolbtn(x,y,64,26,"Eject",117,0,1);
    y+=34; { char t[300],*p=t; char nm[48]; vol_name(&g_vol[fxv_vol],nm,48); p=scopy(p,nm); p=scopy(p,"  "); p=scopy(p,fxv_cwd); if(ro)p=scopy(p,"   (read-only)"); *p=0; char f[120]; fx_fit(f,t,(w-24)/8); dstr(x0+12,y,f,ro?rgb(176,80,0):rgb(90,100,120),1); }
    y+=20; fill_rect(x0+8,y,w-16,1,rgb(229,229,229)); y+=6;
    if(!fxv_n){ dstr(x0+20,y+10,"(empty folder)",rgb(150,158,176),1); return; }
    int rows=(y0+h-y-24)/22; if(rows<1)rows=1; if(rows>40)rows=40; if(fxv_scroll>fxv_n-rows)fxv_scroll=fxv_n-rows; if(fxv_scroll<0)fxv_scroll=0;
    for(int r=0;r<rows&&fxv_scroll+r<fxv_n;r++){ int k=fxv_scroll+r; fxv_ent* e=&fxv_e[k]; int ry=y+r*22; int sel=k==fxv_sel, hov=in_rect(mx,my,x0+8,ry,w-16,21);
        if(sel)fill_rect(x0+8,ry,w-16,21,rgb(204,232,255)); else if(hov)fill_rect(x0+8,ry,w-16,21,rgb(229,243,255));
        fx_icon(x0+14,ry+2,16,e->dir?0:1); char nb[64]; fx_fit(nb,e->name,(w-200)/8); dstr(x0+38,ry+3,nb,rgb(31,31,31),1);
        if(!e->dir){ char sb[24]; tpc_size(sb,e->size); dstr(x0+w-120,ry+3,sb,rgb(109,109,109),1); }
        fx_btn(x0+8,ry,w-16,21,118,k); }
    int by=y+rows*22+2; if(fxv_scroll>0)fx_toolbtn(x0+w-150,by,64,20,"Prev",119,0,1); if(fxv_scroll+rows<fxv_n)fx_toolbtn(x0+w-80,by,64,20,"More",121,0,1);
    if(fxv_trunc)dstr(x0+14,by+4,"Only the first 256 entries are shown",rgb(176,80,0),1); }
