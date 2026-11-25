static int fails=0, checks=0; static void ck(const char* w,int c){ checks++; printf("  %-66s %s\n",w,c?"ok":"FAIL"); if(!c)fails++; }
static u64 rs=0x9E3779B97F4A7C15ull; static u32 rnd(void){ rs^=rs<<13; rs^=rs>>7; rs^=rs<<17; return (u32)(rs>>11); } static int rn(int n){ return n>0?(int)(rnd()%(u32)n):0; }
static u8 B[4096]; static int bn;
static void put(int n,const u8* d){ for(int i=0;i<n&&bn<(int)sizeof B;i++)B[bn++]=d[i]; }
static void pb(int n,...){ __builtin_va_list a; __builtin_va_start(a,n); for(int i=0;i<n;i++){ u8 v=(u8)__builtin_va_arg(a,int); put(1,&v); } __builtin_va_end(a); }
static void cfgh(void){ bn=0; pb(9,9,2,0,0,2,1,0,0x80,50); }
static void cfgfix(void){ if(bn>=4){ B[2]=(u8)bn; B[3]=(u8)(bn>>8); } }
static void ifc(int n,int alt,int ne,int c,int s,int p){ pb(9,9,4,n,alt,ne,c,s,p,0); }
static void epd(int a,int at,int mps,int iv){ pb(7,7,5,a,at,mps&255,mps>>8,iv); }
static const int cls_pick[]={3,14,1,2,0x0A,0xE0,8,9,0xFF,0xEF,7,6,0x0B,0};
static void rand_desc(void){
    int k=rn(12), cl=cls_pick[rn(14)];
    if(k==0){ ifc(rn(6),rn(4),rn(4),cl,rn(5),rn(4)==0?0x20:rn(4)); }
    else if(k==1){ epd(rn(2)?0x80|rn(16):rn(16),rn(4),rn(4)?rn(1100):rnd()&0xFFFF,rn(17)); }
    else if(k==2){ pb(9,9,0x21,0x11,1,0,1,0x22,rn(2)?rn(200):rnd()&0xFF,rn(5)); }
    else if(k==3||k==4||k==5){ int l=rn(4)?2+rn(40):rn(256); u8 d[256]; d[0]=(u8)l; d[1]=0x24; d[2]=(u8)rn(17); for(int i=3;i<256;i++)d[i]=(u8)(rn(3)?rn(8):rnd()); if(rn(2)){ d[4]=1; d[5]=2; } put(l>=2?l:2,d); }
    else if(k==6){ int l=2+rn(22); u8 d[24]; d[0]=(u8)l; d[1]=0x25; d[2]=1; for(int i=3;i<24;i++)d[i]=(u8)rn(40); put(l,d); }
    else if(k==7){ pb(6,6,0x30,rn(16),rn(4),rn(256),rn(8)); }
    else if(k==8){ pb(8,8,0x0B,rn(4),rn(4),cl,rn(4),0,0); }
    else if(k==9){ int l=rn(256); u8 d[256]; for(int i=0;i<256;i++)d[i]=(u8)rnd(); d[0]=(u8)l; put(l?l:1,d); }
    else if(k==10){ int l=rn(4); u8 d[4]={(u8)l,(u8)rn(256),0,0}; put(l?l:1,d); }
    else { u8 d[64]; int l=7+rn(57); d[0]=(u8)l; d[1]=0x24; d[2]=(u8)(rn(2)?6:5); for(int i=3;i<64;i++)d[i]=(u8)rnd(); d[21]=(u8)rn(40); put(l,d); } }
