extern long xzk_decode_into(const u8* in,long n,u8* out,long cap);
extern u64 acpi_hi_map(u64 pa);
extern const u8 _binary_nvfw_gsp_xz_start[], _binary_nvfw_gsp_xz_end[];
extern const u8 _binary_nvfw_bootloader_xz_start[], _binary_nvfw_bootloader_xz_end[];
extern const u8 _binary_nvfw_booter_load_xz_start[], _binary_nvfw_booter_load_xz_end[];
extern const u8 _binary_nvfw_booter_unload_xz_start[], _binary_nvfw_booter_unload_xz_end[];
extern const u8 _binary_nvfw_acr_bl_xz_start[], _binary_nvfw_acr_bl_xz_end[];

typedef struct { u8* d; u32 n; const char* src; } nvblob;
static nvblob g_fw_gsp, g_fw_bl, g_fw_bload, g_fw_bunload, g_fw_acr;

static struct {
    int state; u32 chip;
    u8* elf; u64 elf_size; u8* sig; u8* boot; u32 app_ver;
    u64* r3l0; u64* r3l1; u64* r3l2; u32 r3l2_pages;
    u8* log[3]; u8* rmargs; u8* libos; u8* shm; u8* meta; u8* flush;
    u32 cmd_seq, rpc_seq, rx_seq;
    u32 hclient, hdevice, hsubdev;
    u64 bar1_pde, bar2_pde; u32 gpc_mask, tpcs; char name[64];
    struct { u64 base, limit, reserved; u32 prot; } fbr[16]; u32 nfbr;
    u64 bar1, bar1_len, bar3, bar3_len;
    u32 mthd_size;
    struct { u32 type, runl; } eng[32]; u32 neng;
    u32 err_count, seq_count, ev_count, resumes; char seq_err[96];
} G;

static u8* sa(u64 size,u64 align){ u8* r=(u8*)kmalloc((unsigned long)(size+align)); if(!r)return 0; u8* p=(u8*)(((u64)r+align-1)&~(align-1)); for(u64 i=0;i<size;i++)p[i]=0; return p; }
static u64 PA(const void* p){ u64 a=kva_to_pa((u64)p); return a?a:(u64)p; }
static int contig(const void* p,u64 n){ u64 b=PA(p); for(u64 o=0x1000;o<n;o+=0x1000) if(PA((const u8*)p+o)!=b+o)return 0; return 1; }
static u8* sa_contig(u64 size,u64 align){ for(int t=0;t<6;t++){ u8* p=sa(size,align); if(!p)return 0; if(contig(p,size))return p; } return 0; }
static void cflush(const void* p,u64 n){ for(u64 a=(u64)p&~63ULL;a<(u64)p+n;a+=64)cpu_clflush(a); cpu_mb(); }

static int blob_get(nvblob* b,const char* file,const u8* s,const u8* e,u32 cap){
    if(b->d)return 1;
    int k=fs_find_name(file); if(k>=0&&FS[k].len>0){ b->n=(u32)FS[k].len; b->d=(u8*)kmalloc(b->n+16); if(!b->d)return 0; const u8* q=(const u8*)fsd(k); for(u32 i=0;i<b->n;i++)b->d[i]=q[i]; b->src="from Files"; return 1; }
    b->d=(u8*)kmalloc(cap); if(!b->d)return 0; long n=xzk_decode_into(s,(long)(e-s),b->d,(long)cap); if(n<=0){ kfree(b->d); b->d=0; return 0; } b->n=(u32)n; b->src="built in"; return 1; }
static int fw_load(void){
    if(!blob_get(&g_fw_acr,"bl.bin",_binary_nvfw_acr_bl_xz_start,_binary_nvfw_acr_bl_xz_end,0x10000))return 0;
    if(!blob_get(&g_fw_bload,"booter_load-535.113.01.bin",_binary_nvfw_booter_load_xz_start,_binary_nvfw_booter_load_xz_end,0x20000))return 0;
    if(!blob_get(&g_fw_bunload,"booter_unload-535.113.01.bin",_binary_nvfw_booter_unload_xz_start,_binary_nvfw_booter_unload_xz_end,0x20000))return 0;
    if(!blob_get(&g_fw_bl,"bootloader-535.113.01.bin",_binary_nvfw_bootloader_xz_start,_binary_nvfw_bootloader_xz_end,0x10000))return 0;
    if(!blob_get(&g_fw_gsp,"gsp-535.113.01.bin",_binary_nvfw_gsp_xz_start,_binary_nvfw_gsp_xz_end,24u<<20))return 0;
    return 1; }

static int str_eq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }
static int elf_sect(const u8* f,u32 n,const char* name,const u8** out,u64* sz){
    if(n<64||f[0]!=0x7F||f[1]!='E'||f[4]!=2)return 0;
    u64 sho=*(const u64*)(f+0x28); u16 shn=*(const u16*)(f+0x3C), shs=*(const u16*)(f+0x3E);
    if(sho+(u64)shn*64>n||shs>=shn)return 0;
    u64 sto=*(const u64*)(f+sho+(u64)shs*64+0x18), stz=*(const u64*)(f+sho+(u64)shs*64+0x20); if(sto>=n||stz>n-sto||!stz||f[sto+stz-1])return 0;
    const u8* str=f+sto;
    for(u16 i=0;i<shn;i++){ const u8* s=f+sho+(u64)i*64; if(*(const u32*)s>=stz)continue; if(str_eq((const char*)str+*(const u32*)s,name)){ u64 o=*(const u64*)(s+0x18), z=*(const u64*)(s+0x20); if(o+z>n)return 0; *out=f+o; *sz=z; return 1; } }
    return 0; }

