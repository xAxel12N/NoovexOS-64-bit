static u32 dm_crc32(const u8* p,u32 n){ u32 c=0xFFFFFFFFu; for(u32 i=0;i<n;i++){ c^=p[i]; for(int k=0;k<8;k++)c=(c>>1)^(0xEDB88320u&(0u-(c&1u))); } return ~c; }
static int dm_guid_is(const u8* g,u32 a,u16 b,u16 c,u8 d0,u8 d1,u8 d2,u8 d3,u8 d4,u8 d5,u8 d6,u8 d7){
    return *(const u32*)g==a&&*(const u16*)(g+4)==b&&*(const u16*)(g+6)==c&&g[8]==d0&&g[9]==d1&&g[10]==d2&&g[11]==d3&&g[12]==d4&&g[13]==d5&&g[14]==d6&&g[15]==d7; }
static const char* dm_gpt_type(const u8* g){
    if(dm_guid_is(g,0xC12A7328u,0xF81F,0x11D2,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B))return "EFI system";
    if(dm_guid_is(g,0xEBD0A0A2u,0xB9E5,0x4433,0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7))return "Windows data";
    if(dm_guid_is(g,0xE3C9E316u,0x0B5C,0x4DB8,0x81,0x7D,0xF9,0x2D,0xF0,0x02,0x15,0xAE))return "Microsoft reserved";
    if(dm_guid_is(g,0xDE94BBA4u,0x06D1,0x4D40,0xA1,0x6A,0xBF,0xD5,0x01,0x79,0xD6,0xAC))return "Windows recovery";
    if(dm_guid_is(g,0x0FC63DAFu,0x8483,0x4772,0x8E,0x79,0x3D,0x69,0xD8,0x47,0x7D,0xE4))return "Linux";
    if(dm_guid_is(g,0x0657FD6Du,0xA4AB,0x43C4,0x84,0xE5,0x09,0x33,0xC8,0x4B,0x4F,0x4F))return "Linux swap";
    if(dm_guid_is(g,0x21686148u,0x6449,0x6E6F,0x74,0x4E,0x65,0x65,0x64,0x45,0x46,0x49))return "BIOS boot";
    return "GPT partition"; }