static void seed(int s){
    cfgh();
    if(s==0){ ifc(0,0,1,3,1,1); pb(9,9,0x21,0x11,1,0,1,0x22,63,0); epd(0x81,3,8,10); ifc(1,0,1,3,0,0); pb(9,9,0x21,0x11,1,0,1,0x22,120,0); epd(0x82,3,16,1); }
    else if(s==1){ ifc(0,0,1,14,1,0); pb(13,13,0x24,1,0x10,0x01,0,0,0,0,0,0,0,0); pb(18,18,0x24,2,1,1,2,0,0,0,0,0,0,0,0,3,0x0E,0x20,0); pb(11,11,0x24,5,2,1,0,0,2,0x7F,0x15,0); ifc(1,0,0,14,2,0);
        pb(11,11,0x24,6,1,1,1,1,0,0,0,0); { u8 f[38]={38,0x24,7,1,0,0x80,2,0xE0,1}; f[21]=0x15; f[22]=0x16; f[23]=5; f[25]=0; f[26]=0x15; f[27]=0x16; f[28]=5; f[30]=0x2A; f[31]=0x2C; f[32]=0x0A; f[34]=0x15; f[35]=0x16; f[36]=5; put(38,f); }
        ifc(1,1,1,14,2,0); epd(0x81,5,1024|(2<<11),1); pb(6,6,0x30,0,1,0,12); }
    else if(s==2){ ifc(0,0,0,1,1,0); pb(10,10,0x24,1,0,1,40,0,1,1,2); pb(12,12,0x24,6,2,1,1,3,0,0,0,0,0); ifc(1,0,0,1,2,0); ifc(1,1,1,1,2,0); pb(7,7,0x24,1,1,1,1,0);
        pb(14,14,0x24,2,1,2,2,16,2,0x44,0xAC,0,0x80,0xBB,0); pb(9,9,5,0x01,9,200,0,1,0); pb(7,7,0x25,1,1,1,0);
        ifc(2,0,0,1,2,0x20); ifc(2,1,1,1,2,0x20); pb(16,16,0x24,1,1,0,1,1,0,0,0,2,3,0,0,0,0); pb(6,6,0x24,2,1,2,16); epd(0x82,5,192,1); }
    else if(s==3){ ifc(0,0,0,1,1,0); ifc(1,0,2,1,3,0); pb(7,7,0x24,1,0,1,0,0); pb(9,9,5,0x01,2,64,0,0,0); pb(5,5,0x25,1,1,1); pb(9,9,5,0x81,2,64,0,0,0); pb(5,5,0x25,1,1,1); }
    else if(s==4){ ifc(0,0,1,2,6,0); pb(5,5,0x24,0,0x10,1); pb(5,5,0x24,6,0,1); pb(13,13,0x24,0x0F,4,0,0,0,0,0xEA,5,0,0,0); epd(0x83,3,16,8); ifc(1,0,0,0x0A,0,0); ifc(1,1,2,0x0A,0,0); epd(0x81,2,512,0); epd(0x02,2,512,0); }
    else if(s==5){ ifc(0,0,3,0xE0,1,1); epd(0x81,3,16,1); epd(0x82,2,64,0); epd(0x02,2,64,0); ifc(1,0,2,0xE0,1,1); epd(0x83,1,0,1); epd(0x03,1,0,1); ifc(1,1,2,0xE0,1,1); epd(0x83,1,9,1); epd(0x03,1,9,1); }
    else if(s==6){ ifc(0,0,2,8,6,0x50); epd(0x81,2,512,0); epd(0x02,2,512,0); }
    else { ifc(0,0,1,9,0,0); epd(0x81,3,1,12); ifc(0,1,1,9,0,2); epd(0x81,3,1,12); }
    cfgfix(); }
static void mutate(void){
    int m=rn(6);
    for(int k=0;k<m&&bn>0;k++){ int t=rn(5), p=rn(bn);
        if(t==0)B[p]^=(u8)(1u<<rn(8)); else if(t==1)B[p]=(u8)rnd(); else if(t==2)B[p]=(u8)(rn(3)==0?0:rn(2)?1:255);
        else if(t==3){ int l=1+rn(16); if(bn+l<(int)sizeof B){ memmove(B+p+l,B+p,(size_t)(bn-p)); for(int i=0;i<l;i++)B[p+i]=(u8)rnd(); bn+=l; } }
        else { int l=1+rn(16); if(p+l>bn)l=bn-p; memmove(B+p,B+p+l,(size_t)(bn-p-l)); bn-=l; } }
    if(rn(4)==0&&bn>0)bn=rn(bn+1); }
