static void battery_cmd(void){
    char b[160],*p; acpi_battery_update(1);
    tcol(CYAN); p=b; p=scopy(p,"Battery: "); if(g_acb_pct>=0){ p=udec(p,(u32)g_acb_pct); p=scopy(p,"%, "); p=scopy(p,g_acb_charging?"charging":"on battery"); if(g_acb_mins>=0){ p=scopy(p,", "); p=udec(p,(u32)(g_acb_mins/60)); p=scopy(p," h "); p=udec(p,(u32)(g_acb_mins%60)); p=scopy(p,g_acb_charging?" min to full":" min left"); } } else p=scopy(p,"not present"); *p++='\n'; *p=0; term_puts(b); tcol(WHT);
    p=b; p=scopy(p,"  AC adapter: "); p=scopy(p,!g_acad?"not described by firmware":g_ac_online==1?"plugged in":g_ac_online==0?"unplugged":"unknown"); *p++='\n'; *p=0; term_puts(b);
    for(int i=0;i<g_acb_n;i++){ acbat* x=&g_acb[i]; char path[80]; aml_path(x->dev,path,80); p=b; p=scopy(p,"  "); p=scopy(p,path); p=scopy(p,x->present?"":"  (slot empty)"); *p++='\n'; *p=0; term_puts(b); if(!x->present)continue;
        const char* u=x->mwh?" mWh":" mAh"; const char* r=x->mwh?" mW":" mA";
        p=b; p=scopy(p,"    "); p=scopy(p,x->model[0]?x->model:"?"); p=scopy(p,"  "); p=scopy(p,x->type); p=scopy(p,"  "); p=scopy(p,x->oem); if(x->serial[0]){ p=scopy(p,"  serial "); p=scopy(p,x->serial); } *p++='\n'; *p=0; term_puts(b);
        p=b; p=scopy(p,"    remaining "); p=udec(p,x->remain); p=scopy(p,u); p=scopy(p," of "); p=udec(p,x->full); p=scopy(p,u); p=scopy(p," (design "); p=udec(p,x->design); p=scopy(p,u); p=scopy(p,")\n"); *p=0; term_puts(b);
        p=b; p=scopy(p,"    rate "); if(x->rate==0xFFFFFFFFu)p=scopy(p,"unknown"); else { p=udec(p,x->rate); p=scopy(p,r); } p=scopy(p,", voltage "); p=udec(p,x->pvolt); p=scopy(p," mV, state "); p=acx64(p,x->state);
        if(x->cycles&&x->cycles!=0xFFFFFFFFu){ p=scopy(p,", "); p=udec(p,x->cycles); p=scopy(p," cycles"); } *p++='\n'; *p=0; term_puts(b); } }
static void brightness_cmd(const char* arg){
    if(!g_bl.ok){ term_puts("no controllable backlight found (no _BCM method and no Intel PWM)\n"); return; }
    if(arg[0]=='u')bl_step(1); else if(arg[0]=='d')bl_step(-1); else if(arg[0]>='0'&&arg[0]<='9'){ const char* a=arg; bl_set((int)acpi_parse_num(&a)); }
    char b[120],*p=b; p=scopy(p,"brightness "); p=udec(p,(u32)g_bl.cur); p=scopy(p,"%  levels:"); for(int i=0;i<g_bl.nlv&&i<40;i++){ *p++=' '; p=udec(p,g_bl.lv[i]); } if(!g_bl.out)p=scopy(p," (PWM, any 5-100)"); *p++='\n'; *p=0; term_puts(b); }
static char* hr_fix(char* p,u64 v,u64 div,int dec){ p=udec(p,(u32)(v/div)); if(dec){ *p++='.'; u64 f=v%div; for(int i=0;i<dec;i++){ div/=10; if(!div)break; *p++=(char)('0'+(f/div)%10); } } return p; }
static void timers_cmd(void){
    char b[160],*p; tcol(CYAN); term_puts("Timers\n"); tcol(WHT);
    static const char* refn[5]={"nothing","HPET","PIT channel 2","CPUID crystal","CPUID base clock"};
    p=b; p=scopy(p,"  clock: "); p=scopy(p,g_hr.src==2?(g_hr.tsc_inv?"invariant TSC":"TSC (may drift with power states)"):g_hr.src==1?"HPET":"100 Hz PIT ticks (no precise timer)"); *p++='\n'; *p=0; term_puts(b);
    p=b; p=scopy(p,"  TSC: "); if(g_hr.tsc_hz){ p=hr_fix(p,g_hr.tsc_hz,1000000000ULL,3); p=scopy(p," GHz, measured with "); p=scopy(p,refn[g_hr.ref<5?g_hr.ref:0]); } else p=scopy(p,"not usable"); *p++='\n'; *p=0; term_puts(b);
    p=b; p=scopy(p,"  HPET: "); if(g_hr.hpet_ok){ p=scopy(p,"0x"); p=hx(p,(u32)(g_hr.hpet_pa>>32),8); p=hx(p,(u32)g_hr.hpet_pa,8); p=scopy(p,", "); p=hr_fix(p,1000000000000000000ULL/g_hr.hpet_fs,1000000ULL,3); p=scopy(p," MHz, "); p=scopy(p,g_hr.hpet_64?"64":"32"); p=scopy(p,"-bit, "); p=udec(p,(u32)g_hr.hpet_ntim); p=scopy(p," comparators"); }
        else { p=scopy(p,"not used ("); p=scopy(p,g_hr.why[0]?g_hr.why:"?"); *p++=')'; } *p++='\n'; *p=0; term_puts(b);
    p=b; p=scopy(p,"  local APIC timer: "); if(g_hr.lapic_ok){ p=hr_fix(p,g_hr.lapic_hz,1000000ULL,3); p=scopy(p," MHz after /16, "); p=scopy(p,g_hr.lapic_x2?"x2APIC":"xAPIC"); p=scopy(p," id "); p=udec(p,g_hr.lapic_id); p=scopy(p,", "); p=udec(p,(u32)g_hr.lapic_irqs); p=scopy(p," interrupts"); } else p=scopy(p,"not used"); *p++='\n'; *p=0; term_puts(b);
    u64 ns=hr_ns(); p=b; p=scopy(p,"  uptime "); p=hr_fix(p,ns,1000000000ULL,6); p=scopy(p," s by the clock, "); p=hr_fix(p,(g_ticks-g_hr.boot_ns_ticks)*10ULL,1000ULL,2); p=scopy(p," s by 100 Hz ticks\n"); *p=0; term_puts(b);
    term_puts("  timertest  - measure accuracy (takes about 2 seconds)\n"); }
