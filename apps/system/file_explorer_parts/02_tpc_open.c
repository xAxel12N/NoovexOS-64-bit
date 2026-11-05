    else if(g_renmode==1){ int d=g_rennode; if(d>0&&DT[d].used){ if(fx_exists(DT[d].parent,n,-1,d))toast("That name is already taken");
        else { int j=0; for(;n[j]&&j<63;j++)DT[d].name[j]=n[j]; DT[d].name[j]=0; dt_save(); toast("Renamed"); } } }
    else if(g_renmode==2){ if(fx_exists(fx_dir,n,-1,-1))toast("That name is already taken");
        else { int d=dt_mkdir_at(fx_dir,n); if(d<0)toast("Folder limit reached"); else { DT[d].mode=0040755u; DT[d].uid=g_uid; DT[d].gid=g_uid; dt_save(); fx_seldir=d; g_fsel=-1; toast("Folder created"); } } }
    else if(g_renmode==3){ if(fx_exists(fx_dir,n,-1,-1))toast("That name is already taken"); else if(fx_fname_used(n,-1))toast("File names must be unique on this disk");
        else { int k=-1; for(int s=0;s<FS_N;s++) if(!FS[s].used){k=s;break;}
            if(k<0)toast("No free file slots");
            else { fs_newslot(k); int j=0; for(;n[j]&&j<63;j++)FS[k].name[j]=n[j]; FS[k].name[j]=0; FS[k].used=1; FS[k].folder=0; FS[k].dir=fx_dir; FS[k].mode=0100644u; FS[k].uid=g_uid; FS[k].gid=g_uid;
                fx_persist(k); g_fsel=k; fx_seldir=-1; toast("File created"); } } }
    g_rename=0; g_renmode=0; }
static int tpc_sel=-1, tpc_props=-1, tpc_li=-1; static u64 tpc_lt=0;
static void tpc_open(int i);
void fx_do(int id,int val){
    switch(id){
    case 70: if(tpc_sel>=0)tpc_props=tpc_sel; break;
    case 71: if(tpc_sel>=0)tpc_open(tpc_sel); break;
    case 72: vol_scan(1); toast("Drives refreshed"); break;
    case 73: tpc_props=-1; break;
    case 74: open_win(W_SETTINGS); break;
    case 75: { extern u64 g_click_t; if(val==tpc_li&&g_click_t-tpc_lt<45){ tpc_open(val); tpc_lt=0; } else { tpc_sel=val; tpc_li=val; tpc_lt=g_click_t; } } break;
    case 76: if(val>=0&&val<g_vol_n){ fx_mode=1; g_fsel=-1; fx_seldir=-1; tpc_sel=val; tpc_open(val); } break;
    case 1: fx_back(); break; case 2: fx_fwd(); break; case 3: fx_up(); break;
    case 4: if(fx_mode)fx_mode=0; fx_start_input(2,"New folder",-1,-1); break;
    case 5: if(fx_mode)fx_mode=0; fx_start_input(3,"untitled.txt",-1,-1); break;
    case 6: fx_rename_sel(); break; case 7: fx_delete_sel(); break;
    case 60: fx_restore_sel(); break; case 61: fx_empty_trash(); break;
    case 8: if(g_fsel>=0){ fx_clip=g_fsel; fx_clipcut=0; toast("Copied"); } else toast("Select a file to copy"); break;
    case 9: if(g_fsel>=0){ fx_clip=g_fsel; fx_clipcut=1; toast("Cut - paste it in another folder"); } else toast("Select a file to cut"); break;
    case 10: fx_paste(); break; case 11: if(fx_view!=val){ fx_view=val; fx_scroll=0; } break; case 50: if(g_uid!=0){ toast("Installing needs administrator rights - run: sudo noovexos_install"); break; } ip_open(ST_DESKTOP); break; case 12: fx_prev=!fx_prev; break;
    case 13: case 14: case 15: { int s=id-13; if(fx_sort==s)fx_sdesc=!fx_sdesc; else { fx_sort=s; fx_sdesc=0; } } break;
    case 16: fx_flen=0; fx_filter[0]=0; break;
    case 20: if(val==-2){ fx_mode=1; g_fsel=-1; fx_seldir=-1; } else if(val==-3){ g_files_usb=1; } else fx_go(val); break;
    case 40: fx_go(val); break;
    case 93: if(val>=0&&val<DN_MAX)fx_exp[val]^=1; break;
    }
}

/* ---- drawing ---- */
static short fx_kid[DN_MAX], fx_sib[DN_MAX]; static int fx_tlast=-1, fx_ty, fx_tbot, fx_tx0, fx_tw;
static void fx_dots_v(int x,int y0,int y1){ for(int y=y0;y<y1;y+=2)putpx(x,y,rgb(128,128,128)); }
static void fx_dots_h(int x0,int x1,int y){ for(int x=x0;x<x1;x+=2)putpx(x,y,rgb(128,128,128)); }
static void fx_tree_node(int d,int depth){
    if(fx_ty+18>fx_tbot)return; int rh=18, x=fx_tx0+4+depth*16, y=fx_ty; int kids=fx_kid[d]>=0;
    if(depth>0){ fx_dots_v(x-8,y,y+rh/2); fx_dots_h(x-8,x+(kids?0:4),y+rh/2); }
    if(kids){ fill_rect(x,y+4,9,9,rgb(128,128,128)); fill_rect(x+1,y+5,7,7,rgb(255,255,255)); fill_rect(x+2,y+8,5,1,rgb(0,0,0)); if(!fx_exp[d])fill_rect(x+4,y+6,1,5,rgb(0,0,0)); fx_btn(x-2,y+2,13,13,93,d); }
    fx_icon(x+13,y+1,16,0); const char* nm=d==0?"/":DT[d].name; char nb[40]; int room=(fx_tx0+fx_tw-(x+33))/8; if(room<1)room=1; if(room>38)room=38; fx_fit(nb,nm,room);
    int on=(!fx_mode&&fx_dir==d), tw=slen(nb)*8+4;
    if(on)fill_rect(x+32,y+1,tw,16,rgb(132,53,26)); else if(in_rect(mx,my,x+32,y+1,tw,16))fill_rect(x+32,y+1,tw,16,rgb(227,223,219));
    dstr(x+34,y+2,nb,on?rgb(255,255,255):rgb(0,0,0),1); fx_btn(x+13,y,tw+20,rh,20,d);
    fx_ty+=rh; int top=fx_ty;
    if(kids&&fx_exp[d]){ int last=y+rh; for(int c=fx_kid[d];c>=0;c=fx_sib[c]){ last=fx_ty; fx_tree_node(c,depth+1); if(fx_ty+18>fx_tbot)break; } fx_dots_v(x+4,top-4,last+rh/2); } }
static void fx_tree(int x,int y,int w,int h){
    for(int i=0;i<DN_MAX;i++){ fx_kid[i]=-1; fx_sib[i]=-1; }
    for(int i=DN_MAX-1;i>0;i--){ if(!DT[i].used)continue; int p=DT[i].parent; if(p<0||p>=DN_MAX||p==i||!DT[p].used)continue; if(DT[i].name[0]=='.'&&DT[i].name[1]=='t')continue; if(p==0&&DT[i].name[0]>='A'&&DT[i].name[0]<='Z')continue; fx_sib[i]=fx_kid[p]; fx_kid[p]=(short)i; }
    for(int i=DN_MAX-1;i>0;i--){ if(!DT[i].used||DT[i].parent!=0||DT[i].name[0]<'A'||DT[i].name[0]>'Z')continue; fx_sib[i]=fx_kid[0]; fx_kid[0]=(short)i; }
    if(fx_tlast!=fx_dir){ fx_tlast=fx_dir; fx_exp[0]=1; int c=fx_dir, g=0; while(c>0&&c<DN_MAX&&g++<64){ c=DT[c].parent; if(c>=0&&c<DN_MAX)fx_exp[c]=1; } }
    fx_ty=y+2; fx_tbot=y+h; fx_tx0=x; fx_tw=w; fx_tree_node(0,0); }