#define CMDQ 0x1000u
#define MSGQ 0x41000u
#define QSZ  0x40000u
#define QCNT 63u
static volatile u32* q32(u32 off){ return (volatile u32*)(G.shm+off); }
static u32 g_rpc_buf[0x4000];

static void msgq_drain_events(void);
static int cmdq_push(u32 fn,const void* pl,u32 plen,int noseq){
    u32 rlen=0x20+plen; u32 len=(0x30+rlen+0xFFFu)&~0xFFFu; if(len>16*0x1000u)return 0;
    u8* m=(u8*)g_rpc_buf; for(u32 i=0;i<len;i++)m[i]=0;
    *(u32*)(m+0x24)=G.cmd_seq; *(u32*)(m+0x28)=len/0x1000u;
    *(u32*)(m+0x30)=0x03000000u; *(u32*)(m+0x34)=0x43505256u; *(u32*)(m+0x38)=rlen; *(u32*)(m+0x3C)=fn; *(u32*)(m+0x40)=0xFFFFFFFFu; *(u32*)(m+0x44)=0xFFFFFFFFu; *(u32*)(m+0x48)=noseq?0:G.rpc_seq;
    for(u32 i=0;i<plen;i++)m[0x50+i]=((const u8*)pl)[i];
    u64 cs=0; for(u32 i=0;i<len;i+=8)cs^=*(u64*)(m+i); *(u32*)(m+0x20)=(u32)(cs>>32)^(u32)cs;
    u32 w=*q32(CMDQ+0x10), off=0; u64 t0=hr_ns();
    while(off<len){ u32 rp=*q32(MSGQ+0x20); u32 fr=rp+QCNT-w-1; if(fr>=QCNT)fr-=QCNT;
        if(fr<1){ if(hr_ns()-t0>2000000000ULL)return 0; msgq_drain_events(); cpu_pause(); continue; }
        u32 step=QCNT-w; if(step>fr)step=fr; u32 sz=len-off; if(sz>step*0x1000u)sz=step*0x1000u;
        u8* dst=G.shm+CMDQ+0x1000u+w*0x1000u; for(u32 i=0;i<sz;i++)dst[i]=m[off+i];
        w+=(sz+0xFFFu)/0x1000u; if(w>=QCNT)w-=QCNT; off+=sz; }
    cpu_mb(); *q32(CMDQ+0x10)=w; cpu_mb(); nvw32(0x110C00,0); G.cmd_seq++; if(!noseq)G.rpc_seq++; return 1; }

static void seq_run(const u32* p,u32 plen);
static int msg_one(u32* fn,u8* out,u32 cap,u32* olen,u32 ms){
    u64 t0=hr_ns(); u32 rp=*q32(CMDQ+0x20);
    for(;;){ u32 w=*q32(MSGQ+0x10); u32 used=w+QCNT-rp; if(used>=QCNT)used-=QCNT; if(used>=1)break; if(hr_ns()-t0>(u64)ms*1000000ULL)return 0; cpu_pause(); }
    u8* e=G.shm+MSGQ+0x1000u+rp*0x1000u; u32 rlen=*(u32*)(e+0x38); u32 total=0x30+rlen;
    { u32 w=*q32(MSGQ+0x10); u32 used=w+QCNT-rp; if(used>=QCNT)used-=QCNT; u32 sl=(total+0xFFFu)/0x1000u;
      if(rlen<0x20||total>16*0x1000u||sl>used){ u32 ec=*(u32*)(e+0x28); if(ec<1||ec>16||ec>used)ec=1; nvl_kv("bad status-queue element skipped, length",rlen); rp+=ec; while(rp>=QCNT)rp-=QCNT; cpu_mb(); *q32(CMDQ+0x20)=rp; G.rx_seq++; *fn=0; return -1; } }
    u32 slots=(total+0xFFFu)/0x1000u; static u32 g_rx_buf[0x4000]; u8* m=(u8*)g_rx_buf; u32 left=total, r=rp, o=0;
    while(left){ u32 c=left>0x1000u?0x1000u:left; u8* s=G.shm+MSGQ+0x1000u+r*0x1000u; for(u32 i=0;i<c;i++)m[o+i]=s[i]; o+=c; left-=c; r++; if(r>=QCNT)r=0; }
    u32 seq=*(u32*)(m+0x24); if(seq!=G.rx_seq){ nvl_s("  status queue sequence "); nvl_d(seq); nvl_s(" expected "); nvl_d(G.rx_seq); nvl_nl(); } G.rx_seq=seq+1;
    rp+=slots; while(rp>=QCNT)rp-=QCNT; cpu_mb(); *q32(CMDQ+0x20)=rp;
    *fn=*(u32*)(m+0x3C); u32 res=*(u32*)(m+0x40); u32 pl=rlen-0x20; if(pl>cap)pl=cap; for(u32 i=0;i<pl;i++)out[i]=m[0x50+i]; *olen=pl;
    if(res){ nvl_s("  reply to function "); nvl_d(*fn); nvl_s(" has rpc_result "); nvl_x(res); nvl_nl(); return -2; }
    return 1; }
