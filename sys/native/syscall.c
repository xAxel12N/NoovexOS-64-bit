#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/arch/x86_64/user/ring3.h"
#include "include/nvx/core/proc/elf.h"
#include "include/nvx/core/proc/sched.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/net/stack/tcpip.h"
#include "include/nvx/net/stack/sockets.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/vfs/vfs.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/fs/nvx/nvxfs.h"
#include "include/nvx/fs/fat/fat_ata.h"
#include "include/nvx/drivers/net/wifi/rtw_efuse.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/sys/native/syscall.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/fs/nvx/nvx4.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/shell/commands.h"
#include "include/nvx/apps/internet/browser.h"
#include "include/nvx/apps/system/files_classic.h"
/* ---- Unix-like process spawning: fork+exec+waitpid idiom ---- */

u8 g_spawnbuf[65536];
static int g_last_exit[MAXPROC];   /* exit status by pid slot, kept after reap */

int sys_spawn(const char* path){
    if(!path||!pf_ready) return -1;
    char nm[64]; int k=0; while(path[k]&&path[k]!=' '&&k<63){nm[k]=path[k];k++;} nm[k]=0;
    { int j=0; while(path[j]&&j<2047){g_exec_args[j]=path[j];j++;} g_exec_args[j]=0; }
    int n=fat_read_file(nm,g_spawnbuf,(int)sizeof(g_spawnbuf));
    if(n<64) return -1;
    if(!(g_spawnbuf[0]==0x7F&&g_spawnbuf[1]=='E'&&g_spawnbuf[2]=='L'&&g_spawnbuf[3]=='F')) return -2;
    { int k=fs_find_name(nm); if(k>=0&&k<FS_N&&FS[k].used&&!perm_can_exec(FS[k].mode)) return -4; }   /* EACCES: not executable */
    us_init();
    int pid=proc_create(g_spawnbuf,(u32)n);
    if(pid<0) return -3;
    g_proc[pid].zombie=0; g_proc[pid].exit_status=0;
    /* run cooperatively until it (and any siblings) finish */
    g_r3out_n=0; g_r3out[0]=0;
    sched_run_all();
    int st=g_proc[pid].exit_status;
    g_last_exit[pid]=st;
    /* flush the child's stdout to the terminal */
    if(g_r3out_n>0){ term_puts(g_r3out); g_r3out_n=0; g_r3out[0]=0; }
    return pid;
}
int sys_waitpid(int pid){
    if(pid<0||pid>=MAXPROC) return -1;
    return g_last_exit[pid];   /* status was captured at exit */
}
static int sys_exec(const char* path){
    /* replace-image exec: load ELF and run it in place, return its exit status */
    if(!path||!pf_ready) return -1;
    char nm[64]; int k=0; while(path[k]&&k<63){nm[k]=path[k];k++;} nm[k]=0;
    int n=fat_read_file(nm,g_spawnbuf,(int)sizeof(g_spawnbuf));
    if(n<64||!(g_spawnbuf[0]==0x7F&&g_spawnbuf[1]=='E')) return -1;
    return elf_load_run(g_spawnbuf,(u32)n);
}
/* ---- users: minimal root + one user, with permission enforcement ---- */
const char* cur_user(void){ static char nm[32]; if(ux_uid_name(g_uid,nm,32))return nm; return g_uid==0?"root":"user"; }
/* can the current uid write to a file with this mode? root always; else owner-write bit */
int perm_can_exec(unsigned int mode){ if(g_uid==0)return 1; return (mode&0100)?1:0; }

struct nvx_g_env_t g_env[NENV];
static int g_env_init=0;
static int env_find(const char* nm){ for(int i=0;i<NENV;i++) if(g_env[i].used){ int j=0; while(nm[j]&&g_env[i].name[j]&&nm[j]==g_env[i].name[j])j++; if(!nm[j]&&!g_env[i].name[j])return i; } return -1; }
int env_set(const char* nm,const char* val){ int i=env_find(nm); if(i<0){ for(i=0;i<NENV;i++) if(!g_env[i].used)break; if(i>=NENV)return -1; g_env[i].used=1; int j=0; while(nm[j]&&j<23){g_env[i].name[j]=nm[j];j++;} g_env[i].name[j]=0; } int j=0; while(val[j]&&j<127){g_env[i].val[j]=val[j];j++;} g_env[i].val[j]=0; return 0; }