static const char* dm_mbr_type(u8 t){ switch(t){ case 0x07: return "NTFS/exFAT"; case 0x0B: case 0x0C: return "FAT32"; case 0x0E: case 0x06: case 0x04: return "FAT16"; case 0x01: return "FAT12"; case 0x83: return "Linux"; case 0x82: return "Linux swap"; case 0x05: case 0x0F: return "Extended"; case 0xEF: return "EFI system"; case 0x27: return "Windows recovery"; case NVX_PTYPE: return "NoovexOS"; default: return "Partition"; } }
static void dm_cp(char* d,const char* s,int n){ int i=0; for(;s[i]&&i<n-1;i++)d[i]=s[i]; d[i]=0; }
static void dm_detect_fs(dmpart* d){
    d->fs[0]=0; u8 s[512]; if(gdisk_read(d->start,1,s))return;
    if(s[3]=='N'&&s[4]=='T'&&s[5]=='F'&&s[6]=='S'){ dm_cp(d->fs,"NTFS",24); return; }
    if(s[3]=='E'&&s[4]=='X'&&s[5]=='F'&&s[6]=='A'&&s[7]=='T'){ dm_cp(d->fs,"exFAT",24); return; }
    if(s[82]=='F'&&s[83]=='A'&&s[84]=='T'&&s[85]=='3'&&s[86]=='2'){ dm_cp(d->fs,"FAT32",24); char* p=d->fs+5; char v[12]; int k=0; for(;k<11;k++)v[k]=(char)s[71+k]; v[11]=0; int z=10; while(z>=0&&v[z]==' ')v[z--]=0; if(v[0]&&v[0]!=' '){ p=scopy(p," \""); p=scopy(p,v); *p++='"'; *p=0; } return; }
    if(s[54]=='F'&&s[55]=='A'&&s[56]=='T'){ dm_cp(d->fs,"FAT16",24); return; }
    if(*(u32*)s==0x3458564Eu){ dm_cp(d->fs,"NoovexOS NVX4",24); return; }
    if(d->size>4&&gdisk_read(d->start+2,1,s)==0&&s[56]==0x53&&s[57]==0xEF){ dm_cp(d->fs,"ext4",24); return; }
}
static void dm_load(void){
    gdisk_ensure(); g_dm_np=0; g_dm_gpt=0; for(int i=0;i<512;i++)g_dm_mbr[i]=0;
    for(int i=0;i<DM_ROWS;i++){ g_dm_p[i].used=0; g_dm_p[i].name[0]=0; g_dm_p[i].fs[0]=0; g_dm_p[i].gi=-1; }
    if(g_dktype==DK_NONE)return; gdisk_read(0,1,g_dm_mbr);
    int valid=(g_dm_mbr[510]==0x55&&g_dm_mbr[511]==0xAA), prot=0; u32 R=dk_ratio();
    if(valid) for(int e=0;e<4;e++) if(g_dm_mbr[446+e*16+4]==0xEE)prot=1;
    if(prot&&gdisk_read(1*R,1,g_dm_ghdr)==0&&g_dm_ghdr[0]=='E'&&g_dm_ghdr[1]=='F'&&g_dm_ghdr[2]=='I'&&g_dm_ghdr[3]==' '){
        g_dm_gpt=1; g_dm_gelba=*(u64*)(g_dm_ghdr+72); g_dm_gn=*(u32*)(g_dm_ghdr+80); g_dm_gsz=*(u32*)(g_dm_ghdr+84); g_dm_galt=*(u64*)(g_dm_ghdr+32);
        if(g_dm_gn>256)g_dm_gn=256; if(g_dm_gsz<128||g_dm_gsz>512)g_dm_gsz=128;
        g_dm_gsec=(g_dm_gn*g_dm_gsz+511)/512; if(g_dm_gent)kfree(g_dm_gent); g_dm_gent=(u8*)kmalloc((u64)g_dm_gsec*512); if(!g_dm_gent){ g_dm_gpt=0; return; }
        if(gdisk_readn((u32)(g_dm_gelba*R),(int)g_dm_gsec,g_dm_gent)){ g_dm_gpt=0; return; }
        for(u32 i=0;i<g_dm_gn&&g_dm_np<DM_ROWS;i++){ const u8* e=g_dm_gent+(u64)i*g_dm_gsz; int z=1; for(int k=0;k<16;k++) if(e[k])z=0; if(z)continue;
            u64 f=*(const u64*)(e+32), l=*(const u64*)(e+40); if(l<f)continue; dmpart* d=&g_dm_p[g_dm_np++];
            u64 st=f*R, sz=(l-f+1)*R; d->used=1; d->type=0xEE; d->start=st>0xFFFFFFFFULL?0xFFFFFFFFu:(u32)st; d->size=sz>0xFFFFFFFFULL?0xFFFFFFFFu:(u32)sz; d->gi=(int)i;
            int o=0; for(int k=0;k<36&&o<39;k++){ u16 ch=*(const u16*)(e+56+k*2); if(!ch)break; d->name[o++]=(ch>=32&&ch<127)?(char)ch:'?'; } d->name[o]=0;
            if(!d->name[0])dm_cp(d->name,dm_gpt_type(e),40); else { char t[40]; int q=0; const char* tn=dm_gpt_type(e); for(;tn[q]&&q<39;q++)t[q]=tn[q]; t[q]=0; (void)t; }
            if(st<0xFFFFFFFFULL)dm_detect_fs(d); }
        return; }
    for(int e=0;e<4;e++){ u8* pe=g_dm_mbr+446+e*16; u8 ty=valid?pe[4]:0; u32 st=*(u32*)(pe+8)*R, sz=*(u32*)(pe+12)*R;
        dmpart* d=&g_dm_p[e]; d->used=(valid&&ty!=0&&sz!=0); d->type=ty; d->start=st; d->size=sz; d->gi=e; if(d->used){ dm_cp(d->name,dm_mbr_type(ty),40); dm_detect_fs(d); } }
    g_dm_np=4;
}
static u32 dm_free_start(void){
    u32 top=2048; for(int e=0;e<4;e++) if(g_dm_p[e].used){ u32 end=g_dm_p[e].start+g_dm_p[e].size; if(end>top)top=end; }
    return (top+2047u)&~2047u;
}
static void dm_create(void){
    if(g_dm_gpt){ g_dm_msg=1; g_dm_msgtext="This disk uses GPT. Delete partitions or erase the disk, then Install whole disk."; return; }
    int slot=-1; for(int e=0;e<4;e++) if(!g_dm_p[e].used){ slot=e; break; } if(slot<0){ g_dm_msg=1; g_dm_msgtext="No free partition slots (max 4)."; return; }
    u32 st=dm_free_start(); if(st>=g_dksectors){ g_dm_msg=1; g_dm_msgtext="No free space on the disk."; return; }
    u32 sz=g_dksectors-st; sz-=sz%dk_ratio(); g_dm_p[slot].used=1; g_dm_p[slot].type=0x0C; g_dm_p[slot].start=st; g_dm_p[slot].size=sz;
    u8* pe=g_dm_mbr+446+slot*16; for(int i=0;i<16;i++)pe[i]=0; pe[4]=0x0C; pe[5]=0xFE;pe[6]=0xFF;pe[7]=0xFF; *(u32*)(pe+8)=st/dk_ratio(); *(u32*)(pe+12)=sz/dk_ratio();
    g_dm_mbr[510]=0x55; g_dm_mbr[511]=0xAA; gdisk_write(0,g_dm_mbr); gdisk_flush(); dm_load(); g_dm_sel=slot;
    g_dm_msg=1; g_dm_msgtext="Partition created.";
}
static int dm_gpt_write_side(u64 hdr_lba,const u8* hdr_in,u64 ent_lba){
    u32 R=dk_ratio(); u8 h[512]; for(int i=0;i<512;i++)h[i]=hdr_in[i];
    u32 crc=dm_crc32(g_dm_gent,g_dm_gn*g_dm_gsz); *(u32*)(h+88)=crc; u32 hs=*(u32*)(h+12); if(hs<92||hs>512)hs=92; *(u32*)(h+16)=0; *(u32*)(h+16)=dm_crc32(h,hs);
    for(u32 s=0;s<g_dm_gsec;s++) if(gdisk_write((u32)(ent_lba*R)+s,g_dm_gent+(u64)s*512))return -1;
    return gdisk_write((u32)(hdr_lba*R),h); }