static int nonterm(const char* s,int cap){ for(int i=0;i<cap;i++) if(!s[i])return 0; return 1; }
static int bad;
#define INV(c) do{ if(!(c)){ if(bad<5)printf("    invariant failed: %s (iteration input %d bytes)\n",#c,n); bad++; } }while(0)
static void run_cfg(const u8* src,int n){
    u8* c=(u8*)malloc(n>0?(size_t)n:1); if(n>0)memcpy(c,src,(size_t)n);
    hidif_t hf[6]; int nh=hid_pick(c,n,hf,6); INV(nh>=0&&nh<=6); for(int j=0;j<nh;j++) INV((hf[j].ep&0x80)&&(hf[j].ep&0x0F)&&hf[j].mps>0&&hf[j].mps<=0x7FF);
    (void)uvc_has_video(c,n); (void)aud_has_audio(c,n); (void)umidi_has(c,n);
    cam.speed=rn(5); uvc_choose(c,n,rn(3)-1); INV(cam.nvs>=0&&cam.nvs<=4&&cam.nfr>=0&&cam.nfr<=64&&cam.nalt>=0&&cam.nalt<=24);
    if(cam.nvs>1) uvc_choose(c,n,rn(cam.nvs)); (void)uvc_has_color();
    for(int k=0;k<cam.nfr;k++){ volatile u32 x=cam.fr[k].bestint; (void)x; }
    { uvc_xuc_t xs[32]; int vc=-1; int xn=uvc_xu_list(c,n,rn(4),xs,32,&vc); INV(xn>=0); }
    aud_parse(c,n); INV(aud.nalt>=0&&aud.nalt<=16&&aud.nfu>=0&&aud.nfu<=8); for(int k=0;k<aud.nalt;k++){ INV(aud.alt[k].nrate<=8); (void)aud_pick_rate(&aud.alt[k]); }
    { int p0=aud_pick(0), p1=aud_pick(1); INV(p0>=-1&&p0<aud.nalt&&p1>=-1&&p1<aud.nalt); }
    { struct umc_cfg u; if(umc_parse_cfg(c,n,&u)) INV(u.ncin>=1&&u.ncin<=16&&u.ncout>=1&&u.ncout<=16&&u.im<=0x7FF&&u.om<=0x7FF); }
    { int fi=-1; int a=xhr_mtt_alt(c,n,&fi); INV(a>=-1&&a<=255); }
    { usn_cand_t b; memset(&b,0,sizeof b); g_usn_pref=rn(4); if(usn_parse(c,n,rn(4)+1,&b)) INV(b.in_mps<=0x7FF&&b.out_mps<=0x7FF&&(b.in_ep&0x0F)&&(b.out_ep&0x0F)&&b.kind>=1&&b.kind<=3); }
    btu.vid=(u16)rnd(); btu.pid=(u16)rnd(); if(btu_parse(c,n)){ INV(btu.int_mps<=0x7FF&&(btu.int_ep&0x0F)&&(btu.bin_ep&0x0F)&&(btu.bout_ep&0x0F)); } btu_parse_iso(c,n); INV(bti.n>=0&&bti.n<=8);
    { int ie=0,im=512,oe=0,om=512; msd_cfg_eps(c,n,&ie,&im,&oe,&om); INV((!ie||(ie&0x0F))&&(!oe||(oe&0x0F))&&im>=1&&im<=0x7FF&&om>=1&&om<=0x7FF); }
    { char nm[40]; int l=rn(40); for(int i=0;i<l;i++)nm[i]=(char)(32+rn(90)); nm[l]=0; usbdev_note(rn(4),(u16)rnd(),(u16)rnd(),c,n,rn(4)?nm:0); INV(g_usbdev_n<=USBDEV_MAX);
      for(int k=0;k<USBDEV_MAX;k++) if(g_usbdev[k].used) INV(!nonterm(g_usbdev[k].kind,32)&&!nonterm(g_usbdev[k].name,40)); if(rn(8)==0) for(int k=0;k<USBDEV_MAX;k++)g_usbdev[k].used=0; }
    free(c); }
