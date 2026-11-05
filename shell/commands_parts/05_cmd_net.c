    else if(seq(cmd,"net")){ int r=nic_up(); if(!pci_n)pci_scan(); int k=nic_index();
        tcol(CYAN); term_puts("NIC: "); tcol(WHT); term_puts(k>=0?nic_name(pci_ven[k],pci_dev[k]):"none");
        if(k>=0){ char b[40],*p=b; p=scopy(p,"  ["); p=hx(p,pci_ven[k],4);*p++=':';p=hx(p,pci_dev[k],4);*p++=']'; *p=0; term_puts(b); } term_putc('\n');
        if(r){ char b[64],*p=b; p=scopy(p,"MAC "); for(int i=0;i<6;i++){p=hx(p,my_mac[i],2); if(i<5)*p++=':';} *p=0; term_puts(b); term_putc('\n');
            p=b; p=scopy(p,"IP "); p=ipstr(p,my_ip); p=scopy(p,"/"); { u32 m=my_mask,c=0; while(m&0x80000000u){c++;m<<=1;} p=udec(p,c); } p=scopy(p,"  GW "); p=ipstr(p,gw_ip); p=scopy(p,g_dhcp_ok?"  (DHCP)":"  (static)"); *p=0; tcol(GREEN); term_puts(b); tcol(WHT); term_putc('\n');
            p=b; p=scopy(p,"DNS "); p=ipstr(p,dns_ip); *p=0; tcol(GREY); term_puts(b); tcol(WHT); term_putc('\n');
            term_puts("try: dhcp   ping 10.0.2.2   dns example.com   http example.com\n"); }
        else { tcol(rgb(240,150,120)); term_puts("link down - set the VM adapter to PCnet-FAST III (NAT)\n"); tcol(WHT); } }
    else if(seq(cmd,"dhcp")){ tcol(GREY); term_puts("requesting a lease...\n"); tcol(WHT);
        int r=dhcp_renew();
        if(r==1){ char b[96],*p=b; p=scopy(p,"IP "); p=ipstr(p,my_ip); p=scopy(p,"/"); { u32 m=my_mask,c=0; while(m&0x80000000u){c++;m<<=1;} p=udec(p,c); } p=scopy(p,"  GW "); p=ipstr(p,gw_ip); p=scopy(p,"  DNS "); p=ipstr(p,dns_ip); *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
        else if(r==0){ tcol(rgb(240,150,120)); term_puts("no DHCP server answered - keeping the static address\n"); tcol(WHT); }
        else { term_puts("network not available\n"); } }
    else if(seq(cmd,"ping")){ u32 ip; if(!parse_ip(arg,&ip)){ term_puts("usage: ping A.B.C.D\n"); }
        else { int r=do_ping(ip); if(r==1){ char b[300],*p=b; p=scopy(p,"reply from "); for(int i=0;arg[i]&&i<200;i++)*p++=arg[i]; *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
            else if(r==0) term_puts("no reply (timeout)\n"); else term_puts("network not available\n"); } }
    else if(seq(cmd,"dns")){ if(!arg[0]){ term_puts("usage: dns hostname\n"); }
        else { u32 ip; int r=dns_query(arg,&ip); if(r==1){ char b[300],*p=b; for(int i=0;arg[i]&&i<260;i++)*p++=arg[i]; p=scopy(p," -> "); char t[16]; { u32 v=ip; int q=0; char oc[4]; for(int i=3;i>=0;i--){ u32 o=(v>>(i*8))&0xFF; if(!o)t[q++]='0'; else { int n=0; while(o){oc[n++]='0'+o%10;o/=10;} while(n)t[q++]=oc[--n]; } if(i)t[q++]='.'; } t[q]=0; } p=scopy(p,t); *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
            else term_puts("could not resolve\n"); } }
    else if(seq(cmd,"http")||seq(cmd,"https")||seq(cmd,"wget")||seq(cmd,"get")){
        int insecure=0; while(arg[0]=='-'&&arg[1]=='k'&&(arg[2]==' '||!arg[2])){ insecure=1; arg+=2; while(*arg==' ')arg++; }
        if(!arg[0]){ term_puts("usage: http host[/path]   or   https [-k] host[/path]   (-k skips certificate checks)\n"); }
        else { int secure=seq(cmd,"https"); char host[128],path[300]; int hi=0,pi=0; const char*s=arg;
            if(s[0]=='h'&&s[1]=='t'&&s[2]=='t'&&s[3]=='p'){ if(s[4]=='s')secure=1; while(*s&&*s!=':')s++; if(*s==':'){s++; while(*s=='/')s++;} }
            while(*s&&*s!='/'&&*s!=' '&&hi<127)host[hi++]=*s++; host[hi]=0;
            path[pi++]='/'; if(*s=='/')s++; while(*s&&*s!=' '&&pi<299)path[pi++]=*s++; path[pi]=0;
            tcol(GREY); term_puts(secure?"fetching https://":"fetching http://"); term_puts(host); term_puts("...\n"); tcol(WHT);
            g_tls_insecure=insecure; if(insecure&&secure){ tcol(rgb(240,200,120)); term_puts("warning: certificate checks are OFF for this request\n"); tcol(WHT); }
            int n = secure ? https_fetch(host,path) : http_fetch(host,path);
            g_tls_insecure=0;
            if(n==-100){ tcol(rgb(240,120,120)); term_puts("DNS failed\n"); tcol(WHT); }
            else if(n<=0){ tcol(rgb(240,120,120));
                if(secure){ char b[160],*p=b; p=scopy(p,"HTTPS/TLS failed (code -"); p=udec(p,(u32)(-n)); p=scopy(p,"): "); p=scopy(p,g_tls_why); *p=0; term_puts(b); term_putc('\n'); }
                else if(n==-1) term_puts("no NIC (set VM to PCnet-FAST III)\n");
                else if(n==-2) term_puts("ARP failed (no gateway)\n");
                else if(n==-3) term_puts("no SYN-ACK (timeout)\n");
                else if(n==-4) term_puts("connection refused (RST)\n");
                else term_puts("connected, no data\n");
                tcol(WHT); }
            else { http_buf[n]=0; int bo=n; for(int i=0;i+3<n;i++) if(http_buf[i]=='\r'&&http_buf[i+1]=='\n'&&http_buf[i+2]=='\r'&&http_buf[i+3]=='\n'){ bo=i+4; break; }
                tcol(GREEN); { int e=0; while(e<bo && http_buf[e]!='\r' && http_buf[e]!='\n'){ term_putc(http_buf[e]); e++; } } term_putc('\n'); tcol(GREY);
                { char b[32],*p=b; p=scopy(p,"("); p=udec(p,(u32)n); p=scopy(p," bytes)"); *p=0; term_puts(b); term_putc('\n'); } tcol(WHT);
                int shown=0,intag=0; char last=0;
                for(int i=bo;i<n&&shown<1400;i++){ char c=http_buf[i]; if(c=='<'){intag=1;continue;} if(c=='>'){intag=0;continue;} if(intag)continue; if(c=='\r')continue;
                    if(c==' '||c=='\n'||c=='\t'){ if(last==' '||last=='\n')continue; c=(c=='\n')?'\n':' '; } if(c<32&&c!='\n')continue; term_putc(c); last=c; shown++; }
                term_putc('\n'); } } }
    else if(seq(cmd,"parts")||seq(cmd,"lsblk")||seq(cmd,"partitions")){ parts_print(); }
    else if(cmd[0]=='p'&&cmd[1]=='r'&&cmd[2]=='i'&&cmd[3]=='n'&&cmd[4]=='t'&&(cmd[5]==0||cmd[5]==' '||(cmd[5]=='e'&&cmd[6]=='r'))){ extern char g_prn_status[]; extern u32 g_prn_ip; extern int prn_set(const char*); extern int prn_test_page(void); extern int prn_print_text(const char*,const char*);
        const char* a=arg; while(*a==' ')a++;
        if(cmd[5]=='e'){ if(*a){ if(prn_set(a)){ tcol(RED); term_puts("usage: printer 192.168.1.50\n"); tcol(WHT); } } term_puts(g_prn_status); term_putc('\n'); }
        else if(!*a||seq(a,"test")){ term_puts("printing a test page...\n"); prn_test_page(); term_puts(g_prn_status); term_putc('\n'); }
        else { int k=k_find_file(a); if(k<0||!FS[k].data||FS[k].lazy){ tcol(RED); term_puts("file not found (or not loaded yet - open it once first)\n"); tcol(WHT); } else { char* t=(char*)kmalloc((u64)FS[k].len+1); if(t){ for(int i=0;i<FS[k].len;i++)t[i]=FS[k].data[i]; t[FS[k].len]=0; term_puts("printing...\n"); prn_print_text(a,t); kfree(t); term_puts(g_prn_status); term_putc('\n'); } } } }
    else if(seq(cmd,"laptop")||seq(cmd,"kbdlight")||seq(cmd,"chargelimit")){ extern int g_lap_kind, g_lap_kbd, g_lap_kbdmax, g_lap_chg, g_lap_chg_ok; extern const char* g_lap_name; extern u32 g_lap_events, g_lap_last; extern void laptop_kbd_set(int); extern void laptop_charge_set(int);
        const char* a=arg; while(*a==' ')a++;
        if(seq(cmd,"kbdlight")&&*a>='0'&&*a<='3')laptop_kbd_set(*a-'0');
        if(seq(cmd,"chargelimit")&&*a)laptop_charge_set(a[0]=='o'&&a[1]=='n');
        char b[160],*p=b; p=scopy(p,"laptop: "); p=scopy(p,g_lap_kind?g_lap_name:"no Lenovo or ASUS extras found"); if(g_lap_kind){ p=scopy(p,"  keyboard light "); if(g_lap_kbdmax){ p=udec(p,(u32)(g_lap_kbd<0?0:g_lap_kbd)); *p++='/'; p=udec(p,(u32)g_lap_kbdmax); } else p=scopy(p,"none"); p=scopy(p,"  charge limit "); p=scopy(p,!g_lap_chg_ok?"not supported":g_lap_chg?"on (80%)":"off"); p=scopy(p,"  hotkey events "); p=udec(p,g_lap_events); p=scopy(p,"  last 0x"); p=hx(p,g_lap_last,4); } *p=0; term_puts(b); term_putc('\n'); }
    else if(seq(cmd,"reg")){ char w[4][100]; int nw=0; { const char* a=arg; while(nw<4){ while(*a==' ')a++; if(!*a)break; int k=0; if(nw==3){ while(*a&&k<99)w[nw][k++]=*a++; } else if(*a=='"'){ a++; while(*a&&*a!='"'&&k<99)w[nw][k++]=*a++; if(*a=='"')a++; } else { while(*a&&*a!=' '&&k<99)w[nw][k++]=*a++; } w[nw][k]=0; nw++; } }
        char b[200],*p; const char* sub0=nw?w[0]:"";
        if(seq((char*)sub0,"get")&&nw>=3){ char v[96]; if(!reg_get(w[1],w[2],v,96)){ sh_err("reg: ","not found"); return; } term_puts(v); term_putc('\n'); }
        else if(seq((char*)sub0,"set")&&nw>=4){ int r=reg_set(w[1],w[2],w[3]); if(r)sh_err("reg: ",reg_err(r)); else term_puts("reg: saved\n"); }
        else if((seq((char*)sub0,"delete")||seq((char*)sub0,"del"))&&nw>=2){ int r=reg_delete(w[1],nw>=3?w[2]:""); if(r)sh_err("reg: ",reg_err(r)); else term_puts("reg: deleted\n"); }
        else if(seq((char*)sub0,"add")&&nw>=2){ int r=reg_add_key(w[1]); if(r)sh_err("reg: ",reg_err(r)); else term_puts("reg: key added\n"); }
        else if(seq((char*)sub0,"export")&&nw>=2){ int n=reg_export(w[1],nw>=3?w[2]:""); if(n<0)sh_err("reg: ","could not write the file"); else { p=b; p=scopy(p,"reg: "); p=udec(p,(u32)n); p=scopy(p," values written to "); p=scopy(p,w[1]); *p++='\n'; *p=0; term_puts(b); } }
        else if(seq((char*)sub0,"import")&&nw>=2){ int s=0,f2=0; if(reg_import(w[1],&s,&f2)<0){ sh_err("reg: ","could not read the file"); return; } p=b; p=scopy(p,"reg: "); p=udec(p,(u32)s); p=scopy(p," values set"); if(f2){ p=scopy(p,", "); p=udec(p,(u32)f2); p=scopy(p," refused"); } *p++='\n'; *p=0; term_puts(b); }
        else if(seq((char*)sub0,"list")||!nw){ static char ks[96][64]; int nk=reg_keys(ks,96); const char* pre=nw>=2?w[1]:""; char pn[64]; { int o=0; for(int i=0;pre[i]&&o<63;i++)pn[o++]=pre[i]=='/'?'\\':pre[i]; pn[o]=0; } int pl=slen(pn);
            for(int k=0;k<nk;k++){ if(pl){ int m=1; for(int i=0;i<pl;i++){ char x=ks[k][i], y=pn[i]; if(x>='A'&&x<='Z')x+=32; if(y>='A'&&y<='Z')y+=32; if(x!=y){ m=0; break; } } if(!m||(ks[k][pl]&&ks[k][pl]!='\\'))continue; }
                char nm[48][32]; int c=reg_values(ks[k],nm,48); tcol(CYAN); term_puts(ks[k]); term_putc('\n'); tcol(WHT);
                for(int i=0;i<c;i++){ char v[96]; reg_get(ks[k],nm[i],v,96); p=b; p=scopy(p,"    "); p=scopy(p,nm[i]); p=scopy(p," = "); p=scopy(p,v); *p++='\n'; *p=0; term_puts(b); } } }
        else { term_puts("usage: reg list [KEY] | reg get KEY NAME | reg set KEY NAME VALUE | reg add KEY | reg delete KEY [NAME]\n       reg export FILE [KEY] | reg import FILE      (keys: System\\... or User\\..., / also works)\n       regedit opens the Registry Editor\n"); } }
    else if(seq(cmd,"regedit")){ open_win(W_CHATGPT); }
    else if(seq(cmd,"programs")){ extern void progfiles_sync(int); extern int progfiles_count(void); extern const pf_app* progfiles_get(int); progfiles_sync(1); int n=progfiles_count(); char b[200],*p;
        for(int i=0;i<n;i++){ const pf_app* a=progfiles_get(i); if(a->hidden&&!(arg[0]=='-'&&arg[1]=='a'))continue; p=b; p=scopy(p,"/Programs/"); p=scopy(p,a->folder); while(p-b<44)*p++=' '; p=scopy(p,a->kind==PF_BUILTIN?"built-in":a->kind==PF_LINUX?"Linux app":"apt package "); if(a->pkg[0])p=scopy(p,a->pkg); if(a->hidden)p=scopy(p,"  (hidden)"); *p++='\n'; *p=0; term_puts(b); }
        term_puts("programs -a also lists hidden apps; details are in each folder's app.info\n"); }
    else if(seq(cmd,"restore")){ extern int sysfiles_restore(const char*,char*,int); const char* a=arg; while(*a==' ')a++; static char m[2048]; sysfiles_restore(a,m,2048); term_puts(m); }
    else if(seq(cmd,"focus")){ int n=focus_count(); char b[96],*p;
        if(!arg[0]){ p=b; p=scopy(p,"Focus: "); p=scopy(p,g_focus_cur>=0?focus_get(g_focus_cur)->name:"off"); *p=0; term_puts(b); term_puts("\n  focus list | focus <name> | focus off | focus create <name> | focus delete <name>\n"); }
        else if(seq(arg,"list")){ for(int i=0;i<n;i++){ focus_mode* f=focus_get(i); p=b; p=scopy(p,i==g_focus_cur?"* ":"  "); p=scopy(p,f->id); while(p-b<12)*p++=' '; p=scopy(p,f->name); if(f->sch_on){ p=scopy(p,"  schedule "); p=u2(p,f->sch_from/60); *p++=':'; p=u2(p,f->sch_from%60); *p++='-'; p=u2(p,f->sch_to/60); *p++=':'; p=u2(p,f->sch_to%60); } *p++='\n'; *p=0; term_puts(b); } }
        else if(seq(arg,"off")){ focus_set(-1,0); term_puts("Focus is off\n"); }
        else if(arg[0]=='c'&&arg[1]=='r'&&arg[2]=='e'&&arg[3]=='a'&&arg[4]=='t'&&arg[5]=='e'&&arg[6]==' '){ int k=focus_create(arg+7,4,0x868E96); if(k>=0){ term_puts("Created. Change it in Settings > Focus.\n"); } else { tcol(RED); term_puts(k==-2?"That name is taken\n":"Could not create the focus\n"); tcol(WHT); } }
        else if(arg[0]=='d'&&arg[1]=='e'&&arg[2]=='l'&&arg[3]=='e'&&arg[4]=='t'&&arg[5]=='e'&&arg[6]==' '){ int k=focus_find(arg+7); if(k<0||focus_delete(k)<0){ tcol(RED); term_puts(k<0?"No such focus\n":"Built-in focuses cannot be deleted\n"); tcol(WHT); } else term_puts("Deleted\n"); }
        else { int k=focus_find(arg); if(k<0){ tcol(RED); term_puts("No such focus. Try: focus list\n"); tcol(WHT); } else { focus_set(k,0); p=b; p=scopy(p,focus_get(k)->name); p=scopy(p," is on\n"); *p=0; term_puts(b); } } }
    else if(seq(cmd,"sysfiles")){ extern void sysfiles_build(void); sysfiles_build(); term_puts("/System refreshed\n  ls /System    cat /System/README.txt    cat /System/Drivers/drivers.txt    cat /System/Logs/System.nvg\n"); }
    else if(seq(cmd,"faceid")){ const char* a=arg; while(*a==' ')a++; char sub[16]; int k=0; while(a[k]&&a[k]!=' '&&k<15){ sub[k]=a[k]; k++; } sub[k]=0; const char* v=a+k; while(*v==' ')v++;
        if(g_uid!=0&&!seq(sub,"status")&&sub[0]){ sh_err("faceid: ","only root can do that (try sudo)"); return; }
        if(seq(sub,"enroll")||seq(sub,"verify")){ if(fid_begin(seq(sub,"enroll")?FID_ENROLL:FID_VERIFY,v)<0)sh_err("faceid: ",g_fid_msg); else term_puts("faceid: started - see faceid status\n"); }
        else if(seq(sub,"stop")){ fid_end(); term_puts("faceid: stopped\n"); }
        else if(seq(sub,"delete")){ if(fid_delete(v)<0)sh_err("faceid: ","could not delete"); else term_puts("faceid: deleted\n"); }
        else if(seq(sub,"feed")){ if(!v[0]){ fid_feed_set(0,0,0); term_puts("faceid: using the camera again\n"); return; }
            int f=k_find_file(v); if(f<0||!FS[f].data||FS[f].lazy){ sh_err("faceid: file not found: ",v); return; } const u8* d=(const u8*)fsd(f); int n=FS[f].len, i=2, w=0, h=0, mx2=0;
            if(n<16||d[0]!='P'||d[1]!='6'){ sh_err("faceid: ","need a binary PPM (P6) file"); return; }
            int* tg[3]={&w,&h,&mx2}; for(int t=0;t<3;t++){ while(i<n&&(d[i]==' '||d[i]=='\n'||d[i]=='\r'||d[i]=='\t'))i++; while(i<n&&d[i]>='0'&&d[i]<='9'){ *tg[t]=*tg[t]*10+(d[i]-'0'); i++; } } i++;
            if(w<16||h<16||w>1920||h>1200||mx2!=255||i+w*h*3>n){ sh_err("faceid: ","unsupported PPM size"); return; }
            u32* px=(u32*)kmalloc((u64)w*h*4); if(!px){ sh_err("faceid: ","out of memory"); return; } for(int q=0;q<w*h;q++){ const u8* s=d+i+q*3; px[q]=((u32)s[0]<<16)|((u32)s[1]<<8)|s[2]; } fid_feed_set(px,w,h); term_puts("faceid: test picture added (used instead of the camera)\n"); }
        else { static const char* SN[6]={"idle","starting","looking","success","failed","error"}; char b[200],*p=b; p=scopy(p,"faceid: "); p=scopy(p,SN[g_fid_state>=0&&g_fid_state<6?g_fid_state:0]); p=scopy(p,g_fid_mode==FID_ENROLL?"  (set up)":g_fid_mode==FID_VERIFY?"  (sign-in)":""); p=scopy(p,"  saved "); p=udec(p,(u32)g_fid_count); p=scopy(p,"  distance "); if(g_fid_dist>=0)p=udec(p,(u32)g_fid_dist); else p=scopy(p,"-"); p=scopy(p,"  face "); p=scopy(p,g_fid_face[2]?"seen":"none"); if(g_fid_hint){ p=scopy(p,"  ("); p=scopy(p,g_fid_hint); p=scopy(p,")"); } if(g_fid_msg[0]){ p=scopy(p,"  - "); p=scopy(p,g_fid_msg); } *p++='\n'; *p=0; term_puts(b); } }
    else if(seq(cmd,"usbdevs")||seq(cmd,"lsusb")){ int any=0; for(int k=0;k<g_usbdev_n;k++){ if(!g_usbdev[k].used)continue; any=1; char b[140],*p=b; p=scopy(p,g_usbdev[k].kind); p=scopy(p,"  "); p=scopy(p,g_usbdev[k].name[0]?g_usbdev[k].name:"(no name)"); p=scopy(p,"  "); p=hx(p,g_usbdev[k].vid,4); *p++=':'; p=hx(p,g_usbdev[k].pid,4); p=scopy(p,"  port "); p=udec(p,(u32)g_usbdev[k].port+1); if(g_usbdev[k].cam)p=scopy(p,"  (video streaming not supported yet)"); *p=0; term_puts(b); term_putc('\n'); } if(!any)term_puts("no USB devices found (keyboards, mice, cameras, printers)\n"); }
    else if(cmd[0]=='l'&&cmd[1]=='x'&&cmd[2]=='t'&&cmd[3]=='r'&&cmd[4]=='a'&&cmd[5]=='c'&&cmd[6]=='e'){ extern int g_lxtrace; int n=0; const char* q=cmd+7; while(*q==' ')q++; while(*q>='0'&&*q<='9')n=n*10+(*q++-'0'); g_lxtrace=n?n:400; term_puts("syscall trace on (kernel log)\n"); }
    else if(seq(cmd,"disks")||seq(cmd,"disk")){
        gdisk_ensure();
        { int nb,ns,nfn; if(pci_find(0x01,0x08,-1,&nb,&ns,&nfn)){ char d[90],*q=d; tcol(rgb(255,210,90)); q=scopy(q,"NVMe controller @ bus ");q=udec(q,(u32)nb);q=scopy(q," slot ");q=udec(q,(u32)ns);q=scopy(q,"  init: ");q=scopy(q,nvme_ok?"OK":"FAILED stage ");if(!nvme_ok)q=udec(q,(u32)nvme_failstage);*q=0; term_puts(d); term_putc('\n'); tcol(WHT); } }
        { int ab,as,afn; if(pci_find(0x01,0x06,-1,&ab,&as,&afn)){ char d[90],*q=d; tcol(rgb(255,210,90)); q=scopy(q,"AHCI controller @ bus ");q=udec(q,(u32)ab);q=scopy(q," slot ");q=udec(q,(u32)as);*q=0; term_puts(d); term_putc('\n'); tcol(WHT); } }
        { extern const char* g_vmd_note; extern u32 g_vmd_dev; if(g_vmd_dev){ char d[120],*q=d; tcol(rgb(255,210,90)); q=scopy(q,"Intel VMD (RST) device 8086:"); { const char* hx="0123456789ABCDEF"; for(int k=12;k>=0;k-=4)*q++=hx[(g_vmd_dev>>k)&15]; } q=scopy(q,": "); q=scopy(q,g_vmd_note); *q=0; term_puts(d); term_putc('\n'); tcol(WHT); } }
        { char ab[80],*ap=ab; tcol(GREEN); ap=scopy(ap,"Active disk: ");ap=scopy(ap,(char*)gdisk_name()); if(g_dksectors){u32 mb=(u32)((u64)g_dksectors*512/1048576);ap=scopy(ap,"  ");ap=udec(ap,mb);ap=scopy(ap," MB");} ap=scopy(ap,"  (install target)"); *ap=0; term_puts(ab); term_putc('\n'); tcol(WHT); }
        if(!pci_n)pci_scan(); int found=0;
        for(int k=0;k<pci_n;k++){ if(pci_cls[k]!=0x01)continue; found++;
            u32 cl=pci_cfg(pci_bus[k],pci_slot[k],0,0x08); int prog=(cl>>8)&0xFF, sub=pci_sub[k];
            const char* kind= sub==0x01?"IDE/ATA": sub==0x05?"ATA": sub==0x06?"SATA (AHCI)": sub==0x08?"NVMe": sub==0x04?"RAID":"mass storage";
            char b[100],*p=b; p=scopy(p,"  "); p=hx(p,pci_ven[k],4);*p++=':';p=hx(p,pci_dev[k],4); p=scopy(p,"  "); p=scopy(p,kind); *p=0;
            tcol(CYAN); term_puts(b); tcol(WHT); term_putc('\n');
            if(sub==0x06 && prog==0x01){
                u32 c4=pci_cfg(pci_bus[k],pci_slot[k],0,0x04); pci_wcfg(pci_bus[k],pci_slot[k],0,0x04,c4|0x06);   /* MEM + bus master */
                u32 abar=pci_cfg(pci_bus[k],pci_slot[k],0,0x24)&0xFFFFFFF0u;
                if(!abar){ tcol(GREY); term_puts("    (ABAR invalid)\n"); tcol(WHT); continue; }
                volatile u32* A=(volatile u32*)(u64)abar; A[0x04>>2]|=(1u<<31);   /* GHC.AE */
                u32 pi=A[0x0C>>2]; int any=0;
                for(int port=0;port<32;port++){ if(!((pi>>port)&1))continue; char model[48]; u64 sec=0;
                    if(ahci_identify(A,port,model,&sec)){ any=1; u64 mb=sec>>11; char lb[120],*q=lb;
                        q=scopy(q,"    port "); q=udec(q,(u32)port); q=scopy(q,": "); q=scopy(q,model[0]?model:"(disk)"); q=scopy(q,"  ");
                        if(mb>=1024){ q=udec(q,(u32)(mb>>10)); q=scopy(q," GB"); } else { q=udec(q,(u32)mb); q=scopy(q," MB"); } *q=0;
                        tcol(GREEN); term_puts(lb); tcol(WHT); term_putc('\n'); } }
                if(!any){ tcol(GREY); term_puts("    (no responding SATA disk)\n"); tcol(WHT); }
            } else if(sub==0x08){ tcol(GREY); term_puts(nvme_ok?"    NVMe driver active (see NVMe controller line above)\n":"    NVMe present - driver did not start (see NVMe controller line above)\n"); tcol(WHT); }
        }
        if(!found){ tcol(rgb(240,150,120)); term_puts("no PCI mass-storage controller (eMMC/SD is not detected here)\n"); tcol(WHT); }
        parts_print(); }
    else if(seq(cmd,"netlog")){ int start=netlog_n>18?netlog_n-18:0; for(int i=start;i<netlog_n;i++){ tcol(GREY); term_puts("  "); term_puts(NETLOG[i%18]); term_putc('\n'); } tcol(WHT); if(netlog_n==0)term_puts("(empty)\n"); }
    else if(seq(cmd,"colors")){ const u32 cs[8]={rgb(220,60,60),rgb(230,150,40),rgb(230,220,60),rgb(70,200,90),rgb(70,180,230),rgb(120,110,230),rgb(210,90,210),rgb(230,230,230)};
        for(int k=0;k<8;k++){tcol(cs[k]);term_puts("##");} tcol(WHT); term_putc('\n'); }
    else if(seq(cmd,"settings")){ extern void settings_page_name(const char*); if(arg[0])settings_page_name(arg); open_win(W_SETTINGS); term_puts("opened Settings.\n"); }
    else if(seq(cmd,"camera")||seq(cmd,"webcam")){ extern int uvc_found(void); extern char uvc_msg[]; extern void camera_open(void); if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; } camera_open(); term_puts(uvc_found()?"Camera found - opening the Camera app.\n":"No camera found yet - the Camera app shows the USB devices that were detected.\n"); }
    else if(seq(cmd,"gpu3d")||seq(cmd,"gears")||seq(cmd,"glgears")){ if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; } open_win(W_GPU3D); term_puts("Gears is open (Start > Drivers & Hardware). It is written in C++.\n  1 gears, 2 3 4 more scenes, W wireframe, S shadows, H half resolution, Space pause, arrows and + - move the camera, R reset.\n  G starts the NVIDIA card (nvgsp all); then every frame is copied to the screen by its copy engine.\n"); }
    else if(seq(cmd,"quake")){ if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; } open_win(W_QUAKE); term_puts("Quake is starting (LibreQuake data, or your own pak0.pak if it is in Files).\n  Click inside to aim with the mouse, Esc frees it.\n  Move: WASD or arrows   Fire: left click or Ctrl   Jump: Space   Run: Shift   Weapons: 1-8   Menu: Esc\n"); }
    else if(seq(cmd,"writer")||seq(cmd,"word")){ extern int writer_open_slot(int); if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; } int sl=-1; if(arg[0]) for(int z=0;z<FS_N;z++) if(FS[z].used&&seq(FS[z].name,arg)){ sl=z; break; } if(sl>=0){ writer_open_slot(sl); term_puts("opened in Writer\n"); } else { open_win(W_WRITER); term_puts(arg[0]?"file not found - opened an empty Writer document\n":"Writer opened\n"); } }
    else if(seq(cmd,"keys")||seq(cmd,"keytest")){ extern volatile u32 g_klog[16]; extern volatile int g_klog_n; extern const char* g_lap_name;
        term_puts("Last special keys and laptop events (newest last). Press a key, then run 'keys' again:\n"); term_puts("  laptop driver: "); term_puts(g_lap_name); term_puts("\n");
        int n=g_klog_n, s0=n>16?n-16:0; if(n==0)term_puts("  (none yet)\n");
        for(int k=s0;k<n;k++){ u32 v=g_klog[k&15]; u32 ty=v>>16, c=v&0xFFFF; char b[80],*p=b; static const char* H="0123456789ABCDEF";
            p=scopy(p,ty==1?"  key E0 ":ty==2?"  key ":ty==3?"  ACPI brightness ":ty==4?"  ACPI event ":"  Lenovo VPC bits ");
            for(int sh=(ty==5?12:4);sh>=0;sh-=4)*p++=H[(c>>sh)&15]; *p=0; term_puts(b); term_puts("\n"); } }
    else if(seq(cmd,"recorder")||seq(cmd,"record")){ extern const char* snd_rec_name(void); if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; } open_win(W_RECORDER); term_puts("Voice Recorder opened. Input: "); term_puts(snd_rec_name()); term_puts("\n"); }
    else if(seq(cmd,"screenshot")||seq(cmd,"scrot")){ extern void ss_cmd(const char*); if(g_state==ST_LIVE&&arg[0]!='l'){ term_puts("Type 'desktop' first, then use PrtSc or 'screenshot'.\n"); } else { ss_cmd(arg); if(arg[0]!='l')term_puts(arg[0]=='s'?"Screenshot of the whole screen taken.\n":arg[0]=='w'?"Screenshot of the window under the pointer taken.\n":"Screenshot tool opened: drag an area, or press S (screen) or W (window), Enter to take it, Esc to cancel.\n"); } }
    else if(seq(cmd,"disktest")){ gdisk_ensure(); if(!g_dksectors){ term_puts("disktest: no disk\n"); return; } extern u32 g_nvme_cmds, g_nvme_multi; extern int gdisk_readn(u32,int,u8*);
        u8* b=(u8*)kmalloc(65536); if(!b){ term_puts("disktest: no memory\n"); return; } u32 tot=65536u; if(tot>g_dksectors)tot=g_dksectors; u32 c0=g_nvme_cmds, m0=g_nvme_multi; u64 t0=g_ticks; u32 n=0; int bad=0;
        for(u32 l=0;l<tot;l+=128){ int c=(int)(tot-l>128?128:tot-l); if(gdisk_readn(l,c,b)){ bad=1; break; } n+=(u32)c; } u64 dt=g_ticks-t0; if(!dt)dt=1; kfree(b);
        char o[160],*q=o; q=scopy(q,"disktest: read "); q=udec(q,n/2048); q=scopy(q," MB from "); q=scopy(q,(char*)gdisk_name()); q=scopy(q," in "); q=udec(q,(u32)(dt*10)); q=scopy(q," ms = "); q=udec(q,(u32)((u64)n*512/1024*100/dt/1024)); q=scopy(q," MB/s");
        if(g_nvme_cmds!=c0){ q=scopy(q,", NVMe commands "); q=udec(q,g_nvme_cmds-c0); q=scopy(q," ("); q=udec(q,g_nvme_multi-m0); q=scopy(q," multi-block)"); } if(bad)q=scopy(q,"  READ ERROR"); *q++='\n'; *q=0; term_puts(o); }
    else if(seq(cmd,"bench")||seq(cmd,"benchmark")){ extern void bench_start(int); if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; } open_win(W_BENCH); if(arg[0]=='c')bench_start(1); else if(arg[0]=='g')bench_start(2); term_puts(arg[0]=='c'?"CPU test started in the Benchmark app.\n":arg[0]=='g'?"GPU test started in the Benchmark app.\n":"Benchmark opened. Use 'bench cpu' or 'bench gpu' to start a test.\n"); }
    else if(seq(cmd,"doom")){ extern int g_doom_state; if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; } open_win(W_DOOM); term_puts(g_doom_state==3?"DOOM could not load a WAD - see the DOOM window.\n":"DOOM is starting. Click inside to aim with the mouse, Esc frees it.\n  Move: arrows or WASD   Fire: Ctrl or left click   Use/open: Space or E\n  Run: Shift   Weapons: 1-7   Menu: Esc   Map: Tab\n"); }
    else if(seq(cmd,"taskmgr")||seq(cmd,"taskmanager")){ extern void taskmgr_tab(int,int); int t=(arg[0]=='p'&&arg[1]=='e')?1:arg[0]=='d'?2:arg[0]=='s'?3:arg[0]=='p'?0:-1; int dv=-1; { const char* q=arg; while(*q&&*q!=' ')q++; while(*q==' ')q++; if(*q>='0'&&*q<='9'){ dv=*q-'0'; if(q[1]>='0'&&q[1]<='9')dv=dv*10+q[1]-'0'; } } taskmgr_tab(t,dv); open_win(W_SYSMON); term_puts("opened Task Manager.\n"); }
    else if(cmd[0]=='l'&&cmd[1]=='u'&&cmd[2]=='a'&&(cmd[3]==' '||cmd[3]==0)){
        if(cmd[3]==0){ tcol(CYAN); term_puts("Lua 5.5.1 - usage: lua <code>\n  console: lua print(2^10, math.sqrt(144))\n  window:  lua nvx.window(\"Hi\",400,300) nvx.clear(nvx.rgb(30,40,60)) nvx.text(20,20,\"Hello from Lua\",0xFFFFFF) nvx.present() nvx.waitkey()\n"); tcol(WHT); }
        else { const char* out=nvx_lua_run(cmd+4); tcol(rgb(180,230,180)); term_puts(out); if(out[0]&&out[slen(out)-1]!='\n')term_putc('\n'); tcol(WHT); } }
    else if(seq(cmd,"presentation")||seq(cmd,"slides")||seq(cmd,"pptx")){ int sl=-1; for(int z=0;z<FS_N;z++)if(FS[z].used&&fext_eq(FS[z].name,"pptx")){sl=z;break;} if(sl>=0){pptx_load(sl);open_win(W_PPTX);term_puts("opened Presentation.\n");} else term_puts("no .pptx file found (put one on a USB stick).\n"); }
    else if(seq(cmd,"reboot")||seq(cmd,"restart")){ term_puts("rebooting...\n"); blit_all(); power_reboot(); }
    else if(seq(cmd,"shutdown")||seq(cmd,"poweroff")||seq(cmd,"halt")){ term_puts("shutting down...\n"); blit_all(); power_off(); }
    else if(seq(cmd,"suspend")){ term_puts("sleeping - press a key (or the power button) to wake\n"); blit_all(); int r=power_sleep(); term_puts(r?"woke from ACPI S1 sleep\n":"woke up (this computer has no ACPI S1; the screen was turned off)\n"); }
    else if(seq(cmd,"kcheck")){ kcheck_run(); }
    else if(seq(cmd,"memmap")){ memmap_run(); }
    else if(seq(cmd,"vminfo")){ extern void vminfo_run(void); vminfo_run(); }
    else if(seq(cmd,"tpinfo")){ if(arg[0]=='s')tpinfo_run(1); else if(arg[0]=='d'){ extern void tpinfo_dump(void); tpinfo_dump(); } else tpinfo_run(0); }
    else if(seq(cmd,"touchpad")){ extern int g_tp_tap, g_tp_speed, g_tp_natural, g_tp_dwt, g_tp_zones;
        char w1[16]; int k=0; const char* q=arg; while(*q&&*q!=' '&&k<15)w1[k++]=*q++; w1[k]=0; while(*q==' ')q++;
        int on=(q[0]=='o'&&q[1]=='n')||q[0]=='1'||q[0]=='n';
        if(seq(w1,"tap"))g_tp_tap=on; else if(seq(w1,"zones"))g_tp_zones=on; else if(seq(w1,"typing"))g_tp_dwt=on;
        else if(seq(w1,"scroll"))g_tp_natural=(q[0]=='n');
        else if(seq(w1,"speed")){ int v=0; while(*q>='0'&&*q<='9')v=v*10+(*q++-'0'); if(v>=1&&v<=10)g_tp_speed=v; }
        else if(w1[0]){ term_puts("usage: touchpad [tap on|off] [speed 1-10] [scroll natural|classic] [zones on|off] [typing on|off]\n"); }
        char b[160],*p=b; p=scopy(p,"touchpad: tap-to-click "); p=scopy(p,g_tp_tap?"on":"off"); p=scopy(p,"  speed "); p=udec(p,(u32)g_tp_speed); p=scopy(p,"  scroll "); p=scopy(p,g_tp_natural?"natural":"classic");
        p=scopy(p,"  click zones "); p=scopy(p,g_tp_zones?"on":"off"); p=scopy(p,"  ignore while typing "); p=scopy(p,g_tp_dwt?"on":"off"); *p=0; term_puts(b); term_putc('\n'); }
    else if(seq(cmd,"explorer")||seq(cmd,"files")||seq(cmd,"thispc")){ open_win(W_FILES); if(seq(cmd,"thispc")||arg[0]=='t'){ extern void fx_do(int,int); fx_do(20,-2); } term_puts("opened File Explorer.\n"); }
    else if(seq(cmd,"wallpaper")){ extern int g_theme, g_wallmode; extern void build_bg(void); static const char* WN6[6]={"porsche","crane","wireframe","peaks","earth","yosemite"}; int t=-1;
        if(arg[0]>='0'&&arg[0]<='5')t=arg[0]-'0'; else for(int k=0;k<6;k++) if(seq(arg,WN6[k]))t=k;
        if(t<0) term_puts("usage: wallpaper porsche|crane|wireframe|peaks|earth|yosemite (or 0-5)\n"); else { g_theme=t; g_wallmode=0; build_bg(); term_puts("wallpaper changed\n"); sh_redraw(); } }
    else if(seq(cmd,"gpuinfo")){ extern void gpuinfo_run(void); gpuinfo_run(); }
    else if(seq(cmd,"gpu")){ extern int igpu_flip_on(void); extern void igpu_flip_off(const char*); extern void gpuinfo_run(void);
        if(arg[0]=='f'&&arg[5]=='o'&&arg[6]=='f'){ igpu_flip_off("turned off by the user"); term_puts("tear-free drawing off\n"); }
        else if(arg[0]=='b'&&arg[5]=='g'){ extern void igpu_blt_mode(int); igpu_blt_mode(1); term_puts("screen copies by the GPU blitter (if it is ready - see gpuinfo)\n"); }
        else if(arg[0]=='b'&&arg[5]=='c'){ extern void igpu_blt_mode(int); igpu_blt_mode(0); term_puts("screen copies by the CPU\n"); }
        else if(arg[0]=='f'&&arg[5]=='o'&&arg[6]=='n'){ term_puts(igpu_flip_on()==0?"tear-free drawing on\n":"tear-free drawing could not start - see gpuinfo\n"); }
        else if(arg[0]=='d'&&arg[1]=='r'){ extern int igpu_draw_mode(int); const char* m=arg+5; int r=-1;
            if(m[0]=='o'&&m[1]=='n'){ r=igpu_draw_mode(1); term_puts(r==0?"GPU drawing on\n":"GPU drawing is not available - see gpuinfo\n"); }
            else if(m[0]=='o'&&m[1]=='f'){ igpu_draw_mode(0); term_puts("GPU drawing off - the CPU draws\n"); }
            else if(m[0]=='e'){ r=igpu_draw_mode(2); term_puts(r==0?"GPU drawing emulated on the CPU (test mode)\n":"could not start the emulation\n"); }
            else term_puts("usage: gpu draw on|off|emu\n"); sh_redraw(); }
        else if(arg[0]=='c'&&arg[1]=='h'){ extern int frame_check_cmd(void); extern char g_fc_msg[]; if(frame_check_cmd()) term_puts("open the desktop and run 'gpu check' from its terminal (GPU drawing must be on)\n"); else { term_puts(g_fc_msg); term_putc('\n'); } sh_redraw(); }
        else if(arg[0]=='l'&&arg[1]=='i'){ extern int g_wall_light; g_wall_light=(arg[6]=='o'&&arg[7]=='n'); term_puts(g_wall_light?"drifting light on the wallpaper on\n":"drifting light off\n"); sh_redraw(); }
        else if(arg[0]=='w'&&arg[1]=='a'){ extern int g_wall_fx; extern void build_bg(void); g_wall_fx=(arg[7]=='o'&&arg[8]=='n'); build_bg(); term_puts(g_wall_fx?"wallpaper effects on\n":"wallpaper effects off (plain picture)\n"); sh_redraw(); }
        else if(arg[0]=='p'&&arg[1]=='r'){ extern int g_pres_eng; g_pres_eng=(arg[8]=='s'); term_puts(g_pres_eng?"screen copies by the shader\n":"screen copies by the blitter\n"); sh_redraw(); }
        else if(arg[0]=='p'&&arg[1]=='a'){ extern int g_gfx_srec; g_gfx_srec=!(arg[9]=='f'); term_puts(g_gfx_srec?"partial screen redraws on\n":"partial screen redraws off - the whole screen is rebuilt every frame\n"); sh_redraw(); }
        else if(arg[0]=='f'&&arg[1]=='x'){ extern int g_gfx_fx; g_gfx_fx=(arg[3]=='o'&&arg[4]=='n'); term_puts(g_gfx_fx?"effects on\n":"effects off\n"); sh_redraw(); }
        else gpuinfo_run(); }
    else if(seq(cmd,"tpdump")){ extern void tpinfo_dump(void); tpinfo_dump(); }
    else if(seq(cmd,"memtest")){ memtest_run(); }
    else if(seq(cmd,"usbinfo")){ char b[200],*p;
        if(!g_xhc_n){ term_puts("USB: no xHCI controller found\n"); }
        p=b; p=scopy(p,"USB controllers (xHCI): "); p=udec(p,(u32)g_xhc_n); *p=0; term_puts(b); term_putc('\n');
        for(int k=0;k<g_xhc_n;k++){ xhc_select(k);
            p=b; p=scopy(p," controller "); p=udec(p,(u32)(k+1)); p=scopy(p," at bus "); p=udec(p,(u32)xhci_pci_bus); p=scopy(p," dev "); p=udec(p,(u32)xhci_pci_slot); p=scopy(p," fn "); p=udec(p,(u32)xhci_pci_fn);
            p=scopy(p,xhci_hi4g?"  - registers above 4 GB could not be mapped":xhci_init_ok?"  ready":"  setup failed"); if(!xhci_hi4g){ p=scopy(p,"  scratchpad "); p=udec(p,(u32)g_xspb_need[k]); } *p=0; if(!xhci_init_ok)tcol(rgb(255,205,100)); term_puts(b); term_putc('\n'); tcol(WHT);
            if(!xhci_init_ok)continue;
            int anyp=0;
            for(int q=0;q<xhci_ports_hw&&q<32;q++){ u32 v=XMR(xhci_op+0x400+(u32)q*0x10); if(!(v&1))continue; anyp=1; int sp=(int)((v>>10)&0xF), used=0;
                for(int i=0;i<xhid_n;i++) if(xhid_used[i]&&xhid_ctl[i]==k&&xhid_port[i]==q)used=1;
                if(msd_ok&&msd_ctl==k&&msd_port==q)used=2;
                p=b; p=scopy(p,"   port "); p=udec(p,(u32)(q+1)); p=scopy(p,sp==1?"  full speed (USB 1.1)":sp==2?"  low speed (USB 1.1)":sp==3?"  high speed (USB 2.0)":sp>=4?"  SuperSpeed (USB 3)":"  unknown speed");
                p=scopy(p,used==1?"  -> keyboard/mouse set up":used==2?"  -> USB stick":"  -> NOT set up"); *p=0; if(!used)tcol(rgb(255,205,100)); term_puts(b); term_putc('\n'); tcol(WHT);
                for(int i=0;i<xhid_n;i++){ if(!xhid_used[i]||xhid_ctl[i]!=k||xhid_port[i]!=q)continue; int kb=0,ms=0;
                    if(xhid_rep[i]){ for(int z=0;z<xhid_nlay[i];z++){ if(xhid_lay[i][z].kind==1)kb=1; if(xhid_lay[i][z].kind==2)ms=1; } } else { kb=xhid_iskbd[i]; ms=!kb; }
                    p=b; p=scopy(p,"      "); if(xhid_depth[i]){ p=scopy(p,"behind hub (route "); p=hx(p,xhid_route[i],5); p=scopy(p,")  "); } p=scopy(p,"ep "); p=udec(p,(u32)xhid_dci[i]); p=scopy(p,"  "); p=scopy(p,kb&&ms?"keyboard+mouse":kb?"keyboard":"mouse"); p=scopy(p,xhid_rep[i]?(xhid_hasid[i]?"  report protocol (IDs)":"  report protocol"):"  boot protocol");
                    p=scopy(p,"  reports: "); p=udec(p,xhid_cnt[i]); *p=0; term_puts(b); term_putc('\n');
                    if(xhid_lastn[i]){ p=b; p=scopy(p,"        last:"); for(int z=0;z<xhid_lastn[i];z++){ *p++=' '; *p++="0123456789ABCDEF"[xhid_last[i][z]>>4]; *p++="0123456789ABCDEF"[xhid_last[i][z]&15]; } *p=0; term_puts(b); term_putc('\n'); } } }
            if(!anyp) term_puts("   no devices plugged in\n");
            for(int h=0;h<XHUB_MAX;h++){ if(!xhub[h].used||xhub[h].ctl!=k)continue;
                p=b; p=scopy(p,"   hub on port "); p=udec(p,(u32)(xhub[h].root+1)); if(xhub[h].depth){ p=scopy(p," (route "); p=hx(p,xhub[h].route,5); *p++=')'; } p=scopy(p,": "); p=udec(p,(u32)xhub[h].nports); p=scopy(p,xhub[h].ss?" ports, USB 3":xhub[h].speed==3?" ports, USB 2.0":" ports, USB 1.1"); p=scopy(p,"  connected:");
                int anyc=0; for(int q=1;q<=xhub[h].nports;q++) if((xhub[h].conn>>q)&1u){ *p++=' '; p=udec(p,(u32)q); anyc=1; } if(!anyc)p=scopy(p," none"); *p=0; term_puts(b); term_putc('\n'); } }
        if(g_xhc_n)xhc_select(0);
        if(xhci_failstep[0]){ term_puts(" last setup problem: "); term_puts(xhci_failstep); term_putc('\n'); }
        term_puts(inb(0x64)==0xFF?"PS/2: no controller\n":"PS/2: controller present\n"); }
    else if(seq(cmd,"mousetest")){
        term_puts("Move the mouse, click and scroll. Press any key to stop.\n"); sh_redraw();
        while(kpop()>=0){}
        long tx=0,ty=0; int ax=0,ay=0,aw=0,lb=-1,lr=-1,lm=-1,evs=0; u64 t0=g_ticks, tp=g_ticks; long spins=0;
        for(;;){
            if(xhid_n) xhci_mouse_poll();
            int dx=0,dy=0,sc=0,bl=0,br=0,bm=0,ev=0;
            cpu_irq_off(); if(mouse_evt){ dx=mouse_dx; dy=mouse_dy; sc=mouse_scroll; bl=mouse_btn; br=mouse_rbtn; bm=mouse_mid; mouse_dx=0; mouse_dy=0; mouse_scroll=0; mouse_evt=0; ev=1; } cpu_irq_on(); { extern void in_mq_clear(void); in_mq_clear(); }
            if(ev){ ax+=dx; ay+=dy; aw+=sc; tx+=dx; ty+=dy; evs++;
                int btnchg=(bl!=lb||br!=lr||bm!=lm);
                if(btnchg||(g_ticks_live?(g_ticks-tp>=5):(evs%20==0))){ char b[160],*p=b; tp=g_ticks;
                    p=scopy(p,"  move "); if(ax<0){*p++='-';p=udec(p,(u32)-ax);}else{*p++='+';p=udec(p,(u32)ax);} *p++=','; if(ay<0){*p++='-';p=udec(p,(u32)-ay);}else{*p++='+';p=udec(p,(u32)ay);}
                    p=scopy(p,"   total "); if(tx<0){*p++='-';p=udec(p,(u32)-tx);}else p=udec(p,(u32)tx); *p++=','; if(ty<0){*p++='-';p=udec(p,(u32)-ty);}else p=udec(p,(u32)ty);
                    p=scopy(p,"   buttons "); *p++=bl?'L':'-'; *p++=bm?'M':'-'; *p++=br?'R':'-'; if(aw){ p=scopy(p,"   wheel "); if(aw<0){*p++='-';p=udec(p,(u32)-aw);}else{*p++='+';p=udec(p,(u32)aw);} }
                    *p=0; term_puts(b); term_putc('\n'); sh_redraw(); ax=ay=aw=0; lb=bl; lr=br; lm=bm; } }
            if(kpop()>=0)break;
            spins++; if(g_ticks_live?(g_ticks-t0>3000):(spins>400000000L))break;
            cpu_pause(); }
        { char b[80],*p=b; p=scopy(p,"mouse test ended ("); p=udec(p,(u32)evs); p=scopy(p," mouse events)"); *p=0; term_puts(b); term_putc('\n'); } }
    else if(seq(cmd,"acpi")){ char b[96],*p;
        if(!g_acpi.rsdp){ term_puts("acpi: no ACPI tables found\n"); }
        else { p=b; p=scopy(p,"acpi: revision "); p=udec(p,(u32)g_acpi.rev); p=scopy(p,g_acpi.hwred?"  (hardware-reduced)":""); *p=0; term_puts(b); term_putc('\n');
            p=b; p=scopy(p,"  PM1a_CNT 0x"); p=hx(p,g_acpi.pm1a_cnt,4); p=scopy(p,"  SMI_CMD 0x"); p=hx(p,g_acpi.smi_cmd,4); *p=0; term_puts(b); term_putc('\n');
            p=b; p=scopy(p,"  shutdown (S5): "); p=scopy(p,g_acpi.have_s5?"yes":"no"); p=scopy(p,"   sleep (S1): "); p=scopy(p,g_acpi.have_s1?"yes":"no"); p=scopy(p,"   reset register: "); p=scopy(p,g_acpi.rst_addr?"yes":"no"); *p=0; term_puts(b); term_putc('\n'); } }
    else if(seq(cmd,"ntfs")){
        char sub[16]; int si=0; const char* a=arg; while(*a==' ')a++; while(*a&&*a!=' '&&si<15)sub[si++]=*a++; sub[si]=0; while(*a==' ')a++;
        char p1[200]; int pi=0; while(*a&&*a!=' '&&pi<199)p1[pi++]=*a++; p1[pi]=0; while(*a==' ')a++;
        const char* rest=a;
        if(!sub[0]||seq(sub,"help")){ tcol(ACC); term_puts("ntfs mount [ro] | umount | info | ls [path] | cat <path> | write <path> <text> | mv <old> <new> | mkdir <path> | rm <path>\n"); tcol(WHT); }
        else if(!seq(sub,"ls")&&!seq(sub,"cat")&&!seq(sub,"info")&&!seq(sub,"help")&&sub[0]&&g_uid!=0){ need_root("ntfs"); }
        else if(seq(sub,"mount")&&g_rootfs&&g_rfs==&g_rfs_ntfs){ term_puts("ntfs: the system disk is NTFS and already mounted - use ntfs ls / cat directly\n"); }
        else if(seq(sub,"mount")){ int ro=seq(p1,"ro"); int r=nk_mount(ro);
            if(r==-2){ tcol(rgb(240,120,120)); term_puts("ntfs: no NTFS volume found on the disk\n"); tcol(WHT); }
            else if(r){ tcol(rgb(240,120,120)); term_puts("ntfs: mount failed (see serial log)\n"); tcol(WHT); }
            else { tcol(GREEN); term_puts("ntfs: mounted "); term_puts(ro?"read-only":"read-write"); tcol(WHT); if(nvxntfs_label()[0]){ term_puts("  label: "); term_puts(nvxntfs_label()); } term_putc('\n'); } }
        else if(!nvxntfs_mounted()){ tcol(rgb(240,120,120)); term_puts("ntfs: not mounted - run 'ntfs mount' first\n"); tcol(WHT); }
        else if(seq(sub,"umount")||seq(sub,"unmount")){ nvxntfs_umount(); term_puts("ntfs: unmounted, all changes written\n"); }
        else if(seq(sub,"info")){ term_puts("ntfs: mounted"); if(nvxntfs_label()[0]){ term_puts(", label "); term_puts(nvxntfs_label()); } term_putc('\n'); }
        else if(seq(sub,"ls")){ if(nvxntfs_ls(p1[0]?p1:"/",nk_lscb,0)<0){ tcol(rgb(240,120,120)); term_puts("ntfs: no such directory\n"); tcol(WHT); } }
        else if(seq(sub,"cat")){ static char nb[65536]; long n=nvxntfs_read(p1,nb,65535); if(n<0){ tcol(rgb(240,120,120)); term_puts("ntfs: cannot read "); term_puts(p1); term_putc('\n'); tcol(WHT); } else { nb[n]=0; term_puts(nb); if(n&&nb[n-1]!='\n')term_putc('\n'); } }
        else if(seq(sub,"write")){ long L=0; while(rest[L])L++; char wb[512]; int k=0; for(;k<L&&k<510;k++)wb[k]=rest[k]; wb[k++]='\n'; wb[k]=0;
            if(nvxntfs_write(p1,wb,k)!=k){ tcol(rgb(240,120,120)); term_puts("ntfs: write failed\n"); tcol(WHT); } else term_puts("ntfs: written\n"); }
        else if(seq(sub,"mv")){ char p2[200]; int k=0; while(rest[k]&&rest[k]!=' '&&k<199){p2[k]=rest[k];k++;} p2[k]=0;
            if(!p2[0]||nvxntfs_rename(p1,p2)){ tcol(rgb(240,120,120)); term_puts("ntfs: rename failed (source missing or target exists)\n"); tcol(WHT); } else term_puts("ntfs: renamed\n"); }
        else if(seq(sub,"mkdir")){ if(nvxntfs_mkdir(p1)){ tcol(rgb(240,120,120)); term_puts("ntfs: mkdir failed\n"); tcol(WHT); } }
        else if(seq(sub,"rm")){ if(nvxntfs_delete(p1)){ tcol(rgb(240,120,120)); term_puts("ntfs: cannot remove "); term_puts(p1); term_putc('\n'); tcol(WHT); } }
        else { term_puts("ntfs: unknown command - try 'ntfs help'\n"); } }
    else if(seq(cmd,"panic")){ panic(0,"Manual panic requested via 'panic' command."); }
    else { tcol(rgb(240,120,120)); term_puts("nsh: "); term_puts(cmd); term_puts(": command not found\n"); tcol(WHT); g_lastrc=1; return; }
}
 
