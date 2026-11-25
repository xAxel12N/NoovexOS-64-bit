static int st_lp_conf=-1;
static void st_logi_card(int* cy){
    char a[96], b[96]; int st=hidpp_pair_state(a,96,b,96); if(st<0)return;
    int X1=st_x0+st_w0-24, nd=0; static char hk[16][16]; char ht[16][96]; int hr[16];
    for(int k=0;k<16;k++){ int r=hidpp_info(k,hk[k],16,ht[k],96); if(!r)break; hr[k]=r; nd++; }
    int y=st_card(*cy,64+(st==2?40:0)+nd*44,"Logitech receiver");
    st_rowtext_w(y,a,b,X1-146-(st_x0+20));
    if(st==1||st==2)st_btn(X1-130,y+16,130,"Cancel",2951,0); else if(st>=3)st_btn(X1-130,y+16,130,"OK",2951,1); else st_btn(X1-130,y+16,130,"Pair device",2950,1);
    int ry=y+64;
    if(st==2){ st_sep(ry); st_bold(st_x0+20,ry+12,b,ACC); ry+=40; }
    for(int k=0;k<nd;k++){ st_sep(ry); pdstr(st_x0+20,ry+14,ht[k],hr[k]==1?ST_FG:ST_DIM,1); st_btn(X1-110,ry+5,110,st_lp_conf==k?"Confirm":"Unpair",2952+k,2); ry+=44; }
    *cy=y+64+(st==2?40:0)+nd*44+24; }
static void st_logi_click(int id){
    if(id==2950){ st_lp_conf=-1; toast(hidpp_pair()?"No Logitech Bolt or Unifying receiver":"Pairing started"); return; }
    if(id==2951){ st_lp_conf=-1; hidpp_pair_cancel(); return; }
    if(id>=2952&&id<2968){ int k=id-2952; if(st_lp_conf!=k){ st_lp_conf=k; toast("Click Confirm to unpair this device"); return; } st_lp_conf=-1; toast(hidpp_unpair(k)?"Could not unpair this device":"Unpairing..."); } }
