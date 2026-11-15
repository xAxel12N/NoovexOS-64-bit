static void sh_redraw(void);
static char* uhd_sdec(char* p,int v){ if(v<0){ *p++='-'; return udec(p,(u32)-v); } *p++='+'; return udec(p,(u32)v); }
static char* uhd_hex(char* p,const u8* b,int n){ for(int i=0;i<n;i++){ *p++=' '; p=hx(p,b[i],2); } return p; }
static int uhd_ismouse(int e){ if(!xhid_rep[e])return !xhid_iskbd[e]; for(int z=0;z<xhid_nlay[e];z++) if(xhid_lay[e][z].kind==2)return 1; return 0; }
static int uhd_lastlen(int e,int id){ xhdiag_t* D=&xhid_diag[e]; int n=D->rxi<4?(int)D->rxi:4;
    for(int k=1;k<=n;k++){ int s=(int)((D->rxi-(u32)k)&3u); if(!xhid_hasid[e]||(D->rxn[s]&&D->rx[s][0]==id))return D->rxn[s]; }
    return -1; }
static char* uhd_field(char* p,const char* nm,int off,int sz,int lim){ if(off<0)return p; p=scopy(p,nm); p=udec(p,(u32)off); *p++='+'; p=udec(p,(u32)(sz<0?0:sz)); if(lim>=0&&off+sz>lim)*p++='!'; return p; }
static void uhd_rx_line(int e,int s,const char* pre){ xhdiag_t* D=&xhid_diag[e]; char b[260],*p=b; int n=D->rxn[s]<64?D->rxn[s]:64;
    p=scopy(p,pre); *p++='['; p=udec(p,D->rxn[s]); *p++=']'; p=uhd_hex(p,D->rx[s],n); p=scopy(p,"   dx "); p=uhd_sdec(p,D->rdx[s]); p=scopy(p," dy "); p=uhd_sdec(p,D->rdy[s]); *p=0; term_puts(b); term_putc('\n'); }