static void run_blob(const u8* src,int n){
    u8* c=(u8*)malloc(n>0?(size_t)n:1); if(n>0)memcpy(c,src,(size_t)n);
    { xhr_hub_t h; if(xhr_hubdesc(c,n,rn(2),&h)) INV(h.nports>=1&&h.nports<=15); }
    { u8 mac[6]; (void)usn_strmac(c,n,mac); }
    (void)ncm_parse(c,n); (void)rndis_parse(c,n);
    { const u8* d=0; int dl=0; if(rndis_reply(c,n,rnd(),rnd()&3,&d,&dl)>0&&d) INV(dl>=0&&d>=c&&d+dl<=c+n); }
    { hidlay_t L[HIDLAY_MAX]; int hid=0; int nl=hid_parse(c,n,L,HIDLAY_MAX,&hid); INV(nl>=0&&nl<=HIDLAY_MAX); }
    (void)hpp_vendor_iface(0x046D,c,n);
    free(c); }
static void rand_blob(void){
    int t=rn(5); bn=0;
    if(t==0){ int l=rn(14); pb(1,l); pb(1,rn(2)?0x29:0x2A); for(int i=2;i<l;i++)pb(1,rn(3)?rn(20):rnd()); }
    else if(t==1){ int l=rn(64); pb(2,l,3); for(int i=2;i<l;i++)pb(1,rn(2)?"0123456789ABCDEFabcdefxyz"[rn(25)]:rnd()); if(rn(2))for(int i=3;i<l;i+=2)B[i]=0; }
    else if(t==2){ static const u8 it[]={0x05,0x09,0x19,0x29,0x15,0x25,0x75,0x95,0x81,0xA1,0xC0,0x85,0xA4,0xB4,0x06,0x0A,0x26,0x27,0xFE,0xB1,0x91,0x35,0x45}; int l=rn(300); for(int i=0;i<l;i++)pb(1,rn(2)?it[rn((int)sizeof it)]:rnd()); }
    else if(t==3){ int l=rn(600); pb(4,'N','C','M','H'); pb(4,12,0,rnd(),rnd()); pb(4,rn(64),rn(2),'N','C'); for(int i=12;i<l;i++)pb(1,rn(3)?rn(64):rnd()); }
    else { int l=rn(600); pb(4,rn(2)?1:0x80000004&255,0,0,rn(2)?0:0x80); for(int i=4;i<l;i++)pb(1,rn(3)?rn(80):rnd()); } }