static void dm_delete_do(void){
    dmpart* d=&g_dm_p[g_dm_sel];
    if(g_dm_gpt){ u8* e=g_dm_gent+(u64)d->gi*g_dm_gsz; for(u32 k=0;k<g_dm_gsz;k++)e[k]=0;
        int r=dm_gpt_write_side(1,g_dm_ghdr,g_dm_gelba);
        u8 alt[512]; u32 R=dk_ratio(); if(g_dm_galt&&g_dm_galt*R<g_dksectors&&gdisk_read((u32)(g_dm_galt*R),1,alt)==0&&alt[0]=='E'&&alt[1]=='F'&&alt[2]=='I')r|=dm_gpt_write_side(g_dm_galt,alt,*(u64*)(alt+72));
        gdisk_flush(); dm_load(); g_dm_msg=1; g_dm_msgtext=r?"Writing the partition table failed.":"Partition deleted."; g_dm_sel=-1; return; }
    u8* pe=g_dm_mbr+446+d->gi*16; for(int i=0;i<16;i++)pe[i]=0; g_dm_mbr[510]=0x55; g_dm_mbr[511]=0xAA;
    gdisk_write(0,g_dm_mbr); gdisk_flush(); dm_load(); g_dm_msg=1; g_dm_msgtext="Partition deleted."; g_dm_sel=-1;
}
static void dm_erase_do(void){
    u8 z[512]; for(int i=0;i<512;i++)z[i]=0; u32 R=dk_ratio(); int bad=0;
    for(u32 s=0;s<64u*R&&s<g_dksectors;s++) if(gdisk_write(s,z))bad=1;
    if(g_dksectors>64u*R) for(u32 s=g_dksectors-64u*R;s<g_dksectors;s++) if(gdisk_write(s,z))bad=1;
    gdisk_flush(); dm_load(); g_dm_sel=-1; g_dm_msg=1; g_dm_msgtext=bad?"Erasing failed - the disk reported an error.":"Disk erased. It has no partitions now.";
}
static char* dm_size(char* p,u64 sec){ u64 mb=sec*512/1048576ULL; if(mb>=1024*1024){ p=udec(p,(u32)(mb/(1024*1024))); *p++='.'; p=udec(p,(u32)((mb%(1024*1024))*10/(1024*1024))); return scopy(p," TB"); }
    if(mb>=1024){ p=udec(p,(u32)(mb/1024)); *p++='.'; p=udec(p,(u32)((mb%1024)*10/1024)); return scopy(p," GB"); } p=udec(p,(u32)mb); return scopy(p," MB"); }
static void dm_delete(void){
    if(g_dm_sel<0||g_dm_sel>=g_dm_np||!g_dm_p[g_dm_sel].used){ g_dm_msg=1; g_dm_msgtext="Select a partition to delete."; return; }
    char* p=g_dm_ctext; p=scopy(p,"Delete \""); p=scopy(p,g_dm_p[g_dm_sel].name); p=scopy(p,"\" ("); p=dm_size(p,g_dm_p[g_dm_sel].size); p=scopy(p,")? Its data will be lost."); *p=0; g_dm_confirm=1; }
static void dm_erase(void){ char* p=g_dm_ctext; p=scopy(p,"Erase the whole disk ("); p=dm_size(p,g_dksectors); p=scopy(p,")? All partitions and data will be lost."); *p=0; g_dm_confirm=2; }
static void dm_next_disk(void){
    extern int gdisk_count(void); extern int gdisk_current(void); extern int gdisk_pick(int); extern int disk_ok; extern int g_nvx_generic;
    int n=gdisk_count(); if(n<2){ g_dm_msg=1; g_dm_msgtext="Only one disk was found."; return; }
    int was=gdisk_current(); int nx=(was+1)%n; if(gdisk_pick(nx)>0){ if(disk_ok&&(g_nvx_generic||g_dktype==DK_IDE))disk_ok=0; }
    g_dm_sel=-1; dm_load(); }