static u8 g_evbuf[0x10000];
static int ev_handle(u32 fn,u8* p,u32 n);
static void msgq_drain_events(void){ static u8 b[0x1000]; for(int k=0;k<8;k++){ u32 fn=0,n=0; int r=msg_one(&fn,b,sizeof(b),&n,0); if(r==0)break; if(r>0&&fn>=0x1000)ev_handle(fn,b,n); } }
static int ev_handle(u32 fn,u8* p,u32 n){
    if(fn==0x1002){ G.seq_count++; seq_run((const u32*)p,n); return 1; }
    if((fn==0x1006||fn==0x1004)&&G.err_count>=16){ G.err_count++; return 1; }
    if(fn==0x1006&&n>=12){ G.err_count++; nvl_s("  GSP error log: Xid "); nvl_d(*(u32*)p); nvl_s(" chid "); nvl_d(*(u32*)(p+8)); nvl_s(": "); char t[100]; u32 i=0; for(;i<99&&i+12<n&&p[12+i];i++)t[i]=(char)p[12+i]; t[i]=0; nvl_s(t); nvl_nl(); return 1; }
    if(fn==0x1004&&n>=12){ G.err_count++; nvl_s("  channel error (RC): engine "); nvl_d(*(u32*)p); nvl_s(" chid "); nvl_d(*(u32*)(p+4)); nvl_s(" type "); nvl_d(*(u32*)(p+8)); nvl_nl(); return 1; }
    if(fn==0x1005){ G.err_count++; nvl_s("  GPU MMU fault reported"); nvl_nl(); return 1; }
    G.ev_count++; return 1; }
static int rpc_wait(u32 want,u8* out,u32 cap,u32* olen,u32 ms){
    u64 t0=hr_ns();
    for(;;){ u32 fn=0, n=0; u64 el=(hr_ns()-t0)/1000000ULL; if(el>ms)return 0; int r=msg_one(&fn,g_evbuf,sizeof(g_evbuf),&n,(u32)(ms-el));
        if(r==0)return 0; if(r<0&&fn==want)return -1; if(r<0)continue;
        if(fn==want){ u32 c=n<cap?n:cap; for(u32 i=0;i<c;i++)out[i]=g_evbuf[i]; if(olen)*olen=n; return 1; }
        ev_handle(fn,g_evbuf,n); } }

static u32 g_gsp_boots=0;
static void seq_fail(const char* why){ char* q=G.seq_err; q=scopy(q,why); *q=0; nvl_s("  sequencer: "); nvl_s(why); nvl_nl(); }
static int seq_resume(void){
    nvl_s("  sequencer: CORE_RESUME - GSP reset, LibOS arguments, SEC2 restarted to bring GSP-RM back"); nvl_nl();
    nvm32(NVF_GSP+0x3C0,1u,1u); hr_delay_us(10); nvm32(NVF_GSP+0x3C0,1u,0u);
    if(!nv_poll(NVF_GSP+0x10C,6u,0u,10)){ nvf_dump(NVF_GSP,"GSP falcon"); seq_fail("CORE_RESUME: GSP falcon memory scrubbing did not finish"); return 0; }
    nvw32(NVF_GSP+0x040,(u32)PA(G.libos)); nvw32(NVF_GSP+0x044,(u32)(PA(G.libos)>>32));
    if(nvr32(NVF_SEC2+0x100)&0x40u)nvw32(NVF_SEC2+0x130,2u); else nvw32(NVF_SEC2+0x100,2u);
    int done=nv_poll(0x1180F8,0x04000000u,0x04000000u,2000);
    u32 m0=nvr32(NVF_SEC2+0x040);
    nvl_kv("resume: 0x1180f8",nvr32(0x1180F8)); nvl_kv("resume: SEC2 mbox0",m0);
    if(!done){ nvf_dump(NVF_SEC2,"SEC2"); seq_fail("CORE_RESUME: the booter-done bit (0x1180f8 bit 26) did not come within 2 s"); return 0; }
    if(m0){ nvf_dump(NVF_SEC2,"SEC2"); seq_fail("CORE_RESUME: SEC2 returned an error code in mailbox 0"); return 0; }
    nvw32(0x110080,G.app_ver);
    nvl_kv("resume: GSP RISC-V status (0x111240)",nvr32(0x111240));
    if(!(nvr32(0x111240)&1u)){ nvf_dump(NVF_GSP,"GSP falcon"); seq_fail("CORE_RESUME: the GSP RISC-V core is not running after SEC2 restarted it"); return 0; }
    G.resumes++; nvl_s("  sequencer: GSP-RM resumed"); nvl_nl(); return 1; }
static void seq_run(const u32* p,u32 n){
    if(n<40)return; u32 cnt=p[1]; const u32* c=p+10; u32 i=0; u32 save[8]; for(int k=0;k<8;k++)save[k]=p[2+k];
    while(i<cnt&&(i+1)*4+40<=n){ u32 op=c[i];
        if(op==0){ nvw32(c[i+1],c[i+2]); i+=3; }
        else if(op==1){ nvm32(c[i+1],c[i+2],c[i+3]); i+=4; }
        else if(op==2){ u32 to=c[i+4]?c[i+4]:4000000u; u64 t0=hr_ns(); while((nvr32(c[i+1])&c[i+2])!=c[i+3]){ if(hr_ns()-t0>(u64)to*1000ULL){ nvl_kv("sequencer poll timed out on",c[i+1]); break; } cpu_pause(); } i+=6; }
        else if(op==3){ hr_delay_us(c[i+1]); i+=2; }
        else if(op==4){ if(c[i+2]<8)save[c[i+2]]=nvr32(c[i+1]); i+=3; }
        else if(op==5){ nvf_reset(NVF_GSP); nvm32(0x110624,0x80u,0x80u); nvw32(0x11010C,0); i+=1; }
        else if(op==6){ if(nvr32(0x110100)&0x40u)nvw32(0x110130,2); else nvw32(0x110100,2); i+=1; }
        else if(op==7){ if(!nv_poll(0x110100,0x10u,0x10u,2000)){ seq_fail("CORE_WAIT_FOR_HALT: the GSP did not halt within 2 s"); break; } i+=1; }
        else if(op==8){ i+=1; if(!seq_resume())break; }
        else { nvl_kv("unknown sequencer opcode",op); seq_fail("unknown sequencer opcode"); break; } }
    (void)save; }

