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
static void wdiag_out(const char* t,u32 col){ char k[640],*q=k; q=scopy(q,"wifi: "); q=scopy(q,t); *q=0; klog(k); tcol(col); term_puts("  "); term_puts(t); term_putc('\n'); tcol(WHT); }
static u32 wdiag_d16(u32 a,u32 b){ return (u32)(u16)((u16)b-(u16)a); }
static char* wdiag_errs(char* p,u32 st){
    int any=0;
    if(st&(1u<<13)){ p=scopy(p," got-master-abort"); any=1; }
    if(st&(1u<<12)){ p=scopy(p," got-target-abort"); any=1; }
    if(st&(1u<<11)){ p=scopy(p," sent-target-abort"); any=1; }
    if(st&(1u<<14)){ p=scopy(p," system-error"); any=1; }
    if(st&(1u<<15)){ p=scopy(p," parity-error"); any=1; }
    if(!any)p=scopy(p," no errors");
    return p; }
static void wifi_diag_pci(void){
    extern int g_wpci_bus,g_wpci_slot,g_wpci_bb,g_wpci_bs,g_wpci_bf,g_wpci_snap,g_wpci_mdio,g_wpci_mps; extern u32 g_wpci_st0,g_wpci_bst0,g_wpci_bsec0,g_wpci_bcmd0;
    extern int g_vtd_n,g_vtd_present,g_vtd_changed,g_vtd_failed;
    char b[600],*p; int bus=g_wpci_bus, sl=g_wpci_slot; if(bus<0){ wdiag_out("diag: PCIe - card not prepared yet",RED); return; }
    u32 cs=pci_cfg(bus,sl,0,0x04); int pm=pci_find_cap(bus,sl,0,0x01), pe=pci_find_cap(bus,sl,0,0x10);
    p=b; p=scopy(p,"diag: PCIe card "); p=udec(p,(u32)bus); *p++=':'; p=udec(p,(u32)sl); p=scopy(p," command 0x"); p=hx(p,cs&0xFFFFu,4);
    p=scopy(p,(cs&4u)?" (bus master on)":" (bus master OFF)"); p=scopy(p,", status 0x"); p=hx(p,cs>>16,4); p=scopy(p,":"); p=wdiag_errs(p,cs>>16);
    if(pm){ p=scopy(p,", power D"); p=udec(p,pci_cfg(bus,sl,0,pm+4)&3u); }
    if(pe){ u32 lk=pci_cfg(bus,sl,0,pe+0x10); u32 dc=pci_cfg(bus,sl,0,pe+0x08)&0xFFFFu; u32 d2=pci_cfg(bus,sl,0,pe+0x28)&0xFFFFu;
        p=scopy(p,", link Gen"); p=udec(p,(lk>>16)&0xFu); p=scopy(p," x"); p=udec(p,(lk>>20)&0x3Fu); p=scopy(p,", ASPM "); p=udec(p,lk&3u); p=scopy(p,(lk&(1u<<8))?" clkreq":"");
        p=scopy(p,", payload "); p=udec(p,128u<<((dc>>5)&7u)); p=scopy(p,(d2&0x10u)?", completion timeout off":", completion timeout on"); }
    *p=0; wdiag_out(b,CYAN);
    if(g_wpci_bb>=0){ u32 bc=pci_cfg(g_wpci_bb,g_wpci_bs,g_wpci_bf,0x04), sec=pci_cfg(g_wpci_bb,g_wpci_bs,g_wpci_bf,0x1C)>>16; int be=pci_find_cap(g_wpci_bb,g_wpci_bs,g_wpci_bf,0x10);
        p=b; p=scopy(p,"diag: PCIe bridge "); p=udec(p,(u32)g_wpci_bb); *p++=':'; p=udec(p,(u32)g_wpci_bs); *p++='.'; p=udec(p,(u32)g_wpci_bf); p=scopy(p," (");
        p=hx(p,pci_cfg(g_wpci_bb,g_wpci_bs,g_wpci_bf,0)&0xFFFFu,4); *p++=':'; p=hx(p,pci_cfg(g_wpci_bb,g_wpci_bs,g_wpci_bf,0)>>16,4); p=scopy(p,") command 0x"); p=hx(p,bc&0xFFFFu,4);
        p=scopy(p,(bc&4u)?" (bus master on)":" (bus master OFF)"); p=scopy(p,", status"); p=wdiag_errs(p,bc>>16); p=scopy(p,", downstream status"); p=wdiag_errs(p,sec);
        if(be){ u32 lk=pci_cfg(g_wpci_bb,g_wpci_bs,g_wpci_bf,be+0x10); p=scopy(p,", ASPM "); p=udec(p,lk&3u); u32 bdc=pci_cfg(g_wpci_bb,g_wpci_bs,g_wpci_bf,be+0x08)&0xFFFFu; p=scopy(p,", payload "); p=udec(p,128u<<((bdc>>5)&7u)); }
        *p=0; wdiag_out(b,CYAN); }
    else wdiag_out("diag: PCIe bridge - none found above the card",rgb(230,160,60));
    p=b; p=scopy(p,"diag: PCIe at first start - card status 0x"); p=hx(p,g_wpci_st0,4); p=scopy(p,":"); p=wdiag_errs(p,g_wpci_st0); p=scopy(p,"; bridge command 0x"); p=hx(p,g_wpci_bcmd0,4); p=scopy(p,(g_wpci_bcmd0&4u)?" (bus master was on)":" (bus master was OFF)");
    p=scopy(p,", downstream status"); p=wdiag_errs(p,g_wpci_bsec0); p=scopy(p,"; PHY setting "); p=scopy(p,g_wpci_mdio==0?"written":g_wpci_mdio==1?"not needed":"NOT written"); if(g_wpci_mps)p=scopy(p,"; payload lowered"); *p=0; wdiag_out(b,CYAN);
    p=b; p=scopy(p,"diag: IOMMU - ");
    if(g_vtd_present<=0)p=scopy(p,"no VT-d table, nothing in the way");
    else { p=udec(p,(u32)g_vtd_n); p=scopy(p," unit(s), "); p=udec(p,(u32)g_vtd_changed); p=scopy(p," had translation or memory protection on and were turned off, "); p=udec(p,(u32)g_vtd_failed); p=scopy(p," could not be turned off"); }
    *p=0; wdiag_out(b,CYAN);
    if(!g_rxring){ wdiag_out("diag: DMA memory - RX ring not set up yet",CYAN); return; }
    { u64 r=(u64)g_rxring; p=b; p=scopy(p,"diag: DMA memory - RX ring 0x"); p=hx(p,(u32)r,8); p=scopy(p,(r&0xFFFu)?" (not page aligned)":" (page aligned)");
      p=scopy(p,r<0x100000000ULL?", below 4 GB":", ABOVE 4 GB"); p=scopy(p,mm_is_usable(r,r+4096)?", in usable RAM":", NOT in usable RAM"); *p=0; wdiag_out(b,CYAN); }
}
static void wifi_diag(void){
    char b[600],*p; u32 m=rtw_find_mmio();
    if(!m){ wdiag_out("diag: no Wi-Fi adapter",RED); return; }
    if(!g_rxnum){ wdiag_out("diag: Wi-Fi is not started yet - run: wifi on",RED); return; }
    { const rtw_chip* c=g_rtw_chip; u32 rb0=c->rf_base[0]?c->rf_base[0]:0x2800u, rs0=c->rf_sipi[0]?c->rf_sipi[0]:0x0C90u, rb1=c->rf_base[1]?c->rf_base[1]:0x2C00u, rs1=c->rf_sipi[1]?c->rf_sipi[1]:0x0E90u;
      p=b; p=scopy(p,"diag: chip "); p=scopy(p,c->name); p=scopy(p," on "); p=scopy(p,g_rtw_bus->name); p=scopy(p,", "); p=udec(p,c->n_paths); p=scopy(p,c->n_paths>1?" RF paths":" RF path");
      p=scopy(p," (A: read 0x"); p=hx(p,rb0,4); p=scopy(p,c->rf_mix?" write direct, RF00 via 0x":" write 0x"); p=hx(p,rs0,4);
      if(c->n_paths>1){ p=scopy(p,"; B: read 0x"); p=hx(p,rb1,4); p=scopy(p,c->rf_mix?" write direct, RF00 via 0x":" write 0x"); p=hx(p,rs1,4); }
      p=scopy(p,"), firmware "); p=scopy(p,c->fw_name); p=scopy(p,"; commands sent "); p=udec(p,g_rtw_h2c_sent); p=scopy(p," failed "); p=udec(p,g_rtw_h2c_fail);
      p=scopy(p,"; calibration (IQK) "); p=scopy(p,!g_rtw_iqk_en?"off":g_rtw_iqk_last>0?"done":g_rtw_iqk_last==-2?"timed out":g_rtw_iqk_last==-1?"command not taken":g_rtw_iqk_last==-3?"no command ring":g_rtw_iqk_last==-4?"firmware not running":"not run yet");
      if(g_rtw_iqk_last>0){ p=scopy(p,", fail mask 0x"); p=hx(p,(u32)((g_rtw_iqk_last>>8)&0xFF),2); } *p=0; wdiag_out(b,CYAN); }
    u16 fc=rtw_r16(m,0x0080); u8 cr=rtw_r8(m,0x0100);
    p=b; p=scopy(p,"diag: firmware 0x"); p=hx(p,fc,4); p=scopy(p,(fc&(3u<<14))==(3u<<14)?" (running)":(fc&1u)?" (NOT running, still in download mode)":" (NOT running)"); p=scopy(p,", CR 0x"); p=hx(p,cr,2); p=scopy(p,", RCR 0x"); p=hx(p,rtw_r32(m,0x0608),8);
    p=scopy(p,", TXPAUSE 0x"); p=hx(p,rtw_r16(m,0x0522),4); p=scopy(p,", FUNC 0x"); p=hx(p,rtw_r8(m,0x0002),2); p=scopy(p,", RF_CTRL 0x"); p=hx(p,rtw_r8(m,0x001F),2); *p=0; wdiag_out(b,CYAN);
    { extern int g_dmem_ck,g_imem_ck,g_dmem_sf,g_dmem_if,g_imem_sf,g_imem_if;
      p=b; p=scopy(p,"diag: firmware parts: data "); p=scopy(p,g_dmem_ck?"ok":"BAD"); if(g_dmem_sf)p=scopy(p," not-staged"); if(g_dmem_if)p=scopy(p," copy-timeout"); p=scopy(p,", code "); p=scopy(p,g_imem_ck?"ok":"BAD"); if(g_imem_sf)p=scopy(p," not-staged"); if(g_imem_if)p=scopy(p," copy-timeout");
      p=scopy(p,"; RX DMA 0x284=0x"); p=hx(p,rtw_r32(m,0x0284),8); p=scopy(p," 0x288=0x"); p=hx(p,rtw_r32(m,0x0288),8); p=scopy(p," 0x290=0x"); p=hx(p,rtw_r32(m,0x0290),8); p=scopy(p,", 0x20C=0x"); p=hx(p,rtw_r32(m,0x020C),8); p=scopy(p,", 0x210=0x"); p=hx(p,rtw_r32(m,0x0210),8); p=scopy(p,", 0x1080=0x"); p=hx(p,rtw_r32(m,0x1080),8); *p=0; wdiag_out(b,CYAN); }
    wifi_diag_pci();
    u32 idx=rtw_r32(m,0x03B4), hw=(idx>>16)&0x0FFFu;
    if(idx==0xFFFFFFFFu||hw>=g_rxnum){ wdiag_out("diag: the card does not answer (register reads 0xFFFFFFFF) - it may be powered down",RED); return; }
    p=b; p=scopy(p,"diag: RX ring 0x"); p=hx(p,rtw_r32(m,0x0338),8); p=scopy(p," size "); p=udec(p,rtw_r16(m,0x0382)&0x0FFFu); p=scopy(p," (driver 0x"); p=hx(p,(u32)(u64)g_rxring,8); p=scopy(p," size "); p=udec(p,g_rxnum);
    p=scopy(p,"), card pointer "); p=udec(p,hw); p=scopy(p,", driver pointer "); p=udec(p,g_rxidx); p=scopy(p,", PCI 0x300=0x"); p=hx(p,rtw_r32(m,0x0300),8); *p=0; wdiag_out(b,CYAN);
    p=b; p=scopy(p,"diag: receive filters 0x6A0=0x"); p=hx(p,rtw_r16(m,0x06A0),4); p=scopy(p," 0x6A2=0x"); p=hx(p,rtw_r16(m,0x06A2),4); p=scopy(p," 0x6A4=0x"); p=hx(p,rtw_r16(m,0x06A4),4); p=scopy(p,", RX FIFO 0x11C=0x"); p=hx(p,rtw_r32(m,0x011C),8);
    if(g_rtw_cond_set){ p=scopy(p,", tables cut "); p=udec(p,(u32)g_rtw_cut); p=scopy(p," RFE "); p=udec(p,(u32)g_rtw_rfe); p=scopy(p," package "); p=udec(p,(u32)g_rtw_pkg); } else p=scopy(p,", tables not loaded yet"); *p=0; wdiag_out(b,CYAN);
    { u32 mq=rtw_r32(m,0x03B0), bq=rtw_r32(m,0x03A8), ex=rtw_r32(m,0x023C), lo=rtw_r32(m,0x0234);
      p=b; p=scopy(p,"diag: send queues - mgmt ring 0x"); p=hx(p,rtw_r32(m,0x0310),8); p=scopy(p," size "); p=udec(p,rtw_r16(m,0x0380)&0x0FFFu); p=scopy(p," driver "); p=udec(p,g_mgidx); p=scopy(p," card "); p=udec(p,(mq>>16)&0x0FFFu);
      p=scopy(p,"; data ring driver "); p=udec(p,g_beidx); p=scopy(p," card "); p=udec(p,(bq>>16)&0x0FFFu); p=scopy(p,"; map 0x10C=0x"); p=hx(p,rtw_r16(m,0x010C),4);
      p=scopy(p,", extra pages "); p=udec(p,ex&0xFFFu); p=scopy(p," free "); p=udec(p,(ex>>16)&0xFFFu); p=scopy(p,", low pages "); p=udec(p,lo&0xFFFu); p=scopy(p," free "); p=udec(p,(lo>>16)&0xFFFu); *p=0; wdiag_out(b,CYAN);
      p=b; p=scopy(p,"diag: management frames sent "); p=udec(p,g_mg_sent); p=scopy(p,", left the mgmt queue "); p=udec(p,g_mg_left); p=scopy(p,", stuck there "); p=udec(p,g_mg_stuck); p=scopy(p,", queue full "); p=udec(p,g_mg_noroom);
      p=scopy(p,", sent via data queue "); p=udec(p,g_mg_be); p=scopy(p,"; now using the "); p=scopy(p,g_mg_via_be?"data queue":"mgmt queue"); *p=0; wdiag_out(b,CYAN);
      { extern int g_ampdu_on; extern u32 g_ampdu_sess, g_ampdu_addba, g_ampdu_delba, g_ampdu_decl, g_ampdu_reord, g_ampdu_flush, g_ampdu_old;
        p=b; p=scopy(p,"diag: aggregation (A-MPDU receive) "); p=scopy(p,g_ampdu_on?"on":"off"); p=scopy(p,"; block-ack sessions now "); p=udec(p,g_ampdu_sess); p=scopy(p,", accepted "); p=udec(p,g_ampdu_addba); p=scopy(p,", declined "); p=udec(p,g_ampdu_decl);
        p=scopy(p,", ended by AP "); p=udec(p,g_ampdu_delba); p=scopy(p,"; frames reordered "); p=udec(p,g_ampdu_reord); p=scopy(p,", flushed after timeout "); p=udec(p,g_ampdu_flush); p=scopy(p,", old or duplicate "); p=udec(p,g_ampdu_old); *p=0; wdiag_out(b,CYAN); }
      if(!g_jlog_n)wdiag_out("diag: no join attempt yet - try: wifi connect <name> <password>, then wifi diag",GREY);
      else { u32 n=g_jlog_n<RTW_JLOG_N?g_jlog_n:RTW_JLOG_N; u32 s0=g_jlog_n-n; if(g_jlog_n>RTW_JLOG_N)wdiag_out("diag: last join (oldest lines dropped):",CYAN); else wdiag_out("diag: last join:",CYAN);
        for(u32 k=0;k<n;k++){ p=b; p=scopy(p,"  "); p=scopy(p,g_jlog[(s0+k)%RTW_JLOG_N]); *p=0; wdiag_out(b,WHT); } } }
    int rfe=rtw_rfe_type(); u32 lte=rtw_lte_read(m,0x38);
    p=b; p=scopy(p,"diag: antenna RFE type "); p=udec(p,(u32)rfe); p=scopy(p,", Wi-Fi-only setup "); p=scopy(p,g_rtw_coex_done?"done":"NOT done"); p=scopy(p,", 0x38=0x"); p=hx(p,lte,8);
    p=scopy(p,", 0x4C=0x"); p=hx(p,rtw_r32(m,0x004C),8); p=scopy(p,", 0xCB4=0x"); p=hx(p,rtw_r32(m,0x0CB4),8); p=scopy(p,", 0xCB8=0x"); p=hx(p,rtw_r32(m,0x0CB8),8); p=scopy(p,", 0x73=0x"); p=hx(p,rtw_r8(m,0x0073),2); *p=0; wdiag_out(b,CYAN);
    u8 ch=g_wifi_curch?g_wifi_curch:6; if(!g_wifi_curch){ g_chquiet=1; rtw_set_channel_2g(m,ch); g_chquiet=0; }
    p=b; p=scopy(p,"diag: channel "); p=udec(p,ch); p=scopy(p,", RF18 0x"); p=hx(p,rtw_rf_read_a(m,0x18),5); p=scopy(p,", RF00 0x"); p=hx(p,rtw_rf_read_a(m,0x00),5); p=scopy(p,", listening 1 second..."); *p=0; wdiag_out(b,CYAN);
    rtw_w32(m,0x09A4,rtw_r32(m,0x09A4)|(1u<<17)); rtw_w32(m,0x09A4,rtw_r32(m,0x09A4)&~(1u<<17));
    rtw_w32(m,0x0A2C,rtw_r32(m,0x0A2C)&~(1u<<15)); rtw_w32(m,0x0A2C,rtw_r32(m,0x0A2C)|(1u<<15));
    rtw_w32(m,0x0B58,rtw_r32(m,0x0B58)|1u); rtw_w32(m,0x0B58,rtw_r32(m,0x0B58)&~1u);
    u32 c0c=rtw_r32(m,0x0F04), c0o=rtw_r32(m,0x0F14), c0h=rtw_r32(m,0x0F10), a0o=rtw_r32(m,0x0F08), a0c=rtw_r32(m,0x0FCC);
    u32 hw0=(rtw_r32(m,0x03B4)>>16)&0x0FFFu; if(hw0>=g_rxnum)hw0=hw;
    for(int w=0;w<5;w++)rtw_delay_us(200000);
    u32 c1c=rtw_r32(m,0x0F04), c1o=rtw_r32(m,0x0F14), c1h=rtw_r32(m,0x0F10), a1o=rtw_r32(m,0x0F08), a1c=rtw_r32(m,0x0FCC);
    u32 hw1=(rtw_r32(m,0x03B4)>>16)&0x0FFFu; if(hw1>=g_rxnum)hw1=hw0;
    u32 okc=wdiag_d16(c0c,c1c), oko=wdiag_d16(c0o,c1o), okh=wdiag_d16(c0h,c1h), bad=wdiag_d16(c0c>>16,c1c>>16)+wdiag_d16(c0o>>16,c1o>>16);
    u32 cca=wdiag_d16(a0o>>16,a1o>>16)+wdiag_d16(a0c,a1c);
    u32 dma=(hw1+g_rxnum-hw0)%g_rxnum, back=(hw1+g_rxnum-(g_rxidx%g_rxnum))%g_rxnum;
    p=b; p=scopy(p,"diag: radio decoded CCK "); p=udec(p,okc); p=scopy(p,", OFDM "); p=udec(p,oko); p=scopy(p,", HT "); p=udec(p,okh); p=scopy(p,", bad "); p=udec(p,bad); p=scopy(p,", energy "); p=udec(p,cca); p=scopy(p,"; frames into memory "); p=udec(p,dma); p=scopy(p,", waiting in ring "); p=udec(p,back); *p=0; wdiag_out(b,CYAN);
    u32 good=okc+oko+okh;
    if(dma||back)wdiag_out("diag: result - frames reach memory, the receiver works",GREEN);
    else if(good)wdiag_out("diag: result - the radio decodes frames but none reach memory (MAC receive filter or DMA)",rgb(230,160,60));
    else if(cca||bad)wdiag_out("diag: result - the radio hears energy but decodes nothing (gain, channel or antenna)",rgb(230,160,60));
    else wdiag_out("diag: result - the radio hears nothing (antenna path or radio setup, not the DMA)",rgb(230,120,90));
}
static void iwx_cmd(const char* arg){
    extern void iwx_status(char*,int); extern void iwx_up(char*,int); static char b[1024];
    if(wifi_word(arg,"up")){ term_puts("iwx: starting Intel Wi-Fi (firmware load + ALIVE, up to a few seconds)...\n"); iwx_up(b,sizeof b); }
    else if(!arg[0]||wifi_word(arg,"status"))iwx_status(b,sizeof b);
    else { term_puts("iwx status | iwx up   (Intel AX200/AX201/AX210/AX211)\n"); return; }
    term_puts(b); }