static void timertest_cmd(void){
    char b[160],*p; if(!hr_precise()){ term_puts("no precise clock on this machine, nothing to test\n"); return; }
    cpu_irq_on(); u64 t0=g_ticks; for(int i=0;i<50000000&&g_ticks==t0;i++)cpu_pause();
    u64 k0=g_ticks, n0=hr_ns(), c0=hr_rdtsc(), h0=g_hr.hpet_ok?hr_hpet_count():0;
    hr_delay_ms(1000);
    u64 k1=g_ticks, n1=hr_ns(), c1=hr_rdtsc(), h1=g_hr.hpet_ok?hr_hpet_count():0;
    p=b; p=scopy(p,"  1 s busy-wait: "); p=udec(p,(u32)(k1-k0)); p=scopy(p," PIT ticks (100 expected), clock says "); p=hr_fix(p,n1-n0,1000000ULL,3); p=scopy(p," ms\n"); *p=0; term_puts(b);
    if(g_hr.hpet_ok&&g_hr.tsc_hz){ u64 hn=hr_mul_shr32(h1-h0,g_hr.hpet_mult), tn=hr_muldiv(c1-c0,1000000000ULL,g_hr.tsc_hz); u64 d=hn>tn?hn-tn:tn-hn;
        p=b; p=scopy(p,"  HPET vs TSC over that second: "); p=udec(p,(u32)hr_muldiv(d,1000000ULL,hn?hn:1)); p=scopy(p," ppm apart\n"); *p=0; term_puts(b); }
    if(g_hr.lapic_ok){ u64 worst=0; int got=0;
        for(int k=0;k<5;k++){ u64 irq0=g_hr.lapic_irqs; u64 s=hr_ns(), want=s+5000000ULL; if(!hr_wake_at(want))break; u64 e=s;
            for(u64 i=0;i<200000000ULL;i++){ if(g_hr.lapic_irqs!=irq0){ e=hr_ns(); got++; break; } cpu_pause(); }
            u64 err=e>want?e-want:want-e; if(e==s)err=~0ULL; if(err>worst)worst=err; }
        hr_wake_cancel();
        p=b; p=scopy(p,"  APIC timer 5 ms one-shots: "); p=udec(p,(u32)got); p=scopy(p,"/5 fired"); if(got==5){ p=scopy(p,", worst error "); p=hr_fix(p,worst,1000ULL,1); p=scopy(p," us"); } *p++='\n'; *p=0; term_puts(b); }
    { u64 s=hr_ns(); hr_delay_us(250); u64 e=hr_ns(); p=b; p=scopy(p,"  250 us delay took "); p=hr_fix(p,e-s,1000ULL,1); p=scopy(p," us\n"); *p=0; term_puts(b); } }
static int wifi_word(const char* a,const char* w){ int i=0; while(w[i]&&a[i]==w[i])i++; return !w[i]&&(!a[i]||a[i]==' '); }
static const char* wifi_next(const char* a){ while(*a&&*a!=' ')a++; while(*a==' ')a++; return a; }
static int wifi_tok(const char* a,char* o,int cap){ int n=0; if(*a=='"'){ a++; while(*a&&*a!='"'&&n<cap-1)o[n++]=*a++; } else while(*a&&*a!=' '&&n<cap-1)o[n++]=*a++; o[n]=0; return n; }
static const char* wifi_skip(const char* a){ if(*a=='"'){ a++; while(*a&&*a!='"')a++; if(*a)a++; } else while(*a&&*a!=' ')a++; while(*a==' ')a++; return a; }
static int wifi_cmd2(const char* arg){
    char b[160],*p; extern const char* wifi_sec_name(const scan_ap*);
    if(!arg[0]&&!rtw_find_mmio())return 0;
    if(!arg[0]||wifi_word(arg,"status")){ p=b; p=scopy(p,"Wi-Fi: "); p=scopy(p,wifi_state_name()); if(g_wm_state==WM_CONNECTED){ p=scopy(p," to "); p=scopy(p,g_wifi_conn); p=scopy(p,", channel "); p=udec(p,g_wm_ch); if(g_wifi_rssi>-127){ p=scopy(p,", -"); p=udec(p,(u32)(-g_wifi_rssi)); p=scopy(p," dBm"); } p=scopy(p,", up to "); p=udec(p,(u32)wifi_rate_mbps()); p=scopy(p," Mbit/s"); } *p++='\n'; *p=0; term_puts(b);
        p=b; p=scopy(p,"  "); p=scopy(p,g_wifi_status); p=scopy(p,"\n  auto-connect "); p=scopy(p,g_wm_auto?"on":"off"); p=scopy(p,", 5 GHz "); p=scopy(p,g_wm_5g?"on":"off"); p=scopy(p,", saved networks "); p=udec(p,(u32)g_wnet_n); *p++='\n'; *p=0; term_puts(b);
        if(!arg[0])term_puts("  wifi on|off|scan|list|saved|connect <name> [password]|eap <name> <user> <password>|hidden <name> [password]|forget <name>|disconnect|auto on|off|5g on|off|powersave on|off|rate auto|safe\n  (old bring-up and test commands still work: wifi init, allbuilt, macinit, fw, rings, ...)\n");
        return 1; }
    if(wifi_word(arg,"on")){ if(wifi_up()==0){ wifi_radio_on(); term_puts("Wi-Fi is on\n"); } else { tcol(RED); term_puts(g_wifi_status); term_putc('\n'); tcol(WHT); } return 1; }
    if(wifi_word(arg,"off")){ wifi_radio_off(); term_puts("Wi-Fi is off\n"); return 1; }
    if(wifi_word(arg,"scan")||wifi_word(arg,"list")){ if(wifi_word(arg,"scan")){ if(wifi_up()<0){ tcol(RED); term_puts(g_wifi_status); term_putc('\n'); tcol(WHT); return 1; } term_puts("Looking for networks (2.4 and 5 GHz)...\n"); wifi_scan_blocking(1); }
        for(int i=0;i<g_apn;i++){ p=b; p=scopy(p,"  "); int bars=wifi_signal_bars(g_aps[i].rssi); for(int k=0;k<4;k++)*p++=k<bars?'#':'.'; p=scopy(p,"  "); p=scopy(p,g_aps[i].ssid[0]?g_aps[i].ssid:"<hidden>"); while(p-b<42)*p++=' ';
            p=scopy(p,g_aps[i].ch>14?"5G  ch ":"2.4 ch "); p=udec(p,g_aps[i].ch); while(p-b<56)*p++=' '; p=scopy(p,wifi_sec_name(&g_aps[i])); if(g_aps[i].ht)p=scopy(p,"  n"); if(wifi_saved_find(g_aps[i].ssid)>=0)p=scopy(p,"  saved"); *p++='\n'; *p=0; term_puts(b); }
        if(!g_apn)term_puts("  no networks found\n"); return 1; }
    if(wifi_word(arg,"saved")){ if(!g_wnet_n)term_puts("  no saved networks\n"); for(int i=0;i<g_wnet_n;i++){ p=b; p=scopy(p,"  "); p=scopy(p,g_wnets[i].ssid); p=scopy(p,"  priority "); p=udec(p,(u32)g_wnets[i].prio); if(g_wnets[i].hidden)p=scopy(p," hidden"); if(g_wnets[i].eap)p=scopy(p," enterprise"); if(!g_wnets[i].autoc)p=scopy(p," manual"); *p++='\n'; *p=0; term_puts(b); } term_puts("  stored in /etc/noovex/wifi/networks (root only)\n"); return 1; }
    if(wifi_word(arg,"connect")||wifi_word(arg,"hidden")||wifi_word(arg,"eap")){ int hid=wifi_word(arg,"hidden"), eap=wifi_word(arg,"eap"); const char* a=wifi_next(arg); char ss[33],u[65],pw[65]; u[0]=0; pw[0]=0;
        if(!wifi_tok(a,ss,33)){ term_puts("usage: wifi connect <name> [password]   (use \"quotes\" for names with spaces)\n"); return 1; } a=wifi_skip(a);
        if(eap){ wifi_tok(a,u,65); a=wifi_skip(a); } wifi_tok(a,pw,65);
        int sv=wifi_saved_find(ss); if(!pw[0]&&sv>=0){ int k=0; for(;g_wnets[sv].pass[k];k++)pw[k]=g_wnets[sv].pass[k]; pw[k]=0; if(!u[0]){ for(k=0;g_wnets[sv].user[k];k++)u[k]=g_wnets[sv].user[k]; u[k]=0; } }
        if(wifi_up()<0){ tcol(RED); term_puts(g_wifi_status); term_putc('\n'); tcol(WHT); return 1; }
        term_puts("Connecting...\n"); int r=wifi_connect_ssid(ss,pw,u,hid); for(int k=0;k<65;k++)pw[k]=0;
        tcol(r==0?GREEN:RED); term_puts(g_wifi_status); term_putc('\n'); tcol(WHT); if(r==0){ p=b; p=scopy(p,"  address "); p=ipstr(p,my_ip); p=scopy(p,"  gateway "); p=ipstr(p,gw_ip); *p++='\n'; *p=0; term_puts(b); } return 1; }
    if(wifi_word(arg,"forget")){ const char* a=wifi_next(arg); char ss[33]; wifi_tok(a,ss,33); term_puts(wifi_saved_forget(ss)==0?"Forgotten\n":"Not a saved network\n"); return 1; }
    if(wifi_word(arg,"disconnect")){ wifi_disconnect(); term_puts("Disconnected (auto-connect waits a minute)\n"); return 1; }
    if(wifi_word(arg,"auto")){ g_wm_auto=seq((char*)wifi_next(arg),"on"); wifi_saved_save(); term_puts(g_wm_auto?"Auto-connect on\n":"Auto-connect off\n"); return 1; }
    if(wifi_word(arg,"5g")){ g_wm_5g=seq((char*)wifi_next(arg),"on"); wifi_saved_save(); term_puts(g_wm_5g?"5 GHz scanning on\n":"5 GHz scanning off\n"); return 1; }
    if(wifi_word(arg,"powersave")){ wifi_powersave(seq((char*)wifi_next(arg),"on")); term_puts(g_wm_ps?"Power saving on (experimental: the chip sleeps when idle)\n":"Power saving off\n"); return 1; }
    if(wifi_word(arg,"rate")){ g_wifi_ra=!seq((char*)wifi_next(arg),"safe"); term_puts(g_wifi_ra?"Automatic rates (fast) - used from the next connection\n":"Safe fixed rate (1 Mbit/s on 2.4 GHz, 6 on 5 GHz) - used from the next connection\n"); return 1; }
    return 0; }