static int g_rpc_dead=0;
static void rpc_lost(u32 fn){ if(g_rpc_dead)return; g_rpc_dead=1; nvl_s("  GSP-RM did not answer function "); nvl_d(fn); nvl_s(" within 5 s - no more requests are sent until the card is restarted (a late reply would be mistaken for the next one)"); nvl_nl(); klog("nvgsp: GSP-RM stopped answering - NVIDIA drawing off until restart"); if(G.state>=7)nvgsp_all_off("GSP-RM stopped answering requests"); }
static int rm_alloc(u32 hc,u32 hp,u32 ho,u32 cls,const void* prm,u32 psz){
    if(g_rpc_dead)return -1003; static u8 b[0x1000]; for(u32 i=0;i<0x20+psz;i++)b[i]=0;
    *(u32*)(b+0)=hc; *(u32*)(b+4)=hp; *(u32*)(b+8)=ho; *(u32*)(b+12)=cls; *(u32*)(b+20)=psz; for(u32 i=0;i<psz;i++)b[0x20+i]=((const u8*)prm)[i];
    if(!cmdq_push(103,b,0x20+psz,0))return -1000;
    static u8 r[0x1000]; u32 n=0; int w=rpc_wait(103,r,sizeof(r),&n,5000); if(w==0)rpc_lost(103); if(w<=0)return -1001;
    u32 st=*(u32*)(r+16); if(st){ nvl_s("  RM_ALLOC class "); nvl_x(cls); nvl_s(" handle "); nvl_x(ho); nvl_s(" failed, NV_STATUS "); nvl_x(st); nvl_nl(); return -(int)st; }
    return 0; }
static int rm_ctrl(u32 hc,u32 ho,u32 cmd,void* prm,u32 psz){
    if(g_rpc_dead)return -1003; static u8 b[0x4000]; if(psz>0x3F00)return -1002; for(u32 i=0;i<0x18+psz;i++)b[i]=0;
    *(u32*)(b+0)=hc; *(u32*)(b+4)=ho; *(u32*)(b+8)=cmd; *(u32*)(b+16)=psz; for(u32 i=0;i<psz;i++)b[0x18+i]=((const u8*)prm)[i];
    if(!cmdq_push(76,b,0x18+psz,0))return -1000;
    u32 n=0; int w=rpc_wait(76,b,sizeof(b),&n,5000); if(w==0)rpc_lost(76); if(w<=0)return -1001;
    u32 st=*(u32*)(b+12); if(st){ nvl_s("  RM_CONTROL "); nvl_x(cmd); nvl_s(" failed, NV_STATUS "); nvl_x(st); nvl_nl(); return -(int)st; }
    for(u32 i=0;i<psz&&0x18+i<n;i++)((u8*)prm)[i]=b[0x18+i]; return 0; }
static int rm_free(u32 hc,u32 ho){ if(g_rpc_dead)return -1003; u32 b[4]={hc,0,ho,0}; if(!cmdq_push(10,b,16,0))return -1000; u32 n=0; u8 r[16]; int w=rpc_wait(10,r,sizeof(r),&n,5000); if(w==0)rpc_lost(10); return w>0?0:-1001; }

static int build_sysinfo(u8* s){
    for(int i=0;i<664;i++)s[i]=0;
    *(u64*)(s+0x000)=g_nv_bar0; *(u64*)(s+0x008)=G.bar1; *(u64*)(s+0x010)=G.bar3;
    *(u64*)(s+0x018)=((u64)g_nv_bus<<8)|((u64)g_nv_slot<<3)|(u64)g_nv_fn; *(u64*)(s+0x038)=0x00007FFFFFFFF000ULL;
    *(u32*)(s+0x040)=0x088000u; *(u32*)(s+0x044)=0x1000u; return 664; }
static int build_registry(u8* r){
    static const char* nm[3]={"RMSecBusResetEnable","RMForcePcieConfigSave","RMDevidCheckIgnore"};
    for(int i=0;i<128;i++)r[i]=0; u32 off=8+3*16;
    for(int k=0;k<3;k++){ u8* e=r+8+k*16; *(u32*)e=off; e[4]=1; *(u32*)(e+8)=1; *(u32*)(e+12)=4; const char* s=nm[k]; while(*s)r[off++]=(u8)*s++; r[off++]=0; }
    *(u32*)r=off; *(u32*)(r+4)=3; return (int)off; }

static void put_libos(u8* a,u64 id,u64 pa,u64 sz){ *(u64*)(a+0)=id; *(u64*)(a+8)=pa; *(u64*)(a+16)=sz; a[24]=1; a[25]=1; }

