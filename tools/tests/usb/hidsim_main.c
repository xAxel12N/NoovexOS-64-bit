static u8* g_e0; static u8* g_ibuf;
static u8 g_rd[2][512]; static int g_rdn[2];
static int g_pol, g_setproto_seen, g_proto_dev=1;
enum { POL_REPORT, POL_BOOT, POL_STALL, POL_STALLGET };
static const char* POLN[]={"answers-report","answers-boot","stalls-both","stalls-GET"};
static int xh_ctrl(u8 rt,u8 req,u16 val,u16 idx,u16 len,int in,u8* data){ (void)in;
  if(rt==0x81&&req==6&&val==0x2200){ if(idx>1)return 0; int n=g_rdn[idx]<len?g_rdn[idx]:len; memcpy(g_e0,g_rd[idx],n); return 1; }
  if(rt==0x21&&req==0x0B){ if(g_pol==POL_STALL)return 0; if(idx==1){ g_setproto_seen=1; g_proto_dev=val; } return 1; }
  if(rt==0xA1&&req==0x03){ if(g_pol==POL_STALL||g_pol==POL_STALLGET)return 0; if(data)data[0]=(g_pol==POL_BOOT)?0:1; return 1; }
  if(rt==0x21&&req==0x0A)return 1;
  return 1; }
static u8 RD_KBD[]={0x05,0x01,0x09,0x06,0xA1,0x01,0x05,0x07,0x19,0xE0,0x29,0xE7,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02,0x95,0x01,0x75,0x08,0x81,0x01,
  0x95,0x05,0x75,0x01,0x05,0x08,0x19,0x01,0x29,0x05,0x91,0x02,0x95,0x01,0x75,0x03,0x91,0x01,0x95,0x06,0x75,0x08,0x15,0x00,0x25,0x65,0x05,0x07,0x19,0x00,0x29,0x65,0x81,0x00,0xC0};
typedef struct { int bits, nbtn, minmax, id, combo, mps, sub, proto, pol; } dev_t2;
static const char* STN[]={"list","min/max","separate","use<page","logic32"};
static int w; static u8* o;
static void b(int n,...){ __builtin_va_list a; __builtin_va_start(a,n); for(int i=0;i<n;i++)o[w++]=(u8)__builtin_va_arg(a,int); __builtin_va_end(a); }
static int build_rd(const dev_t2* D,u8* out){ o=out; w=0;
  if(D->combo){ b(6,0x05,0x01,0x09,0x06,0xA1,0x01); b(2,0x85,0x01); b(16,0x05,0x07,0x19,0xE0,0x29,0xE7,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02); b(6,0x95,0x01,0x75,0x08,0x81,0x01);
    b(17,0x95,0x06,0x75,0x08,0x15,0x00,0x26,0xFF,0x00,0x05,0x07,0x19,0x00,0x2A,0xFF,0x00,0x81); b(2,0x00,0xC0); }
  b(6,0x05,0x01,0x09,0x02,0xA1,0x01); if(D->id)b(2,0x85,D->id); b(4,0x09,0x01,0xA1,0x00);
  b(16,0x05,0x09,0x19,0x01,0x29,D->nbtn,0x15,0x00,0x25,0x01,0x95,D->nbtn,0x75,0x01,0x81,0x02); if(D->nbtn%8)b(6,0x95,0x01,0x75,8-D->nbtn%8,0x81,0x01);
  int st=D->minmax;
  if(st==3) b(4,0x09,0x30,0x09,0x31); b(2,0x05,0x01); if(st==1)b(4,0x19,0x30,0x29,0x31); else if(st!=2&&st!=3)b(4,0x09,0x30,0x09,0x31);
  if(st==4){ int lo=-(1<<(D->bits-1))+1, hi=(1<<(D->bits-1))-1; b(10,0x17,lo&0xFF,(lo>>8)&0xFF,(lo>>16)&0xFF,(lo>>24)&0xFF,0x27,hi&0xFF,(hi>>8)&0xFF,0,0); }
  else if(D->bits==8)b(4,0x15,0x81,0x25,0x7F); else if(D->bits==12)b(6,0x16,0x01,0xF8,0x26,0xFF,0x07); else b(6,0x16,0x01,0x80,0x26,0xFF,0x7F);
  if(st==2){ b(8,0x09,0x30,0x75,D->bits,0x95,0x01,0x81,0x06); b(4,0x09,0x31,0x81,0x06); } else b(6,0x75,D->bits,0x95,0x02,0x81,0x06); b(12,0x09,0x38,0x15,0x81,0x25,0x7F,0x75,0x08,0x95,0x01,0x81,0x06); b(9,0x05,0x0C,0x0A,0x38,0x02,0x95,0x01,0x81,0x06); b(2,0xC0,0xC0);
  if(D->combo){ b(6,0x05,0x0C,0x09,0x01,0xA1,0x01); b(2,0x85,0x03); b(17,0x15,0x00,0x26,0xFF,0x03,0x19,0x00,0x2A,0xFF,0x03,0x75,0x10,0x95,0x01,0x81,0x00); b(1,0xC0); }
  return w; }
