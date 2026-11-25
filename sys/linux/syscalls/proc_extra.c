static volatile u64 g_lavg[3];
static volatile int g_lavg_run, g_lavg_tot;
void lx_load_sample(void){
    int n=0, t=0; for(int i=0;i<JMAX;i++){ int s=g_jp[i].state; if(s==S_FREE||s==S_TDEAD||s==S_ZOMB)continue; t++; if(s==S_RUN||g_jp[i].runcpu>0)n++; }
    static const u64 E[3]={1884,2014,2037}; u64 a=(u64)n<<11;
    for(int k=0;k<3;k++){ u64 l=g_lavg[k]; l=(l*E[k]+a*(2048-E[k]))>>11; g_lavg[k]=l; }
    g_lavg_run=n; g_lavg_tot=t; }
static char* lpx_s(char* o,const char* s){ while(*s)*o++=*s++; return o; }
static char* lpx_fix2(char* o,u64 v){ o=lx_u64s(o,v/100); *o++='.'; *o++=(char)('0'+(v/10)%10); *o++=(char)('0'+v%10); return o; }
static char* lpx_kb(char* o,const char* k,u64 v){ o=lpx_s(o,k); int pad=16-(int)(sizeof(char)*0); (void)pad; o=lx_u64s(o,v); return lpx_s(o," kB\n"); }
u64 lx_task_rss_pages(int slot){ if(slot<0||slot>=JMAX)return 0; struct jproc* L=jc_lead(&g_jp[slot]); return lxn_pages(L->cr3); }
u64 lx_task_vsz_bytes(int slot){
    struct jproc* L=jc_lead(&g_jp[slot]); u64 cr3=L->cr3&~0xFFFULL; u64 v=0x2000000ULL;
    if(L->brk>0x40002000000ULL)v+=L->brk-0x40002000000ULL;
    int cnt=g_mapn_i<2048?g_mapn_i:2048; for(int j=0;j<cnt;j++){ struct lx_mapname* m=&g_mapn[(g_mapn_i-cnt+j)%2048]; if((m->cr3&~0xFFFULL)==cr3&&m->e>m->b)v+=m->e-m->b; }
    u64 r=lxn_pages(L->cr3)*4096ULL; if(v<r)v=r; return v; }