static int prepare(void){
    nvl_s("[3] firmware"); nvl_nl();
    if(!fw_load())return nv_fail(3,"could not unpack the NVIDIA firmware");
    nvl_s("  gsp "); nvl_d(g_fw_gsp.n); nvl_s(" bytes ("); nvl_s(g_fw_gsp.src); nvl_s("), booter_load "); nvl_d(g_fw_bload.n); nvl_s(" ("); nvl_s(g_fw_bload.src); nvl_s("), bootloader "); nvl_d(g_fw_bl.n); nvl_s(", acr bl "); nvl_d(g_fw_acr.n); nvl_nl();
    const u8* img=0; u64 isz=0; const u8* sg=0; u64 ssz=0;
    if(!elf_sect(g_fw_gsp.d,g_fw_gsp.n,".fwimage",&img,&isz))return nv_fail(3,"no .fwimage section in the GSP firmware");
    const char* sname=(G.chip==0x167||G.chip==0x168)?".fwsignature_tu11x":".fwsignature_tu10x";
    if(!elf_sect(g_fw_gsp.d,g_fw_gsp.n,sname,&sg,&ssz))return nv_fail(3,"signature section for this chip is missing");
    nvl_kv(".fwimage size",isz); nvl_s("  signature section "); nvl_s(sname); nvl_nl();
    G.elf_size=isz; u64 pages=(isz+0xFFFULL)>>12;
    { static u8* keep=0; static u64 cap=0; if(!keep||cap<(pages<<12)){ keep=sa(pages<<12,0x1000); cap=keep?(pages<<12):0; } G.elf=keep; } if(!G.elf)return nv_fail(3,"no memory for the GSP image (24 MB)");
    for(u64 i=0;i<isz;i++)G.elf[i]=img[i]; for(u64 i=isz;i<(pages<<12);i++)G.elf[i]=0;
    G.r3l2_pages=(u32)(((pages*8)+0xFFFULL)>>12); { static u64* k2=0; static u32 c2=0; static u64* k1=0; static u64* k0=0; if(!k2||c2<G.r3l2_pages){ k2=(u64*)sa((u64)G.r3l2_pages<<12,0x1000); c2=k2?G.r3l2_pages:0; } if(!k1)k1=(u64*)sa(0x1000,0x1000); if(!k0)k0=(u64*)sa(0x1000,0x1000); G.r3l2=k2; G.r3l1=k1; G.r3l0=k0; if(k2)for(u64 i=0;i<((u64)G.r3l2_pages<<9);i++)k2[i]=0; if(k1)for(int i=0;i<512;i++){ k1[i]=0; } if(k0)for(int i=0;i<512;i++)k0[i]=0; }
    if(!G.r3l2||!G.r3l1||!G.r3l0)return nv_fail(3,"no memory for the radix3 tables");
    for(u64 j=0;j<pages;j++)G.r3l2[j]=PA(G.elf+(j<<12));
    for(u32 j=0;j<G.r3l2_pages;j++)G.r3l1[j]=PA((u8*)G.r3l2+((u64)j<<12));
    G.r3l0[0]=PA(G.r3l1);
    G.sig=sa(0x1000,0x1000); if(!G.sig)return nv_fail(3,"no memory"); for(u64 i=0;i<ssz&&i<0x1000;i++)G.sig[i]=sg[i];
    const u8* bf=g_fw_bl.d; if(g_fw_bl.n<0x80||*(const u32*)bf!=0x10DEu)return nv_fail(3,"GSP bootloader file damaged");
    u32 bho=*(const u32*)(bf+0x0C), bdo=*(const u32*)(bf+0x10), bds=*(const u32*)(bf+0x14);
    G.app_ver=*(const u32*)(bf+bho+0x1C); u32 mco=*(const u32*)(bf+bho+0x30), mdo=*(const u32*)(bf+bho+0x28), mfo=*(const u32*)(bf+bho+0x20);
    G.boot=sa(0x1000,0x1000); if(!G.boot||bds>0x1000||bdo+bds>g_fw_bl.n)return nv_fail(3,"GSP bootloader payload too large"); for(u32 i=0;i<bds;i++)G.boot[i]=bf[bdo+i];
    nvl_kv("bootloader payload",bds); nvl_kv("bootloader appVersion",G.app_ver);
    for(int k=0;k<3;k++){ G.log[k]=sa_contig(0x10000,0x1000); if(!G.log[k])return nv_fail(3,"no physically contiguous memory for the GSP log buffers"); for(int p=0;p<16;p++)*(u64*)(G.log[k]+8+p*8)=PA(G.log[k]+((u64)p<<12)); }
    { static u8 *ks=0,*kr=0,*kl=0,*km=0,*kf=0; if(!ks)ks=sa(0x81000,0x1000); if(!kr)kr=sa(0x1000,0x1000); if(!kl)kl=sa(0x1000,0x1000); if(!km)km=sa(0x1000,0x1000); if(!kf)kf=sa(0x1000,0x1000); G.shm=ks; G.rmargs=kr; G.libos=kl; G.meta=km; G.flush=kf; }
    if(!G.shm||!G.rmargs||!G.libos||!G.meta||!G.flush)return nv_fail(3,"no memory for GSP shared buffers");
    for(u64 i=0;i<0x81000;i++)G.shm[i]=0; for(u64 i=0;i<0x1000;i++){ G.rmargs[i]=0; G.libos[i]=0; G.meta[i]=0; G.flush[i]=0; }
    for(int i=0;i<129;i++)*(u64*)(G.shm+i*8)=PA(G.shm+((u64)i<<12));
    *q32(CMDQ+0x00)=0; *q32(CMDQ+0x04)=QSZ; *q32(CMDQ+0x08)=0x1000; *q32(CMDQ+0x0C)=QCNT; *q32(CMDQ+0x10)=0; *q32(CMDQ+0x14)=1; *q32(CMDQ+0x18)=32; *q32(CMDQ+0x1C)=0x1000;
    *(u64*)(G.rmargs+0x00)=PA(G.shm); *(u32*)(G.rmargs+0x08)=129; *(u64*)(G.rmargs+0x10)=CMDQ; *(u64*)(G.rmargs+0x18)=MSGQ;
    put_libos(G.libos+0,0x4C4F47494E4954ULL,PA(G.log[0]),0x10000); put_libos(G.libos+32,0x4C4F47494E5452ULL,PA(G.log[1]),0x10000);
    put_libos(G.libos+64,0x4C4F47524DULL,PA(G.log[2]),0x10000); put_libos(G.libos+96,0x524D41524753ULL,PA(G.rmargs),0x1000);
    G.cmd_seq=0; G.rpc_seq=0; G.rx_seq=0;
    static u8 tmp[1024]; int n=build_sysinfo(tmp); if(!cmdq_push(72,tmp,(u32)n,1))return nv_fail(3,"could not queue SET_SYSTEM_INFO");
    n=build_registry(tmp); if(!cmdq_push(73,tmp,(u32)n,1))return nv_fail(3,"could not queue SET_REGISTRY");
    if(!nv_fb_layout(G.elf_size,0x1000))return nv_fail(2,"VRAM size register looks wrong");
    u8* m=G.meta; nvfb_layout* L=&g_nvl;
    *(u64*)(m+0x00)=0xDC3AAE21371A60B3ULL; *(u64*)(m+0x08)=1; *(u64*)(m+0x10)=PA(G.r3l0); *(u64*)(m+0x18)=G.elf_size;
    *(u64*)(m+0x20)=PA(G.boot); *(u64*)(m+0x28)=0x1000; *(u64*)(m+0x30)=mco; *(u64*)(m+0x38)=mdo; *(u64*)(m+0x40)=mfo;
    *(u64*)(m+0x48)=PA(G.sig); *(u64*)(m+0x50)=0x1000; *(u64*)(m+0x58)=L->nonwpr_addr; *(u64*)(m+0x60)=L->nonwpr_addr; *(u64*)(m+0x68)=L->nonwpr_size;
    *(u64*)(m+0x70)=L->wpr2_addr; *(u64*)(m+0x78)=L->heap_addr; *(u64*)(m+0x80)=L->heap_size; *(u64*)(m+0x88)=L->elf_addr; *(u64*)(m+0x90)=L->boot_addr;
    *(u64*)(m+0x98)=L->frts_addr; *(u64*)(m+0xA0)=L->frts_size; *(u64*)(m+0xA8)=L->vga_addr&~0x1FFFFULL; *(u64*)(m+0xB0)=L->fb_size;
    *(u64*)(m+0xB8)=L->vga_addr; *(u64*)(m+0xC0)=L->vga_size;
    cflush(G.elf,pages<<12); cflush(G.r3l2,(u64)G.r3l2_pages<<12); cflush(G.r3l1,0x1000); cflush(G.r3l0,0x1000); cflush(G.sig,0x1000); cflush(G.boot,0x1000);
    cflush(G.shm,0x81000); cflush(G.rmargs,0x1000); cflush(G.libos,0x1000); cflush(G.meta,0x1000);
    nvl_kv("radix3 level 0",PA(G.r3l0)); nvl_kv("GspFwWprMeta",PA(G.meta)); nvl_kv("LibOS arguments",PA(G.libos)); nvl_kv("message queues",PA(G.shm));
    return 1; }

