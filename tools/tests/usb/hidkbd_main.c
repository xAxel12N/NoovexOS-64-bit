static int ok=1, nchk;
static void chk(const char* w,int c){ nchk++; printf("  %-72s %s\n",w,c?"ok":"FAIL"); if(!c)ok=0; }
static void clr(void){ kqn=0; kq[0]=0; g_hkn=0; }
static void reset_dev(void){ memset(xhid_kprev,0,sizeof xhid_kprev); memset(xhid_kprevx,0,sizeof xhid_kprevx); memset(xhid_ccn,0,sizeof xhid_ccn); memset(kdown,0,sizeof kdown); usbk_rep_code=0; usbk_rep_dev=-1; kcaps=0; clr(); }
static int load(const u8* rd,int n){ int hid=0; int k=hid_parse(rd,n,xhid_lay[0],HIDLAY_MAX,&hid); xhid_nlay[0]=k; xhid_hasid[0]=hid; reset_dev(); return k; }
static void rep(const u8* r,int n){ hid_report_decode(0,r,n); }
static void boot(const u8* r){ u8 b[8]; memcpy(b,r,8); kbd_report(0,xhid_kprev[0],b); }
static int kind(int k){ for(int q=0;q<xhid_nlay[0];q++) if(xhid_lay[0][q].kind==k)return q; return -1; }

static const u8 RD_BOOT[]={0x05,1,0x09,6,0xA1,1,0x05,7,0x19,0xE0,0x29,0xE7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,0x95,1,0x75,8,0x81,1,
 0x95,5,0x75,1,0x05,8,0x19,1,0x29,5,0x91,2,0x95,1,0x75,3,0x91,1,0x95,6,0x75,8,0x15,0,0x25,0x65,0x05,7,0x19,0,0x29,0x65,0x81,0,0xC0};
static const u8 RD_LOGI_ID1[]={0x05,1,0x09,6,0xA1,1,0x85,1,0x95,8,0x75,1,0x15,0,0x25,1,0x05,7,0x19,0xE0,0x29,0xE7,0x81,2,
 0x95,5,0x75,1,0x05,8,0x19,1,0x29,5,0x91,2,0x95,1,0x75,3,0x91,1,0x95,6,0x75,8,0x15,0,0x26,0xFF,0,0x05,7,0x19,0,0x2A,0xFF,0,0x81,0,0xC0,
 0x05,1,0x09,2,0xA1,1,0x85,2,0x09,1,0xA1,0,0x05,9,0x19,1,0x29,3,0x15,0,0x25,1,0x95,3,0x75,1,0x81,2,0x95,1,0x75,5,0x81,1,
 0x05,1,0x09,0x30,0x09,0x31,0x15,0x81,0x25,0x7F,0x75,8,0x95,2,0x81,6,0xC0,0xC0,
 0x05,0x0C,0x09,1,0xA1,1,0x85,3,0x75,16,0x95,2,0x15,1,0x26,0xFF,2,0x19,1,0x2A,0xFF,2,0x81,0,0xC0};
static const u8 RD_NKRO[]={0x05,1,0x09,6,0xA1,1,0x85,4,0x05,7,0x19,0xE0,0x29,0xE7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,
 0x05,7,0x19,0,0x29,0x77,0x15,0,0x25,1,0x75,1,0x95,0x78,0x81,2,0x05,8,0x19,1,0x29,5,0x95,5,0x75,1,0x91,2,0x95,1,0x75,3,0x91,1,0xC0};
static const u8 RD_COMBO[]={0x05,1,0x09,6,0xA1,1,0x85,1,0x05,7,0x19,0xE0,0x29,0xE7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,0x95,1,0x75,8,0x81,1,
 0x95,6,0x75,8,0x15,0,0x25,0x65,0x05,7,0x19,0,0x29,0x65,0x81,0,0xC0,
 0x05,1,0x09,2,0xA1,1,0x85,2,0x09,1,0xA1,0,0x05,9,0x19,1,0x29,3,0x15,0,0x25,1,0x95,3,0x75,1,0x81,2,0x95,1,0x75,5,0x81,1,
 0x05,1,0x09,0x30,0x09,0x31,0x15,0x81,0x25,0x7F,0x75,8,0x95,2,0x81,6,0xC0,0xC0,
 0x05,0x0C,0x09,1,0xA1,1,0x85,3,0x75,16,0x95,1,0x15,0,0x26,0x3C,2,0x19,0,0x2A,0x3C,2,0x81,0,0xC0,
 0x05,1,0x09,0x80,0xA1,1,0x85,4,0x19,0x81,0x29,0x83,0x15,0,0x25,1,0x75,1,0x95,3,0x81,2,0x95,5,0x81,1,0xC0};
