static int fx_rowat(int px,int py){ for(int h=0;h<fx_hn2;h++) if(in_rect(px,py,fx_hx[h],fx_hy[h],fx_hw[h],fx_hh[h]))return fx_hrow[h]; return -1; }
static void fx_confirm_do(int yes){ if(yes&&fx_confirm_dir>0){ int n=fx_rmtree(fx_confirm_dir); dt_save(); char b[40],*p=b; p=scopy(p,"Deleted "); p=udec(p,(u32)n); p=scopy(p," items"); *p=0; toast(b); fx_seldir=-1; if(!DT[fx_dir].used)fx_go(0); } fx_confirm=0; fx_confirm_dir=-1; }
static void fx_click(int px,int py){
    for(int b=0;b<fx_bn;b++) if(in_rect(px,py,fx_bx[b],fx_by[b],fx_bw[b],fx_bh[b])){
        int id=fx_bid[b]; if(id==90){ fx_confirm_do(0); return; } if(id==91){ fx_confirm_do(1); return; }
        if(id==40&&fx_mode){ fx_mode=0; fx_go(0); return; } fx_do(id,fx_bval[b]); return; }
    if(fx_confirm)return;
    if(!fx_mode&&fx_pvw>0){
        if(g_fsel>=0&&FS[g_fsel].used&&fpv_edw>0&&in_rect(px,py,fpv_edx,fpv_edy,fpv_edw,fpv_edh)){ fx_open_file(g_fsel); return; }
        if(g_fsel>=0&&FS[g_fsel].used&&fkind(g_fsel)==4) for(int e=0;e<ze_n;e++) if(px>=fx_pvx0-4&&px<fx_pvx0+fx_pvw&&py>=ze_y[e]-3&&py<ze_y[e]+19){ zsel=e;
            const char* nm=zent[e].name; int q=slen(nm); if(q>0&&(nm[q-1]=='/'||nm[q-1]=='\\'))return;
            int L=zip_extract(g_fsel,e,(u8*)jpeg_scratch,jpeg_scratch_cap()); if(L<0)return;
            const char* base=nm; for(int r=0;nm[r];r++) if(nm[r]=='/'||nm[r]=='\\')base=nm+r+1;
            int k2=file_kind(base); if(k2==0||k2==2){ editor_open_buf((const char*)jpeg_scratch,L); }
            else { int slot=fs_save_bin(base,(u8*)jpeg_scratch,L,1); if(slot>=0){ FS[slot].dir=fx_dir; fx_persist(slot); if(k2==1)g_imgview=slot; else g_fsel=slot; } }
            return; } }
    if(fx_mode){ if(tpc_props<0&&px>=fx_mainx&&py>=fx_mainy&&py<fx_mainy+fx_mainh)tpc_sel=-1; return; }
    int r=fx_rowat(px,py);
    if(r>=0){ int key=fx_rk[r]?(1000+fx_rid[r]):fx_rid[r];
        { extern u64 g_click_t; if(key==fx_lastid&&(g_click_t-fx_lastt)<45){ fx_lastid=-99999; fx_open_row(r); return; }
        fx_lastid=key; fx_lastt=g_click_t; } fx_select_row(r);
        if(fx_rk[r]==1){ g_fdrag=1; g_fdrag_slot=fx_rid[r]; g_fdrag_x=px; g_fdrag_y=py; }
        return; }
    if(in_rect(px,py,fx_mainx,fx_mainy,fx_mainw,fx_mainh)){ g_fsel=-1; fx_seldir=-1; }
}
int fx_drop(int px,int py,int slot){   /* drag-and-drop a file onto a folder row or a sidebar place */
    if(slot<0||!FS[slot].used||fx_mode)return 0; int target=-1;
    int r=fx_rowat(px,py); if(r>=0&&fx_rk[r]==0)target=fx_rid[r];
    if(target<0) for(int b=0;b<fx_bn;b++) if((fx_bid[b]==20||fx_bid[b]==40)&&fx_bval[b]>=0&&in_rect(px,py,fx_bx[b],fx_by[b],fx_bw[b],fx_bh[b])){ target=fx_bval[b]; break; }
    if(target<0)return in_rect(px,py,WN[W_FILES].x,WN[W_FILES].y,WN[W_FILES].w,WN[W_FILES].h);
    if(target==FS[slot].dir)return 1;
    if(fx_exists(target,FS[slot].name,slot,-1)){ toast("A file with that name already exists there"); return 1; }
    FS[slot].dir=target; fx_persist(slot); if(g_fsel==slot)g_fsel=-1;
    char b[64],*p=b; p=scopy(p,"Moved to "); p=scopy(p,target==0?"/":DT[target].name); *p=0; toast(b); return 1; }