static void fx_toolbtn2(int x,int y,int w,int h,const char* lbl,int g,int id,int on,int enabled){
    int hov=enabled&&in_rect(mx,my,x,y,w,h);
    u32 bg=on?FXC(mix(ACC,rgb(255,255,255),196),rgb(190,186,178)):hov?FXC(rgb(232,236,244),rgb(227,223,219)):FXC(rgb(247,248,250),rgb(212,208,200));
    fill_round(x,y,w,h,7,15,bg); if(g_dstyle==1&&(hov||on))fx_bev(x,y,w,h,on);
    u32 fg=enabled?(on?ACC2:rgb(58,66,86)):rgb(184,190,204); u32 gc=enabled?(on?ACC:(g>=4&&g<=10?ACC:rgb(90,100,120))):rgb(200,205,216);
    if(g==7&&enabled)gc=rgb(214,80,80);
    int tw=lbl?slen(lbl)*8:0, gw=g?15:0, sp=(g&&lbl)?7:0, sx=x+(w-(tw+gw+sp))/2;
    if(g)fx_glyph(sx,y+(h-14)/2,g,gc); if(lbl)dstr(sx+gw+sp,y+(h-14)/2,lbl,fg,1);
    if(enabled)fx_btn(x,y,w,h,id,0); }
static void fx_toolbtn(int x,int y,int w,int h,const char* lbl,int id,int on,int enabled){ fx_toolbtn2(x,y,w,h,lbl,0,id,on,enabled); }
extern u32 g_nvx_secs;
static long fx_disk_total(void){ if(disk_ok&&g_nvx_secs)return (long)g_nvx_secs*512L; if(disk_ok&&g_disk_sectors)return (long)g_disk_sectors*512L; return (long)(g_ram_mb?g_ram_mb:16)*1048576L; }
static void fx_disk_list(int x,int y){ extern int gdisk_count(void); extern u32 gdisk_size_of(int); extern const char* gdisk_name_of(int); extern int g_gdisk_probed;
    if(!g_gdisk_probed)return; int n=gdisk_count(); if(n<=0)return; char b[200],*p=b; p=scopy(p,"Physical disks: ");
    for(int i=0;i<n&&i<5;i++){ if(i)p=scopy(p,",  "); p=scopy(p,(char*)gdisk_name_of(i)); p=scopy(p," "); u64 mb=(u64)gdisk_size_of(i)*512ULL/1048576ULL; if(mb>=1024){ p=udec(p,(u32)(mb/1024)); *p++='.'; p=udec(p,(u32)((mb%1024)*10/1024)); p=scopy(p," GB"); } else { p=udec(p,(u32)mb); p=scopy(p," MB"); } }
    *p=0; dstr(x,y,b,rgb(110,118,140),1); }
static void tpc_size(char* o,u64 v){ static const char* U[5]={"bytes","KB","MB","GB","TB"}; int u=0; u64 d=1; while(u<4&&v>=d*1024ULL){ d*=1024ULL; u++; }
    if(!u){ char* p=udec(o,(u32)v); p=scopy(p," bytes"); *p=0; return; }
    u64 w=v/d, f=(v%d)*100/d; char* p=o; if(w>=100){ p=udec(p,(u32)w); } else if(w>=10){ p=udec(p,(u32)w); *p++='.'; p=udec(p,(u32)(f/10)); } else { p=udec(p,(u32)w); *p++='.'; *p++=(char)('0'+f/10); *p++=(char)('0'+f%10); }
    *p++=' '; p=scopy(p,(char*)U[u]); *p=0; }
static void tpc_commas(char* o,u64 v){ char t[32]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); int k=0; for(int i=n-1;i>=0;i--){ o[k++]=t[i]; if(i&&i%3==0)o[k++]=','; } o[k]=0; }
static void tpc_bold(int x,int y,const char* s,u32 c){ dstr(x,y,s,c,1); dstr(x+1,y,s,c,1); }
static void tpc_usage_c(nvol* v){ long used=0; for(int k=0;k<FS_N;k++)if(FS[k].used)used+=FS[k].len; long total=fx_disk_total(); { u64 t4=0,f4=0; n4_usage(&t4,&f4); if(t4){ total=(long)t4; used=(long)(t4-f4); } } if(used>total)used=total; v->total=(u64)total; v->freeb=(u64)(total-used); v->has_free=1; }
static void tpc_icon(int x,int y,int kind,int big){
    int s=big?48:16;
    if(kind==VOL_USB){ int bw=s*3/4, bh=s*3/8, bx=x+(s-bw)/2+s/10, by=y+s/2-bh/2+s/8;
        fill_round(bx-s/6,by+bh/4,s/6+1,bh/2,1,15,rgb(176,182,192)); if(big){ fill_rect(bx-s/6+2,by+bh/4+3,3,3,rgb(120,126,138)); fill_rect(bx-s/6+2,by+bh/2+2,3,3,rgb(120,126,138)); }
        fill_round(bx,by,bw,bh,big?4:2,15,rgb(70,78,92)); fill_rect(bx+2,by+1,bw-4,big?3:1,rgb(110,118,134)); fill_circle(bx+bw-(big?7:3),by+bh-(big?6:3),big?2:1,rgb(110,220,140)); return; }
    int bw=s*7/8, bh=big?20:7, bx=x+(s-bw)/2, by=y+s-bh-(big?6:2);
    fill_round(bx,by,bw,bh,big?4:2,15,rgb(150,156,166)); fill_round(bx,by,bw,bh-(big?5:2),big?4:2,15,rgb(214,218,224)); fill_rect(bx+2,by+1,bw-4,1,rgb(240,242,245));
    fill_rect(bx+(big?5:2),by+bh-(big?4:2),bw-(big?18:7),1,rgb(120,126,136)); fill_circle(bx+bw-(big?7:3),by+bh-(big?3:2),big?2:1,rgb(80,200,120));
    if(kind==VOL_SYS){ int ls=big?20:7, lx=bx+(big?6:2), ly=by-ls+(big?6:2); fill_round(lx,ly,ls,ls,big?4:1,15,ACC); if(big){ dchar(lx+6,ly+3,'N',rgb(255,255,255),1); } } }
static void tpc_bar(int x,int y,int w,int h,u64 used,u64 total,int known){
    fill_rect(x,y,w,h,rgb(188,188,188)); fill_rect(x+1,y+1,w-2,h-2,rgb(230,230,230));
    if(!known||!total)return; u64 fw=used*(u64)(w-2)/total; if(used&&fw<2)fw=2; if(fw>(u64)(w-2))fw=(u64)(w-2);
    int red=used*10>total*9; fill_rect(x+1,y+1,(int)fw,h-2,red?rgb(218,38,38):rgb(38,160,218)); }
static int tpc_cols(int w){ int c=(w-24)/262; return c<1?1:c; }
static void tpc_open(int i){ if(i<0||i>=g_vol_n)return; nvol* v=&g_vol[i];
    if(v->kind==VOL_SYS){ fx_mode=0; fx_go(0); return; }
    if(v->kind==VOL_USB&&ufat_ok&&(v->fs[0]=='F')){ g_files_usb=1; return; }
    tpc_props=i; }