u64 lx_boot_epoch(void){ u64 now=k_realtime_ms()/1000ULL, up=g_ticks/100; return now>up?now-up:0; }
int lx_proc_sys_gen(const char* p,char* out,int cap){
    char* o=out; if(cap<4096)return -1;
    if(lx_streq(p,"/proc/uptime")){ u64 idle=0; for(int i=0;i<NCPU;i++) if(i==0||(g_smp_on&&g_cpu[i].online))idle+=g_cpu[i].idle_ticks; o=lpx_fix2(o,g_ticks); *o++=' '; o=lpx_fix2(o,idle); *o++='\n'; return (int)(o-out); }
    if(lx_streq(p,"/proc/loadavg")){ for(int k=0;k<3;k++){ u64 l=g_lavg[k]; u64 c=(l*100+1024)>>11; o=lpx_fix2(o,c); *o++=' '; } int r=g_lavg_run; if(r<1)r=1; o=lx_u64s(o,(u64)r); *o++='/'; o=lx_u64s(o,(u64)(g_lavg_tot>0?g_lavg_tot:1)); *o++=' '; o=lx_u64s(o,(u64)(g_next_pid>1?g_next_pid-1:1)); *o++='\n'; return (int)(o-out); }
    if(lx_streq(p,"/proc/version")){ o=lpx_s(o,"Linux version 6.12.0-noovex (noovex@noovexos) (gcc) #1 SMP PREEMPT NoovexOS 64 v11.22\n"); return (int)(o-out); }
    if(lx_streq(p,"/proc/meminfo")){
        extern void mem_summary(u64*,u64*,u64*); u64 mt,mu,mh; mem_summary(&mt,&mu,&mh);
        extern void sysroot_cache_stats(u64*,u64*,u64*,u64*); extern void swap_stats(u64*,u64*,u64*,u64*); extern u32 pframe_free_count(void);
        u64 fr=(u64)pframe_free_count()*4; if(fr>mt-mu)fr=mt-mu;
        u64 pcf=0,pcr=0,bcb=0,bcu=0; sysroot_cache_stats(&pcf,&pcr,&bcb,&bcu); u64 st=0,su=0,ss=0,sc=0; swap_stats(&st,&su,&ss,&sc);
        u64 cached=pcf*4+bcb/1024, avail=fr+pcr*4+bcu/1024; if(avail>mt)avail=mt;
        u64 files=0; for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].data)files+=(u64)FS[k].cap; files/=1024;
        u64 anon=mt>fr+cached+files+mh?mt-fr-cached-files-mh:0; u64 shm=0; for(int i=0;i<MFD_MAX;i++) if(g_mfd[i].used)shm+=g_mfd[i].size; shm/=1024;
        o=lpx_kb(o,"MemTotal:       ",mt); o=lpx_kb(o,"MemFree:        ",fr); o=lpx_kb(o,"MemAvailable:   ",avail);
        o=lpx_kb(o,"Buffers:        ",0); o=lpx_kb(o,"Cached:         ",cached+files); o=lpx_kb(o,"SwapCached:     ",0);
        o=lpx_kb(o,"Active:         ",anon+files); o=lpx_kb(o,"Inactive:       ",cached); o=lpx_kb(o,"Active(anon):   ",anon); o=lpx_kb(o,"Inactive(anon): ",0); o=lpx_kb(o,"Active(file):   ",files); o=lpx_kb(o,"Inactive(file): ",cached);
        o=lpx_kb(o,"Unevictable:    ",0); o=lpx_kb(o,"Mlocked:        ",0);
        o=lpx_kb(o,"SwapTotal:      ",st); o=lpx_kb(o,"SwapFree:       ",st>su?st-su:0); o=lpx_kb(o,"Zswap:          ",ss); o=lpx_kb(o,"Zswapped:       ",su);
        o=lpx_kb(o,"Dirty:          ",0); o=lpx_kb(o,"Writeback:      ",0); o=lpx_kb(o,"AnonPages:      ",anon); o=lpx_kb(o,"Mapped:         ",0); o=lpx_kb(o,"Shmem:          ",shm);
        o=lpx_kb(o,"KReclaimable:   ",0); o=lpx_kb(o,"Slab:           ",mh); o=lpx_kb(o,"SReclaimable:   ",0); o=lpx_kb(o,"SUnreclaim:     ",mh);
        o=lpx_kb(o,"KernelStack:    ",0); o=lpx_kb(o,"PageTables:     ",0); o=lpx_kb(o,"CommitLimit:    ",mt/2+st); o=lpx_kb(o,"Committed_AS:   ",mt-fr);
        o=lpx_kb(o,"VmallocTotal:   ",34359738367ULL); o=lpx_kb(o,"VmallocUsed:    ",0); o=lpx_kb(o,"VmallocChunk:   ",0);
        o=lpx_s(o,"HugePages_Total:       0\nHugePages_Free:        0\nHugePages_Rsvd:        0\nHugePages_Surp:        0\nHugepagesize:       2048 kB\n");
        o=lpx_kb(o,"KernelHeap:     ",mh); return (int)(o-out); }
    if(lx_streq(p,"/proc/vmstat")){ extern u32 pframe_free_count(void); u64 sw=0; for(int i=0;i<NCPU;i++)sw+=g_cpu[i].switches;
        o=lpx_s(o,"nr_free_pages "); o=lx_u64s(o,(u64)pframe_free_count()); o=lpx_s(o,"\nnr_inactive_anon 0\nnr_active_anon 0\nnr_inactive_file 0\nnr_active_file 0\nnr_unevictable 0\nnr_mlock 0\nnr_anon_pages 0\nnr_mapped 0\nnr_file_pages 0\nnr_dirty 0\nnr_writeback 0\nnr_shmem 0\nnr_slab_reclaimable 0\nnr_slab_unreclaimable 0\nnr_page_table_pages 0\nnr_kernel_stack 0\nnr_dirtied 0\nnr_written 0\npgpgin 0\npgpgout 0\npswpin 0\npswpout 0\npgalloc_normal 0\npgfree 0\npgactivate 0\npgdeactivate 0\npgfault 0\npgmajfault 0\npgsteal_kswapd 0\npgscan_kswapd 0\npgscan_direct 0\npgsteal_direct 0\noom_kill 0\n"); (void)sw; return (int)(o-out); }
    if(lx_streq(p,"/proc/diskstats")){ extern int gdisk_count(void); int n=gdisk_count(); for(int i=0;i<n&&i<8;i++){ o=lpx_s(o,"   8      "); o=lx_u64s(o,(u64)i*16); o=lpx_s(o," sd"); *o++=(char)('a'+i); o=lpx_s(o," 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"); } return (int)(o-out); }
    if(lx_streq(p,"/proc/sys/kernel/pid_max")){ o=lpx_s(o,"4194304\n"); return (int)(o-out); }
    if(lx_streq(p,"/proc/sys/kernel/threads-max")){ o=lx_u64s(o,JMAX); *o++='\n'; return (int)(o-out); }
    if(lx_streq(p,"/proc/sys/kernel/ostype")){ o=lpx_s(o,"Linux\n"); return (int)(o-out); }
    if(lx_streq(p,"/proc/sys/kernel/hostname")){ o=lpx_s(o,g_hostname); *o++='\n'; return (int)(o-out); }
    if(lx_streq(p,"/proc/sys/kernel/random/boot_id")||lx_streq(p,"/proc/sys/kernel/random/uuid")){ u8 r[16]; csprng_bytes(r,16); static const char H[]="0123456789abcdef"; for(int i=0;i<16;i++){ if(i==4||i==6||i==8||i==10)*o++='-'; *o++=H[r[i]>>4]; *o++=H[r[i]&15]; } *o++='\n'; return (int)(o-out); }
    if(lx_streq(p,"/proc/sys/fs/file-max")){ o=lpx_s(o,"9223372036854775807\n"); return (int)(o-out); }
    if(lx_streq(p,"/proc/sys/fs/nr_open")){ o=lx_u64s(o,FDMAX); *o++='\n'; return (int)(o-out); }
    if(lx_streq(p,"/proc/partitions")){ o=lpx_s(o,"major minor  #blocks  name\n\n"); extern int gdisk_count(void); extern u64 gdisk_size_of(int); int n=gdisk_count(); for(int i=0;i<n&&i<8;i++){ o=lpx_s(o,"   8       "); o=lx_u64s(o,(u64)i*16); o=lpx_s(o,"   "); o=lx_u64s(o,(u64)gdisk_size_of(i)/2); o=lpx_s(o," sd"); *o++=(char)('a'+i); *o++='\n'; } return (int)(o-out); }
    if(lx_streq(p,"/proc/net/dev")){ o=lpx_s(o,"Inter-|   Receive                                                |  Transmit\n face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n    lo:       0       0    0    0    0     0          0         0        0       0    0    0    0     0       0          0\n  eth0:       0       0    0    0    0     0          0         0        0       0    0    0    0     0       0          0\n"); return (int)(o-out); }
    return -1; }