int fx_rclick(int px,int py){   /* returns context-menu kind for a right-click inside Files */
    if(fx_mode||g_files_usb||g_files_tree||fx_confirm)return 0;
    int r=fx_rowat(px,py); if(r>=0){ fx_select_row(r); return fx_rk[r]?2:7; }
    if(in_rect(px,py,fx_mainx,fx_mainy,fx_mainw,fx_mainh)){ g_fsel=-1; fx_seldir=-1; return 8; } return 0; }
void files_key(int c){
    if(g_files_usb||g_files_tree)return;
    if(fx_confirm){ if(c=='\n')fx_confirm_do(1); else if(c==27)fx_confirm_do(0); return; }
    if(fx_mode){ if(c=='\b'||c==27)fx_mode=0; return; }
    fx_build(); int sr=fx_selrow(); int step=(fx_view==1)?fx_cols:1;
    if(c==20||(c==18&&fx_view==1)){ int d=(c==18)?1:step; if(fx_rn){ fx_select_row(sr<0?0:(sr+d<fx_rn?sr+d:fx_rn-1)); } return; }
    if(c==19||(c==17&&fx_view==1)){ int d=(c==17)?1:step; if(fx_rn){ fx_select_row(sr<0?0:(sr-d>=0?sr-d:0)); } return; }
    if(c=='\n'){ if(sr>=0)fx_open_row(sr); return; }
    if(c==27){ if(fx_flen){ fx_flen=0; fx_filter[0]=0; } else { g_fsel=-1; fx_seldir=-1; } return; }
    if(c=='\b'){ if(fx_flen){ fx_filter[--fx_flen]=0; } else fx_up(); return; }
    if(c==127){ fx_delete_sel(); return; }
    if(c>=32&&c<127&&fx_flen<30){ fx_filter[fx_flen++]=(char)c; fx_filter[fx_flen]=0; fx_scroll=0; g_fsel=-1; fx_seldir=-1; }
}
void files_draw(int i){
    if(!g_files_tree&&!g_files_usb){ fx_draw(i); return; }
    if(g_files_tree){                                    /* Windows-style folder tree view */
        win_frame(i,"File Explorer");
        int wx=WN[i].x, wy=WN[i].y+TITLEH, ww=WN[i].w, wh=WN[i].h-TITLEH;
        fill_rect(wx+1,wy+1,ww-2,34,rgb(247,248,250));
        int bh=(mx>=wx+10&&mx<wx+52&&my>=wy+7&&my<wy+30);
        fill_round(wx+10,wy+7,42,22,5,15,bh?mix(ACC,rgb(255,255,255),120):rgb(236,238,242)); dstr(wx+22,wy+11,"<",bh?rgb(255,255,255):rgb(72,80,98),2);
        /* breadcrumb: walk parents */
        { char bc[128]; int chain[12],cn=0,c=g_cwd; while(c>=0&&cn<12){ chain[cn++]=c; c=FTREE[c].parent; }
          char* p=bc; p=scopy(p,"This PC  >  "); for(int k=cn-1;k>=0;k--){ p=scopy(p,(char*)FTREE[chain[k]].name); if(k>0)p=scopy(p,"  >  "); } *p=0;
          dstr(wx+64,wy+11,bc,rgb(90,100,120),1); }
        fill_rect(wx+1,wy+35,ww-2,1,rgb(228,230,236)); fill_rect(wx+1,wy+36,ww-2,wh-38,rgb(255,255,255));
        int y=wy+52; ftrow_n=0;
        /* subfolders of g_cwd */
        for(int nnode=0;nnode<FTREE_N && y<wy+wh-26;nnode++){ if(FTREE[nnode].parent!=g_cwd)continue;
            int hov=(mx>=wx+8&&mx<wx+ww-8&&my>=y-2&&my<y+24);
            if(hov)fill_round(wx+8,y-2,ww-16,26,5,15,rgb(233,236,242));
            fill_round(wx+18,y+2,18,14,3,15,rgb(224,184,96)); fill_round(wx+18,y+2,18,4,3,15,rgb(236,206,130));
            dstr(wx+46,y+3,(char*)FTREE[nnode].name,INK,1);
            ftrow_y[ftrow_n]=y; ftrow_node[ftrow_n]=nnode; ftrow_isfile[ftrow_n]=0; ftrow_n++; y+=28; }
        /* curated real files in this folder */
        for(int fi=0;fi<FFILE_N && y<wy+wh-26 && ftrow_n<24;fi++) if(FFILE[fi].node==g_cwd){
            int hov=(mx>=wx+8&&mx<wx+ww-8&&my>=y-2&&my<y+24);
            if(hov)fill_round(wx+8,y-2,ww-16,26,5,15,rgb(236,241,250));
            mini_doc(wx+20,y+1); dstr(wx+46,y+3,(char*)FFILE[fi].name,INK,1);
            int L=0; while(FFILE[fi].text[L])L++; char sb[16],*p=sb; p=udec(p,(u32)L); p=scopy(p," B"); *p=0; dstr(wx+ww-90,y+3,sb,rgb(150,158,176),1);
            ftrow_y[ftrow_n]=y; ftrow_node[ftrow_n]=fi; ftrow_isfile[ftrow_n]=2; ftrow_n++; y+=28; }
        /* real files if this is Documents(5) or Downloads(6) */
        int showfolder=(g_cwd==5)?0:(g_cwd==6)?1:-1;
        if(showfolder>=0){ for(int k=0;k<FS_N && y<wy+wh-26 && ftrow_n<24;k++) if(FS[k].used && FS[k].folder==showfolder){
            int hov=(mx>=wx+8&&mx<wx+ww-8&&my>=y-2&&my<y+24);
            if(hov)fill_round(wx+8,y-2,ww-16,26,5,15,rgb(236,241,250));
            mini_doc(wx+20,y+1); dstr(wx+46,y+3,FS[k].name,INK,1);
            char sb[16],*p=sb; p=udec(p,(u32)FS[k].len); p=scopy(p," B"); *p=0; dstr(wx+ww-90,y+3,sb,rgb(150,158,176),1);
            ftrow_y[ftrow_n]=y; ftrow_node[ftrow_n]=k; ftrow_isfile[ftrow_n]=1; ftrow_n++; y+=28; } }
        if(ftrow_n==0) dstr(wx+24,wy+56,"(empty folder)",rgb(150,158,176),1);
        if(g_fdrag && g_fdrag_slot>=0 && FS[g_fdrag_slot].used){   /* drag ghost following the cursor */
            int gw=slen(FS[g_fdrag_slot].name)*8+34; fill_round(mx+8,my+8,gw,24,6,15,mix(ACC,rgb(255,255,255),40));
            mini_doc(mx+14,my+12); dstr(mx+34,my+14,FS[g_fdrag_slot].name,rgb(255,255,255),1); }
        return;
    }
    if(g_files_usb){                                     /* USB drive view */
        win_frame(i,"File Explorer  -  USB (E:)");
        int wx=WN[i].x, wy=WN[i].y+TITLEH, ww=WN[i].w, wh=WN[i].h-TITLEH;
        fill_rect(wx+1,wy+1,ww-2,34,rgb(247,248,250));
        int bh=(mx>=wx+10&&mx<wx+96&&my>=wy+7&&my<wy+30);
        fill_round(wx+10,wy+7,86,22,5,15,bh?mix(ACC,rgb(255,255,255),120):rgb(236,238,242)); dstr(wx+20,wy+11,"< This PC",bh?rgb(255,255,255):rgb(72,80,98),1);
        { char t[40],*p=t; p=scopy(p,"USB (E:)  -  "); p=udec(p,(u32)ufs_n); p=scopy(p," files"); *p=0; dstr(wx+110,wy+11,t,rgb(90,100,120),1); }
        fill_rect(wx+1,wy+35,ww-2,1,rgb(228,230,236)); fill_rect(wx+1,wy+36,ww-2,wh-38,rgb(255,255,255));
        if(!ufat_ok){ dstr(wx+20,wy+62,"No USB filesystem detected.",rgb(120,128,146),1); }
        else if(!ufs_n){ dstr(wx+20,wy+62,"(empty)",rgb(120,128,146),1); }
        else { int y=wy+50; for(int k=0;k<ufs_n && y<wy+wh-24;k++){
            int hov=(mx>=wx+8&&mx<wx+ww-8&&my>=y-2&&my<y+22);
            if(hov)fill_round(wx+8,y-2,ww-16,24,5,15,rgb(233,236,242));
            fill_round(wx+16,y+1,15,17,2,15,rgb(240,244,250)); fill_round(wx+16,y+1,15,4,2,15,rgb(224,184,96)); dstr(wx+21,y+7,".",rgb(160,168,182),1);
            dstr(wx+42,y+3,ufs[k].name,INK,1);
            char sb[24],*p=sb;p=udec(p,ufs[k].size);p=scopy(p," B");*p=0; dstr(wx+ww-110,y+3,sb,rgb(120,128,146),1);
            y+=26; } }
        return;
    }
    win_frame(i,"File Explorer");
    int wx=WN[i].x, wy=WN[i].y+TITLEH, ww=WN[i].w, wh=WN[i].h-TITLEH;
    int sbw=150, listw=232, lx0=wx+sbw, pvx=lx0+listw;
    const char*nav[5]={"Home","Desktop","Documents","Downloads","This PC"};
    fill_rect(wx+1,wy+1,ww-2,34,rgb(247,248,250));
    fill_round(wx+sbw+12,wy+7,listw-24,22,5,15,rgb(255,255,255));
    { char bc[48],*p=bc; p=scopy(p,"This PC  >  "); p=scopy(p,(char*)nav[g_filesnav]); *p=0; dstr(wx+sbw+22,wy+11,bc,rgb(90,100,120),1); }
    fill_rect(wx+1,wy+35,ww-2,1,rgb(228,230,236));
    fill_rect(wx+1,wy+36,sbw,wh-38,rgb(243,244,247)); fill_rect(wx+sbw,wy+36,1,wh-38,rgb(228,230,236));
    for(int k=0;k<5;k++){ int iy=wy+46+k*30; if(k==g_filesnav) fill_round(wx+6,iy,sbw-12,26,6,15,mix(ACC,rgb(255,255,255),204));
        else if(mx>=wx+6&&mx<wx+sbw-6&&my>=iy&&my<iy+26) fill_round(wx+6,iy,sbw-12,26,6,15,rgb(233,236,242));
        fill_round(wx+14,iy+8,13,9,2,15,k==g_filesnav?ACC:rgb(224,184,96)); dstr(wx+34,iy+7,nav[k],k==g_filesnav?INK:rgb(72,80,98),1); }
    if(g_filesnav==4){                                 /* This PC -> Devices with capacity bars */
        int mx0=wx+sbw+1, my0=wy+36, mw=ww-sbw-2, mh=wh-38; fill_rect(mx0,my0,mw,mh,rgb(255,255,255));
        dstr(mx0+24,my0+18,"Devices",rgb(90,100,120),1);
        long used=0; for(int k=0;k<FS_N;k++)if(FS[k].used)used+=FS[k].len; long total=fx_disk_total(); { u64 t4=0,f4=0; n4_usage(&t4,&f4); if(t4){ total=(long)t4; used=(long)(t4-f4); } } if(used>total)used=total;
        int tw=(mw-72)/2; if(tw>330)tw=330; if(tw<220){ tw=mw-48; } int th=82;
        int tx=mx0+24, ty=my0+46; g_drv_n=0; g_drv_w=tw;
        int h0=(mx>=tx&&mx<tx+tw&&my>=ty&&my<ty+th); draw_drive_tile(tx,ty,tw,th,"NoovexOS (C:)",used,total,0,h0);
        g_drv_x[g_drv_n]=tx; g_drv_y[g_drv_n]=ty; g_drv_tgt[g_drv_n]=98; g_drv_n++;   /* 98 = open folder tree */
        int cx2 = (tw>=mw-48)? tx : tx+tw+24, cy2=(tw>=mw-48)? ty+th+16 : ty;
        if(g_nvx_signedin){ int h1=(mx>=cx2&&mx<cx2+tw&&my>=cy2&&my<cy2+th); draw_drive_tile(cx2,cy2,tw,th,"Cloud (N:)",0,0,1,h1);
            g_drv_x[g_drv_n]=cx2; g_drv_y[g_drv_n]=cy2; g_drv_tgt[g_drv_n]=2; g_drv_n++; }
        else { dstr(cx2+4,cy2+th/2-4,"Sign in for cloud storage (nvxlogin)",rgb(150,158,176),1); }
        if(msd_ok&&ufat_ok){                            /* USB stick as a removable-drive tile */
            int ux, uy; if(tw>=mw-48){ ux=tx; uy=cy2+th+16; } else { ux=tx; uy=ty+th+16; }
            long uused=0; for(int k=0;k<ufs_n;k++)uused+=ufs[k].size; long utot=(long)(msd_lastlba+1)*512L; if(uused>utot)uused=utot;
            int hu=(mx>=ux&&mx<ux+tw&&my>=uy&&my<uy+th); draw_drive_tile(ux,uy,tw,th,"USB Drive (E:)",uused,utot,2,hu);
            g_drv_x[g_drv_n]=ux; g_drv_y[g_drv_n]=uy; g_drv_tgt[g_drv_n]=99; g_drv_n++;   /* tgt 99 = open USB view */
        }
        return;
    }
    fill_rect(pvx,wy+36,1,wh-38,rgb(228,230,236));
    int lxs=lx0+14, y=wy+50; fl_n=0; fl_x=lx0+4; fl_w=listw-8;
    for(int k=0;k<FS_N;k++) if(FS[k].used && files_show(FS[k].folder)){
        int sel=(k==g_fsel);
        if(sel) fill_round(fl_x,y-4,fl_w,26,5,15,mix(ACC,rgb(255,255,255),200));
        else if(mx>=fl_x&&mx<fl_x+fl_w&&my>=y-4&&my<y+22) fill_round(fl_x,y-4,fl_w,26,5,15,rgb(236,241,250));
        mini_doc(lxs,y-2); dstr(lxs+26,y,FS[k].name,INK,1);
        fl_y[fl_n]=y; fl_idx[fl_n]=k; fl_n++; y+=30; }
    if(fl_n==0) dstr(lx0+18,wy+52,"(empty)",rgb(150,158,176),1);
    int px0=pvx+18, py0=wy+50, pw=ww-(pvx-wx)-32;
    if(g_fsel>=0 && FS[g_fsel].used){
        FsFile*f=&FS[g_fsel]; int kind=fkind(g_fsel);
        fill_round(px0,py0,40,52,4,15,rgb(246,248,252)); for(int k=0;k<6;k++)fill_rect(px0+8,py0+10+k*6,24,2,rgb(196,204,220));
        dstr(px0+54,py0+6,f->name,INK,2);
        { char b[64],*p=b; const char*tn=(kind==1)?"Image":(kind==4)?"ZIP archive":(char*)file_type_name(f->name); p=scopy(p,(char*)tn); p=scopy(p,"  -  "); p=udec(p,(u32)f->len); scopy(p," bytes"); dstr(px0+54,py0+34,b,rgb(120,128,146),1); }
        const char* btn=(kind==1)?"Open Image":kind==2?"Open in Browser":kind==0?"Open in Editor":0;
        if(btn){ fpv_edx=px0+54; fpv_edy=py0+52; fpv_edw=118; fpv_edh=30; fill_round(fpv_edx,fpv_edy,fpv_edw,fpv_edh,7,15,ACC); dstr_ctr(fpv_edx+fpv_edw/2,fpv_edy+8,btn,rgb(255,255,255),1); }
        else fpv_edw=0;
        fill_rect(px0,py0+96,pw,1,rgb(232,234,240));
        int ph2=wy+wh-14-(py0+108);
        if(kind==1){ if(ensure_img(g_fsel)&&view_w>0) blit_img_fit(px0,py0+108,pw,ph2); else dstr(px0,py0+112,"(cannot decode image)",rgb(178,120,120),1); }
        else if(kind==4){ zip_open(g_fsel); ze_n=0;
            if(znent==0){ dstr(px0,py0+112,"Not a ZIP archive, or its directory is missing.",rgb(120,128,146),1); }
            else { char hb[40],*p=hb; p=scopy(p,"ZIP - "); p=udec(p,(u32)znent); scopy(p," entries  (click to open)"); dstr(px0,py0+108,hb,rgb(90,100,120),1);
                int ty=py0+130, maxy=wy+wh-14;
                for(int e=0;e<znent && ty<maxy;e++){ int sel=(e==zsel);
                    if(sel) fill_round(px0-4,ty-3,pw+4,22,4,15,mix(ACC,rgb(255,255,255),204));
                    else if(mx>=px0-4&&mx<px0+pw&&my>=ty-3&&my<ty+19) fill_round(px0-4,ty-3,pw+4,22,4,15,rgb(236,241,250));
                    const char*nm=zent[e].name; int isdir=0; { int q=0; while(nm[q])q++; if(q>0&&(nm[q-1]=='/'||nm[q-1]=='\\'))isdir=1; }
                    fill_round(px0,ty+2,10,12,2,15,isdir?rgb(224,184,96):rgb(150,180,230));
                    char en[44]; int en_n=0; for(int q=0;nm[q]&&en_n<43;q++)en[en_n++]=nm[q]; en[en_n]=0;
                    dstr(px0+16,ty,en,isdir?rgb(120,100,60):INK,1);
                    { char sb[16],*p=sb; p=udec(p,zent[e].usize); *p=0; dstr(px0+pw-slen(sb)*8-2,ty,sb,rgb(150,158,176),1); }
                    ze_y[e]=ty; ze_n++; ty+=22; } } }
        else if(kind==3){ dstr(px0,py0+112,"Binary file - preview and opening not supported.",rgb(120,128,146),1); }
        else { int ty=py0+108, col=pw/8; if(col>127)col=127; int maxy=wy+wh-14, cx2=0; char lb[128];
            for(int q=0; f->data[q] && ty<maxy; q++){ char ch=f->data[q];
                if(ch=='\n'||cx2>=col){ lb[cx2]=0; dstr(px0,ty,lb,rgb(60,66,84),1); ty+=16; cx2=0; if(ch=='\n')continue; }
                if(ch=='\r')continue; if(ch=='\t')ch=' '; if((unsigned char)ch<32)ch='.'; if(cx2<127)lb[cx2++]=ch; }
            if(cx2>0&&ty<maxy){ lb[cx2]=0; dstr(px0,ty,lb,rgb(60,66,84),1); } }
    } else dstr_ctr(pvx+(ww-(pvx-wx))/2, wy+wh/2-10, "Select a file to preview", rgb(150,158,176),1);
}
void files_click(int px,int py){
    if(!g_files_tree&&!g_files_usb){ fx_click(px,py); return; }
    if(g_files_tree){
        int wx=WN[W_FILES].x, wy=WN[W_FILES].y+TITLEH, ww=WN[W_FILES].w;
        if(in_rect(px,py,wx+10,wy+7,42,22)){ if(g_cwd==0){ g_files_tree=0; g_filesnav=4; } else { int p=FTREE[g_cwd].parent; g_cwd=(p<0)?0:p; } return; }   /* back */
        for(int r=0;r<ftrow_n;r++) if(px>=wx+8&&px<wx+ww-8&&py>=ftrow_y[r]-2&&py<ftrow_y[r]+24){
            if(ftrow_isfile[r]==2){ int fi=ftrow_node[r]; int L=0; while(FFILE[fi].text[L])L++; editor_open_buf(FFILE[fi].text,L); }   /* curated file */
            else if(ftrow_isfile[r]==1){ int k=ftrow_node[r]; g_fsel=k; g_fdrag=1; g_fdrag_slot=k; g_fdrag_x=px; g_fdrag_y=py; }   /* NVXFS file: begin drag (open on release if not moved) */
            else g_cwd=ftrow_node[r];                                                                        /* enter subfolder */
            return; }
        return;
    }
    if(g_files_usb){
        int wx=WN[W_FILES].x, wy=WN[W_FILES].y+TITLEH, ww=WN[W_FILES].w, wh=WN[W_FILES].h-TITLEH;
        if(px>=wx+10&&px<wx+96&&py>=wy+7&&py<wy+30){ g_files_usb=0; return; }   /* back to This PC */
        if(ufat_ok){ int y=wy+50; for(int k=0;k<ufs_n && y<wy+wh-24;k++){
            if(px>=wx+8&&px<wx+ww-8&&py>=y-2&&py<y+22){                        /* open a file in the editor */
                u32 sz=ufs[k].size; if(sz>(256u<<20)){ toast("This file is too large to open from the USB stick"); return; }
                char* buf=kmalloc(sz+1); if(!buf){ toast("Not enough memory"); return; }
                int n=ufat_read(k,buf,(int)sz); if(n<0){ kfree(buf); toast("Could not read the file from the USB stick"); return; }
                int s=fs_save_bin(ufs[k].name,(const u8*)buf,n,1); kfree(buf);
                if(s<0){ toast("Could not copy the file"); return; }
                { char b[96],*p=b; p=scopy(p,"Opened "); p=scopy(p,ufs[k].name); p=scopy(p," (copied to Downloads)"); *p=0; toast(b); }
                g_files_usb=0; fx_open_file(s); return; }
            y+=26; } }
        return;
    }
    int wx=WN[W_FILES].x, wy=WN[W_FILES].y+TITLEH, ww=WN[W_FILES].w, sbw=150;
    int pvx=wx+sbw+232, px0=pvx+18, pw=ww-(pvx-wx)-32;
    for(int k=0;k<5;k++){ int iy=wy+46+k*30; if(in_rect(px,py,wx+6,iy,sbw-12,26)){ if(g_filesnav!=k){ g_filesnav=k; g_fsel=-1; } return; } }
    if(g_filesnav==4){ for(int k=0;k<g_drv_n;k++) if(in_rect(px,py,g_drv_x[k],g_drv_y[k],g_drv_w,82)){ if(g_drv_tgt[k]==99){ g_files_usb=1; } else if(g_drv_tgt[k]==98){ g_files_tree=1; g_cwd=0; } else { g_filesnav=g_drv_tgt[k]; g_fsel=-1; } return; } return; }
    if(g_fsel>=0 && FS[g_fsel].used && fkind(g_fsel)==4){
        for(int e=0;e<ze_n;e++) if(px>=px0-4 && px<px0+pw && py>=ze_y[e]-3 && py<ze_y[e]+19){ zsel=e;
            const char*nm=zent[e].name; int q=0; while(nm[q])q++; if(q>0&&(nm[q-1]=='/'||nm[q-1]=='\\')) return;   /* directory */
            int L=zip_extract(g_fsel,e,(u8*)jpeg_scratch,jpeg_scratch_cap());
            if(L<0) return;
            const char*base=nm; for(int r=0;nm[r];r++) if(nm[r]=='/'||nm[r]=='\\') base=nm+r+1;
            int k2=file_kind(base);
            if(k2==0||k2==2){ editor_open_buf((const char*)jpeg_scratch, L); }
            else { int slot=fs_save_bin(base,(u8*)jpeg_scratch,L,1); if(slot>=0){ if(k2==1) g_imgview=slot; else { g_fsel=slot; g_filesnav=3; } } }
            return; } }
    if(g_fsel>=0 && FS[g_fsel].used && fpv_edw>0 && in_rect(px,py,fpv_edx,fpv_edy,fpv_edw,fpv_edh)){
        { int kd=fkind(g_fsel); { extern int sysfiles_open(int); if(sysfiles_open(g_fsel))return; } if(fext_eq(FS[g_fsel].name,"pptx")){ pptx_load(g_fsel); open_win(W_PPTX); } else if(video_is_media_name(FS[g_fsel].name)) video_open_slot(g_fsel); else if(kd==1)g_imgview=g_fsel; else if(kd==2)browser_show_html(fsd(g_fsel),FS[g_fsel].len,FS[g_fsel].name); else { editor_open(g_fsel); if(fext_eq(FS[g_fsel].name,"nvx"))editor_run(); } } return; }
    for(int k=0;k<fl_n;k++) if(px>=fl_x&&px<fl_x+fl_w&&py>=fl_y[k]-4&&py<fl_y[k]+22){ g_fsel=fl_idx[k]; return; }
}
void about_draw(int i){
    win_frame(i,"About NoovexOS");
    int x=WN[i].x+28,y=WN[i].y+TITLEH+24;
    draw_logo(WN[i].x+70, y, 60, ACC, 256);
    dstr(x+90,y+6,"NoovexOS 64",INK,3); dstr(x+90,y+46,"Version 10.60  -  from scratch",rgb(90,100,120),1);
    y+=96; dstr(x,y,"A hobby operating system in C and assembly.",INK,1); y+=22;
    dstr(x,y,"Native x86-64 long mode. No external kernel code.",INK,1); y+=22;
    char b[64],*p=b;p=scopy(p,"CPU: ");p=scopy(p,g_brand);*p=0; dstr(x,y,b,INK,1);
}
int gb_x,gb_y,gb_w,gb_h;
void welcome_draw(int i){
    win_frame(i,"NoovexOS 64  -  Welcome");
    int x=WN[i].x+26,y=WN[i].y+TITLEH+22;
    { const char* hd=(slen("Native x86-64 desktop  -  booted from scratch")*16+52<=WN[i].w)?"Native x86-64 desktop  -  booted from scratch":"Native x86-64 desktop"; dstr(x,y,hd,INK,2); } y+=40;
    char L[96],*p; p=L;p=scopy(p,"CPU:  ");p=scopy(p,g_brand);*p=0; dstr(x,y,L,INK,1); y+=22;
    p=L;p=scopy(p,"Display:  ");p=udec(p,W);*p++='x';p=udec(p,H);p=scopy(p,"  @ ");p=udec(p,BPP);p=scopy(p,"-bit");*p=0; dstr(x,y,L,INK,1); y+=22;
    p=L;p=scopy(p,"Long mode:  active   EFER.LMA=");*p++=(char)('0'+g_lma);*p=0; dstr(x,y,L,INK,1); y+=28;
    dstr(x,y,"Click N for the app launcher. Right-click for menus.",rgb(90,100,120),1); y+=18;
    dstr(x,y,"Drag windows to the edges to snap; Alt+Tab switches apps.",rgb(90,100,120),1); y+=26;
    gb_x=x;gb_y=WN[i].y+WN[i].h-54;gb_w=210;gb_h=38;
    if(g_dstyle==1){ ui_bevel(gb_x,gb_y,gb_w,gb_h,mouse_btn&&mx>=gb_x&&mx<gb_x+gb_w&&my>=gb_y&&my<gb_y+gb_h); dstr_ctr(gb_x+gb_w/2,gb_y+11,"Open Terminal",rgb(0,0,0),1); return; }
    fill_round(gb_x,gb_y,gb_w,gb_h,8,15,ACC);
    dstr_ctr(gb_x+gb_w/2,gb_y+6,"Open Terminal",rgb(255,255,255),2);
}