static void print_layout(void){
    nvfb_layout* L=&g_nvl;
    nvl_kv("VRAM size",L->fb_size); nvl_kv("VGA workspace",L->vga_addr); nvl_kv("FRTS",L->frts_addr); nvl_kv("boot binary",L->boot_addr); nvl_kv("GSP image",L->elf_addr);
    nvl_kv("WPR heap",L->heap_addr); nvl_kv("WPR heap size",L->heap_size); nvl_kv("WPR2 start",L->wpr2_addr); nvl_kv("WPR2 size",L->wpr2_size); nvl_kv("non-WPR heap (reserved from here up)",L->nonwpr_addr); }

static int probe(void){
    nvl_s("[1] PCI and chip"); nvl_nl();
    if(!nv_probe())return nv_fail(1,"no NVIDIA display controller on PCI");
    nv_read_id(); G.chip=(g_nv_boot0>>20)&0x1FFu;
    u32 b1=pci_cfg(g_nv_bus,g_nv_slot,g_nv_fn,0x14); G.bar1=(u64)(b1&0xFFFFFFF0u); int bi=2; if((b1&6u)==4u){ G.bar1|=(u64)pci_cfg(g_nv_bus,g_nv_slot,g_nv_fn,0x18)<<32; bi=3; }
    u32 b3=pci_cfg(g_nv_bus,g_nv_slot,g_nv_fn,0x10+bi*4); G.bar3=(u64)(b3&0xFFFFFFF0u); if((b3&6u)==4u)G.bar3|=(u64)pci_cfg(g_nv_bus,g_nv_slot,g_nv_fn,0x14+bi*4)<<32;
    for(int w=0;w<2;w++){ int r=w?bi:1; u32 o=0x10+(u32)r*4; u32 lo=pci_cfg(g_nv_bus,g_nv_slot,g_nv_fn,(int)o); int b64=(lo&6u)==4u; u32 hi=b64?pci_cfg(g_nv_bus,g_nv_slot,g_nv_fn,(int)o+4):0;
        u32 c=pci_cfg(g_nv_bus,g_nv_slot,g_nv_fn,0x04); pci_cfgw(g_nv_bus,g_nv_slot,g_nv_fn,0x04,c&~2u);
        pci_cfgw(g_nv_bus,g_nv_slot,g_nv_fn,(int)o,0xFFFFFFFFu); if(b64)pci_cfgw(g_nv_bus,g_nv_slot,g_nv_fn,(int)o+4,0xFFFFFFFFu);
        u64 m=(u64)(pci_cfg(g_nv_bus,g_nv_slot,g_nv_fn,(int)o)&0xFFFFFFF0u)|(b64?((u64)pci_cfg(g_nv_bus,g_nv_slot,g_nv_fn,(int)o+4)<<32):0xFFFFFFFF00000000ULL);
        pci_cfgw(g_nv_bus,g_nv_slot,g_nv_fn,(int)o,lo); if(b64)pci_cfgw(g_nv_bus,g_nv_slot,g_nv_fn,(int)o+4,hi); pci_cfgw(g_nv_bus,g_nv_slot,g_nv_fn,0x04,(c&0xFFFFu)|0x406u);
        u64 sz=m?(~m)+1:0; if(w)G.bar3_len=sz; else G.bar1_len=sz; }
    { u32 c=pci_cfg(g_nv_bus,g_nv_slot,g_nv_fn,0x04); pci_cfgw(g_nv_bus,g_nv_slot,g_nv_fn,0x04,(c&0xFFFFu)|0x406u); }
    nvl_kv("PCI device",g_nv_dev); nvl_kv("BOOT0",g_nv_boot0); nvl_kv("chipset",G.chip); nvl_kv("BAR0",g_nv_bar0); nvl_kv("BAR1 (VRAM aperture)",G.bar1); nvl_kv("BAR1 size",G.bar1_len); nvl_kv("BAR3 (instance aperture)",G.bar3); nvl_kv("BAR3 size",G.bar3_len);
    if(G.chip<0x162||G.chip>0x168)return nv_fail(1,"this driver supports Turing (TU102-TU117) only");
    if(G.chip==0x167){ nvl_s("  TU117 boots with the TU116 booter (linux-firmware links tu117/gsp to tu116/gsp)"); nvl_nl(); }
    if(G.chip<0x167){ nvl_s("  TU102/TU104/TU106 need nvidia/tu102/gsp/booter_load-535.113.01.bin in Files (the built-in booter is TU116)"); nvl_nl(); }
    nvl_s("[2] VRAM"); nvl_nl();
    if(!nv_gfw_wait())return nv_fail(2,"the VBIOS did not finish initialising the GPU (GFW_BOOT)");
    nvl_kv("0x100ce0 (memory size register)",nvr32(0x100CE0)); nvl_kv("0x625f04 (VGA workspace)",nvr32(0x625F04));
    u64 fb=nv_vram_size(); nvl_kv("VRAM bytes",fb); nvl_s("  = "); nvl_d(fb>>20); nvl_s(" MB"); nvl_nl();
    if(!fb)return nv_fail(2,"VRAM size reads as 0");
    return 1; }