static void run_cmd_one(char*s){
    g_lastrc=0;
    while(*s==' ')s++; char cmd[24]; int i=0; while(s[i]&&s[i]!=' '&&i<23){cmd[i]=s[i];i++;} cmd[i]=0;
    char*arg=s+i; while(*arg==' ')arg++;
    if(!cmd[0])return;
    if(jc_builtin(cmd,arg))return;
    if(seq(cmd,"help")){ tcol(WHT);
        tcol(rgb(120,220,140)); term_puts("noovexos_install   start the NoovexOS installer\n"); tcol(WHT);
        tcol(ACC); term_puts("jobs fg bg kill ps umask   (job control: cmd &, Ctrl-C, Ctrl-Z, cmd1 | cmd2, > < 2>&1)\n");
        term_puts("programs: count spin sigdemo upper greet pipetest\n");
        term_puts("/bin: ls cat grep wc head tail echo mkdir rmdir rm mv cp touch ln chmod chown stat pwd hostname whoami id uname date sleep mount umount readlink tee sort\n");
        term_puts("pkg (read .deb/.apk)   nvidia/gpu (GPU info)\n"); tcol(WHT);
        term_puts("help clear echo neofetch ver cpu mem time date uptime ls cat edit\n");
        term_puts("net dhcp wifi usb usbdisk usbcat usbwrite usbrm ec acpi battery brightness timers timertest utest ring3 run app posix mem sched fork threads preempt ipc wm compositor cores smpstart smpwork lua ping dns http https netlog pci disks colors settings reboot panic\n");
        term_puts("browse <url> www desktop  (web browser: HTML, CSS, JavaScript, cookies)   qjs [-e code] [file.js]  (QuickJS)\n"); term_puts("browser [netbg|csp|samesite] [on|off]  (browser switches for testing)\n");
        term_puts("play <file|URL>  (fullscreen player: space pause, arrows seek, q quit; -b benchmark)   video <file|URL>  (Video Player window)\n");
        term_puts("fatinfo fatls fatcat  (read files from an IDE/FAT disk)\n");
        term_puts("whoami id groups who last  passwd [-l|-u|-d] [user]  su [user]  sudo [-k|-l] cmd  lock logout  hostname [name]\n");
        term_puts("sudo: adduser [--admin] NAME  useradd [-a] [-c \"Full Name\"] NAME  userdel [-r] NAME  usermod -aG|-rG GROUP|-c|-L|-U NAME\n");
        term_puts("nvxlogin nvxwhoami nvxlogout nvxtest  (Noovex account + API probe)\n");
        term_puts("update [check|install|back|new]  (NoovexOS updates from noovexos.site, keeps the previous version)\n");
        term_puts("crash memcheck cputemp amdgpio gpus  (crash and freeze reports, heap check, CPU temperature, AMD GPIO, graphics cards)\n"); }
    else if(seq(cmd,"fatinfo")||seq(cmd,"mount")){ if(fat_mount()){ char b[96],*p=b; p=scopy(p,"FAT"); p=scopy(p,fat_is32?"32":"16"); p=scopy(p," mounted   drive="); p=udec(p,(u32)g_ata_drv); p=scopy(p,"   sec/clus="); p=udec(p,(u32)fat_spc); p=scopy(p,"   data@LBA "); p=udec(p,fat_datastart); *p=0; tcol(CYAN);term_puts(b);tcol(WHT);term_putc('\n'); } else { tcol(RED);term_puts("no FAT disk found (ATA/IDE PIO only; VM needs an IDE disk with FAT)\n");tcol(WHT); } }
    else if(seq(cmd,"fatls")){ if(fat_mount()){ tcol(GREY);term_puts("root directory:\n");tcol(WHT); fat_listdir(fatls_cb); } else { tcol(RED);term_puts("no FAT disk\n");tcol(WHT); } }
    else if(seq(cmd,"crypto")){
        tcol(rgb(120,200,255)); term_puts("  NoovexOS crypto suite\n"); tcol(WHT);
        term_puts("  hashes:  SHA-256  SHA-1  SHA-384  SHA-512\n");
        term_puts("  macs:    HMAC-SHA256  HMAC-SHA512  HMAC-SHA1\n");
        term_puts("  kdf:     HKDF  PBKDF2\n");
        term_puts("  cipher:  AES-128  AES-256  ChaCha20  AES-GCM  AES-CCM  Poly1305\n");
        term_puts("  pubkey:  X25519\n");
        term_puts("  rng:     ChaCha20 CSPRNG (RDSEED/RDRAND seeded)\n");
        term_puts("  tls:     TLS 1.3 client\n");
        /* live self-test vs known vectors */
        int pass=0,tot=0; char b[80],*p;
        { u8 h[64]; sha512_hash((const u8*)"abc",3,h); tot++; int ok=(h[0]==0xdd&&h[1]==0xaf&&h[2]==0x35); if(ok)pass++;
          p=b;p=scopy(p,"  SHA-512  ");p=scopy(p,ok?"PASS ":"FAIL ");for(int i=0;i<4;i++)p=hx(p,h[i],2);*p=0; tcol(ok?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); }
        { u8 h[48]; sha384_hash((const u8*)"abc",3,h); tot++; int ok=(h[0]==0xcb&&h[1]==0x00&&h[2]==0x75); if(ok)pass++;
          p=b;p=scopy(p,"  SHA-384  ");p=scopy(p,ok?"PASS ":"FAIL ");for(int i=0;i<4;i++)p=hx(p,h[i],2);*p=0; tcol(ok?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); }
        { u8 key[32]; for(int i=0;i<32;i++)key[i]=(u8)i; u8 pt[16]={0,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
          u8 rk[240]; aes256_expand(key,rk); u8 ct[16]; aes256_enc(rk,pt,ct); tot++; int ok=(ct[0]==0x8e&&ct[1]==0xa2&&ct[2]==0xb7); if(ok)pass++;
          p=b;p=scopy(p,"  AES-256  ");p=scopy(p,ok?"PASS ":"FAIL ");for(int i=0;i<4;i++)p=hx(p,ct[i],2);*p=0; tcol(ok?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); }
        { u8 r1[8],r2[8]; csprng_bytes(r1,8); csprng_bytes(r2,8); tot++; int diff=0; for(int i=0;i<8;i++)if(r1[i]!=r2[i])diff=1; if(diff)pass++;
          p=b;p=scopy(p,"  CSPRNG   ");p=scopy(p,diff?"PASS ":"FAIL ");for(int i=0;i<4;i++)p=hx(p,r1[i],2);*p=0; tcol(diff?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); }
        p=b;p=scopy(p,"  ");p=udec(p,(u32)pass);*p++='/';p=udec(p,(u32)tot);p=scopy(p," self-tests passed");*p=0; tcol(pass==tot?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT);
    }
    else if(seq(cmd,"install")){ if(!need_root(cmd))return; ip_open(ST_DESKTOP); }
    else if(seq(cmd,"diskmgr")||seq(cmd,"partitions")){ if(!need_root(cmd))return; dm_open(g_state==ST_LIVE?ST_LIVE:ST_DESKTOP); }
    else if(seq(cmd,"noovexos_install")){ if(!need_root(cmd))return; ip_open(ST_LIVE); }
    else if(seq(cmd,"firefox-mode")){ const char* a=arg; while(*a==' ')a++;
        if(a[0]=='m'){ static char c1[]="bash -c 'mkdir -p /root/.nvx-firefox && touch /root/.nvx-firefox/.nvx-multiprocess'"; run_cmd(c1); term_puts("Firefox will start in multi-process mode (4 content processes) next time. Undo with: firefox-mode single\n"); }
        else if(a[0]=='s'){ static char c2[]="bash -c 'rm -f /root/.nvx-firefox/.nvx-multiprocess'"; run_cmd(c2); term_puts("Firefox will start in single-process mode next time.\n"); }
        else term_puts("usage: firefox-mode multi | single\n"); }
    else if((seq(cmd,"firefox")||seq(cmd,"firefox-esr"))&&!arg[0]){ extern void firefox_launch(void); tcol(GREY); term_puts("starting Firefox (sandbox off, software rendering, full-screen X; log in /tmp/ff.log)\n"); tcol(WHT); firefox_launch(); }
    else if(seq(cmd,"apt")||seq(cmd,"apt-get")||seq(cmd,"apt-cache")){ apt_cmd(arg); }
    else if(seq(cmd,"dpkg")){ dpkg_cmd(arg); }
    else if(seq(cmd,"xstat")){ extern void xstat_print(void (*)(const char*)); extern int g_xd_off; const char* a=arg; while(*a==' ')a++; xstat_print(term_puts); if(a[0]=='o'&&a[1]=='f')g_xd_off=1; else if(a[0]=='o'&&a[1]=='n')g_xd_off=0; }
    else if(seq(cmd,"startx")||seq(cmd,"xserver")){ extern int g_xfb_live; extern void xfull_enter(void); const char* a=arg; while(*a==' ')a++; int full=(a[0]=='f'||(a[0]=='-'&&a[1]=='f'))||seq(cmd,"xserver"); int inwin=(a[0]=='w'); extern int g_xover;
        if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; }
        if(g_xfb_live){ if(full){ term_puts("startx: switching to the full-screen X session (F12 or Ctrl+Alt+Backspace returns)\n"); xfull_enter(); } else { term_puts("startx: the X server is already running on :0\n"); if(!g_xover)open_win(W_XDISP); } }
        else { dt_init(); { int t=dt_child(0,"tmp"); if(t<0)dt_mkdir_at(0,"tmp"); } { int v=dt_child(0,"var"); if(v<0)v=dt_mkdir_at(0,"var"); int l=dt_child(v,"lib"); if(l<0)l=dt_mkdir_at(v,"lib"); if(dt_child(l,"xkb")<0)dt_mkdir_at(l,"xkb"); }
            static char xl[200]; char* p=xl; p=scopy(p,"Xvfb :0 -screen 0 "); if(!inwin){ p=udec(p,W); *p++='x'; p=udec(p,H); } else p=scopy(p,"800x600"); p=scopy(p,full||inwin?"x24 -retro":"x24"); p=scopy(p," -fbdir /tmp -fp built-ins -listen tcp -nolisten unix -nolisten local -ac -xkbdir /usr/share/X11/xkb > /tmp/Xvfb.log 2>&1 &"); *p=0;
            env_set("DISPLAY","127.0.0.1:0"); tcol(GREY); term_puts("starting the X.Org X11 server on :0 (DISPLAY=127.0.0.1:0, log in /tmp/Xvfb.log)\n"); if(full)term_puts("full-screen X session: F12 or Ctrl+Alt+Backspace returns to the NoovexOS desktop\n"); tcol(WHT);
            { extern int x_spawned; x_spawned=1; } run_cmd(xl); { static char xi[]="nvx-xinput > /tmp/nvx-xinput.log 2>&1 &"; run_cmd(xi); static char xw[]="nvxwm > /tmp/nvxwm.log 2>&1 &"; run_cmd(xw); if(!full&&!inwin){ static char xp[]="bash -c 'sleep 4; for f in /usr/lib/firefox-esr/libxul.so /usr/lib/firefox-esr/omni.ja /usr/lib/firefox-esr/browser/omni.ja; do [ -e $f ] && cat $f > /dev/null; done' > /dev/null 2>&1 &"; run_cmd(xp); } } if(full){ static char xr[]="bash -c 'sleep 3; bash /root/.xinitrc' > /tmp/xinitrc.log 2>&1 &"; static char xd[]="xdemo > /tmp/xdemo.log 2>&1 &"; run_cmd(k_find_file("/root/.xinitrc")>=0?xr:xd); }
            if(full){ extern int g_xfull_pending; g_xfull_pending=1; } else if(inwin) open_win(W_XDISP); else { g_xover=1; if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; } } } }
    else if(seq(cmd,"python3-ide")||seq(cmd,"pyide")){ if(!pyide_installed()){ tcol(rgb(240,200,110)); term_puts("Python IDE is not installed. Install it with: sudo apt install python3-ide\n"); tcol(WHT); } else pyide_open(); }
    else if(seq(cmd,"pkg")){
        char sub[16]; int si=0; const char* a=arg; while(*a==' ')a++; while(*a&&*a!=' '&&si<15)sub[si++]=*a++; sub[si]=0; while(*a==' ')a++;
        int doinst=seq(sub,"install")||seq(sub,"add")||seq(sub,"i");
        int doinfo=seq(sub,"info")||seq(sub,"list")||seq(sub,"l"); 
        if(!doinst&&!doinfo){ tcol(rgb(230,160,60)); term_puts("  usage: pkg info <file.deb|.apk>   |   pkg install <file.deb|.apk>\n");
            term_puts("  reads Debian .deb (ar+tar.gz) and Alpine .apk (tar.gz); extracts data files to /pkg/<name>/\n");
            term_puts("  note: Linux ELF binaries inside won't run on NoovexOS - use it for docs/configs/data\n"); tcol(WHT); }
        else { const char* fn=a; if(!*fn){ tcol(rgb(230,120,90)); term_puts("  give a file: pkg install foo.deb\n"); tcol(WHT); }
            else { int f=k_find_file(fn); long n=0; const u8* buf=0;
                if(f>=0){ buf=(const u8*)fsd(f); n=FS[f].len; }
                else { int got=fat_read_file(fn,g_pkg_raw,PKG_RAW_SZ); if(got>0){ buf=g_pkg_raw; n=got; } }
                if(!buf){ tcol(rgb(230,120,90)); term_puts("  file not found: "); term_puts((char*)fn); term_putc('\n'); tcol(WHT); }
                else { const u8* tar=0; long tl=0; int kind=pkg_open(buf,n,&tar,&tl);
                    if(!kind){ tcol(rgb(230,120,90)); term_puts("  not a recognised package (need .deb or gzip .apk; xz/zstd not supported)\n"); tcol(WHT); }
                    else { char b[160],*p;
                        p=b; p=scopy(p,"  "); p=scopy(p,kind==1?"Debian .deb":kind==2?"Alpine .apk":"Android .apk (ZIP)"); *p=0; tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                        if(g_pkg_name[0]){ p=b; p=scopy(p,"  name: "); p=scopy(p,g_pkg_name); if(g_pkg_ver[0]){ p=scopy(p,"  version: "); p=scopy(p,g_pkg_ver);} if(g_pkg_arch[0]){ p=scopy(p,"  arch: "); p=scopy(p,g_pkg_arch);} *p=0; term_puts(b); term_putc('\n'); }
                        if(g_pkg_desc[0]){ p=b; p=scopy(p,"  "); p=scopy(p,g_pkg_desc); *p=0; tcol(GREY); term_puts(b); term_putc('\n'); tcol(WHT); }
                        if(tar&&tl>0){ g_pkg_nfiles=0; g_pkg_total=0;
                            if(doinst){ int k=pkg_install(buf,n,1); (void)k; p=b; p=scopy(p,"  installed "); p=udec(p,(u32)g_pkg_nfiles); p=scopy(p," files to /pkg/"); p=scopy(p,g_pkg_name[0]?g_pkg_name:"unnamed"); *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
                            else { struct pkg_list_ctx c; c.max=40; c.shown=0; tar_walk(tar,tl,pkg_print_cb,&c);
                                pkg_install(buf,n,0); p=b; p=scopy(p,"  "); p=udec(p,(u32)g_pkg_nfiles); p=scopy(p," files, "); p=udec(p,(u32)(g_pkg_total/1024)); p=scopy(p," KB total"); *p=0; term_puts(b); term_putc('\n'); } }
                        else if(kind==3){ tcol(rgb(230,160,60)); term_puts("  Android APK: use the ZIP viewer in Files to browse it\n"); tcol(WHT); }
                    } } } }
    }
    else if(seq(cmd,"fbdamage")){ extern int g_dmg_on; extern u32 g_dmg_frames, g_dmg_full, g_dmg_idle, g_dmg_last_px, g_dmg_last_n, g_dmg_gpu; extern void fb_shadow_invalidate(void);
        if(seq(arg,"on")){ g_dmg_on=1; fb_shadow_invalidate(); } else if(seq(arg,"off")){ g_dmg_on=0; fb_shadow_invalidate(); }
        char b[200],*p=b; p=scopy(p,"screen updates: "); p=scopy(p,g_dmg_on?"only changed areas are sent":"every frame is sent whole"); p=scopy(p,"\n  frames "); p=udec(p,g_dmg_frames); p=scopy(p,", whole "); p=udec(p,g_dmg_full); p=scopy(p,", nothing changed "); p=udec(p,g_dmg_idle);
        p=scopy(p,", by the NVIDIA copy engine "); p=udec(p,g_dmg_gpu); p=scopy(p,"\n  last frame: "); p=udec(p,g_dmg_last_n); p=scopy(p," areas, "); p=udec(p,g_dmg_last_px/1000); p=scopy(p," thousand pixels of "); p=udec(p,(u32)((u64)W*H/1000)); p=scopy(p,"\n  fbdamage on|off\n"); *p=0; term_puts(b); }
    else if(seq(cmd,"nvgsp")){ extern int nvgsp_cmd(const char*); extern char g_nv_log[]; const char* a=arg; while(*a==' ')a++;
        if(a[0]=='a'&&a[1]=='u'&&a[2]=='t'&&a[3]=='o'){ extern int g_nvd_auto; extern void nvd_set_auto(int); const char* m=a+4; while(*m==' ')m++; if(m[0]=='o'&&m[1]=='n')nvd_set_auto(1); else if(m[0]=='o'&&m[1]=='f')nvd_set_auto(0); term_puts(g_nvd_auto?"nvgsp auto: on - the NVIDIA GPU starts by itself after boot\n":"nvgsp auto: off - press G in Gears or run nvgsp all\n"); return; }
        if(!a[0]||seq(a,"help")){ tcol(CYAN); term_puts("nvgsp: NVIDIA Turing (GTX 16 / RTX 20) driver through NVIDIA's GSP firmware\n"); tcol(WHT);
            term_puts("  nvgsp vram      VRAM size, reserved layout and allocator (read only, safe)\n  nvgsp boot      FWSEC, booter, GSP-RM, static info, BAR1/BAR2 windows\n  nvgsp objects   client, device, subdevice, address space, channels, 3D/compute/copy objects, copy test\n  nvgsp all       boot + objects + screen through the copy engine\n  nvgsp present on|off   desktop frames through the NVIDIA copy engine or the CPU\n  nvgsp status    current state\n  nvgsp log       print the last log again (saved as nvgsp-log.txt in Files and NVGSPLOG.TXT on the USB stick)\n  nvgsp unload    stop GSP-RM and tear down WPR2 so it can be started again\n");
            tcol(rgb(230,160,60)); term_puts("  boot can turn off the picture of a screen connected to the NVIDIA card. The log is saved to Files either way.\n"); tcol(WHT); }
        if(a[0]&&!seq(a,"help")){ if(!seq(a,"log")&&!seq(a,"status")&&!seq(a,"vram")&&!(a[0]=='p'&&a[1]=='r')){ term_puts("nvgsp: working - this can take up to 30 seconds\n"); } int r=nvgsp_cmd(a); term_puts(g_nv_log); tcol(r>0?rgb(120,220,120):rgb(240,120,120)); term_puts(r>0?"nvgsp: done\n":"nvgsp: stopped - see the step above (log saved as nvgsp-log.txt)\n"); tcol(WHT); } }
    else if(seq(cmd,"nvidia")||(seq(cmd,"gpu")&&!(arg[0]=='f'||arg[0]=='b'||arg[0]=='i'||arg[0]=='r'||arg[0]=='d'||!arg[0])&&nv_probe())){
        if(!nv_probe()){ tcol(rgb(230,160,60)); term_puts("  no NVIDIA GPU found (looked for vendor 10DE, class 03)\n"); tcol(WHT); }
        else { nv_read_id();
            char b[128],*p;
            p=b; p=scopy(p,"  NVIDIA GPU  10DE:"); p=hx(p,g_nv_dev,4); p=scopy(p,"  bus "); p=udec(p,(u32)g_nv_bus); *p++='/'; p=udec(p,(u32)g_nv_slot); *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT);
            p=b; p=scopy(p,"  BAR0 regs  0x"); p=hx(p,(u32)g_nv_bar0,8); p=scopy(p,"  size "); p=udec(p,g_nv_bar0_sz/1024/1024); p=scopy(p," MB"); *p=0; term_puts(b); term_putc('\n');
            p=b; p=scopy(p,"  BAR1 fb    0x"); p=hx(p,(u32)(g_nv_bar1>>32),8); p=hx(p,(u32)g_nv_bar1,8); *p=0; term_puts(b); term_putc('\n');
            p=b; p=scopy(p,"  PMC_BOOT_0 0x"); p=hx(p,g_nv_boot0,8); p=scopy(p,"  arch "); p=scopy(p,(char*)nv_arch_name2(g_nv_boot0)); p=scopy(p,"  rev "); p=hx(p,(u32)g_nv_rev,2); *p=0; tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
            if(g_nv_boot0==0xFFFFFFFFu||g_nv_boot0==0){ tcol(rgb(230,120,90)); term_puts("  (register read failed - BAR0 not reachable in this environment)\n"); tcol(WHT); }
            if(nv_load_vbios()){ p=b; p=scopy(p,"  VBIOS      "); p=udec(p,(u32)g_nv_vbios_len); p=scopy(p," bytes"); if(nv_find_bit()){ p=scopy(p,"  BIT@0x"); p=hx(p,g_nv_bit_off,4);} *p=0; term_puts(b); term_putc('\n');
                if(nv_find_dcb()){ nv_parse_dcb(); p=b; p=scopy(p,"  DCB@0x"); p=hx(p,g_nv_dcb_off,4); p=scopy(p,"  connectors: "); p=udec(p,(u32)g_nv_ncon); *p=0; term_puts(b); term_putc('\n');
                    for(int i=0;i<g_nv_ncon;i++){ p=b; p=scopy(p,"    - "); p=scopy(p,(char*)nv_con_name(g_nv_con_type[i])); *p=0; tcol(GREY); term_puts(b); term_putc('\n'); tcol(WHT); } }
                else { tcol(GREY); term_puts("  DCB not located\n"); tcol(WHT); } }
            else { tcol(rgb(230,160,60)); term_puts("  VBIOS not readable here (needs the real GPU; VirtualBox has no NVIDIA card)\n"); tcol(WHT); }
            tcol(GREY); term_puts("  display/accel driver: detect+VBIOS stage only (read-only, safe)\n"); tcol(WHT);
            if(g_nv_boot0!=0xFFFFFFFFu&&g_nv_boot0){ char b[160],*p; u32 hm=nvr32(0x610060), hc=nvr32(0x610074);
                p=b; p=scopy(p,"  display engine: "); p=udec(p,hc&0xF); p=scopy(p," heads, "); p=udec(p,(hc>>8)&0xF); p=scopy(p," output encoders"); *p=0; tcol(GREY); term_puts(b); term_putc('\n'); tcol(WHT);
                int shown=0;
                for(int h=0;h<4;h++){ if(!(hm&(1u<<h)))continue; u32 o=0x8000u+(u32)h*0x400u; nvm_head_t hd; nvm_decode_head(nvr32(0x682064+o),nvr32(0x682068+o),nvr32(0x68206C+o),nvr32(0x682070+o),nvr32(0x68200C+o),&hd);
                    if(!hd.active)continue; shown++;
                    p=b; p=scopy(p,"  head "); p=udec(p,(u32)h); p=scopy(p," is ON: "); p=udec(p,hd.hact); *p++='x'; p=udec(p,hd.vact); p=scopy(p," @ "); p=udec(p,hd.hz); p=scopy(p," Hz  (total "); p=udec(p,hd.htot); *p++='x'; p=udec(p,hd.vtot); p=scopy(p,", pixel clock "); p=udec(p,hd.pclk/1000); p=scopy(p," kHz)");
                    for(int s=0;s<8;s++){ if(!((hm>>8)&(1u<<s)))continue; u32 ctl=nvr32(0x680300+0x8000u+(u32)s*0x20u); if(ctl!=0xFFFFFFFFu&&(ctl&0xFF)&(1u<<h)){ p=scopy(p," <- encoder "); p=udec(p,(u32)s); p=scopy(p," "); p=scopy(p,nvm_proto((ctl>>8)&0xF)); } }
                    *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
                if(!shown){ tcol(GREY); term_puts("  no active head found in the display engine's armed state\n"); tcol(WHT); } }
            if(g_nv_vbios_len&&nv_find_dcb()){ nvm_out_t outs[12]; int no=nvm_outputs(g_nv_vbios,g_nv_vbios_len,g_nv_dcb_off,outs,12); char b[120],*p;
                for(int i=0;i<no;i++){ p=b; p=scopy(p,"  output "); p=udec(p,(u32)i); p=scopy(p,": "); p=scopy(p,nvm_conn(outs[i].conntype)); p=scopy(p," socket "); p=udec(p,outs[i].conn); p=scopy(p,", "); p=scopy(p,nvm_sig(outs[i].type)); p=scopy(p,", encoder "); { int si=nvm_sor_index(outs[i].sor); if(si<0)p=scopy(p,"?"); else p=udec(p,(u32)si); } *p=0; term_puts(b); term_putc('\n'); } }
            tcol(GREY); term_puts("  to read the connected monitors (writes the DDC pins): gpumon\n"); tcol(WHT);
            tcol(GREY); term_puts("  modeset: disp907d scaffold present but disabled (unverified - see nvdisp.c)\n"); tcol(WHT);
        }
    }
    else if(seq(cmd,"crash")){ extern void crash_cmd(const char*); crash_cmd(arg); }
    else if(seq(cmd,"memcheck")){ extern int heap_check(int*,int*,char*,int); extern u32 g_heap_bad, g_heap_dfree, g_heap_badfree, g_heap_reent, g_heap_oom; extern int g_heap_poison; extern volatile u32 g_msr_faults; extern unsigned long heap_used(void); const char* a=arg; while(*a==' ')a++;
        if(seq(a,"poison on")){ g_heap_poison=1; term_puts("freed heap memory is now filled with 0xDB (finds use-after-free bugs, may expose crashes)\n"); }
        else if(seq(a,"poison off")){ g_heap_poison=0; term_puts("freed heap memory is left as it is\n"); }
        else { int used=0,bad=0; char first[160]; int nb=heap_check(&used,&bad,first,160); char b[320],*p=b;
            p=scopy(p,"heap: "); p=udec(p,(u32)nb); p=scopy(p," blocks, "); p=udec(p,(u32)used); p=scopy(p," in use, "); p=udec(p,(u32)(heap_used()/1024)); p=scopy(p," KB in use, "); p=udec(p,(u32)bad); p=scopy(p," damaged\n"); *p=0; term_puts(b);
            if(first[0]){ term_puts("  "); term_puts(first); term_puts("\n"); }
            p=b; p=scopy(p,"seen since start: overruns "); p=udec(p,g_heap_bad); p=scopy(p,", double frees "); p=udec(p,g_heap_dfree); p=scopy(p,", bad frees "); p=udec(p,g_heap_badfree); p=scopy(p,", re-entered "); p=udec(p,g_heap_reent); p=scopy(p,", out of memory "); p=udec(p,g_heap_oom); p=scopy(p,", refused CPU register accesses "); p=udec(p,g_msr_faults); p=scopy(p,"\n  memcheck poison on|off   fill freed memory to find use-after-free bugs\n"); *p=0; term_puts(b); } }
    else if(seq(cmd,"update")){ extern void upd_cmd(const char*); upd_cmd(arg); }
    else if(seq(cmd,"cputemp")){ extern void ctemp_cmd(void); ctemp_cmd(); }
    else if(seq(cmd,"amdgpio")){ extern void amdg_cmd(const char*); amdg_cmd(arg); }
    else if(seq(cmd,"gpus")){ extern int gpu_list(void*,int); struct { u16 ven, dev; u8 bus, slot; const char* vendor; const char* name; const char* driver; int accel; } gl[8]; int n=gpu_list(gl,8); char b[200],*p;
        if(!n)term_puts("  no graphics card found on PCI\n");
        for(int k=0;k<n;k++){ p=b; p=scopy(p,"  "); p=scopy(p,gl[k].vendor); p=scopy(p,"  "); p=hx(p,gl[k].ven,4); *p++=':'; p=hx(p,gl[k].dev,4); p=scopy(p,"  "); p=scopy(p,gl[k].name); *p=0; tcol(gl[k].accel?GREEN:WHT); term_puts(b); term_putc('\n'); tcol(GREY); p=b; p=scopy(p,"    "); p=scopy(p,gl[k].driver); *p=0; term_puts(b); term_putc('\n'); tcol(WHT); } }
    else if(seq(cmd,"gpumon")){
        if(!nv_probe()){ term_puts("  no NVIDIA GPU found\n"); }
        else { nv_read_id(); if(!nv_load_vbios()||!nv_find_dcb()){ term_puts("  VBIOS output table not available - run gpu first\n"); }
            else { nvm_out_t outs[12]; int no=nvm_outputs(g_nv_vbios,g_nv_vbios_len,g_nv_dcb_off,outs,12); int seen[32]={0}; char b[160],*p; int found=0;
                for(int i=0;i<no;i++){ nvm_out_t* o=&outs[i]; if(o->port==0xFF||o->port>=32||seen[o->port])continue; seen[o->port]=1;
                    p=b; p=scopy(p,"  "); p=scopy(p,nvm_conn(o->conntype)); p=scopy(p," socket "); p=udec(p,o->conn); p=scopy(p," (DDC port "); p=udec(p,o->port); p=scopy(p,"): "); *p=0; term_puts(b); sh_redraw();
                    u8 e[128]; nvm_edid_t m;
                    if(!nvm_read_edid(o->port,e)){ term_puts(o->type==6?"no answer (DisplayPort monitors need the AUX channel - not supported yet)\n":"no monitor answered\n"); continue; }
                    if(nvm_edid_parse(e,&m)){ term_puts("answered, but the EDID data was not valid\n"); continue; }
                    found++; p=b; p=scopy(p,m.name[0]?m.name:"(no name)"); p=scopy(p,"  maker "); p=scopy(p,m.maker); p=scopy(p,"  native "); p=udec(p,m.hact); *p++='x'; p=udec(p,m.vact); p=scopy(p," @ "); p=udec(p,m.hz); p=scopy(p," Hz"); *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
                p=b; p=scopy(p,"  "); p=udec(p,(u32)found); p=scopy(p," monitor(s) identified on this card"); *p=0; term_puts(b); term_putc('\n'); } } }
    else if(seq(cmd,"gpuedid")){
        if(!nv_probe()){ term_puts("  no NVIDIA GPU found\n"); }
        else { nv_read_id(); tcol(rgb(230,160,60)); term_puts("  experimental: writes the GPU's DDC (i2c) pins at an unverified register\n"); tcol(WHT);
            nv_display_probe();
            if(g_edid_ok){ char b[96],*p=b; p=scopy(p,"  EDID: ok"); if(g_edid_name[0]){ p=scopy(p,"  monitor: "); p=scopy(p,g_edid_name); } *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT);
                if(g_mode_ok){ p=b; p=scopy(p,"  native mode: "); p=udec(p,g_mode.hact); *p++='x'; p=udec(p,g_mode.vact); p=scopy(p,"  pclk "); p=udec(p,g_mode.pclk_khz/1000); p=scopy(p," MHz"); *p=0; term_puts(b); term_putc('\n'); } }
            else { tcol(rgb(230,160,60)); term_puts("  EDID: not read (needs real GPU + a connected display; i2c port mapping may differ per board)\n"); tcol(WHT); }
        } }
    else if(seq(cmd,"sig")){
        tcol(rgb(120,200,255)); term_puts("  NoovexOS build signature\n"); tcol(WHT);
        char b[96],*p;
        p=b;p=scopy(p,"  magic   ");for(int i=0;i<8;i++)*p++=g_buildsig.magic[i];*p=0; term_puts(b); term_putc('\n');
        p=b;p=scopy(p,"  version ");p=scopy(p,(char*)g_buildsig.version);*p=0; term_puts(b); term_putc('\n');
        p=b;p=scopy(p,"  size    ");p=udec(p,g_buildsig.image_size);p=scopy(p," bytes");*p=0; term_puts(b); term_putc('\n');
        p=b;p=scopy(p,"  epoch   ");p=udec(p,g_buildsig.build_epoch);*p=0; term_puts(b); term_putc('\n');
        p=b;p=scopy(p,"  sha256  ");for(int i=0;i<16;i++)p=hx(p,g_buildsig.sha256[i],2);*p=0; term_puts(b); term_putc('\n');
        p=b;p=scopy(p,"          ");for(int i=16;i<32;i++)p=hx(p,g_buildsig.sha256[i],2);*p=0; term_puts(b); term_putc('\n');
        p=b;p=scopy(p,"  builder ");p=scopy(p,(char*)g_buildsig.builder);*p=0; tcol(GREY); term_puts(b); term_putc('\n'); tcol(WHT);
        /* live integrity re-check over the immutable region */
        int live=sig_self_check();
        tcol(live?GREEN:rgb(230,160,60));
        term_puts(live?"  integrity: OK (immutable region hash matches)\n"
                      :"  integrity: MISMATCH (code hash differs from stamp)\n"); tcol(WHT);
        { char b2[64],*q=b2; q=scopy(q,"  boot check: "); q=scopy(q,g_sig_ok?"passed":g_sig_tampered?"FAILED - kernel code modified":"not checked (unstamped build)"); *q=0;
          tcol(GREY); term_puts(b2); term_putc('\n'); tcol(WHT); }
    }
    else if(seq(cmd,"spawn")){
        if(!arg[0]){ term_puts("usage: spawn <FILE.ELF>   (fork+exec a child process, wait for it, show exit status)\n"); }
        else {
            int pid=sys_spawn(arg);
            if(pid<0){ tcol(RED); term_puts(pid==-1?"file not found\n":pid==-2?"not an ELF64\n":"spawn failed\n"); tcol(WHT); }
            else {
                int st=sys_waitpid(pid);
                char b[64],*p=b; p=scopy(p,"  [pid ");p=udec(p,(u32)pid);p=scopy(p,"] exited, status ");p=udec(p,(u32)st);*p=0;
                tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT);
            }
        } }
    else if(seq(cmd,"dlmod")){
        if(!arg[0]){ term_puts("usage: dlmod <FILE.MOD>   (load a PIE module, relocate, call entry)\n"); }
        else {
            int n=rtw_read_table_file(arg,fat_filebuf,(int)sizeof(fat_filebuf));
            if(n<64){ tcol(RED); term_puts("module not found\n"); tcol(WHT); }
            else { nvx_module m; int r=nvx_dlload(fat_filebuf,(u32)n,&m);
                if(r){ char b[48],*p=b; p=scopy(p,"  dlload failed, code ");p=udec(p,(u32)(-r));*p=0; tcol(RED); term_puts(b); term_putc('\n'); tcol(WHT); }
                else { char b[80],*p=b; p=scopy(p,"  loaded at 0x");p=hx(p,(u32)((u64)m.base>>32),8);p=hx(p,(u32)(u64)m.base,8);p=scopy(p,"  entry 0x");p=hx(p,(u32)(u64)m.entry,8);*p=0; tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT);
                    term_puts("  --- module output ---\n"); int rc=nvx_dlcall(&m);
                    char c2[40],*q=c2; q=scopy(q,"  module returned ");q=udec(q,(u32)rc);*q=0; tcol(GREEN); term_puts(c2); term_putc('\n'); tcol(WHT); } }
        } }
    else if(seq(cmd,"run")){
        if(!arg[0]){ term_puts("usage: run <FILE.ELF>   (loads an ELF64 from USB/FAT and executes it)\n"); }
        else {
            int n=rtw_read_table_file(arg,fat_filebuf,(int)sizeof(fat_filebuf));   /* USB stick first, then FAT disk */
            if(n<0){ tcol(RED);term_puts("file not found (checked USB stick and FAT disk)\n");tcol(WHT); }
            else if(n<64 || !(fat_filebuf[0]==0x7F&&fat_filebuf[1]=='E'&&fat_filebuf[2]=='L'&&fat_filebuf[3]=='F')){ tcol(RED);term_puts("not an ELF64 file\n");tcol(WHT); }
            else {
                char b[64],*p=b; p=scopy(p,"  loading ");p=scopy(p,(char*)arg);p=scopy(p," (");p=udec(p,(u32)n);p=scopy(p," bytes)...");*p=0; tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT);
                int rc=elf_load_run(fat_filebuf,(u32)n);
                if(rc==0){ tcol(GREEN); term_puts("  program exited\n"); tcol(WHT); }
                else if(rc==1){ tcol(RED); term_puts("  paging not ready\n"); tcol(WHT); }
                else if(rc==2){ tcol(RED); term_puts("  could not build address space (bad segments?)\n"); tcol(WHT); }
                else { tcol(RED); term_puts("  not a valid ELF64\n"); tcol(WHT); }
            }
        } }
    else if(seq(cmd,"fatcat")){ if(!arg[0]){ term_puts("usage: fatcat <NAME.EXT>\n"); } else { int n=fat_read_file(arg,fat_filebuf,(int)sizeof(fat_filebuf)-1); if(n<0){ tcol(RED);term_puts("file not found on FAT disk\n");tcol(WHT); } else { for(int i=0;i<n&&i<8000;i++){ char c=(char)fat_filebuf[i]; if(c=='\r')continue; term_putc(c); } term_putc('\n'); char b[48],*p=b;p=scopy(p,"(");p=udec(p,(u32)n);p=scopy(p," bytes read)");*p=0; tcol(GREY);term_puts(b);tcol(WHT);term_putc('\n'); } } }
    else if(seq(cmd,"clear")){ term_clear(); }
    else if(seq(cmd,"echo")){ term_puts(arg); term_putc('\n'); }
    else if(seq(cmd,"browse")||seq(cmd,"www")||seq(cmd,"desktop")){ if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; }
        if(seq(cmd,"desktop")){} else if(arg[0]) nvx_open_url(arg); else open_win(W_BROWSER); }
    else if(seq(cmd,"video")||seq(cmd,"player")){ extern int video_open_url(const char*,const char*); extern int video_open_slot(int);
        if(!arg[0]){ term_puts("usage: video <file or http(s) URL>   (opens the Video Player window; 'play' is fullscreen)\n"); }
        else { int isd=0, k=-1; int url=(arg[0]=='h'&&arg[1]=='t'&&arg[2]=='t'&&arg[3]=='p'); if(!url)k=ux_lookup(arg,&isd,1);
            if(!url&&(k<0||isd)){ tcol(RED); term_puts("video: file not found\n"); tcol(WHT); }
            else { if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; }
                if(url)video_open_url(arg,0); else video_open_slot(k); term_puts("opened the Video Player.\n"); } } }