static int wifi_cmd2(const char* arg){
    char b[160],*p; extern const char* wifi_sec_name(const scan_ap*);
    if(!arg[0]&&!rtw_find_mmio())return 0;
    if(!arg[0]||wifi_word(arg,"status")){ p=b; p=scopy(p,"Wi-Fi: "); p=scopy(p,wifi_state_name()); if(g_wm_state==WM_CONNECTED){ p=scopy(p," to "); p=scopy(p,g_wifi_conn); p=scopy(p,", channel "); p=udec(p,g_wm_ch); if(g_wifi_rssi>-127){ p=scopy(p,", -"); p=udec(p,(u32)(-g_wifi_rssi)); p=scopy(p," dBm"); } p=scopy(p,", up to "); p=udec(p,(u32)wifi_rate_mbps()); p=scopy(p," Mbit/s"); } *p++='\n'; *p=0; term_puts(b);
        p=b; p=scopy(p,"  "); p=scopy(p,g_wifi_status); p=scopy(p,"\n  auto-connect "); p=scopy(p,g_wm_auto?"on":"off"); p=scopy(p,", 5 GHz "); p=scopy(p,g_wm_5g?"on":"off"); p=scopy(p,", saved networks "); p=udec(p,(u32)g_wnet_n); *p++='\n'; *p=0; term_puts(b);
        if(!arg[0])term_puts("  wifi on|off|scan|list|saved|connect <name> [password]|eap <name> <user> <password>|hidden <name> [password]|forget <name>|disconnect|auto on|off|5g on|off|powersave on|off|rate auto|safe|diag|coex|mgq on|off|iqk on|off|ampdu on|off\n  (old bring-up and test commands still work: wifi init, allbuilt, macinit, fw, rings, ...)\n");
        return 1; }
    if(wifi_word(arg,"diag")){ if(wifi_up()<0){ tcol(RED); term_puts(g_wifi_status); term_putc('\n'); tcol(WHT); return 1; } wifi_diag(); return 1; }
    if(wifi_word(arg,"iqk")){ const char* w=wifi_next(arg); if(seq((char*)w,"on")){ g_rtw_iqk_en=1; g_rtw_iqk_dead=0; u32 m=rtw_find_mmio(); if(m&&g_rxnum&&rtw_h2c_ring_setup(m)==0)term_puts("  firmware command (H2C) ring set up\n"); } else if(seq((char*)w,"off"))g_rtw_iqk_en=0; else { u32 m=rtw_find_mmio(); if(!m||!g_rxnum){ term_puts("  start Wi-Fi first: wifi on\n"); return 1; } rtw_chip_calibrate(m); }
        term_puts(!g_rtw_iqk_en?"  calibration (IQK): off\n":g_rtw_iqk_last>0?"  calibration (IQK): on, last run done\n":g_rtw_iqk_last<0?"  calibration (IQK): on, last run failed (see wifi diag)\n":"  calibration (IQK): on\n"); return 1; }
    if(wifi_word(arg,"ampdu")){ extern int g_ampdu_on; extern void ampdu_reset(void); const char* w=wifi_next(arg); if(seq((char*)w,"on"))g_ampdu_on=1; else if(seq((char*)w,"off")){ g_ampdu_on=0; ampdu_reset(); }
        term_puts(g_ampdu_on?"  frame aggregation (A-MPDU receive): on - used when the access point asks for it\n":"  frame aggregation (A-MPDU receive): off - new requests are declined (reconnect to stop an active one)\n"); return 1; }
    if(wifi_word(arg,"mgq")){ g_mg_try_mgq=seq((char*)wifi_next(arg),"on"); g_mg_via_be=!g_mg_try_mgq; term_puts(g_mg_try_mgq?"  management frames: management queue first (checked, falls back to the data queue)\n":"  management frames: data queue (default)\n"); return 1; }
    if(wifi_word(arg,"coex")){ u32 m=rtw_find_mmio(); if(!m||!g_rxnum){ term_puts("  start Wi-Fi first: wifi on\n"); return 1; } int ok=rtw_coex_wifi_only(m); term_puts(ok?"  antenna set for Wi-Fi only\n":"  antenna set, but the coex register did not answer\n"); return 1; }
    if(wifi_word(arg,"on")){ wifi_user_power(1); if(wifi_up()==0){ wifi_radio_on(); term_puts("Wi-Fi is on\n"); } else { tcol(RED); term_puts(g_wifi_status); term_putc('\n'); tcol(WHT); } return 1; }
    if(wifi_word(arg,"off")){ wifi_user_power(0); wifi_radio_off(); term_puts("Wi-Fi is off\n"); return 1; }
    if(wifi_word(arg,"scan")||wifi_word(arg,"list")){ if(wifi_word(arg,"scan")){ if(wifi_up()<0){ tcol(RED); term_puts(g_wifi_status); term_putc('\n'); tcol(WHT); return 1; } term_puts("Looking for networks (2.4 and 5 GHz)...\n"); wifi_scan_blocking(1); }
        for(int i=0;i<g_apn;i++){ p=b; p=scopy(p,"  "); int bars=wifi_signal_bars(g_aps[i].rssi); for(int k=0;k<4;k++)*p++=k<bars?'#':'.'; p=scopy(p,"  "); p=scopy(p,g_aps[i].ssid[0]?g_aps[i].ssid:"<hidden>"); while(p-b<42)*p++=' ';
            p=scopy(p,g_aps[i].ch>14?"5G  ch ":"2.4 ch "); p=udec(p,g_aps[i].ch); while(p-b<56)*p++=' '; p=scopy(p,wifi_sec_name(&g_aps[i])); if(g_aps[i].ht)p=scopy(p,"  n"); if(wifi_saved_find(g_aps[i].ssid)>=0)p=scopy(p,"  saved"); *p++='\n'; *p=0; term_puts(b); }
        if(!g_apn)term_puts("  no networks found\n"); return 1; }
    if(wifi_word(arg,"saved")){ if(!g_wnet_n)term_puts("  no saved networks\n"); for(int i=0;i<g_wnet_n;i++){ p=b; p=scopy(p,"  "); p=scopy(p,g_wnets[i].ssid); p=scopy(p,"  priority "); p=udec(p,(u32)g_wnets[i].prio); if(g_wnets[i].hidden)p=scopy(p," hidden"); if(g_wnets[i].eap)p=scopy(p," enterprise"); if(g_wnets[i].autoc!=1)p=scopy(p,g_wnets[i].autoc==2?" manual (password failed)":" manual"); *p++='\n'; *p=0; term_puts(b); } term_puts("  stored in /etc/noovex/wifi/networks (root only)\n"); return 1; }
    if(wifi_word(arg,"connect")||wifi_word(arg,"hidden")||wifi_word(arg,"eap")){ int hid=wifi_word(arg,"hidden"), eap=wifi_word(arg,"eap"); const char* a=wifi_next(arg); char ss[33],u[65],pw[65]; u[0]=0; pw[0]=0;
        if(!wifi_tok(a,ss,33)){ term_puts("usage: wifi connect <name> [password]   (use \"quotes\" for names with spaces)\n"); return 1; } a=wifi_skip(a);
        if(eap){ wifi_tok(a,u,65); a=wifi_skip(a); } wifi_tok(a,pw,65);
        int sv=wifi_saved_find(ss); if(!pw[0]&&sv>=0){ int k=0; for(;g_wnets[sv].pass[k];k++)pw[k]=g_wnets[sv].pass[k]; pw[k]=0; if(!u[0]){ for(k=0;g_wnets[sv].user[k];k++)u[k]=g_wnets[sv].user[k]; u[k]=0; } }
        if(wifi_up()<0){ tcol(RED); term_puts(g_wifi_status); term_putc('\n'); tcol(WHT); return 1; }
        term_puts("Connecting...\n"); int r=wifi_connect_ssid(ss,pw,u,hid); for(int k=0;k<65;k++)pw[k]=0;
        tcol(r==0?GREEN:RED); term_puts(g_wifi_status); term_putc('\n'); tcol(WHT);
        if(r!=0&&g_jlog_n){ u32 n=g_jlog_n<RTW_JLOG_N?g_jlog_n:RTW_JLOG_N; u32 s0=g_jlog_n-n; tcol(GREY); term_puts("  join steps:\n"); for(u32 k=0;k<n;k++){ term_puts("  "); term_puts(g_jlog[(s0+k)%RTW_JLOG_N]); term_putc('\n'); } tcol(WHT); }
        if(r==0){ p=b; p=scopy(p,"  address "); p=ipstr(p,my_ip); p=scopy(p,"  gateway "); p=ipstr(p,gw_ip); *p++='\n'; *p=0; term_puts(b); } return 1; }
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
        term_puts("net usbnet dhcp wifi usb usbdisk usbcat usbwrite usbrm ec acpi battery brightness timers timertest utest ring3 run app posix mem sched fork threads preempt ipc wm compositor cores smpstart smpwork lua ping dns http https netlog pci disks colors settings reboot panic\n");
        term_puts("browse <url> www desktop  (web browser: HTML, CSS, JavaScript, cookies)   qjs [-e code] [file.js]  (QuickJS)\n"); term_puts("browser [netbg|csp|samesite] [on|off]  (browser switches for testing)\n");
        term_puts("play <file|URL>  (fullscreen player: space pause, arrows seek, q quit; -b benchmark)   video <file|URL>  (Video Player window)\n");
        term_puts("fatinfo fatls fatcat  (read files from an IDE/FAT disk)\n");
        term_puts("whoami id groups who last  passwd [-l|-u|-d] [user]  su [user]  sudo [-k|-l] cmd  lock logout  hostname [name]\n");
        term_puts("sudo: adduser [--admin] NAME  useradd [-a] [-c \"Full Name\"] NAME  userdel [-r] NAME  usermod -aG|-rG GROUP|-c|-L|-U NAME\n");
        term_puts("nvxlogin nvxwhoami nvxlogout nvxtest  (Noovex account + API probe)\n");
        term_puts("update [check|install|back|new]  (NoovexOS updates from noovexos.site, keeps the previous version)\n");
        term_puts("crash memcheck cputemp amdgpio gpus  (crash and freeze reports, heap check, CPU temperature, AMD GPIO, graphics cards)\n"); }
    else if(seq(cmd,"fatinfo")||seq(cmd,"mount")){ if(fat_mount()){ char b[96],*p=b; p=scopy(p,"FAT"); p=scopy(p,fat_is32?"32":"16"); p=scopy(p," mounted   drive="); p=udec(p,(u32)g_ata_drv); p=scopy(p,"   sec/clus="); p=udec(p,(u32)fat_spc); p=scopy(p,"   data@LBA "); p=udec(p,fat_datastart); *p=0; tcol(CYAN);term_puts(b);tcol(WHT);term_putc('\n'); } else { tcol(RED);term_puts("no FAT disk found (ATA/IDE PIO only; VM needs an IDE disk with FAT)\n");tcol(WHT); } }
    else if(seq(cmd,"lag")){ extern void lag_report(void); lag_report(); }
    else if(seq(cmd,"log")||seq(cmd,"dmesg")){ extern int klog_ring(char*,int,int); int want=40; { const char* q=arg; int v=0; while(*q>='0'&&*q<='9')v=v*10+(*q++-'0'); if(v>0&&v<400)want=v; }
        char* kb=(char*)kmalloc(16385); if(!kb){ term_puts("log: out of memory\n"); return; } int n=klog_ring(kb,16384,1); if(n<0)n=0; kb[n]=0;
        int st=n, lines=0; while(st>0){ if(kb[st-1]=='\n'&&st<n){ if(++lines>=want)break; } st--; }
        if(!n)term_puts("log: the kernel log is empty\n"); else term_puts(kb+st); kfree(kb); }
    else if(seq(cmd,"report")){
        if(g_capture){ term_puts("report: cannot run inside a pipe or redirection\n"); return; }
        extern int klog_ring(char*,int,int); extern int fs_save_bin(const char*,const u8*,int,int); extern int msd_ok, ufat_ok; extern int ufat_write_file(const char*,const char*,int); extern int disk_ok; extern void nvx3_sync_all(void); extern int fs_find_name(const char*);
        const int CAP=65536; char* rb=(char*)kmalloc((u64)CAP+1); if(!rb){ term_puts("report: out of memory\n"); return; } int rn=0;
        static const char* RC[6]={"ver","usbdevs","usbinfo","usbhid","lag","crash"};
        term_puts("report: collecting, please wait...\n");
        for(int c=0;c<6;c++){ const char* h="== "; for(int k=0;h[k]&&rn<CAP;k++)rb[rn++]=h[k]; for(int k=0;RC[c][k]&&rn<CAP;k++)rb[rn++]=RC[c][k]; for(const char* t=" ==\n";*t&&rn<CAP;t++)rb[rn++]=*t;
            g_capture=1; g_cap_n=0; g_cap[0]=0; char line[16]; int k=0; for(;RC[c][k]&&k<15;k++)line[k]=RC[c][k]; line[k]=0; run_cmd_one(line); g_capture=0;
            for(int j=0;j<g_cap_n&&rn<CAP;j++)rb[rn++]=g_cap[j]; if(g_cap_n>=8191){ for(const char* t="\n(output cut at 8 KB)\n";*t&&rn<CAP;t++)rb[rn++]=*t; } if(rn<CAP)rb[rn++]='\n'; }
        g_cap_n=0;
        for(const char* t="== kernel log (last 8 KB) ==\n";*t&&rn<CAP;t++)rb[rn++]=*t;
        { int room=CAP-rn; if(room>8192)room=8192; if(room>0){ int got=klog_ring(rb+rn,room,1); if(got>0)rn+=got; } }
        rb[rn]=0;
        int a1=fs_save_bin("report.txt",(const u8*)rb,rn,0); if(a1>=0&&disk_ok)nvx3_sync_all(); int a2=(msd_ok&&ufat_ok)?ufat_write_file("REPORT.TXT",rb,rn):-1;
        int ndev=0; for(int k=0;k<g_usbdev_n;k++)if(g_usbdev[k].used)ndev++;
        int hascrash=disk_ok&&fs_find_name("crash-report.txt")>=0;
        char b[200],*p;
        tcol(rgb(255,210,90)); term_puts("=== NoovexOS report ===\n"); tcol(WHT);
        p=b; p=scopy(p,"Version: "); p=scopy(p,(char*)g_buildsig.version); *p++='\n'; *p=0; term_puts(b);
        p=b; p=scopy(p,"USB devices: "); p=udec(p,(u32)ndev); *p++='\n'; *p=0; term_puts(b);
        p=b; p=scopy(p,"Crash report: "); p=scopy(p,hascrash?"yes (crash-report.txt)":"none"); *p++='\n'; *p=0; term_puts(b);
        p=b; p=scopy(p,"Report size: "); p=udec(p,(u32)rn); p=scopy(p," bytes\n"); *p=0; term_puts(b);
        p=b; p=scopy(p,"Saved: "); if(a2==0)p=scopy(p,"REPORT.TXT on the USB stick"); if(a2==0&&a1>=0)p=scopy(p," and "); if(a1>=0)p=scopy(p,"report.txt in Files"); if(a1<0&&a2!=0)p=scopy(p,"NOT saved - plug in a USB stick and type report again"); *p++='\n'; *p=0;
        tcol(a1>=0||a2==0?GREEN:rgb(255,120,120)); term_puts(b); tcol(WHT); kfree(rb); }
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
    else if(seq(cmd,"usbclone")||seq(cmd,"clone-to-usb")){ if(!need_root(cmd))return; extern int clone_launch(int); clone_launch(g_state==ST_LIVE?ST_LIVE:ST_DESKTOP); }
    else if(seq(cmd,"install")){ if(!need_root(cmd))return; if(arg[0]=='c')ip_open(g_state==ST_LIVE?ST_LIVE:ST_DESKTOP); else inst_launch(g_state==ST_LIVE?ST_LIVE:ST_DESKTOP); }
    else if(seq(cmd,"diskmgr")||seq(cmd,"partitions")){ if(!need_root(cmd))return; dm_open(g_state==ST_LIVE?ST_LIVE:ST_DESKTOP); }
    else if(seq(cmd,"noovexos_install")){ if(!need_root(cmd))return; inst_launch(ST_LIVE); }
    else if(seq(cmd,"firefox-mode")){ const char* a=arg; while(*a==' ')a++;
        if(a[0]=='m'){ static char c1[]="bash -c 'mkdir -p $HOME/.nvx-firefox && touch $HOME/.nvx-firefox/.nvx-multiprocess'"; run_cmd(c1); term_puts("Firefox will start in multi-process mode (4 content processes) next time. Undo with: firefox-mode single\n"); }
        else if(a[0]=='s'){ static char c2[]="bash -c 'rm -f $HOME/.nvx-firefox/.nvx-multiprocess'"; run_cmd(c2); term_puts("Firefox will start in single-process mode next time.\n"); }
        else term_puts("usage: firefox-mode multi | single\n"); }
    else if((seq(cmd,"firefox")||seq(cmd,"firefox-esr"))&&!arg[0]){ extern void firefox_launch(void); tcol(GREY); term_puts("starting Firefox (sandbox off, software rendering, full-screen X; log in /tmp/ff.log)\n"); tcol(WHT); firefox_launch(); }
    else if(seq(cmd,"apt")||seq(cmd,"apt-get")||seq(cmd,"apt-cache")){ apt_cmd(arg); }
    else if(seq(cmd,"firefox-webgl")){ if(!need_root(cmd))return; term_puts("Installing Mesa (software OpenGL) so Firefox can use WebGL. Restart Firefox afterwards.\n"); apt_cmd("install libegl1 libgles2 libegl-mesa0 libgl1-mesa-dri libglx-mesa0"); }
    else if(seq(cmd,"dpkg")){ dpkg_cmd(arg); }
    else if(seq(cmd,"xstat")){ extern void xstat_print(void (*)(const char*)); extern int g_xd_off; const char* a=arg; while(*a==' ')a++; xstat_print(term_puts); if(a[0]=='o'&&a[1]=='f')g_xd_off=1; else if(a[0]=='o'&&a[1]=='n')g_xd_off=0; }
    else if(seq(cmd,"startx")||seq(cmd,"xserver")){ extern int g_xfb_live; extern void xfull_enter(void); const char* a=arg; while(*a==' ')a++; int full=(a[0]=='f'||(a[0]=='-'&&a[1]=='f'))||seq(cmd,"xserver"); int inwin=(a[0]=='w'); extern int g_xover;
        if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; }
        if(g_xfb_live){ if(full){ term_puts("startx: switching to the full-screen X session (F12 or Ctrl+Alt+Backspace returns)\n"); xfull_enter(); } else { term_puts("startx: the X server is already running on :0\n"); if(!g_xover)open_win(W_XDISP); } }
        else { dt_init(); { int t=dt_child(0,"tmp"); if(t<0)dt_mkdir_at(0,"tmp"); } { int v=dt_child(0,"var"); if(v<0)v=dt_mkdir_at(0,"var"); int l=dt_child(v,"lib"); if(l<0)l=dt_mkdir_at(v,"lib"); if(dt_child(l,"xkb")<0)dt_mkdir_at(l,"xkb"); }
            static char xl[200]; char* p=xl; p=scopy(p,"Xvfb :0 -screen 0 "); if(!inwin){ p=udec(p,W); *p++='x'; p=udec(p,H); } else p=scopy(p,"800x600"); p=scopy(p,full||inwin?"x24 -retro":"x24"); p=scopy(p," -dumbSched -fbdir /tmp -fp built-ins -listen tcp -listen unix -nolisten local -ac -xkbdir /usr/share/X11/xkb > /tmp/Xvfb.log 2>&1 &"); *p=0;
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
            term_puts("  nvgsp vram      VRAM size, reserved layout and allocator (read only, safe)\n  nvgsp boot      FWSEC, booter, GSP-RM, static info, BAR1/BAR2 windows\n  nvgsp objects   client, device, subdevice, address space, channels, 3D/compute/copy objects, copy test\n  nvgsp all       boot + objects + screen through the copy engine\n  nvgsp present on|off   desktop frames through the NVIDIA copy engine or the CPU\n  nvgsp monitors  outputs, connected monitors and their native modes (read only)\n  nvgsp mode      second screen on: native mode, same picture as the first screen\n  nvgsp mode all  the same, and the first screen is set to its native mode too\n  nvgsp mode off  screens set by nvgsp mode off again\n  nvgsp 3d        3D engine test: triangles with depth, read back and checked (needed before Gears draws on the GPU)\n  nvgsp status    current state\n  nvgsp log       print the last log again (saved as nvgsp-log.txt in Files and NVGSPLOG.TXT on the USB stick)\n  nvgsp unload    stop GSP-RM and tear down WPR2 so it can be started again\n");
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
    else if(seq(cmd,"pagecache")){
        extern void sysroot_pc_stats(u64*,u64*,u64*,u64*,u64*); extern u64 g_vm_file_faults, g_elf_lazy_pages, g_elf_copy_pages, g_lx_fmaps, g_lx_fmap_pages;
        u64 fi=0,fl=0,hi=0,ns=0,by=0; sysroot_pc_stats(&fi,&fl,&hi,&ns,&by);
        char b[160],*p;
        tcol(rgb(120,200,255)); term_puts("  Linux files: page cache and unpacking\n"); tcol(WHT);
        p=b; p=scopy(p,"  files with cached pages  "); p=udec(p,(u32)fi); p=scopy(p,"\n"); *p=0; term_puts(b); klog(b);
        p=b; p=scopy(p,"  pages read into cache    "); p=udec(p,(u32)fl); p=scopy(p," ("); p=udec(p,(u32)(fl/256)); p=scopy(p," MB), reused "); p=udec(p,(u32)hi); p=scopy(p," times\n"); *p=0; term_puts(b); klog(b);
        p=b; p=scopy(p,"  page faults from files   "); p=udec(p,(u32)g_vm_file_faults); p=scopy(p,"\n"); *p=0; term_puts(b); klog(b);
        p=b; p=scopy(p,"  mapped files (mmap)      "); p=udec(p,(u32)g_lx_fmaps); p=scopy(p,", "); p=udec(p,(u32)(g_lx_fmap_pages/256)); p=scopy(p," MB reserved\n"); *p=0; term_puts(b); klog(b);
        p=b; p=scopy(p,"  program pages at start   "); p=udec(p,(u32)g_elf_lazy_pages); p=scopy(p," on demand, "); p=udec(p,(u32)g_elf_copy_pages); p=scopy(p," copied\n"); *p=0; term_puts(b); klog(b);
        p=b; p=scopy(p,"  unpacked                 "); p=udec(p,(u32)(by>>20)); p=scopy(p," MB in "); p=udec(p,(u32)(ns/1000000ULL)); p=scopy(p," ms\n"); *p=0; term_puts(b); klog(b); }
    else if(seq(cmd,"security")||seq(cmd,"secureboot")){
        tcol(rgb(120,200,255)); term_puts("  NoovexOS signed boot\n"); tcol(WHT); secboot_report(term_puts); }
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