static u64 g_con_end=0;
static void vram_alloc_init(void){
    u64 lo=0, hi=g_nvl.nonwpr_addr; u64 fbpa=*(volatile u64*)(NV_BOOTINFO); u32 pitch=*(volatile u32*)(NV_BOOTINFO+8); u32 h=*(volatile u16*)(NV_BOOTINFO+14);
    g_con_end=0; if(G.bar1&&fbpa>=G.bar1&&fbpa<G.bar1+G.bar1_len){ g_con_end=((fbpa-G.bar1)+(u64)pitch*h+0xFFFFFULL)&~0xFFFFFULL; }
    if(G.nfbr){ hi=0; for(u32 i=0;i<G.nfbr;i++) if(!G.fbr[i].reserved&&!G.fbr[i].prot&&G.fbr[i].limit+1>hi){ lo=G.fbr[i].base; hi=G.fbr[i].limit+1; } }
    nvv_init(lo,hi); nvv_reserve(0,g_con_end+0x100000ULL);
    nvl_kv("VRAM allocator from",lo); nvl_kv("VRAM allocator to",hi); nvl_kv("console framebuffer reserved below",g_con_end+0x100000ULL); nvl_kv("VRAM free for the driver",nvv_free_bytes()); }

static int gsp_pre(void){
    int r=prepare(); if(r<=0)return r; print_layout();
    nvw32(0x100C10,(u32)(PA(G.flush)>>8)); nvl_kv("sysmem flush page",PA(G.flush));
    nvl_s("[4] FWSEC-FRTS on the GSP falcon"); nvl_nl();
    if(!nvb_read_rom())return nv_fail(4,"could not read the VBIOS ROM");
    if(nvr32(0x1FA828)){ nvl_s("  WPR2 is still set from an earlier start (the last system did not unload the GPU) - clearing it first: GSP reset, FWSEC-SB, booter_unload"); nvl_nl();
        nvf_reset(NVF_GSP); r=nv_run_fwsec(g_fw_acr.d,g_fw_acr.n,0x19); if(r<=0)return r;
        if(nvr32(0x1FA828)){ r=nv_booter(g_fw_bunload.d,g_fw_bunload.n,0,1); if(r<=0)return r; }
        if(nvr32(0x1FA828))return nv_fail(4,"WPR2 from an earlier start could not be cleared - turn the computer fully off and on again");
        nvl_s("  WPR2 cleared"); nvl_nl(); }
    r=nv_run_fwsec(g_fw_acr.d,g_fw_acr.n,0x15); if(r<=0)return r;
    nvl_s("  WPR2 is set up"); nvl_nl();
    nvm32(0x1103C0,1u,1u); hr_delay_us(10); nvm32(0x1103C0,1u,0u); nv_poll(0x11010C,6u,0u,10);
    nvw32(0x110040,(u32)PA(G.libos)); nvw32(0x110044,(u32)(PA(G.libos)>>32));
    nvl_s("[5] booter_load on SEC2"); nvl_nl();
    r=nv_booter(g_fw_bload.d,g_fw_bload.n,PA(G.meta),0); if(r<=0)return r;
    cflush(G.meta,256); nvl_kv("GspFwWprMeta.verified",*(volatile u64*)(G.meta+0xF8));
    G.state=1; return 1; }