void wpa_connect_saved(u32 m,int idx,const char* pw){
    if(idx<0||idx>=g_apn){ term_puts("  bad network id\n"); return; }
    if(!m)m=rtw_find_mmio();
    if(!m||!g_rxnum){ tcol(rgb(230,120,90)); term_puts("  run wifi init/allbuilt/macinit/fw/rings first\n"); tcol(WHT); return; }
    char b[96],*p; p=b;p=scopy(p,"  [wifi wpa] connecting to \"");p=scopy(p,g_aps[idx].ssid[0]?g_aps[idx].ssid:"<hidden>");p=scopy(p,"\" on ch ");p=udec(p,g_aps[idx].ch);p=scopy(p,"...");*p=0;
    tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT);
    g_chquiet=1;
    int r;
    if(g_aps[idx].sec==0) r=rtw_assoc(m,&g_aps[idx],g_aps[idx].ssid);
    else r=rtw_wpa2_join(m,&g_aps[idx],g_aps[idx].ssid,pw);
    g_chquiet=0;
    if(r==0){ tcol(GREEN); term_puts(g_aps[idx].sec==0?"  *** CONNECTED (open) ***\n":"  *** CONNECTED - 4-way handshake complete ***\n"); tcol(WHT); }
    else if(r==1){ tcol(rgb(230,160,60)); term_puts("  no auth response (AP not answering our TX)\n"); tcol(WHT); }
    else if(r==2){ tcol(rgb(230,160,60)); term_puts("  auth rejected\n"); tcol(WHT); }
    else if(r==3){ tcol(rgb(230,160,60)); term_puts("  no assoc response\n"); tcol(WHT); }
    else if(r==4){ tcol(rgb(230,160,60)); term_puts("  assoc rejected\n"); tcol(WHT); }
    else if(r==5){ tcol(rgb(230,160,60)); term_puts("  no EAPOL msg1 (associated, no handshake)\n"); tcol(WHT); }
    else if(r==6){ tcol(rgb(230,160,60)); term_puts("  wrong password (handshake MIC rejected)\n"); tcol(WHT); }
    else { tcol(rgb(230,160,60)); term_puts("  handshake failed\n"); tcol(WHT); }
}
void wpa_do_join(const char* ss,const char* pw){
    u32 m=rtw_find_mmio();
    if(!m){ tcol(rgb(230,120,90)); term_puts("  no Wi-Fi adapter\n"); tcol(WHT); return; }
    if(!g_rxnum||!g_benum){ tcol(rgb(230,120,90)); term_puts("  run 'wifi rings' first (after init/allbuilt/macinit/fw)\n"); tcol(WHT); return; }
    char b[128],*p;
    p=b;p=scopy(p,"  [wifi wpa] joining \"");p=scopy(p,(char*)ss);p=scopy(p,"\" ...");*p=0; tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT);
    g_chquiet=1;
    g_apn=0; rtw_rx_ring_up(m);
    static const u8 wc[]={1,6,11,2,3,4,5,7,8,9,10,12,13};
    int tgt=-1;
    term_puts("  scanning: "); 
    for(int ci=0;ci<13 && tgt<0;ci++){ rtw_set_channel_2g(m,wc[ci]); ui_pump();
        { char cb[6]; char* z=cb; z=udec(z,wc[ci]); *z++=' '; *z=0; tcol(GREY); term_puts(cb); tcol(WHT); }
        for(int w=0;w<4;w++){ rtw_delay_us(62000); ui_pump(); rtw_scan_drain(m,wc[ci]); }
        for(int q=0;q<g_apn;q++){ int eq=1,k=0; while(ss[k]&&k<32){ char x=g_aps[q].ssid[k],y=ss[k]; if(x>='A'&&x<='Z')x+=32; if(y>='A'&&y<='Z')y+=32; if(x!=y)eq=0;k++;} if(g_aps[q].ssid[k]||ss[k])eq=0; if(eq){tgt=q;break;} } }
    term_putc('\n');
    g_chquiet=0;
    if(tgt<0){ tcol(rgb(230,160,60)); term_puts("  network not found. networks seen this scan:\n"); tcol(WHT);
        for(int q=0;q<g_apn;q++){ char* z=b; z=scopy(z,"    "); z=scopy(z,g_aps[q].ssid[0]?g_aps[q].ssid:"<hidden>"); z=scopy(z, g_aps[q].sec==0?"  (open)":g_aps[q].sec==2?"  (WPA2)":"  (WEP)"); *z=0; tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT); }
        if(g_apn==0) term_puts("    (none - move closer / re-run)\n"); return; }
    p=b;p=scopy(p,"  found on ch ");p=udec(p,g_aps[tgt].ch);p=scopy(p,"  deriving PMK (PBKDF2 4096, ~1s)...");*p=0; tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
    g_chquiet=1; int r=rtw_wpa2_join(m,&g_aps[tgt],ss,pw); g_chquiet=0;
    if(r==0){ tcol(GREEN); term_puts("  *** 4-WAY HANDSHAKE COMPLETE - PTK derived, keys ready ***\n"); tcol(WHT); }
