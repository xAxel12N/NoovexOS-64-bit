/* Simulated RTL8821CE send queues for the v10.99 management-frame path.
   0x3A8 = data (BE) queue pointer, 0x3B0 = management queue pointer: host index in bits 11:0,
   card read index in bits 27:16. The data queue always drains; the management queue drains only
   when mg_works is set. card_gone makes every read return 0xFFFFFFFF. */
static int mg_works=1, card_gone=0; static u32 host_be=0, host_mg=0, hw_be=0, hw_mg=0; static u64 waited=0;
u32 rtw_r32(u32 m,u32 r){ (void)m; if(card_gone)return 0xFFFFFFFFu;
    if(r==0x03A8){ hw_be=host_be; return (hw_be<<16)|host_be; }
    if(r==0x03B0){ if(mg_works)hw_mg=host_mg; return (hw_mg<<16)|host_mg; }
    return 0; }
void rtw_w16(u32 m,u32 r,u16 v){ (void)m; if(r==0x03A8)host_be=v&0x0FFFu; if(r==0x03B0)host_mg=v&0x0FFFu; }
void rtw_delay_us(u32 us){ waited+=us; }
void rtw_txdesc_rate(u8* p,int mg){ (void)p; (void)mg; }
void cpu_wmb(void){}
static u8 dpool[40*4096] __attribute__((aligned(4096))); static int dused=0;   /* .bss: below 4 GB when built -no-pie */
void* dma_alloc_pages(int n){ if(dused+n>40)return 0; void* p=dpool+dused*4096; dused+=n; return p; }
static u8 bering[4096] __attribute__((aligned(4096))), mgring[4096] __attribute__((aligned(4096)));
static void reset(int works){ mg_works=works; card_gone=0; host_be=host_mg=hw_be=hw_mg=0; g_beidx=g_mgidx=0; g_mg_via_be=0; g_mg_last=0;
    g_mg_sent=g_mg_left=g_mg_stuck=g_mg_be=g_mg_noroom=0; memset(bering,0,sizeof bering); memset(mgring,0,sizeof mgring); rtw_jlog_reset(); }