static void usbhid_show(void){
    int any=0;
    for(int c=0;c<g_xhc_n;c++){ xhc_select(c);
        for(int e=0;e<xhid_n;e++){ if(!xhid_used[e]||xhid_ctl[e]!=c)continue; xhdiag_t* D=&xhid_diag[e]; char b[260],*p; any=1;
            p=b; p=scopy(p,"dev "); p=udec(p,(u32)e); p=scopy(p,"  "); p=hx(p,D->vid,4); *p++=':'; p=hx(p,D->pid,4); p=scopy(p,"  ctl "); p=udec(p,(u32)(c+1)); p=scopy(p," port "); p=udec(p,(u32)(xhid_port[e]+1));
            p=scopy(p,"  if "); p=udec(p,D->iface); p=scopy(p,"  ep 0x"); p=hx(p,D->ep,2); p=scopy(p," dci "); p=udec(p,(u32)xhid_dci[e]);
            p=scopy(p,xhid_rep[e]?(xhid_hasid[e]?"  report proto (IDs)":"  report proto"):"  boot proto"); p=scopy(p,"  sub/pr "); p=udec(p,D->sub); *p++='/'; p=udec(p,D->proto); *p=0; tcol(CYAN); term_puts(b); tcol(WHT); term_putc('\n');
            p=b; p=scopy(p,"  MPS "); p=udec(p,D->mps); p=scopy(p,"  rlen "); p=udec(p,(u32)xhid_rlen[e]); p=scopy(p,"  reports "); p=udec(p,xhid_cnt[e]); p=scopy(p,"  layouts "); p=udec(p,(u32)xhid_nlay[e]);
            p=scopy(p,"  last dx/dy "); p=uhd_sdec(p,D->ldx); *p++=' '; p=uhd_sdec(p,D->ldy); *p=0; term_puts(b); term_putc('\n');
            if(xhid_rep[e]&&uhd_ismouse(e)){ p=b; p=scopy(p,"  receiver sends "); p=scopy(p,D->pfmt==2?"report format (wheel ok)":D->pfmt==1?"BOOT format (no wheel)":"no reports yet"); p=scopy(p,"  SET_PROTOCOL "); p=scopy(p,D->pset==1?"ok":D->pset==3?"ok on 2nd try":D->pset==2||D->pset==4?"no answer":"not sent"); p=scopy(p,"  boot packets "); p=udec(p,D->nboot); p=scopy(p,"  retries "); p=udec(p,D->ptry); *p=0; term_puts(b); term_putc('\n'); }
            p=b; p=scopy(p,"  report descriptor "); p=udec(p,D->rdlen); p=scopy(p," bytes"); if(D->rdlen>D->rdn){ p=scopy(p," (first "); p=udec(p,D->rdn); *p++=')'; } *p++=':'; *p=0; term_puts(b); term_putc('\n');
            tcol(GREY); for(int o=0;o<D->rdn;o+=16){ int n=D->rdn-o<16?D->rdn-o:16; p=b; p=scopy(p,"   "); p=hx(p,(u32)o,3); *p++=':'; p=uhd_hex(p,D->rd+o,n); *p=0; term_puts(b); term_putc('\n'); } tcol(WHT);
            for(int z=0;z<xhid_nlay[e];z++){ hidlay_t* L=&xhid_lay[e][z]; int ll=uhd_lastlen(e,L->id); int lim=ll<0?-1:(ll-(xhid_hasid[e]?1:0))*8;
                p=b; p=scopy(p,"  layout id "); p=udec(p,L->id); p=scopy(p,L->kind==2?" mouse":L->kind==1?" keyboard":L->kind==3?" consumer":" ?");
                if(L->kind==2){ p=uhd_field(p,"  btn ",L->btn_off,L->btn_cnt,lim); p=uhd_field(p,"  X ",L->x_off,L->x_sz,lim); p=uhd_field(p,"  Y ",L->y_off,L->y_sz,lim); p=uhd_field(p,"  wheel ",L->w_off,L->w_sz,lim); }
                else if(L->kind==1){ p=uhd_field(p,"  mod ",L->mod_off,8,lim); p=uhd_field(p,"  keys ",L->arr_off,L->arr_cnt*8,lim); p=uhd_field(p,"  bitmap ",L->bm_off,L->bm_cnt,lim); }
                else if(L->kind==3){ p=uhd_field(p,"  cc ",L->cc_off,L->cc_sz*L->cc_cnt,lim); if(L->cv_cnt){ p=scopy(p,"  bits "); p=udec(p,(u32)L->cv_cnt); } }
                p=scopy(p,"  (bits, after id; last len "); if(ll<0)*p++='-'; else p=udec(p,(u32)ll); *p++=')'; *p=0; term_puts(b); term_putc('\n'); }
            int n=D->rxi<4?(int)D->rxi:4;
            for(int k=n;k>=1;k--) uhd_rx_line(e,(int)((D->rxi-(u32)k)&3u),"  rx ");
            if(!n) term_puts("  rx: no reports yet\n"); } }
    if(g_xhc_n)xhc_select(0);
    if(!any) term_puts("usbhid: no USB keyboard/mouse set up (see usbinfo)\n");
    else term_puts("'!' = field ends past the last received report.  usbhid live: watch mouse reports\n");
}
static void usbhid_live(void){
    u32 seen[XHID_MAX]; int cnt=0; long sx=0,sy=0;
    for(int e=0;e<XHID_MAX;e++)seen[e]=xhid_diag[e].rxi;
    term_puts("usbhid live: slide the mouse forward/back. 15 s, any key stops.\n"); sh_redraw();
    while(kpop()>=0){}
    u64 t0=g_ticks; long spins=0;
    for(;;){
        if(xhid_n) xhci_mouse_poll();
        cpu_irq_off(); mouse_dx=0; mouse_dy=0; mouse_scroll=0; mouse_evt=0; cpu_irq_on(); { extern void in_mq_clear(void); in_mq_clear(); }
        int pr=0;
        for(int e=0;e<xhid_n&&e<XHID_MAX;e++){ if(!xhid_used[e]||!uhd_ismouse(e))continue; xhdiag_t* D=&xhid_diag[e]; u32 r=D->rxi; if(r==seen[e])continue;
            u32 st=r-seen[e]>4u?r-4u:seen[e]; if(st!=seen[e])term_puts("  (reports skipped)\n");
            for(u32 i=st;i!=r;i++){ int s=(int)(i&3u); char pre[16],*p=pre; *p++='d'; p=udec(p,(u32)e); *p++=' '; *p=0; uhd_rx_line(e,s,pre); sx+=D->rdx[s]; sy+=D->rdy[s]; cnt++; pr=1; }
            seen[e]=r; }
        if(pr)sh_redraw();
        if(kpop()>=0)break;
        spins++; if(g_ticks_live?(g_ticks-t0>1500):(spins>400000000L))break;
        cpu_pause(); }
    char b[120],*p=b; p=scopy(p,"usbhid live ended: "); p=udec(p,(u32)cnt); p=scopy(p," reports, sum dx "); p=uhd_sdec(p,(int)sx); p=scopy(p," dy "); p=uhd_sdec(p,(int)sy); *p=0; term_puts(b); term_putc('\n');
}
static void usbhid_cmd(const char* arg){ if(arg[0]=='l')usbhid_live(); else usbhid_show(); }