int env_pack(char* buf,int cap,int* off,int max){
    env_init(); int used=0, n=0;
    for(int i=0;i<NENV&&n<max;i++){ if(!g_env[i].used)continue;
        int L=0; while(g_env[i].name[L])L++; int V=0; while(g_env[i].val[V])V++;
        if(used+L+V+2>cap)break; off[n++]=used;
        for(int k=0;k<L;k++)buf[used++]=g_env[i].name[k]; buf[used++]='=';
        for(int k=0;k<V;k++)buf[used++]=g_env[i].val[k]; buf[used++]=0; }
    return n;
}
void env_init(void){ if(g_env_init)return; g_env_init=1; env_set("PATH","/opt/gnu/bin:/bin:/usr/bin"); env_set("HOME","/home"); env_set("PWD","/home"); env_set("USER","root"); env_set("SHELL","/bin/nsh"); env_set("TERM","xterm-256color"); env_set("XZ_DEFAULTS","--memlimit-compress=20MiB"); env_set("PYTHON_BASIC_REPL","1"); }

               static int env_find(const char*);   

void syscall_dispatch(struct sysregs* r){
    if(g_js_running&&g_jcur>=0&&jc_syscall(r)){ if(jc_sig_pending_cur())jc_yield_sys(r); return; }
    switch(r->rax){
        case 0: if(g_curproc>=0){ g_proc[g_curproc].exit_status=(int)r->rbx; g_proc[g_curproc].zombie=1; }
                if(g_preempt_on){ g_proc[g_curproc].alive=0; g_proc[g_curproc].used=0; int live=0; for(int i=0;i<MAXPROC;i++) if(g_proc[i].used&&g_proc[i].alive)live++; if(live==0){ g_preempt_on=0; cpu_set_pt(g_kern_cr3); k_longjmp(g_preempt_kern,1); } cpu_irq_on(); for(;;)cpu_halt(); } else if(g_sched_active) sched_exit_now(); else k_longjmp(g_kjmp,1); break;   /* exit */
        case 20: if(g_sched_active) sched_yield_now(); break;                            /* yield */
        case 2: r->rax=(u64)do_fork(r); break;                                           /* fork */
        case 5: r->rax=(u64)(long)sys_spawn((const char*)r->rbx); break;                  /* spawn(path)->pid */
        case 6: r->rax=(u64)(long)sys_waitpid((int)r->rbx); break;                        /* waitpid(pid)->status */
        case 4: r->rax=(u64)(long)sys_exec((const char*)r->rbx); break;                   /* exec(path) */
        case 1: { const char* s=(const char*)r->rbx; while(*s && g_r3out_n<255)g_r3out[g_r3out_n++]=*s++; g_r3out[g_r3out_n]=0; } break;  /* write(str) */
        case 3: { if(g_r3out_n<255)g_r3out[g_r3out_n++]=(char)r->rbx; g_r3out[g_r3out_n]=0; } break;                                      /* putc */
        case 7: switch(r->rbx){ case 0:r->rax=W;break; case 1:r->rax=H;break; case 2:r->rax=g_ram_mb;break; case 3:r->rax=(u64)g_ticks;break; case 4:r->rax=(u64)(disk_ok?1:0);break; default:r->rax=0;} break; /* sysinfo */
        case 15: r->rax=(u64)(g_ticks*2654435761u + 12345u); break;                      /* rand */
        /* ---- files (NVXFS FS[]) ---- */
        case 30: { const char* nm=(const char*)r->rbx; int flags=(int)r->rcx; if(flags==1)flags=O_CREAT|O_RDWR;   /* open(name,flags) - legacy create=1 maps to O_CREAT|O_RDWR */
            r->rax=(u64)(long)vfs_open(nm,flags); } break;
        case 31: r->rax=(u64)(long)vfs_read((int)r->rbx,(char*)r->rcx,(int)r->rdx); break;    /* read(fd,buf,len) */
        case 32: { int fd=(int)r->rbx; int n=vfs_write(fd,(const char*)r->rcx,(int)r->rdx);   /* write(fd,buf,len) */
            if(fd>=3&&fd<FDMAX&&g_fd[fd]>=0&&g_vf[g_fd[fd]].dev==0&&g_vf[g_fd[fd]].fs_idx>=0&&disk_ok){ int idx=g_vf[g_fd[fd]].fs_idx; fs_sync(idx); }
            r->rax=(u64)(long)n; } break;
        case 33: r->rax=(u64)(long)vfs_close((int)r->rbx); break;                             /* close(fd) */
        case 34: r->rax=(u64)(long)vfs_fstat((int)r->rbx); break;                             /* fstat(fd)->size */
        case 36: r->rax=(u64)(long)vfs_lseek((int)r->rbx,(int)r->rcx,(int)r->rdx); break;     /* lseek(fd,off,whence) */
        case 37: r->rax=(u64)(long)vfs_dup((int)r->rbx); break;                               /* dup(fd) */
        case 35: { int n=(int)r->rbx; char* nb=(char*)r->rcx; int c=0;    /* readdir: nth used file name */
            for(int k=0;k<FS_N;k++)if(FS[k].used){ if(c==n){ int j=0;for(;FS[k].name[j]&&j<63;j++)nb[j]=FS[k].name[j];nb[j]=0; r->rax=1; goto rd_done; } c++; } r->rax=0; rd_done:; } break;
        /* ---- framebuffer (draws to the compositor SCENE; present blits to screen) ---- */
        case 40: fill_rect(0,0,(int)W,(int)H,(u32)r->rbx); break;                        /* fb_clear(color) */
        case 41: putpx((int)r->rbx,(int)r->rcx,(u32)r->rdx); break;                      /* fb_pixel(x,y,color) */
        case 42: fill_rect((int)r->rbx,(int)r->rcx,(int)r->rdx,(int)r->rsi,(u32)r->rdi); break; /* fb_rect(x,y,w,h,color) */
        case 44: dstr((int)r->rbx,(int)r->rcx,(const char*)r->rdx,(u32)r->rsi,1); break; /* fb_text(x,y,str,color) */
        case 45: blit_all(); break;                                                      /* fb_present */
        /* ---- input ---- */
        case 50: { int k=kpop(); r->rax=(u64)(long)k; } break;                           /* getkey (non-blocking, -1) */
        case 51: { int k; while((k=kpop())<0){ cpu_idle(); } r->rax=(u64)(long)k; } break;  /* waitkey */
        case 52: { int* xp=(int*)r->rbx,*yp=(int*)r->rcx,*bp=(int*)r->rdx; if(xp)*xp=mx; if(yp)*yp=my; if(bp)*bp=mouse_btn; } break; /* getmouse */
        /* ---- memory: sbrk (grow the user heap) ---- */
        case 61: r->rax=(u64)thread_create_k(r->rbx); break;                             /* thread_create(fn) */
        case 80: r->rax=(u64)(long)shm_get_k((int)r->rbx,(int)r->rcx); break;             /* shm_get(key,size)->id */
        case 81: r->rax=shm_map_k((int)r->rbx); break;                                    /* shm_map(id)->vaddr */
        case 82: r->rax=(u64)(long)msg_send_k((int)r->rbx,(const u8*)r->rcx,(int)r->rdx); break;  /* msg_send(q,buf,len) */
        case 83: r->rax=(u64)(long)msg_recv_k((int)r->rbx,(u8*)r->rcx,(int)r->rdx); break;        /* msg_recv(q,buf,cap) */
        /* ---- RUNG 5: privileged compositor screen syscalls (compositor process only) ---- */
        case 90: fill_rect(0,0,(int)W,(int)H,(u32)r->rbx); break;                                  /* screen_clear(color) */
        case 91: { int key=(int)r->rbx,x=(int)r->rcx,y=(int)r->rdx,w=(int)r->rsi,h=(int)r->rdi; int id=-1;   /* blit an app's shm buffer */
            for(int i=0;i<SHM_MAX;i++) if(g_shm[i].used&&g_shm[i].key==key){id=i;break;}
            if(id>=0){ for(int yy=0;yy<h;yy++)for(int xx=0;xx<w;xx++){ u32 idx=(u32)yy*w+xx; int fr=idx>>10,fo=idx&1023; if(fr<g_shm[id].nframes){ u32 px=((u32*)g_shm[id].frames[fr])[fo]; putpx(x+xx,y+yy,px);} } } } break;
        case 92: fill_rect((int)r->rbx,(int)r->rcx,(int)r->rdx,(int)r->rsi,(u32)r->rdi); break;    /* screen_rect */
        case 93: dstr((int)r->rbx,(int)r->rcx,(const char*)r->rdx,(u32)r->rsi,1); break;           /* screen_text */
        case 94: blit_all(); break;                                                               /* screen_present */
        case 95: r->rax=(u64)(long)msg_recv_k(0,(u8*)r->rbx,(int)r->rdx>0?(int)r->rdx:64); break;  /* wm_poll (queue 0) */
        case 60: { long inc=(long)r->rbx; u64 old=g_ubrk; u64 nb=g_ubrk+((inc+0xFFF)&~0xFFFULL);
            u64 cr3; cr3=cpu_get_pt();
            for(u64 v=(old+0xFFF)&~0xFFFULL; v<nb; v+=0x1000){ u64 f=pframe_alloc(); if(!f){r->rax=(u64)-1;goto sb_done;} vm_map(cr3,v,f,0x7ULL); }
            g_ubrk=nb; r->rax=old; sb_done:; } break;                                        /* sbrk(inc) */
        /* ---- POSIX rung 2+3: process memory ---- */
        case 9: { u64 len=r->rcx; u64 pages=(len+0xFFF)>>12; if(pages<1)pages=1; u64 base=g_mmap_next;   /* mmap(addr,len,prot,flags,fd) anonymous */
            u64 cr3; cr3=cpu_get_pt();
            for(u64 i=0;i<pages;i++){ u64 f=pframe_alloc(); if(!f){ r->rax=(u64)-1; goto mm_done; } vm_map(cr3,base+i*0x1000ULL,f,0x7ULL); u32* z=(u32*)f; for(int j=0;j<1024;j++)z[j]=0; }
            g_mmap_next+=pages*0x1000ULL; r->rax=base; mm_done:; } break;
        case 11: r->rax=0; break;                                                            /* munmap (accepted; no reclaim yet) */
        case 12: { u64 addr=r->rbx; if(addr==0){ r->rax=g_ubrk; } else { u64 nb=(addr+0xFFF)&~0xFFFULL; u64 cr3; cr3=cpu_get_pt(); while(g_ubrk<nb){ u64 f=pframe_alloc(); if(!f)break; vm_map(cr3,g_ubrk,f,0x7ULL); g_ubrk+=0x1000ULL; } r->rax=g_ubrk; } } break;  /* brk(addr) */
        case 39: r->rax=(u64)(long)(g_curproc>=0?g_curproc+1:1); break;                       /* getpid */
        case 120: { /* getenv(name)->copies value into caller buf(rcx,cap rdx), rax=len or -1 */
                    const char* nm=(const char*)r->rbx; char* out=(char*)r->rcx; int cap=(int)r->rdx;
                    int v=env_find(nm); if(v<0){ r->rax=(u64)-1; break; }
                    int i=0; const char* val=g_env[v].val; while(val[i]&&i<cap-1){out[i]=val[i];i++;} out[i]=0; r->rax=(u64)(long)i; } break;
        case 121: { /* setenv(name,val) */ r->rax=(u64)(long)env_set((const char*)r->rbx,(const char*)r->rcx); } break;
        case 100: r->rax=(u64)(long)sock_alloc(); break;                                          /* socket() -> sid */
        case 101: { /* connect(sid, ip_be, port) */ r->rax=(u64)(long)sock_connect((int)r->rbx,(u32)r->rcx,(u16)r->rdx); } break;
        case 102: r->rax=(u64)(long)sock_send((int)r->rbx,(const u8*)r->rcx,(int)r->rdx); break;   /* send */
        case 103: r->rax=(u64)(long)sock_recv((int)r->rbx,(u8*)r->rcx,(int)r->rdx); break;         /* recv */
        case 104: r->rax=(u64)(long)sock_close((int)r->rbx); break;                                /* close socket */
        case 110: { /* stat(path, out[3]={mode,size,isdir}) */ const char* p=(const char*)r->rbx; u32* out=(u32*)r->rcx;
                    dt_init(); int dv=dev_id(p);
                    if(dv){ out[0]=0020666u; out[1]=0; out[2]=0; r->rax=0; }         /* char device */
                    else { int dd=dt_resolve_dir(p); if(dd>=0){ out[0]=dt_mode(dd); out[1]=0; out[2]=1; r->rax=0; }
                        else { int k=fs_find_name(p); if(k<0)for(k=0;k<FS_N;k++)if(FS[k].used&&seq(FS[k].name,(char*)p))break;
                            if(k>=0&&k<FS_N&&FS[k].used){ out[0]=(FS[k].mode?FS[k].mode:0100644u); out[1]=(u32)FS[k].len; out[2]=0; r->rax=0; } else r->rax=(u64)-1; } } } break;
        case 105: { /* resolve(hostname)->ip_be, 0 on fail */ u32 ip=0; const char* h=(const char*)r->rbx;
                    if(!parse_ip(h,&ip)){ if(dns_query(h,&ip)<=0)ip=0; } r->rax=(u64)ip; } break;
        case 16: { u64 ms=r->rbx; for(u64 t=0;t<ms;t++)rtw_delay_us(1000); } break;              /* sleep_ms */
        case 17: r->rax=0; break;                                                                /* kill (accepted; single-proc) */
        case 18: { char* b=(char*)r->rbx; int n=(int)r->rcx; dt_init(); char pb[192]; dt_path(g_dcwd,pb); int i=0; for(;pb[i]&&i<n-1;i++)b[i]=pb[i]; b[i]=0; r->rax=(u64)(long)b; } break;   /* getcwd */
        case 19: { const char* p=(const char*)r->rbx; dt_init(); int d=dt_resolve_dir(p); if(d>=0){g_dcwd=d;r->rax=0;}else r->rax=(u64)-1; } break;   /* chdir */
        case 21: { const char* p=(const char*)r->rbx; dt_init(); int nd=dt_mkdir_at(g_dcwd,p); if(nd>=0)dt_setmode(nd,0040000u|(0777u&~(unsigned)g_umask)); r->rax=(u64)(long)((nd>=0)?0:-1); } break;   /* mkdir */
        case 38: r->rax=(u64)(long)ux_unlink((const char*)r->rbx); break;
        case 160: r->rax=(u64)(long)ux_link((const char*)r->rbx,(const char*)r->rcx); break;
        case 161: r->rax=(u64)(long)ux_symlink((const char*)r->rbx,(const char*)r->rcx); break;
        case 162: r->rax=(u64)(long)ux_readlink((const char*)r->rbx,(char*)r->rcx,(int)r->rdx); break;
        case 163: r->rax=(u64)(long)ux_stat2((const char*)r->rbx,(u32*)r->rcx,(int)r->rdx); break;
        case 165: r->rax=(u64)(long)ux_chown((const char*)r->rbx,(int)r->rcx,(int)r->rdx,(int)r->rsi); break;
        case 166: r->rax=(u64)(long)ux_chmod((const char*)r->rbx,(unsigned)r->rcx); break;
        case 167: r->rax=(u64)(long)ux_utime((const char*)r->rbx,(u32)r->rcx,(u32)r->rdx); break;
        case 168: r->rax=(u64)(long)ux_mount((const char*)r->rbx,(const char*)r->rcx); break;
        case 169: r->rax=(u64)(long)ux_umount((const char*)r->rbx); break;
        case 170: { char* b=(char*)r->rbx; int n=(int)r->rcx; int i=0; for(;g_hostname[i]&&i<n-1;i++)b[i]=g_hostname[i]; if(n>0)b[i]=0; r->rax=0; } break;
        case 171: if(g_uid!=0)r->rax=(u64)-1; else { const char* h=(const char*)r->rbx; int i=0; for(;h[i]&&h[i]!='\n'&&i<31;i++)g_hostname[i]=h[i]; g_hostname[i]=0; r->rax=0; } break;                          /* unlink(path) - path aware */
        /* ---- identity, real time, and file management (v8.38) ---- */
        case 130: r->rax=(u64)(long)g_uid; break;                                                  /* getuid */
        case 131: { int nu=(int)r->rbx; if(g_uid==0||nu==g_uid){ g_uid=nu; env_set("USER",nu==0?"root":"user"); r->rax=0; } else r->rax=(u64)-1; } break;   /* setuid */
        case 132: r->rax=(u64)(long)((g_curproc>=0&&g_proc[g_curproc].ppid>=0)?g_proc[g_curproc].ppid+1:1); break;   /* getppid */
        case 133: r->rax=k_realtime_ms(); break;                                                   /* realtime ms since 1970 (RTC) */
        case 134: r->rax=(u64)(long)k_rename((const char*)r->rbx,(const char*)r->rcx); break;      /* rename */
        case 135: r->rax=(u64)(long)k_rmdir((const char*)r->rbx); break;                           /* rmdir */
        case 136: r->rax=(u64)(long)k_truncate((const char*)r->rbx,(long)r->rcx); break;           /* truncate */
        case 137: r->rax=(u64)(long)k_ftruncate((int)r->rbx,(long)r->rcx); break;                  /* ftruncate */
        case 138: r->rax=(u64)(long)k_fsync((int)r->rbx); break;                                   /* fsync */
        case 139: r->rax=(u64)(long)k_readdir_at((const char*)r->rbx,(int)r->rcx,(char*)r->rdx,(int*)r->rsi); break;   /* readdir(path,n,name,type) */
        case 140: r->rax=(u64)g_ticks*10ULL; break;                                                /* monotonic ms since boot */
        /* ---- time ---- */
        case 70: r->rax=(u64)g_ticks; break;                                             /* ticks (100 Hz) */
        case 71: { u64 until=g_ticks+(r->rbx/10); while(g_ticks<until)cpu_idle(); } break;  /* sleep(ms) */
        default: r->rax=(u64)-1; break;
    }
}
static void fs_add(const char*name,const char*content){
    for(int i=0;i<FS_N;i++) if(!FS[i].used){ fs_newslot(i); int n=0; while(name[n]&&n<63){FS[i].name[n]=name[n];n++;} FS[i].name[n]=0;
        int L=0; while(content[L])L++; if(fs_set(i,content,L))return; FS[i].used=1; FS[i].folder=0; FS[i].dir=1; FS[i].mode=0100644u; FS[i].uid=0; FS[i].gid=0; return; } }
