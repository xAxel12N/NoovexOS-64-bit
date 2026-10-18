#include "kernel64.c"
u64 hk_setup(void){
    percpu_init_bsp();
    heap_base=0x10000000ULL; heap_cur=heap_base; heap_top=heap_base+0x4000000ULL; heap_head=0;
    for(u32 i=0;i<sizeof(pf_bitmap);i++)pf_bitmap[i]=0; pf_used=0; pf_ready=1; g_ram_mb=512;
    g_us_ready=1; fd_init(); dt_init(); env_init(); vfs_init(); g_capture=1; g_cap_n=0; g_cap[0]=0; disk_ok=0;
    return pframe_alloc();
}
int hk_spawn(const char* args){
    int i=jc_create(args,0,0); if(i<0)return i;
    g_jp[i].sid=1; g_tty_fg=g_jp[i].pgid; return i;
}
int hk_step(void){
    int live=0; for(int i=0;i<JMAX;i++) if(g_jp[i].state!=S_FREE&&g_jp[i].state!=S_ZOMB)live++;
    if(!live)return 0;
    if(jc_any_runnable())jc_run();
    g_ticks++;
    return 1;
}
int hk_state(int i){ return g_jp[i].state; }
int hk_exitst(int i){ return g_jp[i].exitst; }
int hk_pid(int i){ return g_jp[i].pid; }
void hk_reap(int i){ if(g_jp[i].state==S_ZOMB)g_jp[i].state=S_FREE; }
const char* hk_cap(int* n){ *n=g_cap_n; return g_cap; }
void hk_cap_reset(void){ g_cap_n=0; g_cap[0]=0; }
int hk_running(void){ return g_js_running; }
void hk_fault(void* r){ jc_fault((regs_t*)r); }
u64 hk_fsbase(void){ return lsys_fsbase; }
u32 hk_free_frames(void){ return pframe_free_count(); }
int hk_free_vfiles(void){ int n=0; for(int i=0;i<VMAXOPEN;i++) if(!g_vf[i].used)n++; return n; }
int hk_free_pipes(void){ int n=0; for(int i=0;i<PIPE_MAX;i++) if(!g_pipe[i].used)n++; return n; }
int hk_used_files(void){ int n=0; for(int k=0;k<FS_N;k++) if(FS[k].used)n++; return n; }
void hk_setenv(const char* a,const char* b){ env_set(a,b); }
int hk_addfile(const char* dir,const char* name,const unsigned char* d,int n){
    int dd=dt_resolve_dir(dir); if(dd<0||n>FS_CAP-1)return -1;
    for(int k=0;k<FS_N;k++) if(!FS[k].used){ fs_newslot(k); int q=0; while(name[q]&&q<63){ FS[k].name[q]=name[q]; q++; } FS[k].name[q]=0;
        if(fs_set(k,(const char*)d,n))return -1; FS[k].used=1; FS[k].dir=dd; FS[k].folder=0; FS[k].mode=0100755u; FS[k].uid=0; FS[k].gid=0; FS[k].nlink=1; return k; }
    return -1;
}
void hk_delfile(int k){ if(k>=0&&k<FS_N&&FS[k].used){ FS[k].used=0; fs_slot_freed(k); } }
int hk_readfile(const char* p,char* out,int cap){ int isd=0; int k=ux_lookup(p,&isd,1); if(k<0||isd)return -1; int n=FS[k].len<cap?FS[k].len:cap; const char* sd=fsd(k); for(int i=0;i<n;i++)out[i]=sd[i]; return n; }
int hk_exists(const char* p){ int isd=0; return ux_lookup(p,&isd,1)>=0; }
int hk_pf(u64 va,int wr){ if(!g_js_running||g_jcur<0)return -1; return vm_fault_user(cpu_get_pt(),va,wr); }
u32 hk_frame_refs_total(void){ u32 n=0; for(u32 i=0;i<PF_N;i++) if(pf_bitmap[i>>3]&(1u<<(i&7)))n+=pf_ref[i]; return n; }
int hk_dns_build(const char* host,unsigned char* out,int cap){ return dns_build_query(host,0x1234,out,cap); }
void hk_udp_big(void){ int save=net_ok; net_ok=1; static u8 pl[4000]; u8 mac[6]={0}; udp_send(0x0A000001u,1,2,pl,4000,mac); net_ok=save; }
void xfb_untrack_cr3(u64 cr3){ (void)cr3; }
void xfb_untrack_range(u64 cr3,u64 lo,u64 hi){ (void)cr3; (void)lo; (void)hi; }
void xfb_drop(u64 a){ (void)a; }
void xfb_register(u64* frames,int n){ (void)frames; (void)n; }
void xfb_track(u64 cr3,u64 va,int n){ (void)cr3; (void)va; (void)n; }
int xfb_has_window(void){ return 0; }
int g_xfb_live=0;
int hk_shm_clear(void){ int n=0; for(int i=0;i<64;i++) if(g_lxshm[i].used){ lx_shm_free(i); n++; } return n; }