static int tpc_isin(int sx,int sy,int r2o,int r2i,int dx,int dy){ int d=dx*dx+dy*dy; (void)sx; (void)sy; return d<=r2o&&d>=r2i; }
static long long tpc_pa(long long ux,long long uy){ if(ux>=0&&uy>=0)return (ux+uy)?(ux*65536)/(ux+uy):0; if(ux>=0&&uy<0)return 65536+((-uy)*65536)/(ux-uy); if(ux<0&&uy<0)return 131072+((-ux)*65536)/(-ux-uy); return 196608+(uy*65536)/(uy-ux); }
static void tpc_pie(int cx,int cy,int R,int r,u64 used,u64 total,u32 cu,u32 cf){
    static const short SN[91]={0,175,349,523,698,872,1045,1219,1392,1564,1736,1908,2079,2250,2419,2588,2756,2924,3090,3256,3420,3584,3746,3907,4067,4226,4384,4540,4695,4848,5000,5150,5299,5446,5592,5736,5878,6018,6157,6293,6428,6561,6691,6820,6947,7071,7193,7314,7431,7547,7660,7771,7880,7986,8090,8192,8290,8387,8480,8572,8660,8746,8829,8910,8988,9063,9135,9205,9272,9336,9397,9455,9511,9563,9613,9659,9703,9744,9781,9816,9848,9877,9903,9925,9945,9962,9976,9986,9994,9998,10000};
    long long deg=total?(long long)(used*3600ULL/total):0; long long bp;
    if(deg>=3600)bp=262144; else { int d=(int)(deg/10); int q=d/90, rr=d%90; long long sx,cy2; int sn=SN[rr], cs=SN[90-rr];
        if(q==0){ sx=sn; cy2=cs; } else if(q==1){ sx=cs; cy2=-sn; } else if(q==2){ sx=-sn; cy2=-cs; } else { sx=-cs; cy2=sn; } bp=tpc_pa(sx,cy2); }
    for(int dy=-R;dy<=R;dy++) for(int dx=-R;dx<=R;dx++){ if(!tpc_isin(cx,cy,R*R,r*r,dx,dy))continue; long long p=tpc_pa(dx,-dy); u32 c=(p<bp)?cu:cf; int d2=dx*dx+dy*dy;
        int edge=(d2>(R-1)*(R-1))||(d2<(r+1)*(r+1)); putpx(cx+dx,cy+dy,edge?mix(c,rgb(255,255,255),110):c); } }
static void tpc_dialog(int wx,int wy,int ww,int wh){
    nvol* v=&g_vol[tpc_props]; if(v->kind==VOL_SYS)tpc_usage_c(v);
    for(int y=wy;y<wy+wh-1;y++)for(int x=wx+1;x<wx+ww-1;x++){ u32*pp=&SCENE[(u64)y*SW+x]; *pp=mix(*pp,rgb(20,24,34),70); }
    int dw=410, dh=560; if(dh>(int)H-50)dh=(int)H-50; int dx=((int)W-dw)/2, dy=((int)H-dh)/2-20; if(dy<4)dy=4; shadow_soft(dx,dy,dw,dh,8);
    fill_round(dx,dy,dw,dh,8,15,rgb(240,240,240)); fill_rect(dx,dy+31,dw,1,rgb(218,218,218));
    char nm[64]; vol_name(v,nm,64); char t[96],*p=t; p=scopy(p,nm); p=scopy(p," Properties"); *p=0; tpc_icon(dx+10,dy+8,v->kind,0); dstr(dx+34,dy+9,t,rgb(30,30,30),1);
    { int cx=dx+dw-46; int hv=in_rect(mx,my,cx,dy,46,31); if(hv)fill_round(cx,dy,46,31,8,2,rgb(196,43,28)); dstr_ctr(cx+23,dy+9,"x",hv?rgb(255,255,255):rgb(60,60,60),1); fx_btn(cx,dy,46,31,73,0); }
    int ty=dy+40; { const char* T[3]={"General","Tools","Hardware"}; int tx=dx+10; for(int k=0;k<3;k++){ int tw=slen(T[k])*8+20; if(k==0){ fill_rect(tx,ty,tw,26,rgb(255,255,255)); fill_rect(tx,ty,tw,1,rgb(200,200,200)); fill_rect(tx,ty,1,26,rgb(200,200,200)); fill_rect(tx+tw-1,ty,1,26,rgb(200,200,200)); }
        dstr(tx+10,ty+6,T[k],k==0?rgb(30,30,30):rgb(140,140,140),1); tx+=tw; } }
    int bx=dx+10, by=ty+26, bw=dw-20, bh=dh-(by-dy)-50; fill_rect(bx,by,bw,bh,rgb(255,255,255)); fill_rect(bx,by,bw,1,rgb(200,200,200)); fill_rect(bx,by+bh-1,bw,1,rgb(200,200,200)); fill_rect(bx,by,1,bh,rgb(200,200,200)); fill_rect(bx+bw-1,by,1,bh,rgb(200,200,200));
    int x=bx+16, y=by+16; tpc_icon(x,y-4,v->kind,1);
    fill_rect(x+70,y+6,bw-102,24,rgb(122,122,122)); fill_rect(x+71,y+7,bw-104,22,rgb(255,255,255)); dstr(x+76,y+10,v->label[0]?v->label:"",rgb(30,30,30),1); y+=52;
    fill_rect(x,y,bw-32,1,rgb(222,222,222)); y+=12;
    int c2=x+110; dstr(x,y,"Type:",rgb(30,30,30),1); dstr(c2,y,v->kind==VOL_USB?"USB Drive":"Local Disk",rgb(30,30,30),1); y+=22;
    dstr(x,y,"File system:",rgb(30,30,30),1); dstr(c2,y,v->fs[0]?v->fs:"Unknown",rgb(30,30,30),1); y+=22;
    dstr(x,y,"Location:",rgb(30,30,30),1); { char d[30]; fx_fit(d,v->dev,(bw-140)/8); dstr(c2,y,d,rgb(30,30,30),1); } y+=26;
    fill_rect(x,y,bw-32,1,rgb(222,222,222)); y+=12;
    u64 used=v->has_free&&v->total>=v->freeb?v->total-v->freeb:0; u32 cu=rgb(38,129,214), cf=rgb(214,214,214);
    char b1[32],b2[24]; int cb=x+124, cg=bx+bw-78;
    fill_rect(x,y+2,12,12,cu); dstr(x+18,y,"Used space:",rgb(30,30,30),1); if(v->has_free){ tpc_commas(b1,used); p=b1; while(*p)p++; scopy(p," bytes")[0]=0; tpc_size(b2,used); } else { scopy(b1,"unknown")[0]=0; b2[0]=0; } dstr(cb,y,b1,rgb(30,30,30),1); dstr(cg,y,b2,rgb(30,30,30),1); y+=22;
    fill_rect(x,y+2,12,12,cf); dstr(x+18,y,"Free space:",rgb(30,30,30),1); if(v->has_free){ tpc_commas(b1,v->freeb); p=b1; while(*p)p++; scopy(p," bytes")[0]=0; tpc_size(b2,v->freeb); } else { scopy(b1,"unknown")[0]=0; b2[0]=0; } dstr(cb,y,b1,rgb(30,30,30),1); dstr(cg,y,b2,rgb(30,30,30),1); y+=26;
    fill_rect(x,y,bw-32,1,rgb(222,222,222)); y+=10;
    dstr(x,y,"Capacity:",rgb(30,30,30),1); tpc_commas(b1,v->total); p=b1; while(*p)p++; scopy(p," bytes")[0]=0; tpc_size(b2,v->total); dstr(cb,y,b1,rgb(30,30,30),1); dstr(cg,y,b2,rgb(30,30,30),1); y+=24;
    int R=52, pcx=bx+bw/2, pcy=y+R+8; if(pcy+R+24>by+bh){ R=(by+bh-y-40)/2; pcy=y+R+8; } if(R>=20){ tpc_pie(pcx,pcy,R,R*5/10,v->has_free?used:0,v->has_free?v->total:1,cu,cf); char d[16]; p=d; p=scopy(p,"Drive "); *p++=v->letter; *p++=':'; *p=0; dstr_ctr(pcx,pcy+R+8,d,rgb(30,30,30),1); }
    int oy=dy+dh-40; const char* B[3]={"OK","Cancel","Apply"}; for(int k=0;k<3;k++){ int ox=dx+dw-10-(3-k)*86; int hv=k<2&&in_rect(mx,my,ox,oy,80,28);
        fill_round(ox,oy,80,28,4,15,k==0?(hv?rgb(0,95,184):rgb(0,103,192)):hv?rgb(229,229,229):rgb(253,253,253)); if(k)fill_round(ox,oy,80,28,4,15,hv?rgb(229,229,229):rgb(253,253,253));
        if(k){ fill_rect(ox+3,oy,74,1,rgb(208,208,208)); fill_rect(ox+3,oy+27,74,1,rgb(188,188,188)); }
        dstr_ctr(ox+40,oy+7,B[k],k==0?rgb(255,255,255):k==2?rgb(160,160,160):rgb(30,30,30),1); if(k<2)fx_btn(ox,oy,80,28,73,0); } }
