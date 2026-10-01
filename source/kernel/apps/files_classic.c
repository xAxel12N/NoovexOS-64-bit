/* =========================================================================
   Files / About / Welcome
   ========================================================================= */
static void mini_doc(int x,int y){ fill_round(x+4,y+1,12,18,2,15,rgb(244,246,251)); for(int k=0;k<4;k++)fill_rect(x+7,y+5+k*3,7,1,rgb(184,192,208)); }
static int fl_y[FS_N],fl_idx[FS_N],fl_n=0,fl_x,fl_w; static int g_fsel=-1; static int fpv_edx,fpv_edy,fpv_edw,fpv_edh; static int g_filesnav=2;
/* ---- Windows-style folder tree (navigable) ---- */
static const struct { const char* name; short parent; } FTREE[]={
  {"Local Disk (C:)",-1}, {"System",0}, {"Programs",0}, {"Users",0}, {"Drivers",0}, {"Documents",0}, {"Downloads",0}, {"Recovery",0},
  {"Drivers",1}, {"Recover",1}, {"Tools",1}, {"Config",1},
  {"NoovexApps",2}, {"Rune",2}, {"Web Browser",2},
  {"noovex",3},
  {"Display",4}, {"Storage",4}, {"USB",4}, {"Network",4} };
#define FTREE_N ((int)(sizeof(FTREE)/sizeof(FTREE[0])))
static int g_files_tree=0, g_cwd=0;       /* tree mode + current folder node */
static int ftrow_y[24], ftrow_node[24], ftrow_isfile[24], ftrow_n=0;   /* clickable rows */
/* curated real files placed in the folder tree (node index refers to FTREE[]) */
static const struct { short node; const char* name; const char* text; } FFILE[]={
 {0,"README.txt",
  "NoovexOS 64  -  a 64-bit operating system written from scratch.\r\n"
  "================================================================\r\n\r\n"
  "Everything here was built by hand in C and x86-64 assembly:\r\n"
  "  - a custom bootloader (myboot) that boots on BIOS and UEFI\r\n"
  "  - a native 64-bit kernel (no Linux, no libc)\r\n"
  "  - the Rune programming language, compiled into the kernel\r\n"
  "  - a desktop shell with windows, dock, notifications, search\r\n"
  "  - drivers: xHCI USB (mouse/keyboard/storage), IDE/AHCI/NVMe\r\n"
  "  - a web browser, image decoders (PNG/JPEG), and DOOM\r\n"
  "  - a graphical disk manager + installer (install to disk)\r\n\r\n"
  "See VERSION.txt for build details and SYSTEM\\source.txt for the\r\n"
  "list of source files that make up the OS.\r\n"},
 {0,"boot.cfg",
  "# NoovexOS boot configuration (read by the bootloader)\r\n"
  "timeout=2\r\n" "default=0\r\n" "video=1280x720\r\n" "bootlog=4\r\n\r\n"
  "entry=NoovexOS 64\r\n" "kernel=/KERNEL.ELF\r\n" "cmdline=noovexos\r\n"},
 {0,"VERSION.txt",
  "NoovexOS 64\r\n" "Version 8.13\r\n" "Architecture: x86-64 (long mode)\r\n"
  "Bootloader: myboot (BIOS + UEFI)\r\n" "Filesystem: NVXFS (disk) + FAT16/32 (USB)\r\n"
  "Toolchain: gcc -m64 -Os -ffreestanding\r\n" "Built from scratch - no external OS.\r\n"},
 {0,"LICENSE.txt",
  "NoovexOS 64\r\n" "Copyright (c) the NoovexOS author.\r\n\r\n"
  "This is a personal hobby operating system. Reference sources used\r\n"
  "for protocol documentation are credited in their driver files.\r\n"},
 {1,"kernel.info",
  "KERNEL SUBSYSTEM\r\n----------------\r\n"
  "The kernel runs in ring 0 (kernel space) with full hardware access.\r\n"
  "It provides: memory (heap allocator), the compositor, the window\r\n"
  "manager, the VFS-lite file layer, and all device drivers.\r\n\r\n"
  "There is currently no separate user space - the desktop and apps\r\n"
  "run inside the kernel (a monolithic design).\r\n"},
 {1,"swconf.dat",
  "; NoovexOS software configuration\r\n" "accent=blue\r\n" "theme=light\r\n"
  "refresh=60\r\n" "panel=lcd\r\n" "accel2d=1\r\n"},
 {8,"ide.drv",   "IDE / ATA driver\r\nLBA28 PIO read/write on the legacy ATA ports (0x1F0).\r\nUsed by NVXFS and the disk installer when the disk is in IDE mode.\r\n"},
 {8,"ahci.drv",  "AHCI (SATA) driver\r\nCommand list + FIS + PRDT over the controller's ABAR (BAR5).\r\nREAD/WRITE DMA EXT. Used for SATA SSDs/HDDs in AHCI mode.\r\n"},
 {8,"nvme.drv",  "NVMe driver\r\nAdmin + I/O submission/completion queues over PCIe.\r\nIDENTIFY, then READ/WRITE. Used for modern NVMe SSDs.\r\n"},
 {8,"xhci.drv",  "xHCI USB driver\r\nController init, device enumeration, HID (mouse/keyboard) and\r\nUSB mass storage (Bulk-Only Transport + SCSI).\r\n"},
 {9,"recover.txt","RECOVER\r\nThe graphical Recovery Environment: Continue, Safe Mode, Command\r\nPrompt, Startup Repair, System Restore, Reset, UEFI settings.\r\n"},
 {10,"diskmgr.txt","Disk Manager\r\nReads the MBR partition table, creates/deletes partitions, names\r\nvolumes, and installs NoovexOS to a chosen partition or whole disk.\r\n"},
 {11,"settings.dat","; per-user settings\r\naccent=blue\r\nwallpaper=default\r\nshow_seconds=1\r\n"},
 {12,"apps.txt", "NoovexApps: Browser, Code Editor, Paint, Calculator, Clock,\r\nCalendar, Task Manager, Snake, Flappy, and DOOM.\r\n"},
 {13,"fib.rn",
  "// fib.rn - a real Rune program, compiled by the Rune compiler\r\n"
  "fn fib(n: i32) -> i32 {\r\n"
  "    if n < 2 { return n; }\r\n"
  "    return fib(n - 1) + fib(n - 2);\r\n"
  "}\r\n\r\n"
  "fn main() -> i32 {\r\n"
  "    return fib(10);   // -> 55\r\n"
  "}\r\n"},
 {13,"hello.rn",
  "// hello.rn\r\n" "fn main() -> i32 {\r\n" "    return 42;\r\n" "}\r\n"},
 {14,"browser.info","Web Browser\r\nHTTPS via a hand-written TLS stack, HTML/CSS layout, images\r\n(PNG/JPEG/WebP), cookies, and forms.\r\n"},
 {15,"welcome.txt","Welcome to NoovexOS!\r\nThis is your home folder. Files you create in Documents and\r\nDownloads are saved to the NVXFS disk.\r\n"},
 {15,"notes.txt", "My notes\r\n---------\r\n- NoovexOS boots on BIOS and UEFI now\r\n- installs to NVMe / AHCI / IDE disks\r\n- USB mouse, keyboard and storage all work\r\n"},
 {16,"framebuffer.txt","Display / framebuffer\r\nLinear framebuffer via VBE (BIOS) or GOP (UEFI). SSE-accelerated\r\nfill/blit, 24bpp and 32bpp, dirty-rectangle compositing.\r\n"},
 {17,"storage.txt","Storage stack\r\nIDE, AHCI and NVMe behind one generic disk layer, plus FAT16/32\r\nread/write/delete on USB sticks and NVXFS on the system disk.\r\n"},
 {18,"usb.txt",   "USB stack\r\nxHCI host controller, HID boot protocol (mouse + keyboard),\r\nand Bulk-Only mass storage. Hotplug auto-opens the file manager.\r\n"},
 {19,"rtl8168.txt","Network (planned)\r\nWired NIC (Realtek RTL8168) is the reachable networking target;\r\nWiFi (RTL8821CE) is firmware-gated and not driveable from scratch.\r\n"},
 {7,"recovery.txt","Recovery partition\r\nHolds the recovery environment and tools used when the system\r\nneeds repair.\r\n"},
 {1,"source.txt",
  "NoovexOS 64 - source files\r\n==========================\r\n"
  "kernel64.c        ~5600 lines   the entire kernel + desktop + apps\r\n"
  "boot64.S                        BIOS entry / long-mode transition\r\n"
  "boot64_mb.S                     myboot ELF64 entry (BIOS + UEFI)\r\n"
  "isr64.S                         interrupt service routines\r\n"
  "kernel64.ld                     linker script (.text @1MB, .bss @12MB)\r\n"
  "rune_gfx.rn                     Rune module linked into the kernel\r\n"
  "png.h / jpeg.h                  image decoders\r\n"
  "myboot/                         the dual-firmware bootloader\r\n"
  "  stage1.S stage2.c efi.c       BIOS + UEFI boot paths\r\n"
  "rune-compiler/                  the Rune language compiler\r\n\r\n"
  "The full source is in the NoovexOS source.zip release.\r\n"},
};
#define FFILE_N ((int)(sizeof(FFILE)/sizeof(FFILE[0])))
static int files_show(int folder){ if(g_filesnav==2)return folder==0; if(g_filesnav==3)return folder==1; if(g_filesnav==4)return 0; return folder!=2; }
static u8 viewbuf[6*1024*1024]; static int view_w=0, view_h=0, view_slot=-1; static int g_imgview=-1;
static int fext_eq(const char*name,const char*e){ const char*dot=0; for(const char*q=name;*q;q++)if(*q=='.')dot=q; if(!dot)return 0; dot++; int i=0; for(;e[i];i++){ char a=dot[i]; if(a>='A'&&a<='Z')a+=32; if(a!=e[i])return 0; } return dot[i]==0; }
/* ---- ZIP (central-directory reader, DEFLATE via pz_inflate_raw) ---- */
#define ZIP_MAXE 128
static struct { char name[64]; u32 usize, csize, off; int method; } zent[ZIP_MAXE]; static int znent=0, zip_slot=-1, zsel=0, ze_y[ZIP_MAXE], ze_n=0;
static u32 zr16(const u8*p){ return (u32)p[0]|((u32)p[1]<<8); }
static u32 zr32(const u8*p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static void zip_open(int slot){
    if(slot==zip_slot) return; zip_slot=slot; znent=0; zsel=0;
    if(slot<0||!FS[slot].used) return;
    const u8* d=(const u8*)FS[slot].data; int n=FS[slot].len;
    int eo=-1; for(int i=n-22;i>=0 && i>n-22-65536;i--) if(d[i]==0x50&&d[i+1]==0x4B&&d[i+2]==0x05&&d[i+3]==0x06){ eo=i; break; }
    if(eo<0) return;
    int cnt=(int)zr16(d+eo+10); int p=(int)zr32(d+eo+16);
    for(int e=0;e<cnt && znent<ZIP_MAXE && p>=0 && p+46<=n; e++){
        if(!(d[p]==0x50&&d[p+1]==0x4B&&d[p+2]==0x01&&d[p+3]==0x02)) break;
        int fnl=(int)zr16(d+p+28), exl=(int)zr16(d+p+30), cml=(int)zr16(d+p+32);
        zent[znent].method=(int)zr16(d+p+10); zent[znent].csize=zr32(d+p+20); zent[znent].usize=zr32(d+p+24); zent[znent].off=zr32(d+p+42);
        int j=0; for(; j<fnl && j<63 && p+46+j<n; j++) zent[znent].name[j]=(char)d[p+46+j]; zent[znent].name[j]=0;
        znent++; p += 46+fnl+exl+cml;
    }
}
static int zip_extract(int slot,int idx,u8* out,int cap){
    if(slot<0||!FS[slot].used||idx<0||idx>=znent) return -1;
    const u8* d=(const u8*)FS[slot].data; int n=FS[slot].len; int p=(int)zent[idx].off;
    if(p<0||p+30>n||!(d[p]==0x50&&d[p+1]==0x4B&&d[p+2]==0x03&&d[p+3]==0x04)) return -1;
    int fnl=(int)zr16(d+p+26), exl=(int)zr16(d+p+28); int doff=p+30+fnl+exl;
    if(doff>n) return -1; int cs=(int)zent[idx].csize; if(doff+cs>n)cs=n-doff;
    if(zent[idx].method==0){ int L=cs; if(L>cap)L=cap; for(int i=0;i<L;i++)out[i]=d[doff+i]; return L; }
    if(zent[idx].method==8) return pz_inflate_raw(d+doff, cs, out, cap);
    return -1;
}
/* ================= .pptx slide viewer (unzip -> extract <a:t> text per slide) ================= */
#define PPTX_MAXSLIDE 48
static u8   pptx_scratch[200000];
static char pptx_slide[PPTX_MAXSLIDE][2000];
static int  pptx_ns=0, pptx_cur=0, pptx_slot=-1;
/* pptx image support */
#define PPTX_MAXIMG 4
struct pptximg { long ex,ey,ecx,ecy; int zidx; };
static struct pptximg pptx_img[PPTX_MAXSLIDE][PPTX_MAXIMG]; static int pptx_nimg[PPTX_MAXSLIDE];
static long pptx_emu_w=9144000, pptx_emu_h=6858000;
#define PPTXIMG_MAXW 420
#define PPTXIMG_MAXH 320
static u8  pptx_ibuf[PPTX_MAXIMG][PPTXIMG_MAXW*PPTXIMG_MAXH*4]; static int pptx_iw[PPTX_MAXIMG], pptx_ih[PPTX_MAXIMG];
static u8  pptx_media[2*1024*1024]; static int pptx_cached=-1, pptx_ncached=0;
static long xml_attr(const u8* d,int a,int b,const char* key){   /* find key="NUM" within [a,b) -> NUM or -1 */
    int kl=0; while(key[kl])kl++;
    for(int i=a;i+kl<b;i++){ int m=1; for(int k=0;k<kl;k++) if(d[i+k]!=(u8)key[k]){m=0;break;} if(!m)continue;
        int j=i+kl; while(j<b&&(d[j]=='"'||d[j]=='\''||d[j]==' '||d[j]=='='))j++; long v=0; int any=0; while(j<b&&d[j]>='0'&&d[j]<='9'){v=v*10+(d[j]-'0');j++;any=1;} if(any)return v; }
    return -1;
}
static void pptx_blit(const u8* rgba,int iw,int ih,int dx,int dy,int dw,int dh){
    if(iw<=0||ih<=0||dw<=0||dh<=0)return;
    for(int y=0;y<dh;y++){ int sy=ih*y/dh; if(sy>=ih)sy=ih-1; const u8* srow=rgba+(u64)sy*iw*4;
        for(int x=0;x<dw;x++){ int sx=iw*x/dw; if(sx>=iw)sx=iw-1; const u8* p=srow+sx*4; if(p[3]>16) putpx(dx+x,dy+y,rgb(p[0],p[1],p[2])); } }
}
static void pptx_load(int slot){
    if(slot==pptx_slot) return; pptx_slot=slot; pptx_ns=0; pptx_cur=0;
    if(slot<0||!FS[slot].used) return;
    int prev=zip_slot; zip_slot=-1; zip_open(slot); (void)prev;
    pptx_cached=-1;
    /* slide size from presentation.xml (default 10" x 7.88") */
    pptx_emu_w=9144000; pptx_emu_h=6858000;
    { int pi=-1; for(int e=0;e<znent;e++) if(seq(zent[e].name,"ppt/presentation.xml")){pi=e;break;}
      if(pi>=0){ int L=zip_extract(slot,pi,pptx_scratch,(int)sizeof(pptx_scratch));
        if(L>0){ long cx=xml_attr(pptx_scratch,0,L,"sldSz cx="); if(cx<0)cx=xml_attr(pptx_scratch,0,L,"cx="); long cy=xml_attr(pptx_scratch,0,L,"cy=");
            if(cx>100000)pptx_emu_w=cx; if(cy>100000)pptx_emu_h=cy; } } }
    for(int nsl=1; nsl<=PPTX_MAXSLIDE; nsl++){
        char want[40]; char* w=want; w=scopy(w,"ppt/slides/slide"); w=udec(w,(u32)nsl); w=scopy(w,".xml"); *w=0;
        int idx=-1; for(int e=0;e<znent;e++) if(seq(zent[e].name,want)){ idx=e; break; }
        if(idx<0) break;                                   /* slides are contiguous 1..N */
        int L=zip_extract(slot,idx,pptx_scratch,(int)sizeof(pptx_scratch)); if(L<=0){ continue; }
        pptx_nimg[pptx_ns]=0;
        /* --- images: find <p:pic> blocks, read a:off/a:ext + r:embed, resolve via the .rels --- */
        { /* load this slide's relationships */
          char rn[56]; char* r=rn; r=scopy(r,"ppt/slides/_rels/slide"); r=udec(r,(u32)nsl); r=scopy(r,".xml.rels"); *r=0;
          int ridx=-1; for(int e=0;e<znent;e++) if(seq(zent[e].name,rn)){ridx=e;break;}
          static u8 relbuf[16384]; int rl=0; if(ridx>=0) rl=zip_extract(slot,ridx,relbuf,(int)sizeof(relbuf)); if(rl<0)rl=0;
          for(int p=0;p+6<L && pptx_nimg[pptx_ns]<PPTX_MAXIMG;p++){
            if(pptx_scratch[p]=='<'&&pptx_scratch[p+1]=='p'&&pptx_scratch[p+2]==':'&&pptx_scratch[p+3]=='p'&&pptx_scratch[p+4]=='i'&&pptx_scratch[p+5]=='c'){
                int e=p; { int depth=0; for(int z=p;z+6<L;z++){ if(pptx_scratch[z]=='<'&&pptx_scratch[z+1]=='p'&&pptx_scratch[z+2]==':'&&pptx_scratch[z+3]=='p'&&pptx_scratch[z+4]=='i'&&pptx_scratch[z+5]=='c')depth++; if(pptx_scratch[z]=='<'&&pptx_scratch[z+1]=='/'&&pptx_scratch[z+2]=='p'&&pptx_scratch[z+3]==':'&&pptx_scratch[z+4]=='p'&&pptx_scratch[z+5]=='i'&&pptx_scratch[z+6]=='c'){depth--; if(depth<=0){e=z;break;}} } }
                long ox=xml_attr(pptx_scratch,p,e,"x="), oy=xml_attr(pptx_scratch,p,e,"y="), cx=xml_attr(pptx_scratch,p,e,"cx="), cy=xml_attr(pptx_scratch,p,e,"cy=");
                /* r:embed="rIdN" -> find in relbuf: Id="rIdN" ... Target="../media/imageM.ext" */
                int zi=-1; for(int q=p;q+8<e;q++){ if(pptx_scratch[q]=='r'&&pptx_scratch[q+1]==':'&&pptx_scratch[q+2]=='e'&&pptx_scratch[q+3]=='m'&&pptx_scratch[q+4]=='b'&&pptx_scratch[q+5]=='e'&&pptx_scratch[q+6]=='d'){
                    int j=q+7; while(j<e&&(pptx_scratch[j]=='='||pptx_scratch[j]=='"'||pptx_scratch[j]==' '))j++; char rid[16]; int rl2=0; while(j<e&&pptx_scratch[j]!='"'&&rl2<15)rid[rl2++]=(char)pptx_scratch[j++]; rid[rl2]=0;
                    /* find Id="rid" in relbuf, get its Target */
                    for(int t=0;t+4<rl;t++){ if(relbuf[t]=='I'&&relbuf[t+1]=='d'&&relbuf[t+2]=='='&&relbuf[t+3]=='"'){ int u=t+4; int mm=1; for(int k=0;rid[k];k++){ if(relbuf[u+k]!=(u8)rid[k]){mm=0;break;} } if(mm && relbuf[u+rl2]=='"'){
                        /* find Target=" after this */ for(int v=t; v+8<rl; v++){ if(relbuf[v]=='T'&&relbuf[v+1]=='a'&&relbuf[v+2]=='r'&&relbuf[v+3]=='g'&&relbuf[v+4]=='e'&&relbuf[v+5]=='t'&&relbuf[v+6]=='='){ int x2=v+7; while(x2<rl&&(relbuf[x2]=='"'||relbuf[x2]==' '))x2++; char tgt[64]; int tl=0; while(x2<rl&&relbuf[x2]!='"'&&tl<63)tgt[tl++]=(char)relbuf[x2++]; tgt[tl]=0;
                            /* resolve "../media/imageN.ext" -> "ppt/media/imageN.ext" */
                            char full[80]; char* fp=full; const char* base="ppt/media/"; const char* mm2=tgt; { const char* sl=0; for(const char* c=tgt;*c;c++) if(*c=='/')sl=c; if(sl)mm2=sl+1; } fp=scopy(fp,(char*)base); fp=scopy(fp,(char*)mm2); *fp=0;
                            for(int me=0;me<znent;me++) if(seq(zent[me].name,full)){ zi=me; break; }
                                break;
                            } /* if Target */
                        } /* for v */
                        break;
                    } /* if(mm && Id matches) */
                    } /* if Id== */
                    } /* for t */
                    break;
                } /* if r:embed */
                } /* for q */
                if(zi>=0 && ox>=0 && cx>0){ struct pptximg* im=&pptx_img[pptx_ns][pptx_nimg[pptx_ns]]; im->ex=ox; im->ey=oy<0?0:oy; im->ecx=cx; im->ecy=cy<0?cx:cy; im->zidx=zi; pptx_nimg[pptx_ns]++; }
                p=e;
            } /* if <p:pic> */
          } /* for p */
        } /* rels block */
        char* o=pptx_slide[pptx_ns]; int op=0;
        for(int i=0;i<L;i++){
            /* paragraph break on <a:p> */
            if(i+4<L && pptx_scratch[i]=='<'&&pptx_scratch[i+1]=='a'&&pptx_scratch[i+2]==':'&&pptx_scratch[i+3]=='p'&&(pptx_scratch[i+4]=='>'||pptx_scratch[i+4]==' '||pptx_scratch[i+4]=='/')){
                if(op>0 && o[op-1]!='\n' && op<1998) o[op++]='\n';
            }
            /* text run <a:t> ... </a:t> */
            else if(i+4<L && pptx_scratch[i]=='<'&&pptx_scratch[i+1]=='a'&&pptx_scratch[i+2]==':'&&pptx_scratch[i+3]=='t'&&(pptx_scratch[i+4]=='>'||pptx_scratch[i+4]==' ')){
                int j=i+4; while(j<L&&pptx_scratch[j]!='>')j++; j++;
                while(j+4<L && !(pptx_scratch[j]=='<'&&pptx_scratch[j+1]=='/'&&pptx_scratch[j+2]=='a'&&pptx_scratch[j+3]==':'&&pptx_scratch[j+4]=='t')){
                    char c=(char)pptx_scratch[j];
                    if(c=='&'){ /* decode the basic XML entities */
                        if(j+4<L&&pptx_scratch[j+1]=='a'&&pptx_scratch[j+2]=='m'&&pptx_scratch[j+3]=='p'&&pptx_scratch[j+4]==';'){c='&';j+=4;}
                        else if(j+3<L&&pptx_scratch[j+1]=='l'&&pptx_scratch[j+2]=='t'&&pptx_scratch[j+3]==';'){c='<';j+=3;}
                        else if(j+3<L&&pptx_scratch[j+1]=='g'&&pptx_scratch[j+2]=='t'&&pptx_scratch[j+3]==';'){c='>';j+=3;}
                        else if(j+5<L&&pptx_scratch[j+1]=='q'&&pptx_scratch[j+2]=='u'&&pptx_scratch[j+3]=='o'&&pptx_scratch[j+4]=='t'&&pptx_scratch[j+5]==';'){c='"';j+=5;}
                        else if(j+5<L&&pptx_scratch[j+1]=='a'&&pptx_scratch[j+2]=='p'&&pptx_scratch[j+3]=='o'&&pptx_scratch[j+4]=='s'&&pptx_scratch[j+5]==';'){c='\'';j+=5;}
                    }
                    if(op<1998) o[op++]=c;
                    j++;
                }
                i=j;
            }
        }
        o[op]=0; pptx_ns++; if(pptx_ns>=PPTX_MAXSLIDE) break;
    }
    zip_slot=-1;   /* leave the zip cache clean */
}
static int ppt_row_y[PPTX_MAXSLIDE];
static void pptx_decode_cur(void){
    if(pptx_cur==pptx_cached) return; pptx_cached=pptx_cur; pptx_ncached=0;
    int s=pptx_cur; if(s<0||s>=pptx_ns) return;
    for(int k=0;k<pptx_nimg[s] && k<PPTX_MAXIMG;k++){ pptx_iw[k]=0;
        int mlen=zip_extract(pptx_slot,pptx_img[s][k].zidx,pptx_media,(int)sizeof(pptx_media)); if(mlen<=0) continue;
        int iw=0,ih=0,r=-1;
        if(pptx_media[0]==0x89&&pptx_media[1]==0x50) r=png_decode(pptx_media,mlen,PNG_RAW,PNG_RAW_SZ,viewbuf,(int)sizeof(viewbuf),&iw,&ih);
        else if(pptx_media[0]==0xFF&&pptx_media[1]==0xD8) r=jpeg_decode(pptx_media,mlen,viewbuf,(int)sizeof(viewbuf),&iw,&ih);
        if(r<0||iw<=0||ih<=0) continue;
        int dw=iw,dh=ih; if(dw>PPTXIMG_MAXW){dh=dh*PPTXIMG_MAXW/dw;dw=PPTXIMG_MAXW;} if(dh>PPTXIMG_MAXH){dw=dw*PPTXIMG_MAXH/dh;dh=PPTXIMG_MAXH;} if(dw<1)dw=1; if(dh<1)dh=1;
        for(int y=0;y<dh;y++){ int sy=ih*y/dh; if(sy>=ih)sy=ih-1; const u8* sr=viewbuf+(u64)sy*iw*4; u8* dr=pptx_ibuf[k]+(u64)y*dw*4;
            for(int x=0;x<dw;x++){ int sx=iw*x/dw; if(sx>=iw)sx=iw-1; const u8* p=sr+sx*4; dr[x*4]=p[0];dr[x*4+1]=p[1];dr[x*4+2]=p[2];dr[x*4+3]=p[3]; } }
        pptx_iw[k]=dw; pptx_ih[k]=dh;
    }
    pptx_ncached=pptx_nimg[s];
}
static void pptx_draw(int i){
    win_frame(i,"Presentation");
    int wx=WN[i].x, wy=WN[i].y+TITLEH, ww=WN[i].w, wh=WN[i].h-TITLEH;
    fill_rect(wx+1,wy,ww-2,wh-1,rgb(28,30,40));
    if(pptx_ns==0){ dstr_ctr(wx+ww/2,wy+wh/2-8,"No slides found in this .pptx",rgb(200,205,220),1); return; }
    if(pptx_cur<0)pptx_cur=0; if(pptx_cur>=pptx_ns)pptx_cur=pptx_ns-1;
    /* slide surface */
    int sx=wx+40, sy=wy+30, sw=ww-80, sh=wh-90;
    fill_round(sx,sy,sw,sh,10,15,rgb(250,250,252));
    fill_rect(sx,sy,sw,6,rgb(200,80,60));   /* PowerPoint-ish accent bar */
    /* render the slide text: first non-empty line = title (big), rest = bullets */
    const char* t=pptx_slide[pptx_cur]; int ty=sy+30; int line=0;
    char ln[128]; int lp=0; int shown=0;
    for(int k=0;;k++){ char c=t[k];
        if(c=='\n'||c==0){ ln[lp]=0;
            if(lp>0){ if(line==0){ dstr(sx+30,ty,ln,rgb(30,34,48),3); ty+=44; } else { dstr(sx+40,ty,"\x95",rgb(200,80,60),1); dstr(sx+58,ty,ln,rgb(50,54,68),2); ty+=30; } line++; shown++; }
            lp=0; if(ty>sy+sh-24) break; if(c==0)break; }
        else if(lp<126) ln[lp++]=c;
    }
    if(!shown) dstr(sx+30,sy+40,"(this slide has no text)",rgb(150,155,170),1);
    /* images: decode current slide's images (once) + draw at their real slide coordinates */
    pptx_decode_cur();
    for(int k=0;k<pptx_ncached && k<PPTX_MAXIMG;k++){ if(pptx_iw[k]<=0)continue;
        struct pptximg* im=&pptx_img[pptx_cur][k];
        int dx=sx + (int)((long long)im->ex * sw / pptx_emu_w);
        int dy=sy + (int)((long long)im->ey * sh / pptx_emu_h);
        int dw=(int)((long long)im->ecx * sw / pptx_emu_w);
        int dh=(int)((long long)im->ecy * sh / pptx_emu_h);
        if(dx<sx)dx=sx; if(dy<sy)dy=sy; if(dw>sx+sw-dx)dw=sx+sw-dx; if(dh>sy+sh-dy)dh=sy+sh-dy;
        if(dw>2&&dh>2) pptx_blit(pptx_ibuf[k], pptx_iw[k], pptx_ih[k], dx, dy, dw, dh);
    }
    /* footer: slide counter + nav hint */
    char cnt[40]; char* p=cnt; p=scopy(p,"Slide ");p=udec(p,(u32)(pptx_cur+1));p=scopy(p," / ");p=udec(p,(u32)pptx_ns);*p=0;
    dstr(wx+40,wy+wh-30,cnt,rgb(200,205,220),1);
    dstr(wx+ww-260,wy+wh-30,"<- ->  or click to change slide",rgb(150,156,172),1);
    /* prev/next buttons */
    fill_round(wx+ww-70,wy+wh-34,28,24,6,15,rgb(60,64,84)); dstr(wx+ww-62,wy+wh-30,">",rgb(255,255,255),1);
    fill_round(wx+ww-104,wy+wh-34,28,24,6,15,rgb(60,64,84)); dstr(wx+ww-96,wy+wh-30,"<",rgb(255,255,255),1);
}
static void pptx_key(int c){ if(c==19||c==' '||c=='\r'||c=='\n'){ if(pptx_cur<pptx_ns-1)pptx_cur++; } else if(c==18||c==8){ if(pptx_cur>0)pptx_cur--; } }
static void pptx_click(int px,int py){ int wx=WN[W_PPTX].x, wy=WN[W_PPTX].y+TITLEH, ww=WN[W_PPTX].w, wh=WN[W_PPTX].h-TITLEH;
    if(in_rect(px,py,wx+ww-70,wy+wh-34,28,24)){ if(pptx_cur<pptx_ns-1)pptx_cur++; return; }
    if(in_rect(px,py,wx+ww-104,wy+wh-34,28,24)){ if(pptx_cur>0)pptx_cur--; return; }
    if(px>wx+ww/2){ if(pptx_cur<pptx_ns-1)pptx_cur++; } else { if(pptx_cur>0)pptx_cur--; }   /* click right half = next */
}
static const char* file_type_name(const char*name){
    if(fext_eq(name,"c"))return "C source"; if(fext_eq(name,"h"))return "C header";
    if(fext_eq(name,"cpp")||fext_eq(name,"cc")||fext_eq(name,"cxx"))return "C++ source"; if(fext_eq(name,"hpp"))return "C++ header";
    if(fext_eq(name,"json"))return "JSON"; if(fext_eq(name,"js"))return "JavaScript"; if(fext_eq(name,"ts"))return "TypeScript";
    if(fext_eq(name,"css"))return "CSS"; if(fext_eq(name,"html")||fext_eq(name,"htm"))return "HTML"; if(fext_eq(name,"xml"))return "XML";
    if(fext_eq(name,"md"))return "Markdown"; if(fext_eq(name,"txt"))return "Text"; if(fext_eq(name,"py"))return "Python";
    if(fext_eq(name,"sh"))return "Shell script"; if(fext_eq(name,"rs"))return "Rust"; if(fext_eq(name,"go"))return "Go";
    if(fext_eq(name,"s")||fext_eq(name,"asm"))return "Assembly"; if(fext_eq(name,"log"))return "Log"; if(fext_eq(name,"csv"))return "CSV";
    if(fext_eq(name,"cfg")||fext_eq(name,"ini")||fext_eq(name,"conf")||fext_eq(name,"toml"))return "Config";
    if(fext_eq(name,"yml")||fext_eq(name,"yaml"))return "YAML"; if(fext_eq(name,"ld"))return "Linker script"; if(fext_eq(name,"mk"))return "Makefile";
    if(fext_eq(name,"png")||fext_eq(name,"jpg")||fext_eq(name,"jpeg"))return "Image"; if(fext_eq(name,"gif")||fext_eq(name,"bmp"))return "Image (unsupported)";
    if(fext_eq(name,"zip")||fext_eq(name,"jar")||fext_eq(name,"apk"))return "ZIP archive";
    if(fext_eq(name,"gz")||fext_eq(name,"tgz")||fext_eq(name,"tar"))return "Archive";
    if(fext_eq(name,"pdf"))return "PDF"; if(fext_eq(name,"iso")||fext_eq(name,"img"))return "Disk image"; if(fext_eq(name,"exe"))return "Executable";
    return "File";
}
static int file_kind(const char*name){                    /* 0 text/code  1 image  2 html  3 binary  4 archive */
    if(fext_eq(name,"png")||fext_eq(name,"jpg")||fext_eq(name,"jpeg")) return 1;
    if(fext_eq(name,"html")||fext_eq(name,"htm")) return 2;
    if(fext_eq(name,"zip")||fext_eq(name,"jar")||fext_eq(name,"apk")) return 4;
    if(fext_eq(name,"iso")||fext_eq(name,"img")||fext_eq(name,"bin")||fext_eq(name,"exe")||fext_eq(name,"gz")||fext_eq(name,"tgz")||fext_eq(name,"tar")||fext_eq(name,"pdf")||fext_eq(name,"mp3")||fext_eq(name,"mp4")||fext_eq(name,"dmg")||fext_eq(name,"deb")||fext_eq(name,"rpm")||fext_eq(name,"gif")||fext_eq(name,"bmp")||fext_eq(name,"o")||fext_eq(name,"so")) return 3;
    return 0;
}
static int fkind(int slot){   /* content-aware kind for an FS slot (magic bytes beat the name) */
    if(slot<0||!FS[slot].used) return 3;
    const unsigned char* d=(const unsigned char*)FS[slot].data; int n=FS[slot].len;
    if(n>=4 && d[0]==0x89&&d[1]==0x50&&d[2]==0x4E&&d[3]==0x47) return 1;   /* PNG */
    if(n>=12 && d[0]=='R'&&d[1]=='I'&&d[2]=='F'&&d[3]=='F'&&d[8]=='W'&&d[9]=='E'&&d[10]=='B'&&d[11]=='P') return 1; /* WebP */
    if(n>=3 && d[0]==0xFF&&d[1]==0xD8&&d[2]==0xFF) return 1;               /* JPEG */
    if(n>=4 && d[0]==0x50&&d[1]==0x4B&&d[2]==0x03&&d[3]==0x04) return 4;   /* ZIP */
    return file_kind(FS[slot].name);
}
static int ensure_img(int slot){
    if(slot==view_slot) return view_w>0;
    view_slot=slot; view_w=0; view_h=0; if(slot<0||!FS[slot].used) return 0;
    const u8* d=(const u8*)FS[slot].data; int n=FS[slot].len, w=0,h=0,r=-1;
    if(n>=4 && d[0]==0x89 && d[1]==0x50) r=png_decode(d,n,PNG_RAW,PNG_RAW_SZ,viewbuf,(int)sizeof(viewbuf),&w,&h);
    else if(n>=3 && d[0]==0xFF && d[1]==0xD8) r=jpeg_decode(d,n,viewbuf,(int)sizeof(viewbuf),&w,&h);
    else if(is_webp(d,n)) r=webp_decode(d,n,viewbuf,(int)sizeof(viewbuf),&w,&h);
    if(r==0 && w>0 && h>0){ view_w=w; view_h=h; return 1; } return 0;
}
static void blit_img_fit(int ox,int oy,int aw,int ah){    /* scale viewbuf into aw x ah, keep aspect, centred */
    if(view_w<=0||view_h<=0||aw<=0||ah<=0)return; int scn=1000;
    if((u32)view_w*(u32)scn/1000>(u32)aw) scn=aw*1000/view_w; if((u32)view_h*(u32)scn/1000>(u32)ah) scn=ah*1000/view_h;
    int dw=view_w*scn/1000, dh=view_h*scn/1000; if(dw<1)dw=1; if(dh<1)dh=1; int x0=ox+(aw-dw)/2, y0=oy+(ah-dh)/2;
    for(int r=0;r<dh;r++){ int sy=r*view_h/dh; u8* srow=viewbuf+(u64)sy*view_w*4;
        for(int c=0;c<dw;c++){ int sx=c*view_w/dw; u8* p=srow+sx*4; putpx(x0+c,y0+r,rgb(p[0],p[1],p[2])); } }
}
static int ivx,ivy,ivw,ivh;
static void img_viewer_draw(void){
    if(g_imgview<0)return; int ok=ensure_img(g_imgview);
    for(u32 y=0;y<H;y++)for(u32 x=0;x<W;x++){ u32*p=&SCENE[(u64)y*SW+x]; *p=mix(*p,rgb(8,10,18),150); }
    ivw=(int)W*72/100; ivh=(int)H*74/100; ivx=((int)W-ivw)/2; ivy=((int)H-ivh)/2;
    shadow_soft(ivx,ivy,ivw,ivh,14); fill_round(ivx,ivy,ivw,ivh,14,15,rgb(24,26,34));
    dstr(ivx+16,ivy+12,FS[g_imgview].used?FS[g_imgview].name:"image",rgb(236,240,250),1);
    int cxb=ivx+ivw-30, cyb=ivy+8; fill_round(cxb,cyb,22,22,6,15,rgb(52,56,68));
    for(int t=-5;t<=5;t++){ putpx(cxb+11+t,cyb+11+t,rgb(230,232,240)); putpx(cxb+11+t,cyb+11-t,rgb(230,232,240)); }
    if(ok){ char b[40],*p=b; p=udec(p,(u32)view_w);*p++='x';p=udec(p,(u32)view_h);*p=0; dstr(ivx+16,ivy+ivh-20,b,rgb(150,158,180),1);
        blit_img_fit(ivx+14,ivy+38,ivw-28,ivh-64); }
    else dstr_ctr(ivx+ivw/2,ivy+ivh/2-6,"Cannot display this image",rgb(210,150,150),1);
}
static u32 g_ram_mb=0;
static u32 detect_ram_mb(void){
    u32 top=64;
    for(u32 mb=128; mb<=4032; mb+=64){
        volatile u32* p=(volatile u32*)((u64)mb<<20);
        u32 save=*p; u32 pat=0x51A5C3E7u ^ (mb*2654435761u);
        *p=pat; __asm__ volatile("wbinvd":::"memory");
        int ok=(*p==pat); *p=save;
        if(ok) top=mb+64; else break;
    }
    return top;
}
static char* fmt_size(char* p,long b){ if(b>=1073741824L){ p=udec(p,(u32)(b/1073741824L)); *p++='.'; *p++=(char)('0'+(int)((b%1073741824L)/107374183L)); p=scopy(p," GB"); } else if(b>=1048576){ p=udec(p,(u32)(b/1048576)); *p++='.'; *p++=(char)('0'+(int)((b%1048576)*10/1048576)); p=scopy(p," MB"); } else if(b>=1024){ p=udec(p,(u32)(b/1024)); p=scopy(p," KB"); } else { p=udec(p,(u32)b); p=scopy(p," B"); } return p; }
static int g_drv_x[3],g_drv_y[3],g_drv_w,g_drv_tgt[3],g_drv_n;
static void draw_drive_tile(int x,int y,int w,int h,const char* name,long used,long total,int cloud,int hov){
    fill_round(x,y,w,h,9,15,hov?rgb(238,242,249):rgb(250,251,253));
    for(int t=0;t<w;t++){ if(!cut(t,0,w,h,9,3)){u32*p=&SCENE[(u64)y*SW+x+t];*p=mix(*p,rgb(220,224,232),200);} if(!cut(t,h-1,w,h,9,12)){u32*p=&SCENE[(u64)(y+h-1)*SW+x+t];*p=mix(*p,rgb(220,224,232),200);} }
    int ix=x+16, iy=y+h/2-16;                          /* drive icon */
    fill_round(ix,iy,48,32,4,15,cloud==1?mix(ACC,rgb(255,255,255),110):cloud==2?rgb(90,160,110):rgb(150,158,176));
    fill_round(ix+6,iy+6,36,13,2,15,rgb(78,88,108)); fill_circle(ix+10,iy+26,3,rgb(120,220,140));
    dstr(x+78,y+14,name,INK,1);
    int bx=x+78,by=y+h-30,bw=w-98,bh=9; fill_round(bx,by,bw,bh,4,15,rgb(224,228,236));
    if(cloud==1){ if(bw/6>0)fill_round(bx,by,bw/6,bh,4,15,ACC); }
    else { long pct=total>0?(used*bw/total):0; if(used>0&&pct<3)pct=3; if(pct>bw)pct=bw; if(pct>0)fill_round(bx,by,(int)pct,bh,4,15,cloud==2?rgb(90,160,110):ACC); }
    char b[80],*p=b; if(cloud==1){ p=scopy(p,"Synced to your account"); } else { p=fmt_size(p,total-used); p=scopy(p," free of "); p=fmt_size(p,total); } *p=0;
    dstr(x+78,y+h-14,b,rgb(120,128,146),1);
}