static const u8 RD_TWOKBD[]={0x05,1,0x09,6,0xA1,1,0x85,1,0x05,7,0x19,0xE0,0x29,0xE7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,0x95,1,0x75,8,0x81,1,
 0x95,6,0x75,8,0x15,0,0x25,0x65,0x05,7,0x19,0,0x29,0x65,0x81,0,0xC0,
 0x05,1,0x09,6,0xA1,1,0x85,5,0x05,7,0x19,0xE0,0x29,0xE7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,0x95,1,0x75,8,0x81,1,
 0x95,6,0x75,8,0x15,0,0x25,0x65,0x05,7,0x19,0,0x29,0x65,0x81,0,0xC0};

int main(void){
  printf("== boot keyboard (boot protocol and the same descriptor in report protocol) ==\n");
  int k=load(RD_BOOT,sizeof RD_BOOT);
  chk("descriptor: one keyboard layout, no report ID, mods at bit 0, 6 keys at bit 16", k==1&&!xhid_hasid[0]&&xhid_lay[0][0].kind==1&&xhid_lay[0][0].mod_off==0&&xhid_lay[0][0].arr_off==16&&xhid_lay[0][0].arr_cnt==6);
  { u8 a[8]={0x02,0,0x0B}, z[8]={0}, b[8]={0,0,0x0C}; boot(a); boot(z); boot(b); boot(z); }
  chk("boot reports: Shift+h, i -> \"Hi\"", !strcmp(kq,"Hi"));
  chk("all keys and Shift released afterwards", !kdown[0x23]&&!kdown[0x17]&&!kdown[0x2A]&&!usbk_rep_code);
  clr(); { u8 a[]={0x02,0,0x0B,0,0,0,0,0}, z[8]={0}; rep(a,8); rep(z,8); }
  chk("report protocol decode of the same bytes -> \"H\"", !strcmp(kq,"H"));
  clr(); { u8 a[8]={0,0,0x04}; boot(a); }
  chk("hold a -> 'a', key down, repeat armed", !strcmp(kq,"a")&&kdown[0x1E]&&usbk_rep_code=='a');
  { u8 p[8]={0,0,1,1,1,1,1,1}; boot(p); }
  chk("phantom ErrorRollOver report: nothing typed, a still held", !strcmp(kq,"a")&&kdown[0x1E]&&usbk_rep_code=='a');
  { u8 p[8]={0,0,0x04,0x05,0x01,0x01,0x01,0x01}; boot(p); }
  chk("partial rollover (0x01 in later slots) ignored too", !strcmp(kq,"a")&&!kdown[0x30]);
  { u8 p[8]={0,0,0x04,0x05}; boot(p); }
  chk("then a+b: only b is typed (a not typed twice)", !strcmp(kq,"ab")&&kdown[0x1E]&&kdown[0x30]);
  { u8 p[8]={0,0,0x06}; boot(p); }
  chk("missed release: next report (c only) releases a and b", !strcmp(kq,"abc")&&!kdown[0x1E]&&!kdown[0x30]&&kdown[0x2E]&&usbk_rep_code=='c');
  g_ticks+=60; usb_kbd_repeat();
  chk("held c repeats after the delay", !strcmp(kq,"abcc"));
  { u8 z[8]={0}; boot(z); } g_ticks+=60; usb_kbd_repeat();
  chk("release stops the repeat (no stuck key)", !strcmp(kq,"abcc")&&!usbk_rep_code&&!kdown[0x2E]);
  clr(); { u8 p[8]={0,0,0xE1,0x04}, z[8]={0}; boot(p); chk("Shift sent as array key 0xE1 (cheap receivers) -> 'A', Shift down", !strcmp(kq,"A")&&kdown[0x2A]); boot(z); }
  chk("and released", !kdown[0x2A]);

  printf("== Logitech-style receiver keyboard: report ID 1 on a shared interface ==\n");
  k=load(RD_LOGI_ID1,sizeof RD_LOGI_ID1); int kb=kind(1);
  chk("keyboard (ID 1), mouse (ID 2) and media keys (ID 3) found", k==3&&xhid_hasid[0]&&kb>=0&&kind(2)>=0&&kind(3)>=0&&xhid_lay[0][kb].id==1);
  chk("keyboard: mods right after the ID, keys at bit 8 (no reserved byte)", kb>=0&&xhid_lay[0][kb].mod_off==0&&xhid_lay[0][kb].arr_off==8);
  { u8 a[]={1,0x02,0x0B,0,0,0,0,0}, z[]={1,0,0,0,0,0,0,0}; rep(a,8); rep(z,8); }
  chk("ID 1 report: the 0x01 ID is not read as Left Ctrl; Shift+h -> \"H\"", !strcmp(kq,"H")&&!kdown[0x1D]&&!kdown[0x2A]);
  clr(); { u8 m[]={3,0xE9,0,0,0}, z[]={3,0,0,0,0}; rep(m,5); rep(z,5); }
  chk("media key (ID 3, volume up): hotkey 1, no letter typed", !strcmp(kq,"")&&g_hkn==1&&g_hk[0]==1);
  clr(); mouse_dx=mouse_dy=0; { u8 m[]={2,0x01,5,(u8)-3}; rep(m,4); }
  chk("mouse report (ID 2) moves, types nothing", !strcmp(kq,"")&&mouse_dx==5&&mouse_dy==3);
  clr(); { u8 a[]={1,0,0x04,0x05,0x06,0x07,0x08,0x09}, z[]={1,0,0,0,0,0,0,0}; rep(a,8); rep(z,8); }
  chk("six keys at once -> \"abcdef\", all released", !strcmp(kq,"abcdef")&&!kdown[0x1E]&&!kdown[0x21]);
  clr(); { u8 r[]={1,0x41,0x2A,0,0,0,0,0}; rep(r,8); u8 z[]={1,0,0,0,0,0,0,0}; rep(z,8); }
  chk("Ctrl+Alt+Backspace (Left Ctrl + Right Alt) reaches its hotkey (30)", kq[0]==30);

  printf("== NKRO bitmap keyboard (report protocol, ID 4) ==\n");
  k=load(RD_NKRO,sizeof RD_NKRO); kb=kind(1);
  chk("bitmap layout: 120 keys from usage 0, mods at bit 0", k==1&&kb>=0&&xhid_lay[0][kb].bm_cnt==120&&xhid_lay[0][kb].bm_min==0&&xhid_lay[0][kb].bm_off==8&&xhid_lay[0][kb].arr_cnt==0);
  clr(); { u8 r[17]={4,0x02,0x30}; rep(r,17); u8 z[17]={4}; rep(z,17); }
  chk("bitmap a+b with Shift -> \"AB\", released", !strcmp(kq,"AB")&&!kdown[0x1E]&&!kdown[0x30]&&!kdown[0x2A]);
  clr(); { u8 r[17]={4,0,0xF0,0xFF}; rep(r,17); u8 z[17]={4}; rep(z,17); }
  chk("12 keys down: first 6 typed, no garbage", !strcmp(kq,"abcdef"));
  clr(); { u8 r[17]={4,0,0x0E}; rep(r,17); }
  chk("bitmap bits for usages 1..3 are not phantom keys", !strcmp(kq,"")&&!usbk_rep_code);
  clr(); { u8 r[17]={4,0}; r[2+(0x28>>3)]=1<<(0x28&7); rep(r,17); u8 z[17]={4}; rep(z,17); }
  chk("Enter (usage 0x28, byte 5 bit 0) -> newline", !strcmp(kq,"\n"));

  printf("== combo keyboard+mouse receiver (ID 1 kbd, 2 mouse, 3 consumer, 4 system) ==\n");
  k=load(RD_COMBO,sizeof RD_COMBO);
  chk("keyboard, mouse and consumer layouts (system control ignored)", k==3&&kind(1)>=0&&kind(2)>=0&&kind(3)>=0);
  clr(); { u8 a[]={1,0,0,0x0B,0x0C,0,0,0,0}, z[]={1,0,0,0,0,0,0,0,0}; rep(a,9); rep(z,9); }
  chk("ID 1 types \"hi\"", !strcmp(kq,"hi"));
  clr(); { u8 c[]={3,0x0B,0}, z[]={3,0,0}; rep(c,3); rep(z,3); }
  chk("consumer usage 0x0B on ID 3 is not read as letter 'h'", !strcmp(kq,"")&&!kdown[0x23]);
  clr(); { u8 c[]={3,0xE2,0}, z[]={3,0,0}; rep(c,3); rep(z,3); }
  chk("consumer mute (0xE2) -> hotkey 3, no letter", !strcmp(kq,"")&&g_hkn==1&&g_hk[0]==3);
  clr(); { u8 s[]={4,0x01}; rep(s,2); }
  chk("system-control report (ID 4) types nothing", !strcmp(kq,""));
  clr(); { u8 a[]={1,0,0,0x04,0,0,0,0,0}; rep(a,9); u8 c[]={3,0xE9,0}; rep(c,3); }
  chk("a media report while a is held does not release a", !strcmp(kq,"a")&&kdown[0x1E]&&usbk_rep_code=='a');
  { u8 z[]={1,0,0,0,0,0,0,0,0}; rep(z,9); u8 c[]={3,0,0}; rep(c,3); }
  { u8 x[]={9,0x04,0,0,0}; rep(x,5); }
  chk("unknown report ID ignored", !strcmp(kq,"a"));

  printf("== two keyboards behind one receiver (IDs 1 and 5) ==\n");
  k=load(RD_TWOKBD,sizeof RD_TWOKBD);
  chk("two keyboard layouts", k==2&&xhid_lay[0][0].kind==1&&xhid_lay[0][1].kind==1);
  clr(); { u8 a[]={1,0,0,0x04,0,0,0,0,0}; rep(a,9); u8 b[]={5,0,0,0x05,0,0,0,0,0}; rep(b,9); }
  chk("keyboard 1 holds a, keyboard 2 types b: \"ab\", a still down", !strcmp(kq,"ab")&&kdown[0x1E]&&kdown[0x30]);
  { u8 b[]={5,0,0,0,0,0,0,0,0}; rep(b,9); }
  chk("keyboard 2 releases b: a stays down", kdown[0x1E]&&!kdown[0x30]);
  { u8 a[]={1,0,0,0,0,0,0,0,0}; rep(a,9); }
  chk("keyboard 1 releases a", !kdown[0x1E]&&!usbk_rep_code);

  clr(); { u8 a[]={1,0x02,0,0,0,0,0,0,0}; rep(a,9); u8 b[]={5,0,0,0x05,0,0,0,0,0}; rep(b,9); }
  chk("Shift held on keyboard 1, keyboard 2 types b: Shift stays down, \"B\" not typed as b", kdown[0x2A]&&!strcmp(kq,"b"));
  { u8 a[]={1,0,0,0,0,0,0,0,0}; rep(a,9); u8 b[]={5,0,0,0,0,0,0,0,0}; rep(b,9); }
  chk("keyboard 1 releases Shift: Shift up", !kdown[0x2A]);

  printf("== two boot keyboards on separate interfaces (devices 0 and 1) ==\n");
  reset_dev(); { u8 c[8]={0x01}; kbd_report(0,xhid_kprev[0],c); u8 x[8]={0,0,0x06}, z[8]={0}; kbd_report(1,xhid_kprev[1],x); kbd_report(1,xhid_kprev[1],z); }
  chk("Ctrl held on device 0 survives reports from device 1", kdown[0x1D]&&!strcmp(kq,"c"));
  { u8 z[8]={0}; kbd_report(0,xhid_kprev[0],z); }
  chk("device 0 releases Ctrl", !kdown[0x1D]);
  { u8 c[8]={0x01}; kbd_report(0,xhid_kprev[0],c); xhid_used[0]=0; usbk_mods_sync(); xhid_used[0]=1; memset(xhid_kprev[0],0,8); }
  chk("unplugging a device while Ctrl is held releases Ctrl", !kdown[0x1D]);
  { u8 c[8]={0x02,0,1,1,1,1,1,1}; reset_dev(); kbd_report(0,xhid_kprev[0],c); }
  chk("rollover report still updates modifiers", kdown[0x2A]);

  printf("== truncated / random reports ==\n");
  load(RD_COMBO,sizeof RD_COMBO); clr(); { u8 a[]={1,0,0}; rep(a,3); u8 b[]={1}; rep(b,1); rep(b,0); }
  chk("short reports do not invent keys", !strcmp(kq,""));
  unsigned seed=7; const u8* rds[4]={RD_BOOT,RD_LOGI_ID1,RD_NKRO,RD_COMBO}; int rdn[4]={sizeof RD_BOOT,sizeof RD_LOGI_ID1,sizeof RD_NKRO,sizeof RD_COMBO};
  for(int it=0;it<20000;it++){ if(!(it&255))load(rds[(it>>8)&3],rdn[(it>>8)&3]); u8 r[64]; int n=1+(int)((seed=seed*1103515245u+12345u)>>16)%64; for(int i=0;i<n;i++)r[i]=(u8)((seed=seed*1103515245u+12345u)>>16); rep(r,n); kqn=0; g_hkn=0; }
  chk("20000 random reports: no crash (ASan/UBSan)", 1);
  printf(ok?"ALL PASS (%d checks)\n":"FAILURES (%d checks)\n",nchk); return ok?0:1;
}