u8 g_ficon[FS_N], g_dicon[DN_MAX], g_aicon[NWIN];
static int g_icons_loaded;
static const char* const ICN[12]={"Folder","Document","Code","Picture","Archive","Settings","Program","Web page","Red folder","Green folder","Blue folder","Purple folder"};
const char* icon_choice_name(int ic){ return ic>=0&&ic<12?ICN[ic]:"Default"; }
void icon_choice_draw(int x,int y,int s,int ic){
    if(ic>=8&&ic<12){ static const u32 FC[4][3]={{0x8C2020,0xD85050,0xF08080},{0x206C30,0x44B060,0x80D898},{0x204C9C,0x4C84D8,0x88B4F0},{0x5A2C8C,0x9058C8,0xC090E8}}; const u32* c=FC[ic-8];
        int h=s*3/4, t=y+s/5; fill_rect(x,y+s/8,s*2/5,s/6,c[0]); fill_rect(x+1,y+s/8+1,s*2/5-2,s/6,c[1]); fill_rect(x,t,s,h,c[0]); fill_rect(x+1,t+1,s-2,h-2,c[1]); fill_rect(x+1,t+s/8,s-2,h-s/8-1,c[2]); return; }
    fx_icon(x,y,s,ic<0?1:ic);
}
void icons_save(void){ extern void nvx_mark_file(const char*,const char*); static char t[16384]; char* p=t;
    for(int i=0;i<FS_N&&p<t+16300;i++) if(g_ficon[i]&&FS[i].used){ *p++='f'; p=udec(p,(u32)i); *p++=' '; p=udec(p,(u32)g_ficon[i]); *p++='\n'; }
    for(int i=0;i<DN_MAX&&p<t+16300;i++) if(g_dicon[i]&&DT[i].used){ *p++='d'; p=udec(p,(u32)i); *p++=' '; p=udec(p,(u32)g_dicon[i]); *p++='\n'; }
    for(int i=0;i<NWIN&&p<t+16300;i++) if(g_aicon[i]){ *p++='a'; p=udec(p,(u32)i); *p++=' '; p=udec(p,(u32)g_aicon[i]); *p++='\n'; }
    *p=0; nvx_mark_file("/etc/nvx-icons",t); }