static void put(u8* r,int bit,int sz,int v){ for(int i=0;i<sz;i++){ int p=bit+i; if((v>>i)&1)r[p>>3]|=(u8)(1<<(p&7)); else r[p>>3]&=(u8)~(1<<(p&7)); } }
static int build_report(const dev_t2* D,u8* r,int dx,int dy){ memset(r,0,64); int base=0; if(D->id){ r[0]=(u8)D->id; base=8; }
  int bb=(D->nbtn+7)/8*8; put(r,base+bb,D->bits,dx); put(r,base+bb+D->bits,D->bits,dy); int tot=base+bb+2*D->bits+16; return (tot+7)/8; }
static int build_cfg(const dev_t2* D,u8* c){ o=c; w=0; b(9,0x09,0x02,0,0,2,1,0,0xA0,50);
  b(9,0x09,0x04,0,0,1,3,1,1,0); b(9,0x09,0x21,0x11,0x01,0,1,0x22,g_rdn[0]&0xFF,g_rdn[0]>>8); b(7,0x07,0x05,0x81,0x03,8,0,10);
  b(9,0x09,0x04,1,0,1,3,D->sub,D->proto,0); b(9,0x09,0x21,0x11,0x01,0,1,0x22,g_rdn[1]&0xFF,g_rdn[1]>>8); b(7,0x07,0x05,0x82,0x03,D->mps&0xFF,D->mps>>8,1);
  c[2]=(u8)w; c[3]=0; return w; }
typedef struct { int pos, cap, halted, e, done; } td_t;
static int g_nrecov;
static void td_complete(td_t* T,int cc){ hid_xfer(1,T->e<0?0:xhid_dci[T->e],(u8)cc,(u32)(T->cap-T->pos),0); T->pos=0; T->done++; }
static void td_packet(td_t* T,const u8* p,int n,int mps){ if(T->halted)return;
  u8* buf=(u8*)(u64)xhid_dbuf[T->e];
  if(n>T->cap-T->pos){ int r0=g_recov; td_complete(T,3); if(g_recov==r0)T->halted=1; else g_nrecov++; return; }
  memcpy(buf+T->pos,p,n); T->pos+=n;
  if(n<mps||T->pos==T->cap) td_complete(T,T->pos<T->cap?13:1); }