void dm_present(void){
    extern int gdisk_count(void); extern int gdisk_current(void);
    for(u32 yy=0;yy<H;yy++)for(u32 xx=0;xx<W;xx++)SCENE[(u64)yy*SW+xx]=rgb(18,26,44);
    dstr((int)W*8/100,(int)H*5/100,"Disk Manager",rgb(240,246,252),3);
    char b[160],*p;
    p=b;p=scopy(p,"Disk ");p=udec(p,(u32)(gdisk_current()+1));p=scopy(p," of ");p=udec(p,(u32)gdisk_count());p=scopy(p,": ");p=scopy(p,(char*)gdisk_name());
    if(g_dksectors){ p=scopy(p,"   "); p=dm_size(p,g_dksectors); } else p=scopy(p,"  (none detected)");
    p=scopy(p,g_dktype==DK_NONE?"":g_dm_gpt?"   GPT partition table":(g_dm_mbr[510]==0x55&&g_dm_mbr[511]==0xAA)?"   MBR partition table":"   no partition table"); *p=0;
    dstr((int)W*8/100,(int)H*5/100+40,b,rgb(150,175,215),1);
    int lx=(int)W*8/100, ly=(int)H*14/100, lw=(int)W*84/100, rh=40;
    dstr(lx,ly-22,"Partitions:",rgb(150,175,215),1);
    int rows=g_dm_gpt?g_dm_np:4; if(rows<1&&g_dm_gpt)rows=1;
    for(int e=0;e<DM_ROWS;e++)dm_rowy[e]=-1000;
    for(int e=0;e<rows&&e<DM_ROWS;e++){ int ry=ly+e*(rh+6); dm_rowy[e]=ry; int selr=(g_dm_sel==e);
        int hov=(mx>=lx&&mx<lx+lw&&my>=ry&&my<ry+rh)||selr;
        fill_round(lx,ry,lw,rh,8,15, selr?rgb(40,90,150):hov?rgb(30,44,72):rgb(26,36,58));
        dmpart* d=&g_dm_p[e]; p=b; p=udec(p,(u32)(e+1)); p=scopy(p,".  ");
        if(d->used){ p=scopy(p,d->name); if(d->fs[0]){ p=scopy(p,"   ["); p=scopy(p,d->fs); *p++=']'; } p=scopy(p,"   "); p=dm_size(p,d->size); p=scopy(p,"   @ LBA "); p=udec(p,d->start);
            if(!g_dm_gpt&&(g_dm_mbr[446+d->gi*16]&0x80))p=scopy(p,"  [active]"); }
        else p=scopy(p,g_dm_gpt?"(no partitions)":"(empty)");
        *p=0; dstr(lx+16,ry+12,b, d->used?rgb(232,240,252):rgb(120,132,152),1); }
    int ny=ly+(rows<4?4:rows)*(rh+6)+14; dstr(lx,ny,"Volume name:",rgb(150,175,215),1);
    int nfx=lx+130, nfw=260; fill_round(nfx,ny-6,nfw,28,6,15, g_dm_editname?rgb(255,255,255):rgb(40,52,80));
    p=b;p=scopy(p,g_dm_name); if(g_dm_editname&&((g_ticks/50)&1))*p++='_'; *p=0; dstr(nfx+10,ny,b, g_dm_editname?rgb(20,28,44):rgb(232,240,252),1);
    dm_bx[5]=nfx;dm_by[5]=ny-6;dm_bw[5]=nfw;
    const char* bl[8]={"Create partition","Delete partition","Install to selected","Install whole disk","Back","","Erase disk","Next disk"};
    u32 bcol[8]={rgb(60,120,80),rgb(150,70,70),rgb(50,110,190),rgb(150,110,50),rgb(60,70,92),0,rgb(170,50,50),rgb(70,90,130)};
    static const int ord[7]={0,1,6,7,2,3,4};
    int byb=ny+46, bxs=lx;
    for(int q=0;q<7;q++){ int k=ord[q]; int bw2=(k==2||k==3)?200:(k==4?100:160); if(bxs+bw2>lx+lw){ bxs=lx; byb+=48; }
        int hov=(mx>=bxs&&mx<bxs+bw2&&my>=byb&&my<byb+38);
        fill_round(bxs,byb,bw2,38,8,15, hov?mix(bcol[k],rgb(255,255,255),50):bcol[k]); dstr_ctr(bxs+bw2/2,byb+11,(char*)bl[k],rgb(255,255,255),1);
        dm_bx[k]=bxs; dm_by[k]=byb; dm_bw[k]=bw2; bxs+=bw2+12; }
    if(g_dm_confirm){ int pw=620,ph=140,px=(int)W/2-pw/2,py=(int)H/2-ph/2; shadow_soft(px,py,pw,ph,14); fill_round(px,py,pw,ph,14,15,rgb(250,251,254));
        dstr_ctr((int)W/2,py+34,g_dm_ctext,rgb(150,30,40),1); int bw=140,gap=20,yb=py+ph-50, x1=(int)W/2-bw-gap/2, x2=(int)W/2+gap/2;
        fill_round(x1,yb,bw,34,8,15,rgb(190,50,50)); dstr_ctr(x1+bw/2,yb+10,g_dm_confirm==2?"Erase":"Delete",rgb(255,255,255),1);
        fill_round(x2,yb,bw,34,8,15,rgb(110,118,135)); dstr_ctr(x2+bw/2,yb+10,"Cancel",rgb(255,255,255),1);
        dm_bx[8]=x1; dm_by[8]=yb; dm_bw[8]=bw; dm_bx[4]=x2; dm_by[4]=yb; dm_bw[4]=bw; }
    else if(g_dm_msg){ int pw=640,ph=120,px=(int)W/2-pw/2,py=(int)H/2-ph/2; shadow_soft(px,py,pw,ph,14); fill_round(px,py,pw,ph,14,15,rgb(250,251,254));
        dstr_ctr((int)W/2,py+34,(char*)g_dm_msgtext,rgb(30,36,54),1); int okw=120,okx=(int)W/2-okw/2,oky=py+ph-46;
        fill_round(okx,oky,okw,32,8,15,ACC); dstr_ctr(okx+okw/2,oky+9,"OK",rgb(255,255,255),1); dm_bx[4]=okx;dm_by[4]=oky;dm_bw[4]=okw; }
    draw_cursor(); blit_all();
}
static void dm_do_install(int whole){
    int idx=0;
    if(!whole){ if(g_dm_gpt){ g_dm_msg=1; g_dm_msgtext="On a GPT disk use Install whole disk (it replaces all partitions)."; return; }
        if(g_dm_sel<0||!g_dm_p[g_dm_sel].used){ g_dm_msg=1; g_dm_msgtext="Select a partition first."; return; } idx=g_dm_p[g_dm_sel].gi; }
    ip_open_preset(g_dm_ret==ST_INSTALL?ST_BOOTMENU:g_dm_ret,whole?0:1,idx);
}
static void dm_close(void){ g_dm_open=0; if(g_dm_ret==ST_INSTALL){ ip_load(); g_state=ST_INSTALL; } else g_state=g_dm_ret; }
int dm_input(void){
    int changed=0;
    if(mouse_evt){ int dx,dy,btn; cpu_irq_off(); dx=mouse_dx;dy=mouse_dy;mouse_dx=0;mouse_dy=0;btn=mouse_btn;mouse_evt=0; cpu_irq_on();
        if(dx||dy){ mx+=dx;my-=dy; if(mx<0)mx=0;if(my<0)my=0;if(mx>=(int)W)mx=W-1;if(my>=(int)H)my=H-1; changed=1; }
        if(btn&&!mouse_btn_prev){ changed=1;
            if(g_dm_confirm){ if(in_rect(mx,my,dm_bx[8],dm_by[8],dm_bw[8],34)){ int c=g_dm_confirm; g_dm_confirm=0; if(c==1)dm_delete_do(); else dm_erase_do(); } else if(in_rect(mx,my,dm_bx[4],dm_by[4],dm_bw[4],34)) g_dm_confirm=0; }
            else if(g_dm_msg){ if(in_rect(mx,my,dm_bx[4],dm_by[4],dm_bw[4],32)) g_dm_msg=0; }
            else { g_dm_editname=0;
                for(int e=0;e<DM_ROWS;e++) if(dm_rowy[e]>-1000&&in_rect(mx,my,(int)W*8/100,dm_rowy[e],(int)W*84/100,40)){ g_dm_sel=e; }
                if(in_rect(mx,my,dm_bx[5],dm_by[5],dm_bw[5],28)) g_dm_editname=1;
                else if(in_rect(mx,my,dm_bx[0],dm_by[0],dm_bw[0],38)) dm_create();
                else if(in_rect(mx,my,dm_bx[1],dm_by[1],dm_bw[1],38)) dm_delete();
                else if(in_rect(mx,my,dm_bx[6],dm_by[6],dm_bw[6],38)) dm_erase();
                else if(in_rect(mx,my,dm_bx[7],dm_by[7],dm_bw[7],38)) dm_next_disk();
                else if(in_rect(mx,my,dm_bx[2],dm_by[2],dm_bw[2],38)) dm_do_install(0);
                else if(in_rect(mx,my,dm_bx[3],dm_by[3],dm_bw[3],38)) dm_do_install(1);
                else if(in_rect(mx,my,dm_bx[4],dm_by[4],dm_bw[4],38)) dm_close();
            } }
        mouse_btn_prev=btn;
    }
    int c; while((c=kpop())>=0){ changed=1;
        if(g_dm_confirm){ if(c=='y'||c=='Y'){ int cf=g_dm_confirm; g_dm_confirm=0; if(cf==1)dm_delete_do(); else dm_erase_do(); } else if(c==27||c=='n'||c=='N')g_dm_confirm=0; continue; }
        if(g_dm_msg){ g_dm_msg=0; continue; }
        if(g_dm_editname){ int L=0; while(g_dm_name[L])L++;
            if(c=='\n'){ g_dm_editname=0; } else if(c=='\b'){ if(L>0)g_dm_name[L-1]=0; }
            else if(L<11 && ((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-')){ g_dm_name[L]=(char)c; g_dm_name[L+1]=0; }
            continue; }
        if(c==27){ dm_close(); }
        else if(c==19){ if(g_dm_sel>0)g_dm_sel--; else g_dm_sel=0; } else if(c==20){ int mxr=g_dm_gpt?g_dm_np:4; if(g_dm_sel<mxr-1)g_dm_sel++; }
    }
    return changed;
}
extern const unsigned char _binary_inst_efi_bin_start[], _binary_inst_efi_bin_end[];
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/vfs/dirtree.h"
void nvx_mark_file(const char* path,const char* txt){
    extern void fs_newslot(int); extern int fs_set(int,const char*,long); extern int fs_sync(int); extern int disk_ok;
    int k=k_find_file(path);
    if(k<0){ int d; char base[64]; if(k_split(path,&d,base)<0||!base[0]){ dt_mkdir_at(0,"etc"); if(k_split(path,&d,base)<0||!base[0])return; }
        for(int i=0;i<FS_N;i++) if(!FS[i].used){ k=i; break; } if(k<0)return;
        fs_newslot(k); int q=0; while(base[q]&&q<63){ FS[k].name[q]=base[q]; q++; } FS[k].name[q]=0; FS[k].used=1; FS[k].folder=0; FS[k].dir=d; FS[k].mode=0100644u; FS[k].uid=0; FS[k].gid=0; }
    long n=0; while(txt[n])n++; fs_set(k,txt,n); g_fs_dirty[k]=1; if(disk_ok)fs_sync(k); }
#define INST_BOOTP 1048576u
#define INST_DATAP (((NVX_REGION_SECS)+2047u)&~2047u)
#define KSNAP_MAX 0x6000000u
#define KDATA_MAX 0x400000u
static u8 g_kdata_snap[KDATA_MAX]; static u8* g_kimg_snap=0; static u32 g_kimg_len=0, g_inst_datalen=0, g_kimm_len=0;
#define KPG_MAX (KSNAP_MAX/4096u)
static u32 g_kpg_sum[KPG_MAX];
static u32 kpg_hash(const u8* p,u32 n){ u32 h=2166136261u; for(u32 i=0;i<n;i++){ h^=p[i]; h*=16777619u; } return h; }
void inst_snapshot_data(void){
    extern char __immutable_end[], __blob_end[], __file_end[];
    u32 klen=*(volatile u32*)(u64)(0x100000ULL+24); u32 flen=(u32)((u64)__file_end-0x100000ULL);
    if(klen<4096||klen>KSNAP_MAX)klen=flen; if(klen>KSNAP_MAX)klen=0;
    u32 code=(u32)((u64)__immutable_end-0x100000ULL), imm=(u32)((u64)__blob_end-0x100000ULL); if(imm<code||imm>klen||klen-imm>KDATA_MAX){ g_kimg_len=0; return; }
    const u8* k=(const u8*)0x100000ULL; for(u32 i=imm;i<klen;i++)g_kdata_snap[i-imm]=k[i];
    { extern char boot_pt_first[], boot_pt_last[]; u64 a=(u64)boot_pt_first, e=(u64)boot_pt_last; if(a>=0x100000ULL+imm&&e<=0x100000ULL+klen&&a<e) for(u64 x=a;x<e;x++)g_kdata_snap[x-0x100000ULL-imm]=0; }
    { extern u64 early_idt_image(u64*,u64*); u64 f=0,n=0; u64 a=early_idt_image(&f,&n); if(a>=0x100000ULL+imm&&a+n<=0x100000ULL+klen){ u8* d=g_kdata_snap+(a-0x100000ULL-imm); for(u64 x=0;x<n;x++)d[x]=0; for(int x=0;x<8;x++)d[x]=(u8)(f>>(x*8)); } }
    for(u32 pg=0;pg*4096u<code&&pg<KPG_MAX;pg++){ u32 n=code-pg*4096u; if(n>4096u)n=4096u; g_kpg_sum[pg]=kpg_hash(k+pg*4096u,n); }
    g_kimg_len=klen; g_kimm_len=imm; g_inst_datalen=klen-imm;
}
static const u8* kimg_full(void){
    if(g_kimg_snap)return g_kimg_snap; if(!g_kimg_len)return 0;
    u8* t=(u8*)kmalloc(g_kimg_len); if(!t)return 0; const u8* k=(const u8*)0x100000ULL;
    for(u32 i=0;i<g_kimm_len;i++)t[i]=k[i]; for(u32 i=g_kimm_len;i<g_kimg_len;i++)t[i]=g_kdata_snap[i-g_kimm_len];
    g_kimg_snap=t; return t; }
static int kimg_check(const u8* img,u32 klen,int* imm_ok,int* full_ok){
    extern char __immutable_end[];
    u32 imm=(u32)((u64)__immutable_end-0x100000ULL); u32 so=(u32)((u64)&g_buildsig-0x100000ULL), ho=so+32, fo=so+88;
    *imm_ok=-1; *full_ok=-1; if(!klen||imm>klen||fo+32>klen)return -1;
    int zero=1; for(int i=0;i<32;i++) if(g_buildsig.sha256_full[i])zero=0; if(zero)return 1;
    u8* t=(u8*)kmalloc(klen); if(!t)return -1; for(u32 i=0;i<klen;i++)t[i]=img[i];
    for(int i=0;i<32;i++)t[ho+i]=0; u8 d[32]; sha256_hash(t,(int)imm,d);
    *imm_ok=1; for(int i=0;i<32;i++) if(d[i]!=g_buildsig.sha256[i])*imm_ok=0;
    for(int i=0;i<32;i++)t[fo+i]=0; sha256_hash(t,(int)klen,d);
    *full_ok=1; for(int i=0;i<32;i++) if(d[i]!=g_buildsig.sha256_full[i])*full_ok=0;
    kfree(t); return 0;
}
void kcheck_run(void){ char b[160],*p; extern char __immutable_end[];
        u32 imm=(u32)((u64)__immutable_end-0x100000ULL); u32 stamp=*(volatile u32*)(u64)(0x100000ULL+24);
        p=b; p=scopy(p,"kernel build "); p=scopy(p,(char*)g_buildsig.version); p=scopy(p,"  size stamp "); p=udec(p,stamp); p=scopy(p,"  captured at boot "); p=udec(p,g_kimg_len); p=scopy(p,"  code part "); p=udec(p,imm); *p=0; term_puts(b); term_putc('\n');
        int io=-1,fo=-1; const u8* full=kimg_full(); int r=(g_kimg_len&&full)?kimg_check(full,g_kimg_len,&io,&fo):-1;
        if(r<0) term_puts("  boot image: not captured\n");
        else if(r>0) term_puts("  boot image: this build has no signature hashes\n");
        else { p=b; p=scopy(p,"  boot image: code "); p=scopy(p,io?"MATCHES":"DIFFERS"); p=scopy(p,"   whole file "); p=scopy(p,fo?"MATCHES":"DIFFERS"); *p=0; tcol(io&&fo?GREEN:rgb(240,120,120)); term_puts(b); term_putc('\n'); tcol(WHT); klog2("kcheck:",b); }
        if(r==0&&!io){ term_puts("  boot image pages that differ from the file's own hash cannot be listed - comparing with memory now:\n"); }
        int nd=0; const u8* k=(const u8*)0x100000ULL;
        u32 lim=imm<g_kimg_len?imm:g_kimg_len;
        for(u32 pg=0;pg*4096u<lim&&pg<KPG_MAX;pg++){ u32 n=lim-pg*4096u; if(n>4096u)n=4096u; u32 first=0xFFFFFFFFu;
            if(kpg_hash(k+pg*4096u,n)!=g_kpg_sum[pg])first=pg*4096u;
            if(first!=0xFFFFFFFFu){ if(nd<12){ p=b; p=scopy(p,"  changed since boot: 0x"); p=hx(p,0x100000u+first,8); *p=0; term_puts(b); term_putc('\n'); } nd++; } }
        p=b; p=scopy(p,"  code pages changed since boot: "); p=udec(p,(u32)nd); *p=0; term_puts(b); term_putc('\n'); }

static char* hx64(char* p,u64 v){ if(v>>32)p=hx(p,(u32)(v>>32),8); return hx(p,(u32)v,8); }
typedef struct { const char* name; u64 s,e; int skips; } karea;
static int kareas(karea* a){ extern char __bss_end[]; int n=0;
    a[n].name="boot information"; a[n].s=0x5000; a[n].e=0x8000; a[n++].skips=0;
    a[n].name="kernel image"; a[n].s=0x100000; a[n].e=0x6500000; a[n++].skips=0;
    if(g_sr_src==1){ u32 sl=0; sysroot_base_blob(&sl); a[n].name="Linux files"; a[n].s=sysroot_slot_base(); a[n].e=sysroot_slot_base()+sl; a[n++].skips=0; }
    a[n].name="boot stack"; a[n].s=0x9800000; a[n].e=0x9A00000; a[n++].skips=0;
    { extern u32 g_devbuf; if(g_devbuf==0x09A00000u){ a[n].name="USB and disk buffers"; a[n].s=0x9A00000; a[n].e=0x9C00000; a[n++].skips=0; } }
    a[n].name="page tables and kernel data"; a[n].s=0x10000000; a[n].e=(u64)__bss_end; a[n++].skips=0;
    a[n].name="DMA arena"; a[n].s=DMA_ARENA_BASE; a[n].e=DMA_ARENA_TOP; a[n++].skips=1;
    a[n].name="page pool"; a[n].s=PF_BASE; a[n].e=PF_TOP; a[n++].skips=1;
    a[n].name="module area"; a[n].s=0x0E000000ULL; a[n].e=0x10000000ULL; a[n++].skips=0;
    a[n].name="heap"; a[n].s=heap_base; a[n].e=heap_top; a[n++].skips=0;
    if(pf_x_top){ a[n].name="page pool (from RAM)"; a[n].s=pf_x_base; a[n].e=pf_x_top; a[n++].skips=1; }
    return n; }
static int ka_bad(u64 s,u64 e,int clean,u64* bs,u64* be,u32* bt){ extern int early_remap_moved(u64,u64); u64 c=s;
    for(int g=0;g<64&&c<e;g++){ if(!mm_first_bad_ex(c,e,clean,bs,be,bt))return 0; u64 x=*bs;
        while(x<*be){ u64 pe=(x&~0x1FFFFFULL)+0x200000ULL; if(pe>*be)pe=*be; if(!early_remap_moved(x,pe)){ *bs=x; return 1; } x=pe; } c=*be; } return 0; }
int mm_area_conflicts(void){ karea a[12]; int n=kareas(a), bad=0; for(int i=0;i<n;i++){ if(a[i].skips||(a[i].s==0x0E000000ULL&&!*(volatile u32*)0x5010ULL))continue; u64 bs,be; u32 bt; if(ka_bad(a[i].s,a[i].e,0,&bs,&be,&bt))bad++; } return bad; }
void mm_conflicts_log(void){ karea a[12]; int n=kareas(a); char b[160],*p;
    for(int i=0;i<n;i++){ if(a[i].skips||(a[i].s==0x0E000000ULL&&!*(volatile u32*)0x5010ULL))continue; u64 bs,be; u32 bt; if(!ka_bad(a[i].s,a[i].e,0,&bs,&be,&bt))continue;
        p=b; p=scopy(p,"memory:   "); p=scopy(p,a[i].name); p=scopy(p,": firmware "); p=scopy(p,bt==0xFFFFFFFFu?"hole (no RAM)":mm_tname(bt)); p=scopy(p," at 0x"); p=hx(p,(u32)bs,8); p=scopy(p,"-0x"); p=hx(p,(u32)be,8); *p=0; klog(b); } }
void memmap_run(void){ char b[200],*p;
    if(!mm_known()){ term_puts("firmware memory map: not available (started without the UEFI loader)\n"); }
    else { p=b; p=scopy(p,"firmware memory map: "); p=udec(p,(u32)g_mmr_n); p=scopy(p,g_mmr_over?" regions (list was cut short)":" regions"); *p=0; term_puts(b); term_putc('\n');
        int shown=0; for(int i=0;i<g_mmr_n&&shown<48;i++){ if(g_mmr[i].s>=0x100000000ULL)continue;
            p=b; p=scopy(p,"  "); p=hx64(p,g_mmr[i].s); *p++='-'; p=hx64(p,g_mmr[i].e-1); p=scopy(p,"  "); p=scopy(p,mm_tname(g_mmr[i].t)); *p=0;
            tcol(g_mmr[i].t==7?WHT:rgb(255,205,100)); term_puts(b); term_putc('\n'); shown++; }
        tcol(WHT); }
    term_puts("kernel memory areas:\n");
    karea a[12]; int n=kareas(a);
    for(int i=0;i<n;i++){ p=b; p=scopy(p,"  "); p=scopy(p,a[i].name); while(p-b<32)*p++=' '; p=hx64(p,a[i].s); *p++='-'; p=hx64(p,a[i].e-1);
        u64 c=a[i].s; int k=0; u64 bs,be; u32 bt;
        if(a[i].s==0x0E000000ULL&&!*(volatile u32*)0x5010ULL&&mm_known()){ p=scopy(p,"  unused"); }
        while(mm_known()&&c<a[i].e&&k<3&&ka_bad(c,a[i].e,a[i].skips,&bs,&be,&bt)){ p=scopy(p,k?", ":a[i].skips?"  skips ":"  OVERLAPS "); p=scopy(p,mm_tname(bt)); *p++=' '; p=hx64(p,bs); *p++='-'; p=hx64(p,be-1); c=be; k++; }
        if(!k&&mm_known()&&!(a[i].s==0x0E000000ULL&&!*(volatile u32*)0x5010ULL))p=scopy(p,"  ok"); *p=0;
        tcol(k&&!a[i].skips?rgb(240,120,120):k?rgb(255,205,100):GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
    { extern u32 g_remap_n, g_remap_restored; extern u64 g_remap_lo; if(g_remap_n){ p=b; p=scopy(p,"kernel memory moved away from firmware memory: "); p=udec(p,g_remap_n*2); p=scopy(p," MB to "); p=hx64(p,g_remap_lo); if(g_remap_restored){ p=scopy(p,", firmware data put back: "); p=udec(p,g_remap_restored/1024); p=scopy(p," KB"); } *p=0; term_puts(b); term_putc('\n'); } }
    p=b; p=scopy(p,"heap in use: "); p=hx64(p,heap_base); *p++='-'; p=hx64(p,heap_cur); p=scopy(p,g_heap_mapped==2?"  (memory the firmware never used)":g_heap_mapped?"  (limited to firmware RAM)":"  (size from RAM probe)"); *p=0; term_puts(b); term_putc('\n');
    p=b; p=scopy(p,"devices whose firmware DMA was stopped at boot: "); p=udec(p,(u32)g_pci_quiesced); *p=0; term_puts(b); term_putc('\n');
    p=b; p=scopy(p,"DMA arena next free: "); p=hx64(p,dma_cur); p=scopy(p,"   page pool free frames: "); p=udec(p,pframe_free_count()); *p=0; term_puts(b); term_putc('\n'); }
static u64 mt_pat(u64 a){ return (a*0x9E3779B97F4A7C15ULL)^0x5A5AC3C3A5A53C3CULL; }
static u32 g_mt_bad, g_mt_late; static u64 g_mt_first, g_mt_firstlate, g_mt_seen;
static void mt_fill(u64 s,u64 e){ for(u64 a=s;a+8<=e;a+=8){ *(volatile u64*)a=mt_pat(a); } }
static void mt_check(u64 s,u64 e,int late){ for(u64 a=s;a+8<=e;a+=8){ u64 v=*(volatile u64*)a; if(v!=mt_pat(a)){ if(late){ if(!g_mt_late){ g_mt_firstlate=a; g_mt_seen=v; } g_mt_late++; } else { if(!g_mt_bad){ g_mt_first=a; g_mt_seen=v; } g_mt_bad++; } *(volatile u64*)a=mt_pat(a); } } }
static void mt_wait(u32 ms){ u64 t0=g_ticks; u32 lim=ms/10u; cpu_irq_on(); for(u32 i=0;i<ms*3000u;i++){ if(g_ticks_live&&g_ticks-t0>=lim)break; cpu_pause(); } }
static int mt_ranges(u64* rs,u64* re,const char** rn){ int n=0;
    { u64 c=(dma_cur+4095)&~4095ULL; while(c<DMA_ARENA_TOP&&n<14){ u64 bs,be; u32 bt; if(mm_first_bad_ex(c,DMA_ARENA_TOP,1,&bs,&be,&bt)){ if(bs>c){ rs[n]=c; re[n]=bs; rn[n++]="DMA arena"; } c=be; } else { rs[n]=c; re[n]=DMA_ARENA_TOP; rn[n++]="DMA arena"; break; } } }
    { u32 i=0; while(i<pf_n&&n<20){ while(i<pf_n&&(pf_bitmap[i>>3]&(1u<<(i&7))))i++; u32 j=i; while(j<pf_n&&!(pf_bitmap[j>>3]&(1u<<(j&7)))&&(j==i||pf_pa(j)==pf_pa(j-1)+4096ULL))j++; if(j>i){ rs[n]=pf_pa(i); re[n]=pf_pa(j-1)+4096ULL; rn[n++]="page pool"; } i=j; } }
    if(!*(volatile u32*)0x5010ULL&&!g_doom_wad&&mm_is_usable(0x0E000000ULL,0x10000000ULL)&&n<22){ rs[n]=0x0E000000ULL; re[n]=0x10000000ULL; rn[n++]="module area"; }
    return n; }
void memtest_run(void){ char b[200],*p;
    { int nb=0,bad=0; u64 badat=0; for(hblk* h=heap_head; h&&nb<1000000; h=h->next){ u64 a=(u64)h; if(a<heap_base||a>=heap_top||h->magic!=HBLK_MAGIC){ bad=1; badat=a; break; } nb++; }
      p=b; p=scopy(p,"heap blocks: "); p=udec(p,(u32)nb); if(bad){ p=scopy(p,"  DAMAGED block header at "); p=hx64(p,badat); } else p=scopy(p,"  all headers intact"); *p=0; tcol(bad?rgb(240,120,120):GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
    u64 rs[24],re[24]; const char* rn[24]; int n=mt_ranges(rs,re,rn);
    u8* hb[16]; int hn=0; for(;hn<16;hn++){ hb[hn]=(u8*)kmalloc(16u*1024u*1024u); if(!hb[hn])break; if(n<24){ rs[n]=((u64)hb[hn]+7)&~7ULL; re[n]=(u64)hb[hn]+16u*1024u*1024u; rn[n++]="heap"; } }
    u64 tot=0; for(int i=0;i<n;i++)tot+=re[i]-rs[i];
    p=b; p=scopy(p,"testing "); p=udec(p,(u32)(tot>>20)); p=scopy(p," MB of free memory (fill, read back, wait 3 s, read again)..."); *p=0; term_puts(b); term_putc('\n'); blit_all();
    g_mt_bad=0; g_mt_late=0;
    for(int i=0;i<n;i++)mt_fill(rs[i],re[i]);
    for(int i=0;i<n;i++)mt_check(rs[i],re[i],0);
    u32 bad0=g_mt_bad; u64 f0=g_mt_first, s0=g_mt_seen;
    mt_wait(3000);
    for(int i=0;i<n;i++)mt_check(rs[i],re[i],1);
    for(int k=0;k<hn;k++)kfree(hb[k]);
    for(int i=0;i<n;i++){ p=b; p=scopy(p,"  "); p=scopy(p,rn[i]); while(p-b<16)*p++=' '; p=hx64(p,rs[i]); *p++='-'; p=hx64(p,re[i]-1); *p=0; term_puts(b); term_putc('\n'); if(i>=11&&n>13){ term_puts("  ...\n"); break; } }
    if(!bad0&&!g_mt_late){ tcol(GREEN); term_puts("memory test passed: every byte kept its value\n"); tcol(WHT); return; }
    tcol(rgb(240,120,120));
    if(bad0){ p=b; p=udec(p,bad0); p=scopy(p," words did not keep their value, first at "); p=hx64(p,f0); p=scopy(p,"  read "); p=hx64(p,s0); *p=0; term_puts(b); term_putc('\n'); }
    if(g_mt_late){ p=b; p=udec(p,g_mt_late); p=scopy(p," words were changed by something else within 3 s, first at "); p=hx64(p,g_mt_firstlate); p=scopy(p,"  read "); p=hx64(p,g_mt_seen); *p=0; term_puts(b); term_putc('\n');
        p=b; p=scopy(p,"  bytes there now:"); for(int i=0;i<32;i++){ *p++=' '; p=hx(p,*(volatile u8*)(g_mt_firstlate+(u64)i),2); } *p=0; term_puts(b); term_putc('\n'); }
    tcol(WHT); }