static void fx_draw_thispc(int x0,int y0,int w,int h){
    vol_scan(0); if(g_vol_n>0&&g_vol[0].kind==VOL_SYS)tpc_usage_c(&g_vol[0]); if(tpc_sel>=g_vol_n)tpc_sel=-1; if(tpc_props>=g_vol_n)tpc_props=-1;
    fill_rect(x0,y0,w,h,rgb(255,255,255));
    int y=y0+14; dstr(x0+20,y,"v",rgb(96,96,96),1); tpc_bold(x0+36,y,"Devices and drives",rgb(31,31,31)); fill_rect(x0+36+18*8+12,y+8,w-(36+18*8+12)-24,1,rgb(229,229,229));
    int cols=tpc_cols(w), tw=256, th=68, gx=x0+24, gy=y+30;
    for(int i=0;i<g_vol_n;i++){ nvol* v=&g_vol[i]; int tx=gx+(i%cols)*(tw+6), ty=gy+(i/cols)*(th+6); if(ty+th>y0+h-4)break;
        int sel=(i==tpc_sel), hov=in_rect(mx,my,tx,ty,tw,th);
        if(sel){ fill_rect(tx,ty,tw,th,rgb(153,209,255)); fill_rect(tx+1,ty+1,tw-2,th-2,rgb(204,232,255)); } else if(hov){ fill_rect(tx,ty,tw,th,FXC(rgb(229,243,255),rgb(210,192,182))); }
        tpc_icon(tx+6,ty+10,v->kind,1);
        char nm[48]; vol_name(v,nm,48); char nf[28]; fx_fit(nf,nm,24); dstr(tx+62,ty+6,nf,rgb(31,31,31),1);
        u64 used=v->has_free&&v->total>=v->freeb?v->total-v->freeb:0; tpc_bar(tx+62,ty+26,180,13,used,v->total,v->has_free);
        char b[64],*p=b; if(v->has_free){ char a1[20],a2[20]; tpc_size(a1,v->freeb); tpc_size(a2,v->total); p=scopy(p,a1); p=scopy(p," free of "); p=scopy(p,a2); } else { char a2[20]; tpc_size(a2,v->total); p=scopy(p,a2); } *p=0;
        dstr(tx+62,ty+46,b,rgb(109,109,109),1); fx_btn(tx,ty,tw,th,75,i); }
    { int rows=(g_vol_n+cols-1)/cols; int iy=gy+rows*(th+6)+24; if(iy+70<y0+h&&g_uid==0&&!disk_ok){ int iw=w-48; int hov=in_rect(mx,my,gx,iy,iw,56);
        fill_rect(gx,iy,iw,56,hov?FXC(rgb(229,243,255),rgb(210,192,182)):rgb(243,243,243)); fill_circle(gx+28,iy+28,16,ACC); line_bl(gx+28,iy+19,gx+28,iy+34,rgb(255,255,255),255,1); line_bl(gx+22,iy+28,gx+28,iy+35,rgb(255,255,255),255,1); line_bl(gx+34,iy+28,gx+28,iy+35,rgb(255,255,255),255,1);
        dstr(gx+56,iy+12,"Install NoovexOS",rgb(31,31,31),1); dstr(gx+56,iy+30,"Put this system on a disk so it starts without the USB stick",rgb(109,109,109),1); fx_btn(gx,iy,iw,56,50,0); } }
}
static void fx_tpc_cmdbar(int wx,int wy,int ww){
    int y=wy+42, x=wx+10; int has=tpc_sel>=0&&tpc_sel<g_vol_n; int wide=ww>=760;
    fx_toolbtn2(x,y,wide?92:34,28,wide?"Properties":0,6,70,0,has); x+=wide?96:38;
    fx_toolbtn2(x,y,wide?66:34,28,wide?"Open":0,4,71,0,has); x+=wide?74:44; fill_rect(x-5,y+6,1,16,FXC(rgb(220,224,232),rgb(128,128,128)));
    fx_toolbtn2(x,y,wide?82:34,28,wide?"Refresh":0,3,72,0,1); x+=wide?90:44; fill_rect(x-5,y+6,1,16,FXC(rgb(220,224,232),rgb(128,128,128)));
    fx_toolbtn2(x,y,wide?120:34,28,wide?"Open Settings":0,0,74,0,1);
}
static void fx_preview(int px0,int py0,int pw,int ybot){
    fx_pvx0=px0; fx_pvy0=py0; fx_pvw=pw; fpv_edw=0; ze_n=0;
    if(fx_seldir>=0&&DT[fx_seldir].used){ fx_icon_item(px0+pw/2-28,py0+8,56,1,fx_seldir); char b[40]; fx_fit(b,DT[fx_seldir].name,pw/16); dstr_ctr(px0+pw/2,py0+74,b,INK,2);
        int nd=0,nf=0; for(int i=0;i<DN_MAX;i++)if(DT[i].used&&i!=fx_seldir&&DT[i].parent==fx_seldir)nd++; for(int k=0;k<FS_N;k++)if(FS[k].used&&FS[k].dir==fx_seldir)nf++;
        char s[48],*p=s; p=udec(p,(u32)nd); p=scopy(p,nd==1?" folder, ":" folders, "); p=udec(p,(u32)nf); p=scopy(p,nf==1?" file":" files"); *p=0; dstr_ctr(px0+pw/2,py0+104,s,rgb(120,128,146),1);
        char pth[192]; dt_path(fx_seldir,pth); char pf[48]; fx_fit(pf,pth,pw/8); dstr_ctr(px0+pw/2,py0+124,pf,rgb(150,158,176),1);
        char ms[12]; mode_str(DT[fx_seldir].mode?DT[fx_seldir].mode:0040755u,ms); dstr_ctr(px0+pw/2,py0+144,ms,rgb(150,158,176),1); return; }
    if(g_fsel<0||!FS[g_fsel].used){ dstr_ctr(px0+pw/2,py0+(ybot-py0)/2-20,"Select a file",rgb(160,166,180),1); dstr_ctr(px0+pw/2,py0+(ybot-py0)/2,"to see a preview",rgb(160,166,180),1); return; }
    FsFile* f=&FS[g_fsel]; int kind=fkind(g_fsel), ik=item_icon_kind(0,g_fsel);
    fx_icon(px0,py0+2,40,ik); char nb[40]; fx_fit(nb,f->name,(pw-52)/8); dstr(px0+50,py0+4,nb,INK,1);
    { char b[64],*p=b; p=scopy(p,(char*)fx_kname(ik,g_fsel)); *p=0; dstr(px0+50,py0+22,b,rgb(120,128,146),1); }
    { char b[48],*p=b; p=fmt_size(p,f->len); p=scopy(p,"   "); char ms[12]; mode_str(f->mode?f->mode:0100644u,ms); p=scopy(p,ms); *p=0; dstr(px0,py0+50,b,rgb(120,128,146),1); }
    const char* btn=(kind==1)?"Open image":kind==2?"Open in browser":(kind==0&&!fx_is_elf(g_fsel))?"Open in editor":0;
    int y=py0+72;
    if(btn){ fpv_edx=px0; fpv_edy=y; fpv_edw=pw; fpv_edh=28; fill_round(fpv_edx,fpv_edy,fpv_edw,fpv_edh,7,15,in_rect(mx,my,fpv_edx,fpv_edy,fpv_edw,fpv_edh)?ACC2:ACC); dstr_ctr(fpv_edx+pw/2,fpv_edy+7,btn,rgb(255,255,255),1); y+=38; }
    fill_rect(px0,y,pw,1,FXC(rgb(232,234,240),rgb(128,128,128))); y+=10; int ph=ybot-y;
    if(kind==1){ if(ensure_img(g_fsel)&&view_w>0) blit_img_fit(px0,y,pw,ph); else dstr(px0,y,"(cannot decode image)",rgb(178,120,120),1); }
    else if(kind==4){ zip_open(g_fsel);
        if(znent==0)dstr(px0,y,"Not a readable ZIP archive",rgb(120,128,146),1);
        else { char hb[40],*p=hb; p=udec(p,(u32)znent); scopy(p," entries - click to open")[0]=0; dstr(px0,y,hb,rgb(90,100,120),1); int ty=y+22;
            for(int e=0;e<znent&&ty<ybot-18;e++){ int sel=(e==zsel); if(sel)fill_round(px0-4,ty-3,pw+4,22,4,15,FXC(mix(ACC,rgb(255,255,255),204),rgb(210,192,182))); else if(in_rect(mx,my,px0-4,ty-3,pw+4,22))fill_round(px0-4,ty-3,pw+4,22,4,15,rgb(236,241,250));
                const char* nm=zent[e].name; int q=slen(nm); int isdir=(q>0&&(nm[q-1]=='/'||nm[q-1]=='\\')); fx_icon(px0,ty,14,isdir?0:1);
                char en[40]; fx_fit(en,nm,(pw-24)/8); dstr(px0+20,ty,en,INK,1); ze_y[e]=ty; ze_n++; ty+=22; } } }
    else if(fx_is_elf(g_fsel)||kind==3){ dstr(px0,y,fx_is_elf(g_fsel)?"Executable program.":"Binary file.",rgb(120,128,146),1); dstr(px0,y+18,fx_is_elf(g_fsel)?"Run it from the Terminal.":"No preview available.",rgb(150,158,176),1);
        int ty=y+44; for(int r=0;r<8&&r*8<f->len&&ty<ybot-14;r++){ char hx2[40],*p=hx2; for(int c=0;c<8&&r*8+c<f->len;c++){ p=hx(p,(u8)f->data[r*8+c],2); *p++=' '; } *p=0; dstr(px0,ty,hx2,rgb(150,158,176),1); ty+=16; } }
    else { int col=pw/8; if(col>80)col=80; int cx2=0; char lb[84]; int ty=y;
        for(int q=0;q<f->len&&f->data[q]&&ty<ybot-14;q++){ char ch=f->data[q];
            if(ch=='\n'||cx2>=col){ lb[cx2]=0; dstr(px0,ty,lb,rgb(60,66,84),1); ty+=16; cx2=0; if(ch=='\n')continue; }
            if(ch=='\r')continue; if(ch=='\t')ch=' '; if((unsigned char)ch<32)ch='.'; if(cx2<80)lb[cx2++]=ch; }
        if(cx2>0&&ty<ybot-14){ lb[cx2]=0; dstr(px0,ty,lb,rgb(60,66,84),1); }
        if(f->len==0)dstr(px0,ty,"(empty file)",rgb(150,158,176),1); }
}
static void fx_draw_body(int i);
static void fx_draw(int i){ extern int g_round_off; g_round_off=(g_dstyle==1); fx_draw_body(i); g_round_off=0; }
static void fx_draw_body(int i){
    fx_init(); if(fx_dir<0||!DT[fx_dir].used){ extern int g_home_dir; if(g_home_dir>0&&g_home_dir<DN_MAX&&DT[g_home_dir].used)fx_go(g_home_dir); else fx_go(0); }
    if(g_fsel>=0&&(!FS[g_fsel].used||FS[g_fsel].dir!=fx_dir))g_fsel=-1; if(fx_seldir>=0&&(!DT[fx_seldir].used||DT[fx_seldir].parent!=fx_dir))fx_seldir=-1;
    win_frame(i,g_dstyle==1?"File Manager":"File Explorer");
    int wx=WN[i].x, wy=WN[i].y+TITLEH, ww=WN[i].w, wh=WN[i].h-TITLEH; fx_bn=0; fx_hn2=0;
    fill_rect(wx+1,wy,ww-2,wh-1,rgb(255,255,255)); fill_rect(wx+1,wy,ww-2,76,FXC(rgb(247,248,250),rgb(212,208,200)));
    /* row 1: navigation + path + filter */
    fx_toolbtn2(wx+10,wy+8,30,28,0,1,1,0,fx_mode||fx_hp>0); fx_toolbtn2(wx+42,wy+8,30,28,0,2,2,0,!fx_mode&&fx_hp<fx_hn-1); fx_toolbtn2(wx+74,wy+8,30,28,0,3,3,0,fx_mode||fx_dir>0);
    int sw=ww>=700?200:150, px=wx+112, pwid=ww-(px-wx)-sw-18;
    fill_round(px,wy+8,pwid,28,7,15,rgb(255,255,255)); fill_rect(px+6,wy+35,pwid-12,1,FXC(rgb(222,226,234),rgb(128,128,128)));
    if(fx_mode){ int ix=px+10; fill_round(ix,wy+14,18,12,2,15,rgb(70,120,190)); fill_rect(ix+2,wy+16,14,8,rgb(160,205,250)); fill_rect(ix+6,wy+27,6,2,rgb(110,118,134)); dstr(ix+26,wy+15,">",rgb(150,158,176),1); dstr(ix+40,wy+15,"This PC",INK,1); }
    else { int chain[16],cn=0,c=fx_dir; while(c>=0&&cn<16){ chain[cn++]=c; c=DT[c].parent; }
        int x=px+10, maxx=px+pwid-10, first=cn-1;
        { int tot=0; for(int k=cn-1;k>=0;k--)tot+=(k==cn-1?2:slen(DT[chain[k]].name))*8+22; while(first>0&&tot>pwid-40){ tot-=slen(DT[chain[first-1]].name)*8+22; first--; } }
        if(first<cn-1){ dstr(x,wy+15,"..",rgb(150,158,176),1); x+=24; }
        for(int k=(first<cn-1?first:cn-1);k>=0&&x<maxx;k--){ const char* nm=(chain[k]==0)?"/":DT[chain[k]].name; int w=slen(nm)*8+10;
            int hov=in_rect(mx,my,x-4,wy+10,w,24); if(hov)fill_round(x-4,wy+10,w,24,5,15,FXC(rgb(232,237,246),rgb(227,223,219)));
            dstr(x+1,wy+15,nm,k==0?INK:rgb(90,100,120),1); fx_btn(x-4,wy+10,w,24,40,chain[k]); x+=w; if(k>0&&chain[k]!=0){ dstr(x,wy+15,">",rgb(180,186,200),1); x+=14; } } }
    { int sx=wx+ww-sw-8; fill_round(sx,wy+8,sw,28,7,15,rgb(255,255,255)); fill_round(sx+1,wy+9,sw-2,26,6,15,focus==W_FILES?rgb(252,253,255):rgb(255,255,255));
        fill_circle(sx+15,wy+20,5,rgb(150,158,176)); fill_circle(sx+15,wy+20,3,rgb(255,255,255)); fill_rect(sx+18,wy+24,4,2,rgb(150,158,176));
        if(fx_flen){ char fb[34]; fx_fit(fb,fx_filter,(sw-54)/8); dstr(sx+28,wy+15,fb,INK,1); fill_rect(sx+28+slen(fb)*8+1,wy+14,2,15,((g_ticks/50)&1)?ACC:rgb(255,255,255));
            fill_circle(sx+sw-14,wy+22,8,in_rect(mx,my,sx+sw-24,wy+12,20,20)?rgb(210,214,224):rgb(228,231,238)); dstr(sx+sw-18,wy+15,"x",rgb(90,100,120),1); fx_btn(sx+sw-24,wy+12,20,20,16,0); }
        else dstr(sx+28,wy+15,fx_mode?"Search This PC":"Type to filter",rgb(170,176,190),1); }
    /* row 2: actions */
    if(fx_mode)fx_tpc_cmdbar(wx,wy,ww); else
    { int y=wy+42, x=wx+10; int have=(g_fsel>=0)||(fx_seldir>0);
        int wide=ww>=880;
        if(fx_in_trash(fx_dir)){ fx_toolbtn2(x,y,wide?82:34,28,wide?"Restore":0,60,6,0,have&&(g_fsel<0||FS[g_fsel].dir==fx_trash_id())); x+=wide?86:38; fx_toolbtn2(x,y,wide?110:34,28,wide?"Empty Trash":0,61,7,0,1); x+=wide?118:44; }
        else { fx_toolbtn2(x,y,wide?82:34,28,wide?"Folder":0,4,4,0,1); x+=wide?86:38; fx_toolbtn2(x,y,wide?66:34,28,wide?"File":0,5,5,0,1); x+=wide?74:44; }
        fill_rect(x-5,y+6,1,16,FXC(rgb(220,224,232),rgb(128,128,128)));
        fx_toolbtn2(x,y,wide?82:34,28,wide?"Rename":0,6,6,0,have); x+=wide?86:38; fx_toolbtn2(x,y,wide?82:34,28,wide?"Delete":0,7,7,0,have); x+=wide?90:44;
        fill_rect(x-5,y+6,1,16,FXC(rgb(220,224,232),rgb(128,128,128)));
        fx_toolbtn2(x,y,wide?66:34,28,wide?"Copy":0,8,8,0,g_fsel>=0); x+=wide?70:38; fx_toolbtn2(x,y,wide?58:34,28,wide?"Cut":0,9,9,0,g_fsel>=0); x+=wide?62:38; fx_toolbtn2(x,y,wide?70:34,28,wide?"Paste":0,10,10,0,fx_clip>=0&&FS[fx_clip].used);
        int rx=wx+ww-10; if(ww>=700){ rx-=34; fx_toolbtn2(rx,y,34,28,0,13,12,fx_prev,1); }
        rx-=34; fx_toolbtn2(rx,y,34,28,0,11,40001,fx_view==1,1); fx_bid[fx_bn-1]=11; fx_bval[fx_bn-1]=1;
        rx-=36; fx_toolbtn2(rx,y,34,28,0,12,40000,fx_view==0,1); fx_bid[fx_bn-1]=11; fx_bval[fx_bn-1]=0; }
    fill_rect(wx+1,wy+76,ww-2,1,FXC(rgb(226,229,236),rgb(128,128,128)));
    /* sidebar */
    int sbw=ww>=620?164:128, top=wy+77, bot=wy+wh-26;
    fill_rect(wx+1,top,sbw,bot-top,FXC(rgb(245,246,249),rgb(255,255,255))); fill_rect(wx+sbw+1,top,1,bot-top,FXC(rgb(228,230,236),rgb(128,128,128)));
    if(g_dstyle==1){ vol_scan(0); int nv=g_vol_n>4?4:g_vol_n; int dh=26+(1+nv)*20; if(dh>(bot-top)/2)dh=(bot-top)/2; int ty=bot-dh;
        fx_tree(wx+4,top+3,sbw-6,ty-top-6); fill_rect(wx+4,ty-1,sbw-6,1,rgb(212,208,200)); dstr(wx+10,ty+4,"Devices",rgb(128,128,128),1); int y=ty+22;
        { int on=fx_mode&&tpc_sel<0; int tw=sbw-34; if(on)fill_rect(wx+30,y,tw,18,rgb(132,53,26)); else if(in_rect(mx,my,wx+30,y,tw,18))fill_rect(wx+30,y,tw,18,rgb(227,223,219));
          fill_rect(wx+10,y+3,16,11,rgb(64,64,64)); fill_rect(wx+11,y+4,14,8,rgb(120,170,220)); fill_rect(wx+15,y+15,8,2,rgb(64,64,64)); dstr(wx+32,y+1,"This PC",on?rgb(255,255,255):rgb(0,0,0),1); fx_btn(wx+6,y,sbw-10,18,20,-2); y+=20; }
        for(int vi=0;vi<nv&&y+18<=bot-2;vi++){ nvol* v=&g_vol[vi]; int on=fx_mode&&tpc_sel==vi; int tw=sbw-50; char nm[48]; vol_name(v,nm,48); char nf[24]; fx_fit(nf,nm,(sbw-60)/8);
            if(on)fill_rect(wx+46,y,tw,18,rgb(132,53,26)); else if(in_rect(mx,my,wx+46,y,tw,18))fill_rect(wx+46,y,tw,18,rgb(227,223,219));
            tpc_icon(wx+30,y+1,v->kind,0); dstr(wx+48,y+1,nf,on?rgb(255,255,255):rgb(0,0,0),1); fx_btn(wx+6,y,sbw-10,18,76,vi); y+=20; } }
    else
    { static const char* PN[8]={"Home","Downloads","Programs","Config","Temporary","Devices","Root","Trash"}; static const char* PP[8]={"/home","/downloads","/bin","/etc","/tmp","/dev","/","/.trash"};
        int y=top+10; dstr(wx+14,y,"PLACES",rgb(150,158,176),1); y+=20;
        for(int k=0;k<8;k++){ extern char g_home[]; char hp[112]; const char* pp=PP[k]; if(k==0)pp=g_home; else if(k==1){ char* q=hp; q=scopy(q,g_home); q=scopy(q,"/Downloads"); *q=0; if(dt_resolve_dir(hp)>=0)pp=hp; } int d=dt_resolve_dir(pp); if(d<0||y+26>bot)continue; int on=(!fx_mode&&fx_dir==d);
            if(on){ fill_round(wx+6,y,sbw-10,26,7,15,FXC(mix(ACC,rgb(255,255,255),210),rgb(210,192,182))); fill_round(wx+6,y+6,3,14,1,15,ACC); } else if(in_rect(mx,my,wx+6,y,sbw-10,26))fill_round(wx+6,y,sbw-10,26,7,15,FXC(rgb(233,236,242),rgb(227,223,219)));
            fx_icon(wx+16,y+5,16,0); dstr(wx+40,y+6,PN[k],on?INK:rgb(72,80,98),1); fx_btn(wx+6,y,sbw-10,26,20,d); y+=28; }
        y+=8; if(y+48<bot){ dstr(wx+14,y,"DEVICES",rgb(150,158,176),1); y+=20; }
        if(y+26<=bot){ int on=fx_mode; if(on)fill_round(wx+6,y,sbw-10,26,6,15,FXC(mix(ACC,rgb(255,255,255),204),rgb(210,192,182))); else if(in_rect(mx,my,wx+6,y,sbw-10,26))fill_round(wx+6,y,sbw-10,26,6,15,FXC(rgb(233,236,242),rgb(227,223,219)));
            fill_round(wx+14,y+8,18,12,3,15,rgb(120,130,150)); fill_rect(wx+17,y+11,12,3,rgb(200,206,218)); dstr(wx+38,y+6,"This PC",on?INK:rgb(72,80,98),1); fx_btn(wx+6,y,sbw-10,26,20,-2); y+=28; }
        vol_scan(0);
        for(int vi=0;vi<g_vol_n&&y+26<=bot-4;vi++){ nvol* v=&g_vol[vi]; int on=fx_mode&&tpc_sel==vi; if(on)fill_round(wx+6,y,sbw-10,24,6,15,FXC(rgb(229,243,255),rgb(210,192,182))); else if(in_rect(mx,my,wx+6,y,sbw-10,24))fill_round(wx+6,y,sbw-10,24,6,15,FXC(rgb(233,236,242),rgb(227,223,219)));
            tpc_icon(wx+22,y+4,v->kind,0); char nm[48]; vol_name(v,nm,48); char nf[24]; fx_fit(nf,nm,(sbw-50)/8); dstr(wx+44,y+5,nf,rgb(72,80,98),1); fx_btn(wx+6,y,sbw-10,24,76,vi); y+=26; }
        if(fx_clip>=0&&FS[fx_clip].used&&y<bot-44){ y=bot-40; fill_round(wx+8,y,sbw-14,32,6,15,rgb(236,240,248)); dstr(wx+14,y+2,fx_clipcut?"Cut:":"Copied:",rgb(120,128,146),1);
            char cb[24]; fx_fit(cb,FS[fx_clip].name,(sbw-24)/8); dstr(wx+14,y+16,cb,INK,1); } }
    /* main area (+ preview) */
    int pvw=(fx_prev&&!fx_mode&&ww>=700)?(ww>=900?280:230):0;
    fx_mainx=wx+sbw+2; fx_mainy=top; fx_mainw=ww-sbw-3-pvw; fx_mainh=bot-top;
    if(fx_mode){ fx_draw_thispc(fx_mainx,fx_mainy,fx_mainw+pvw,fx_mainh); }
    else {
        fx_build();
        int x0=fx_mainx, y0=fx_mainy, mw=fx_mainw;
        if(fx_view==0){
            int cType=x0+mw*48/100, cSize=x0+mw*72/100, cPerm=x0+mw*84/100; int showPerm=(mw>=520); if(!showPerm){ cType=x0+mw*56/100; cSize=x0+mw*80/100; }
            fill_rect(x0,y0,mw,28,FXC(rgb(250,251,253),rgb(212,208,200))); fill_rect(x0,y0+27,mw,1,FXC(rgb(232,234,240),rgb(128,128,128)));
            { const char* hn[3]={"Name","Size","Type"}; int hxs[3]={x0+42,cSize,cType}; int hid[3]={13,14,15};
              for(int h=0;h<3;h++){ int w=(h==0)?(cType-x0-44):(h==1?(showPerm?cPerm-cSize:x0+mw-cSize):(cSize-cType)); int hov=in_rect(mx,my,hxs[h]-6,y0,w,27); if(hov)fill_rect(hxs[h]-6,y0+2,w,24,FXC(rgb(238,241,247),rgb(227,223,219)));
                  dstr(hxs[h],y0+7,hn[h],fx_sort==(hid[h]-13)?INK:rgb(120,128,146),1); if(fx_sort==(hid[h]-13)){ int ax=hxs[h]+slen(hn[h])*8+6; dstr(ax,y0+7,fx_sdesc?"v":"^",ACC,1); }
                  fx_btn(hxs[h]-6,y0,w,27,hid[h],0); }
              if(showPerm)dstr(cPerm,y0+7,"Permissions",rgb(120,128,146),1); }
            int rh=28, vis=(fx_mainh-32)/rh; if(vis<1)vis=1; int maxs=fx_rn-vis; if(maxs<0)maxs=0; if(fx_scroll>maxs)fx_scroll=maxs; if(fx_scroll<0)fx_scroll=0;
            { int sr=fx_selrow(); if(sr>=0){ if(sr<fx_scroll)fx_scroll=sr; if(sr>=fx_scroll+vis)fx_scroll=sr-vis+1; } }
            int y=y0+32; { extern void gfx_scroll_area(int,int,int,int,int,int); gfx_scroll_area(W_FILES,x0,y0+32,mw,vis*rh,fx_scroll*rh); }
            for(int r=fx_scroll;r<fx_rn&&r<fx_scroll+vis;r++){ int isd=(fx_rk[r]==0), id=fx_rid[r];
                int sel=isd?(id==fx_seldir):(id==g_fsel); int cut=(!isd&&fx_clipcut&&id==fx_clip);
                if(sel)fill_round(x0+6,y,mw-12,rh-2,6,15,FXC(mix(ACC,rgb(255,255,255),200),rgb(210,192,182))); else if(in_rect(mx,my,x0+6,y,mw-12,rh-2))fill_round(x0+6,y,mw-12,rh-2,6,15,FXC(rgb(240,243,249),rgb(227,223,219)));
                const char* nm=isd?DT[id].name:FS[id].name; char nb[64]; fx_fit(nb,nm,(cType-x0-56)/8);
                fx_icon_item(x0+14,y+4,20,isd,id); dstr(x0+42,y+7,nb,cut?rgb(150,158,176):INK,1);
                if(isd){ dstr(cType,y+7,"Folder",rgb(120,128,146),1); int n=0; for(int q=0;q<DN_MAX;q++)if(DT[q].used&&q!=id&&DT[q].parent==id)n++; for(int q=0;q<FS_N;q++)if(FS[q].used&&FS[q].dir==id)n++;
                    char s[20],*p=s; p=udec(p,(u32)n); p=scopy(p,n==1?" item":" items"); *p=0; dstr(cSize,y+7,s,rgb(150,158,176),1); }
                else { char tb[24]; fx_fit(tb,fx_kname(fx_ikind(id),id),(cSize-cType-10)/8); dstr(cType,y+7,tb,rgb(120,128,146),1);
                    char s[20],*p=s; p=fmt_size(p,FS[id].len); *p=0; dstr(cSize,y+7,s,rgb(120,128,146),1); }
                if(showPerm){ char ms[12]; mode_str(isd?(DT[id].mode?DT[id].mode:0040755u):(FS[id].mode?FS[id].mode:0100644u),ms); dstr(cPerm,y+7,ms,rgb(150,158,176),1); }
                if(fx_hn2<FX_MAXROWS){ fx_hx[fx_hn2]=x0+6; fx_hy[fx_hn2]=y; fx_hw[fx_hn2]=mw-12; fx_hh[fx_hn2]=rh-2; fx_hrow[fx_hn2]=r; fx_hn2++; }
                y+=rh; }
            if(fx_rn>vis){ int th=fx_mainh-36, bh=th*vis/fx_rn; if(bh<20)bh=20; int by=y0+32+(th-bh)*fx_scroll/(maxs?maxs:1); fill_round(x0+mw-7,by,4,bh,2,15,rgb(200,205,216)); }
        } else {
            int tw=104, th=96, cols=(mw-16)/tw; if(cols<1)cols=1; fx_cols=cols; int rows=(fx_mainh-12)/th; if(rows<1)rows=1;
            int total=(fx_rn+cols-1)/cols, maxs=total-rows; if(maxs<0)maxs=0; if(fx_scroll>maxs)fx_scroll=maxs; if(fx_scroll<0)fx_scroll=0;
            { int sr=fx_selrow(); if(sr>=0){ int rr=sr/cols; if(rr<fx_scroll)fx_scroll=rr; if(rr>=fx_scroll+rows)fx_scroll=rr-rows+1; } }
            for(int r=fx_scroll*cols;r<fx_rn&&r<(fx_scroll+rows)*cols;r++){ int idx=r-fx_scroll*cols; int tx=x0+10+(idx%cols)*tw, ty=y0+10+(idx/cols)*th;
                int isd=(fx_rk[r]==0), id=fx_rid[r]; int sel=isd?(id==fx_seldir):(id==g_fsel);
                if(sel)fill_round(tx,ty,tw-6,th-6,8,15,FXC(mix(ACC,rgb(255,255,255),200),rgb(210,192,182))); else if(in_rect(mx,my,tx,ty,tw-6,th-6))fill_round(tx,ty,tw-6,th-6,8,15,FXC(rgb(240,243,249),rgb(227,223,219)));
                fx_icon_item(tx+(tw-6)/2-22,ty+8,44,isd,id);
                if(!isd){ const char* e=fx_ext(FS[id].name); int el=slen(e); if(el>0&&el<=4){ char eb[6]; int q=0; for(;q<el;q++){ char c=e[q]; if(c>='a'&&c<='z')c-=32; eb[q]=c; } eb[q]=0;
                    int bw=el*8+8, bx=tx+(tw-6)/2-bw/2, byy=ty+38; fill_round(bx,byy,bw,15,4,15,rgb(255,255,255)); fill_round(bx+1,byy+1,bw-2,13,3,15,rgb(58,66,86)); dstr(bx+4,byy,eb,rgb(255,255,255),1); } }
                char nb[16]; fx_fit(nb,isd?DT[id].name:FS[id].name,11); dstr_ctr(tx+(tw-6)/2,ty+62,nb,INK,1);
                if(fx_hn2<FX_MAXROWS){ fx_hx[fx_hn2]=tx; fx_hy[fx_hn2]=ty; fx_hw[fx_hn2]=tw-6; fx_hh[fx_hn2]=th-6; fx_hrow[fx_hn2]=r; fx_hn2++; } } }
        if(fx_rn==0){ if(fx_flen)dstr_ctr(x0+mw/2,y0+fx_mainh/2-10,"No matches",rgb(150,158,176),1);
            else { fx_icon(x0+mw/2-24,y0+fx_mainh/2-60,48,0); dstr_ctr(x0+mw/2,y0+fx_mainh/2,"This folder is empty",rgb(150,158,176),1); dstr_ctr(x0+mw/2,y0+fx_mainh/2+20,"Use + Folder or + File to add something",rgb(170,176,190),1); } }
        if(pvw){ int pvx=wx+ww-pvw-1; fill_rect(pvx,top,1,bot-top,FXC(rgb(228,230,236),rgb(128,128,128))); fill_rect(pvx+1,top,pvw-1,bot-top,FXC(rgb(251,252,254),rgb(255,255,255))); fx_preview(pvx+16,top+14,pvw-32,bot-8); }
        else { fx_pvw=0; fpv_edw=0; ze_n=0; }
    }
    /* status bar */
    fill_rect(wx+1,bot,ww-2,1,FXC(rgb(228,230,236),rgb(128,128,128))); fill_rect(wx+1,bot+1,ww-2,wh-(bot-wy)-2,FXC(rgb(247,248,250),rgb(212,208,200)));
    if(g_dstyle==1){ fx_bev(wx+2,top+1,sbw-1,bot-top-2,1); fx_bev(fx_mainx,top+1,fx_mainw,bot-top-2,1); fx_bev(wx+3,bot+3,ww-6,wh-(bot-wy)-6,1); fx_bev(wx+112,wy+8,ww-112-(ww>=700?200:150)-18,28,1); fx_bev(wx+ww-(ww>=700?200:150)-8,wy+8,ww>=700?200:150,28,1); }
    { char s[128],*p=s;
        if(fx_mode){ p=udec(p,(u32)g_vol_n); p=scopy(p,g_vol_n==1?" item":" items"); if(tpc_sel>=0&&tpc_sel<g_vol_n)p=scopy(p,"     1 item selected"); }
        else { int nd=0,nf=0; long tot=0; for(int r=0;r<fx_rn;r++){ if(fx_rk[r]==0)nd++; else { nf++; tot+=FS[fx_rid[r]].len; } }
            p=udec(p,(u32)nd); p=scopy(p,nd==1?" folder, ":" folders, "); p=udec(p,(u32)nf); p=scopy(p,nf==1?" file (":" files ("); p=fmt_size(p,tot); *p++=')';
            if(g_fsel>=0&&FS[g_fsel].used){ p=scopy(p,"     Selected: "); char nb[28]; fx_fit(nb,FS[g_fsel].name,26); p=scopy(p,nb); p=scopy(p,"  "); p=fmt_size(p,FS[g_fsel].len); }
            else if(fx_seldir>=0){ p=scopy(p,"     Selected: "); char nb[28]; fx_fit(nb,DT[fx_seldir].name,26); p=scopy(p,nb); } }
        *p=0; dstr(wx+12,bot+6,s,rgb(110,118,136),1);
        char r[40],*q=r; q=scopy(q,disk_ok?"Saved to disk":"RAM only - not saved"); *q=0; dstr(wx+ww-slen(r)*8-14,bot+6,r,disk_ok?rgb(80,150,100):rgb(200,130,60),1); }
    /* drag ghost */
    if(g_fdrag&&g_fdrag_slot>=0&&FS[g_fdrag_slot].used){ int dd=(mx-g_fdrag_x)*(mx-g_fdrag_x)+(my-g_fdrag_y)*(my-g_fdrag_y);
        if(dd>=36){ int gw=slen(FS[g_fdrag_slot].name)*8+40; fill_round(mx+10,my+10,gw,26,7,15,mix(ACC,rgb(255,255,255),40)); fx_icon(mx+16,my+14,18,fx_ikind(g_fdrag_slot)); dstr(mx+40,my+16,FS[g_fdrag_slot].name,rgb(255,255,255),1); } }
    if(fx_mode&&tpc_props>=0&&tpc_props<g_vol_n){ fx_bn=0; fx_hn2=0; tpc_dialog(wx,wy,ww,wh); }
    /* recursive-delete confirmation */
    if(fx_confirm&&fx_confirm_dir>0&&DT[fx_confirm_dir].used){
        for(int y=wy;y<wy+wh-1;y++)for(int x=wx+1;x<wx+ww-1;x++){ u32*pp=&SCENE[(u64)y*SW+x]; *pp=mix(*pp,rgb(20,24,34),90); }
        int dw=380, dh=150, dx=wx+(ww-dw)/2, dy=wy+(wh-dh)/2; shadow_soft(dx,dy,dw,dh,12); fill_round(dx,dy,dw,dh,12,15,rgb(255,255,255));
        dstr(dx+22,dy+20,"Delete folder?",INK,2);
        char b[80],*p=b; p=scopy(p,"\""); char nb[30]; fx_fit(nb,DT[fx_confirm_dir].name,28); p=scopy(p,nb); p=scopy(p,"\" and everything in it"); *p=0; dstr(dx+22,dy+56,b,rgb(90,100,120),1);
        dstr(dx+22,dy+74,"will be permanently deleted.",rgb(90,100,120),1);
        int by=dy+dh-44; fill_round(dx+dw-212,by,94,30,7,15,in_rect(mx,my,dx+dw-212,by,94,30)?rgb(228,231,238):rgb(240,242,246)); dstr_ctr(dx+dw-165,by+8,"Cancel",INK,1);
        fill_round(dx+dw-110,by,90,30,7,15,in_rect(mx,my,dx+dw-110,by,90,30)?rgb(200,60,60):rgb(220,72,72)); dstr_ctr(dx+dw-65,by+8,"Delete",rgb(255,255,255),1);
        fx_bn=0; fx_btn(dx+dw-212,by,94,30,90,0); fx_btn(dx+dw-110,by,90,30,91,0); fx_hn2=0; }
}
