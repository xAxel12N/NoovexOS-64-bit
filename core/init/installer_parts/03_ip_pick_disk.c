static void ip_pick_disk(int i){ extern int gdisk_count(void); extern int gdisk_pick(int); extern int disk_ok; extern int g_nvx_generic;
    if(i<0||i>=gdisk_count())return; int pr=gdisk_pick(i); if(pr>0){ if(disk_ok&&(g_nvx_generic||g_dktype==DK_IDE))disk_ok=0; } dm_load(); ip_load(); ip_mode=0; ip_part=-1; }
static void ip_act(int id){
    if(ip_page==IP_FS){ if(id>=40&&id<=43){ g_ip_fs=id-40; return; } if(id==2){ ip_page=IP_WELCOME; return; } return; }
    /* compact classic flow:
       disk-select(Continue)=2 -> CONFIRM ; CONFIRM Back=1 Install=2 ; DONE Reboot=5 ; ERROR Back=3 */
    if(ip_page==IP_WELCOME||ip_page==IP_TARGET){
        if(id==10){ return; }
        if(id>=60&&id<66){ ip_pick_disk(id-60); return; }
        if(id==2){ ip_mode=0; ip_part=-1; ip_page=IP_CONFIRM; ip_ack=1; return; }
        if(id==6){ setup_begin(); return; }
        return;
    }
    if(ip_page==IP_CONFIRM){
        if(id==1){ ip_page=IP_TARGET; return; }
        if(id==2){ ip_run(); return; }
        return;
    }
    if(ip_page==IP_DONE){
        if(id==5){ power_reboot(); }
        return;
    }
    if(ip_page==IP_ERROR){
        if(id==3){ ip_page=IP_WELCOME; ip_load(); return; }
        return;
    }
}
int ip_input(void){
    int changed=0;
    if(mouse_evt){ int dx,dy,btn; cpu_irq_off(); dx=mouse_dx;dy=mouse_dy;mouse_dx=0;mouse_dy=0;btn=mouse_btn;mouse_evt=0; cpu_irq_on();
        if(dx||dy){ int h0=ip_hover(); mx+=dx;my-=dy; if(mx<0)mx=0;if(my<0)my=0;if(mx>=(int)W)mx=W-1;if(my>=(int)H)my=H-1; if(ip_hover()!=h0)changed=1; else g_ip_curmoved=1; }
        if(btn&&!mouse_btn_prev&&ip_page!=IP_RUN){ changed=1; for(int b=ip_bn-1;b>=0;b--) if(in_rect(mx,my,ip_bx[b],ip_by[b],ip_bw[b],ip_bh[b])){ ip_act(ip_bid[b]); break; } }
        mouse_btn_prev=btn; }
    if(xhid_n)xhci_mouse_poll();
    int c; while((c=kpop())>=0){ changed=1;
        if(c==27){ if(ip_page==IP_FS||ip_page==IP_DONE) g_state=ip_ret; else if(ip_page==IP_WELCOME||ip_page==IP_TARGET) ip_page=IP_FS; else if(ip_page==IP_CONFIRM) ip_page=IP_TARGET; else if(ip_page==IP_ERROR){ ip_page=IP_FS; ip_load(); } }
        else if(ip_page==IP_FS&&(c==19||c==20)){ if(c==20&&g_ip_fs<3)g_ip_fs++; else if(c==19&&g_ip_fs>0)g_ip_fs--; }
        else if(c=='\n'){ if(ip_page!=IP_RUN) ip_act(g_ip_focus); }
        else if(c==17||c==18||c=='\t'){ if(ip_page!=IP_RUN) ip_focus_move(c==17?-1:1); }
        else if((ip_page==IP_TARGET||ip_page==IP_WELCOME)&&(c==19||c==20)){ extern int gdisk_current(void); ip_pick_disk(gdisk_current()+(c==20?1:-1)); }
        else if((ip_page==IP_TARGET||ip_page==IP_WELCOME)&&(c=='p'||c=='P')){ dm_open(ST_INSTALL); } }
    return changed;
}
static void ip_open_preset(int ret,int mode,int part){ ip_open(ret); if(!ip_disk_ok())return; ip_mode=mode; ip_part=part; ip_page=(mode==0||ip_part_ok(part))?IP_CONFIRM:IP_TARGET; }
void bootmenu_present(void){
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
int bootmenu_input(void){
    int changed=0;
    if(mouse_evt){ int dx,dy,btn; cpu_irq_off(); dx=mouse_dx;dy=mouse_dy;mouse_dx=0;mouse_dy=0;btn=mouse_btn;mouse_evt=0; cpu_irq_on();
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