static int gsp_post(void){
    nvl_s("[6] starting GSP-RM"); nvl_nl();
    nvw32(0x110080,G.app_ver);
    nvl_kv("GSP RISC-V status (0x111240)",nvr32(0x111240));
    if(!(nvr32(0x111240)&1u)){ nvf_dump(NVF_GSP,"GSP falcon"); return nv_fail(6,"GSP RISC-V core is not running after booter"); }
    nvl_s("[7] waiting for GSP_INIT_DONE"); nvl_nl(); G.seq_err[0]=0; G.resumes=0;
    u64 t0=hr_ns(); int got=0;
    while(hr_ns()-t0<15000000000ULL){ u32 fn=0,n=0; int m=msg_one(&fn,g_evbuf,sizeof(g_evbuf),&n,500); if(m==0)continue; if(m<0&&m!=-2)break; if(fn==0x1001){ got=1; break; } if(m>0)ev_handle(fn,g_evbuf,n); }
    nvl_kv("LOGINIT put pointer",*(volatile u64*)G.log[0]); nvl_kv("LOGRM put pointer",*(volatile u64*)G.log[2]); nvl_kv("GSP mailbox0",nvr32(0x110040)); nvl_kv("sequencer runs",G.seq_count);
    if(!got){ nvf_dump(NVF_GSP,"GSP falcon"); nvl_kv("cmdq write/read",((u64)*q32(CMDQ+0x10)<<32)|*q32(MSGQ+0x20)); nvl_kv("msgq write/read",((u64)*q32(MSGQ+0x10)<<32)|*q32(CMDQ+0x20));
        if(G.seq_err[0]){ static char w[160]; char* q=w; q=scopy(q,"GSP-RM asked the CPU for a sequence that failed - "); q=scopy(q,G.seq_err); *q=0; return nv_fail(7,w); }
        return nv_fail(7,"GSP-RM did not report GSP_INIT_DONE within 15 s"); }
    if(G.resumes){ nvl_kv("GSP-RM resumes done for the sequencer",G.resumes); }
    G.state=2; g_rpc_dead=0; g_gsp_boots++; nvl_s("  GSP-RM is running"); nvl_nl();
    nvl_s("[8] GET_GSP_STATIC_INFO"); nvl_nl();
    static u8 si[2168]; for(int i=0;i<2168;i++)si[i]=0;
    if(!cmdq_push(65,si,2168,0))return nv_fail(8,"could not send GET_GSP_STATIC_INFO");
    u32 n=0; if(rpc_wait(65,si,sizeof(si),&n,5000)<=0)return nv_fail(8,"no reply to GET_GSP_STATIC_INFO");
    G.gpc_mask=*(u32*)(si+0x124); G.tpcs=0; for(int g=0;g<32;g++) if(G.gpc_mask&(1u<<g)){ u32 tm=*(u32*)(si+0x128+g*8+4); while(tm){ G.tpcs+=tm&1u; tm>>=1; } }
    G.nfbr=*(u32*)(si+0x348); if(G.nfbr>16)G.nfbr=16;
    for(u32 i=0;i<G.nfbr;i++){ u8* e=si+0x348+8+i*48; G.fbr[i].base=*(u64*)e; G.fbr[i].limit=*(u64*)(e+8); G.fbr[i].reserved=*(u64*)(e+16); G.fbr[i].prot=e[30];
        nvl_s("  FB region "); nvl_d(i); nvl_s(": "); nvl_x(G.fbr[i].base); nvl_s(" - "); nvl_x(G.fbr[i].limit); nvl_s(G.fbr[i].reserved?" reserved":""); nvl_s(G.fbr[i].prot?" protected":""); nvl_nl(); }
    G.bar1_pde=*(u64*)(si+0x828); G.bar2_pde=*(u64*)(si+0x830); G.hclient=*(u32*)(si+0x868); G.hdevice=*(u32*)(si+0x86C); G.hsubdev=*(u32*)(si+0x870);
    for(int i=0;i<63;i++)G.name[i]=(char)si[0x71C+i]; G.name[63]=0;
    nvl_s("  GPU name: "); nvl_s(G.name); nvl_nl(); nvl_kv("GPC mask",G.gpc_mask); nvl_kv("TPCs",G.tpcs); nvl_kv("BAR1 root PD",G.bar1_pde); nvl_kv("BAR2 root PD",G.bar2_pde); nvl_kv("internal client",G.hclient);
    nvw32(0x110004,0x40);
    vram_alloc_init();
    G.state=3; return 1; }