static int lpx_uread(u64 cr3,u64 va,u8* dst,int n){
    if(!cr3)return -1;
    while(n>0){ if((va>>39)!=8||((va+(u64)n-1)>>39)!=8)return -1; u64* t=(u64*)(cr3&PTE_ADDR); u64 e=t[(va>>39)&511]; if((e&5)!=5)return -1; t=(u64*)(e&PTE_ADDR); e=t[(va>>30)&511]; if((e&5)!=5)return -1;
        u64 pa; if(e&0x80)pa=(e&0x000FFFFFC0000000ULL)+(va&0x3FFFFFFFULL); else { t=(u64*)(e&PTE_ADDR); e=t[(va>>21)&511]; if((e&5)!=5)return -1;
            if(e&0x80)pa=(e&0x000FFFFFFFE00000ULL)+(va&0x1FFFFFULL); else { t=(u64*)(e&PTE_ADDR); e=t[(va>>12)&511]; if((e&5)!=5)return -1; pa=(e&PTE_ADDR)+(va&0xFFFULL); } }
        { extern u32 g_ram_mb; if(pa>=((u64)g_ram_mb<<20)+0x100000000ULL)return -1; }
        int c=(int)(4096-(va&0xFFF)); if(c>n)c=n; const u8* s=(const u8*)pa; for(int i=0;i<c;i++)dst[i]=s[i]; dst+=c; va+=(u64)c; n-=c; }
    return 0; }
int lx_task_cmdline(struct jproc* L,char* out,int cap){
    if(!L->argv_va||L->argc_n<=0||L->argc_n>4096)return -1; int o=0;
    for(int i=0;i<L->argc_n&&o<cap-1;i++){ u64 ap=0; if(lpx_uread(L->cr3,L->argv_va+(u64)i*8,(u8*)&ap,8)||!ap)return o?o:-1;
        for(;o<cap-1;){ u8 c; if(lpx_uread(L->cr3,ap++,&c,1))return o?o:-1; out[o++]=(char)c; if(!c)break; } }
    return o; }