void icons_load(void){ if(g_icons_loaded)return; g_icons_loaded=1; static char t[16384]; int n=ux_read_file("/etc/nvx-icons",t,sizeof t); if(n<=0)return;
    const char* p=t; while(*p){ char k=*p++; u32 a=0,v=0; while(*p>='0'&&*p<='9')a=a*10+(u32)(*p++-'0'); if(*p==' ')p++; while(*p>='0'&&*p<='9')v=v*10+(u32)(*p++-'0'); while(*p&&*p!='\n')p++; if(*p)p++;
        if(v>12)continue; if(k=='f'&&a<FS_N)g_ficon[a]=(u8)v; else if(k=='d'&&a<DN_MAX)g_dicon[a]=(u8)v; else if(k=='a'&&a<NWIN)g_aicon[a]=(u8)v; } }
int item_icon_kind(int isd,int id){ icons_load(); if(isd){ if(id>=0&&id<DN_MAX&&g_dicon[id])return g_dicon[id]-1; return 0; } if(id>=0&&id<FS_N&&g_ficon[id])return g_ficon[id]-1; return fx_ikind(id); }
static void fx_icon_item(int x,int y,int s,int isd,int id){ icon_choice_draw(x,y,s,item_icon_kind(isd,id)); }
void fx_dir_stats(int d,int* nf,int* nd,u64* bytes){ for(int q=0;q<DN_MAX;q++) if(DT[q].used&&q!=d&&DT[q].parent==d){ (*nd)++; if(*nd<4000)fx_dir_stats(q,nf,nd,bytes); }
    for(int q=0;q<FS_N;q++) if(FS[q].used&&FS[q].dir==d){ (*nf)++; *bytes+=(u64)FS[q].len; } }
int fx_sel_dir(void){ return fx_seldir; }