static const int MV[][2]={{5,0},{0,6},{0,-2},{-3,0},{2,4},{0,-9},{7,-1}};
#define NMV (int)(sizeof MV/sizeof MV[0])
static int nunexp, nfail, ncase, nlosty, nwrongy, nother;
static const char* why;
static int run(const dev_t2* D,int verbose){
  g_pol=D->pol; g_setproto_seen=0; g_proto_dev=1;
  memcpy(g_rd[0],RD_KBD,sizeof RD_KBD); g_rdn[0]=sizeof RD_KBD; g_rdn[1]=build_rd(D,g_rd[1]);
  u8 cfg[256]; int cn=build_cfg(D,cfg);
  hidif_t hf[6]; int nh=hid_pick(cfg,cn,hf,6);
  simenum_t S; memset(&S,0,sizeof S); int r=sim_enum(hf,nh,&S);
  int j=-1; for(int q=0;q<S.nh;q++) if(S.hf[q].ep==0x82)j=q;
  memset(xhid_used,0,sizeof xhid_used); xhid_n=0; mouse_dx=mouse_dy=0; g_armed=0; g_nrecov=0;
  why="ok";
  if(r<0||j<0){ why="mouse interface dropped"; return 2; }
  int e=j; xhid_used[e]=1; xhid_ctl[e]=0; xhid_slot[e]=1; xhid_dci[e]=(0x82&0xF)*2+1; xhid_rlen[e]=S.rlen[j]; xhid_rep[e]=S.mode[j]; xhid_hasid[e]=S.hasid[j]; xhid_nlay[e]=S.nlays[j];
  for(int q=0;q<HIDLAY_MAX;q++)xhid_lay[e][q]=S.lays[j][q]; xhid_iskbd[e]=0; xhid_ccn[e]=0; xhid_n=e+1; xhid_epin[e]=0x1000; xhid_dbuf[e]=(u32)(u64)(g_ibuf+e*XHID_INFLIGHT*64);
  td_t T={0,S.rlen[j],0,e,0}; int sx=0, sy=0, rn=0;
  for(int m=0;m<NMV;m++){ u8 rep[64]; rn=build_report(D,rep,MV[m][0],MV[m][1]); sx+=MV[m][0]; sy+=MV[m][1];
    for(int off=0;off<rn;off+=D->mps){ int n=rn-off<D->mps?rn-off:D->mps; td_packet(&T,rep+off,n,D->mps); } }
  int gx=mouse_dx, gy=-mouse_dy; int res=0;
  if(T.halted) { res=2; why="HALT"; }
  else if(gx==sx&&gy==sy) res=0;
  else if(gx==sx&&gy==0){ res=1; why="LOST-Y (dx right, dy 0)"; }
  else if(gx==sx){ res=3; why="Y WRONG (dx right, dy garbage)"; }
  else { res=2; why="X wrong too"; }
  int hasms=0; for(int q=0;q<S.nlays[j];q++) if(S.lays[j][q].kind==2)hasms=1;
  const char* cls=(T.halted||g_nrecov)?"XFAIL-halt: rlen>report, report%MPS==0, TD stays open, next report babbles (controller_init.c:322)":
    !hasms&&D->minmax==3?"XFAIL-parse: usages before Usage Page, hid_parse uses page at usage time (controller_init.c:118)":
    !S.mode[j]?"XFAIL-boot: GET_PROTOCOL=boot but device keeps report format; boot decode db[1],db[2] (controller_init.c:305, hid_keyboard_mouse.c:84)":"UNEXPECTED";
  if(verbose||res){
    printf("  %-5s %.10s bits=%-2d btn=%-2d %-9s id=%d%s mps=%-2d if1=%d/%d %-15s | rep=%dB rlen=%d mode=%s hasid=%d inl=%d tds=%d%s | dx %d/%d dy %d/%d\n",
      res==0?"ok":res==1?"LOSTY":res==3?"BADY":T.halted?"HALT":"FAIL",res?cls:"",D->bits,D->nbtn,STN[D->minmax],D->id,D->combo?"+kbd1+cc3":"",D->mps,D->sub,D->proto,POLN[D->pol],
      rn,S.rlen[j],S.mode[j]?"report":"boot",S.hasid[j],S.inl[j],T.done,T.pos?" (data stuck in open TD)":"",gx,sx,gy,sy);
  }
  if(res&&verbose>=0)printf("        -> %s\n",cls);
  if(res&&!strcmp(cls,"UNEXPECTED"))nunexp++;
  return res; }
int main(void){
  g_e0=mmap((void*)0x10000000,1<<16,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
  g_ibuf=mmap((void*)0x20000000,1<<16,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
  if(g_e0==MAP_FAILED||g_ibuf==MAP_FAILED||(u64)g_e0>>32||(u64)g_ibuf>>32){ printf("mmap below 4G failed\n"); return 2; }
  cur_dbuf=(u32)(u64)g_e0;
  static const int BITS[]={8,12,16}, BTN[]={5,16}, MPS[]={4,5,6,7,8,16,64};
  static const int IDS[][2]={{0,0},{1,0},{2,0},{2,1}};
  static const int IFC[][2]={{1,2},{0,0}};
  int counts[4]={0,0,0,0};
  printf("== combo receiver end-to-end: config -> hid_pick -> report desc -> hid_parse -> rlen -> protocol -> TD -> hid_xfer -> decode ==\n");
  for(int a=0;a<3;a++)for(int bt=0;bt<2;bt++)for(int mm=0;mm<5;mm++)for(int id=0;id<4;id++)for(int mp=0;mp<7;mp++)for(int ic=0;ic<2;ic++)for(int pl=0;pl<4;pl++){
    dev_t2 D={BITS[a],BTN[bt],mm,IDS[id][0],IDS[id][1],MPS[mp],IFC[ic][0],IFC[ic][1],pl};
    int r=run(&D,0); ncase++; counts[r]++; if(r)nfail++; if(r==1)nlosty++; if(r==3)nwrongy++; if(r==2)nother++; }
  printf("== summary: %d cases, ok %d, LOST-Y %d, Y-garbage %d, other %d ==\n",ncase,counts[0],nlosty,nwrongy,nother);
  printf("unexpected (not in a known XFAIL class): %d\n",nunexp);
  printf("(failures above are EXPECTED with the current driver: they document the dx-ok/dy-lost bug class)\n");
  printf("%s (%d cases, %d lose or corrupt an axis)\n",nfail?"FAILURES":"ALL PASS",ncase,nfail); return nfail?1:0; }