int fs_save_bin(const char*name,const u8*data,int len,int folder){   /* create or overwrite; returns slot or -1 */
    int slot=-1; for(int i=0;i<FS_N;i++) if(FS[i].used&&seq(FS[i].name,name)){ slot=i; break; }
    if(slot<0){ for(int i=0;i<FS_N;i++) if(!FS[i].used){ slot=i; break; } if(slot>=0)fs_newslot(slot); }
    if(slot<0) return -1;
    int n=0; while(name[n]&&n<63){FS[slot].name[n]=name[n];n++;} FS[slot].name[n]=0;
    if(len>FS_CAP-1)len=FS_CAP-1; if(fs_set(slot,(const char*)data,len))return -1; FS[slot].used=1; FS[slot].folder=folder; ux_touch(slot,1);
    if(g_cloud_fs && g_nvx_signedin && !g_fs_pulling && !seq(name,"history.html") && !seq(name,"bookmarks.html")) nvx_file_put(name,(const u8*)fsd(slot),len);
    if(disk_ok && !g_fs_pulling && !seq(name,"history.html") && !seq(name,"bookmarks.html")) fs_sync(slot);
    return slot; }
#include "assets/demo_pptx.h"
void fs_init(void){
    fs_save_bin("demo.pptx",demo_pptx,demo_pptx_len,0);
    fs_add("readme.txt","Welcome to NoovexOS 64.\n\nA from-scratch x86-64 operating\nsystem: bootloader, kernel, desktop.\n\nEdit this text and press Save.\n");
    fs_add("hello.nvx","/* NoovexOS program - edit and press Run,\n   or save as .nvx to make an app. */\nint main(){\n    printf(\"Hello from NoovexOS!\\n\");\n    for(int i=1;i<=5;i++){\n        printf(\"%d squared is %d\\n\", i, i*i);\n    }\n    return 0;\n}\n");
    fs_add("notes.md","# Notes\n\n- long mode works\n- nsh shell + neofetch\n- files + editor + calculator\n- procedural wallpapers\n\nType away...\n");
    fs_add("noovex.cfg","theme = cobalt\naccent = blue\ncursor = arrow\nshow_seconds = 1\n");
}