int main(int argc,char** argv){
    long iters=argc>1?atol(argv[1]):200000; if(argc>2)rs^=(u64)atoll(argv[2])*0x2545F4914F6CDD1Dull;
    printf("USB descriptor fuzz (host test of drivers/usb, bluetooth/transport/btusb.c; real parsers, exact-size buffers)\n");
    { u8 r[256]; memset(r,0,sizeof r); for(int i=0;i<=243;i+=9){ r[i]=9; r[i+1]=0x24; } r[252]=4; r[253]=2; r[254]=4; r[255]=0; int ie=0,im=512,oe=0,om=512; msd_cfg_eps(r,256,&ie,&im,&oe,&om); r[252]=2; r[253]=4; msd_cfg_eps(r,256,&ie,&im,&oe,&om);
      u8 e[16]={9,4,0,0,2,8,6,0x50,0,7,5,0x81,2,0,0,0}; msd_cfg_eps(e,16,&ie,&im,&oe,&om); ck("regression: storage endpoint walk stays inside a 256-byte config (tail)",ie==0x81&&im==512); }
    { u8 e[23]={9,4,0,0,2,8,6,0x50,0,7,5,0x81,2,0x00,0xFC,0,7,5,0x02,2,0x00,0xF8,0}; int ie=0,im=512,oe=0,om=512; msd_cfg_eps(e,23,&ie,&im,&oe,&om); ck("regression: storage endpoint packet size masked to 11 bits",ie==0x81&&im==0x400&&oe==2&&om==512); }
    { u8 e[25]={9,4,0,0,2,8,6,0x50,0,4,4,0,0,9,5,0x81,2,0,2,0,7,5,0x02,2,0}; int ie=0,im=512,oe=0,om=512; msd_cfg_eps(e,23,&ie,&im,&oe,&om); ck("regression: short interface descriptor ignored, truncated endpoint ignored",ie==0x81&&oe==0); }
    { u8 e[20]={9,4,1,0,0,14,2,0,0,9,4,1,0,0,14,2,0,0,2,0x24}; u8* h=(u8*)malloc(20); memcpy(h,e,20); cam.speed=3; uvc_choose(h,20,-1); free(h); ck("regression: 2-byte camera class descriptor at the end is not read past",cam.vs_if==1&&cam.nfr==0); }
    { u8 e[25]={9,4,0,0,1,3,1,1,0,9,0x21,0x11,1,0,1,0x22,63,0,7,5,0x80,3,8,0,10}; hidif_t hf[2]; ck("regression: HID endpoint 0x80 (would be the control endpoint) refused",hid_pick(e,25,hf,2)==0); }
    for(int s=0;s<8;s++){ seed(s); run_cfg(B,bn); }
    ck("seed configs (HID, UVC, UAC1/2, MIDI, CDC-ECM, Bluetooth, storage, hub) parse",bad==0);
    { seed(0); hidif_t hf[6]; ck("seed keyboard: two HID interfaces found",hid_pick(B,bn,hf,6)==2&&hf[0].rdlen==63); seed(1); cam.speed=4; uvc_choose(B,bn,-1); ck("seed camera: one stream, MJPEG frame",cam.nvs==1&&cam.nfr==1&&cam.nalt==1);
      seed(2); aud_parse(B,bn); ck("seed headset: UAC1 + UAC2 streams",aud.nalt==2); seed(3); struct umc_cfg u; ck("seed MIDI: two bulk endpoints",umc_parse_cfg(B,bn,&u)&&u.ie==0x81&&u.oe==1);
      seed(4); usn_cand_t b; memset(&b,0,sizeof b); ck("seed CDC-ECM: data interface alt 1",usn_parse(B,bn,1,&b)&&b.alt==1&&b.in_ep==0x81); seed(5); btu.vid=0; btu.pid=0; ck("seed Bluetooth: 3 endpoints + 1 ISO alt",btu_parse(B,bn)&&(btu_parse_iso(B,bn),bti.n==1)); }
    long mut=0, gen=0, blob=0;
    for(long it=0;it<iters;it++){
        int w=rn(3);
        if(w==0){ seed(rn(8)); mutate(); mut++; }
        else if(w==1){ cfgh(); int k=rn(24); for(int i=0;i<k;i++)rand_desc(); if(rn(4))cfgfix(); if(rn(3)==0)mutate(); gen++; }
        else { rand_blob(); if(rn(2))mutate(); run_blob(B,bn); blob++; continue; }
        run_cfg(B,bn); }
    char m[160]; snprintf(m,sizeof m,"fuzz: %ld mutated + %ld generated configs + %ld hub/string/HID/NCM/RNDIS blobs, invariants hold",mut,gen,blob); ck(m,bad==0);
    printf("%d checks, %d failed\n",checks,fails); if(!fails)printf("usb descriptor fuzz: ALL PASS\n"); return fails?1:0; }