static u8 fr[64]; static int frl;
static void mkframe(int bcast){ memset(fr,0,sizeof fr); fr[0]=0xB0; for(int i=0;i<6;i++){ fr[4+i]=bcast?0xFF:(u8)(0x10+i); fr[10+i]=(u8)(0x20+i); fr[16+i]=fr[4+i]; } frl=30; }
static u8* pkt_of(u8* ring,u32 slot){ return (u8*)(u64)(*(u32*)(ring+slot*16+4)); }
int main(){ int bad=0;
    if(g_mg_via_be!=1||g_mg_try_mgq!=0){ bad++; printf("0 default should be the data queue (via_be=%d)\n",g_mg_via_be); }
    g_bering=bering; g_benum=256; g_mgring=mgring; g_mgnum=256;
    /* pkt pointers are stored as 32-bit in the descriptors: only check them when the heap is below 4 GB */
    u8* probe=rtw_tx_buf(0); int lowmem=((u64)probe>>32)==0; if(!lowmem)printf("note: buffers above 4 GB - descriptor checks skipped\n");
    /* 1: working management queue */
    reset(1); mkframe(0);
    if(rtw_tx_mgmt_path(0,fr,frl,0)!=0||g_mg_left!=1||g_mg_via_be||g_mg_last!=1||host_mg!=1||host_be!=0){ bad++; printf("1 working mgmt: left=%u via_be=%d last=%d host_mg=%u host_be=%u\n",g_mg_left,g_mg_via_be,g_mg_last,host_mg,host_be); }
    if(lowmem){ u8* p=pkt_of(mgring,0); u32 w0=*(u32*)p, w1=*(u32*)(p+4);
        if(!(w0&(1u<<31))||((w1>>8)&0x1Fu)!=18u||(w0&(1u<<24))||(w0&0xFFFFu)!=(u32)frl||memcmp(p+48,fr,frl)){ bad++; printf("1 mgmt descriptor wrong w0=%08x w1=%08x\n",w0,w1); } }
    if(strcmp(rtw_mg_last_name(),"mgmt queue")){ bad++; printf("1 name %s\n",rtw_mg_last_name()); }
    /* 2: stuck management queue -> resend through the data queue and stay there */
    reset(0); mkframe(0); waited=0;
    if(rtw_tx_mgmt_path(0,fr,frl,0)!=0||!g_mg_via_be||g_mg_stuck!=1||g_mg_be!=1||g_mg_last!=2||host_be!=1){ bad++; printf("2 stuck: via_be=%d stuck=%u be=%u last=%d host_be=%u\n",g_mg_via_be,g_mg_stuck,g_mg_be,g_mg_last,host_be); }
    if(waited>30000){ bad++; printf("2 waited %llu us on a stuck queue (should be ~20 ms)\n",waited); }
    if(g_jlog_n<2||!strstr(g_jlog[0],"STUCK")||!strstr(g_jlog[1],"data queue")){ bad++; printf("2 log: n=%u [%s] [%s]\n",g_jlog_n,g_jlog[0],g_jlog[1]); }
    if(lowmem){ u8* p=pkt_of(bering,0); u32 w0=*(u32*)p, w1=*(u32*)(p+4);
        if((w0&(1u<<31))||((w1>>8)&0x1Fu)!=0u||(w0&(1u<<24))){ bad++; printf("2 data-queue descriptor wrong w0=%08x w1=%08x\n",w0,w1); } }
    u32 mg_before=host_mg; rtw_tx_mgmt_path(0,fr,frl,0);
    if(host_mg!=mg_before||host_be!=2||g_mg_be!=2){ bad++; printf("2 second frame did not go straight to the data queue (mg %u->%u be %u)\n",mg_before,host_mg,host_be); }
    /* 3: broadcast frames get the BMC bit, forced data queue works even with a good mgmt queue */
    reset(1); mkframe(1);
    if(rtw_tx_mgmt_path(0,fr,frl,1)!=0||host_be!=1||host_mg!=0||g_mg_via_be){ bad++; printf("3 forced data queue: be=%u mg=%u via_be=%d\n",host_be,host_mg,g_mg_via_be); }
    if(lowmem){ u32 w0=*(u32*)pkt_of(bering,0); if(!(w0&(1u<<24))){ bad++; printf("3 broadcast without BMC w0=%08x\n",w0); } }
    /* 4: card gone - no hang, error returned */
    reset(1); card_gone=1; mkframe(0); waited=0;
    if(rtw_tx_mgmt_path(0,fr,frl,0)!=-1||waited>1000){ bad++; printf("4 card gone: waited %llu\n",waited); }
    rtw_tx_mgmt_drain(0);
    /* 5: bad lengths are refused */
    reset(1); mkframe(0);
    if(rtw_tx_mgmt_path(0,fr,5,0)!=-1||rtw_tx_mgmt_path(0,fr,5000,0)!=-1){ bad++; printf("5 bad length accepted\n"); }
    /* 6: ring wrap - 1000 frames through a working queue, then through the data queue */
    reset(1); mkframe(0);
    for(int i=0;i<1000;i++) if(rtw_tx_mgmt_path(0,fr,frl,0)!=0){ bad++; printf("6 wrap failed at %d\n",i); break; }
    if(g_mg_left!=1000||g_mgidx!=1000u%256u||g_mg_via_be){ bad++; printf("6 wrap: left=%u idx=%u\n",g_mg_left,g_mgidx); }
    for(int i=0;i<600;i++) rtw_tx_mgmt_path(0,fr,frl,1);
    if(g_beidx!=600u%256u||g_mg_be!=600){ bad++; printf("6 data wrap idx=%u be=%u\n",g_beidx,g_mg_be); }
    rtw_tx_mgmt_drain(0);
    /* 7: join log - overflow and long text stay inside the buffers */
    rtw_jlog_reset(); char big[400]; memset(big,'x',sizeof big-1); big[sizeof big-1]=0;
    for(int i=0;i<200;i++) rtw_jlog(i&1?big:"step ",i&2?big:"x",i);
    for(int i=0;i<RTW_JLOG_N;i++) if(strlen(g_jlog[i])>=RTW_JLOG_W){ bad++; printf("7 line %d too long\n",i); break; }
    rtw_jlog_reset(); rtw_jlog("auth answer, status","",0); rtw_jlog("a","b",-1); rtw_jlog("n","",4294967);
    if(strcmp(g_jlog[0],"auth answer, status 0")||strcmp(g_jlog[1],"ab")||strcmp(g_jlog[2],"n 4294967")){ bad++; printf("7 format [%s] [%s] [%s]\n",g_jlog[0],g_jlog[1],g_jlog[2]); }
    printf(bad?"FAIL %d\n":"mgmt ok\n",bad); return bad; }
