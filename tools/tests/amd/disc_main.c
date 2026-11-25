static u8 B[10240];
static void w16(u32 o,u32 v){ B[o]=(u8)v; B[o+1]=(u8)(v>>8); }
static void w32(u32 o,u32 v){ w16(o,v&0xFFFF); w16(o+2,v>>16); }
static u32 put_ip(u32 p,u16 hw,u8 inst,u8 ma,u8 mi,u8 rv,int nb,const u32* base,int b64){
    w16(p,hw); B[p+2]=inst; B[p+3]=(u8)nb; B[p+4]=ma; B[p+5]=mi; B[p+6]=rv; B[p+7]=0; p+=8;
    for(int i=0;i<nb;i++){ w32(p,base[i]); if(b64){ w32(p+4,0); p+=8; } else p+=4; } return p; }
static u32 build(int v2,int hv4,int b64){
    memset(B,0,sizeof B); w32(0,0x28211407u); w16(4,v2?2:1); u32 tl=v2?16:12; if(v2)w16(12,7);
    u32 ih=64; w16(tl,ih); u32 h=ih; w32(h,0x53445049u); w16(h+4,hv4?4:3); w16(h+12,1); w16(h+14,0); w16(h+16,160); if(hv4&&b64)B[h+78]=1;
    u32 p=160; w16(p,0); u32 nipo=p+2; p+=4; int n=0;
    const u32 gcb[]={0x1260,0xA000,0x2402C00}, dcb[]={0x12,0xC0,0x34C0,0x9000,0x2403C00}, mp1b[]={0x16000,0x2400000,0xE0000}, nbb[]={0,0x14,0xD20,0x10400,0x241B000};
    p=put_ip(p,11,0,9,3,0,3,gcb,b64); n++; p=put_ip(p,271,0,2,1,0,5,dcb,b64); n++; p=put_ip(p,1,0,12,0,0,3,mp1b,b64); n++;
    p=put_ip(p,108,0,7,2,0,5,nbb,b64); n++; p=put_ip(p,42,0,4,1,2,1,gcb,b64); n++; p=put_ip(p,42,1,4,1,2,1,gcb,b64); n++; p=put_ip(p,999,0,1,0,0,2,gcb,b64); n++;
    w16(nipo,(u32)n); w16(h+6,p-h); w16(10,p); u16 s=0; for(u32 i=10;i<p;i++)s=(u16)(s+B[i]); w16(8,s); return p; }
static int fails;
#define CK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); fails++; } }while(0)
int main(void){
    for(int m=0;m<3;m++){ int v2=m>0,hv4=m==2,b64=m==2; u32 n=build(v2,hv4,b64);
        CK(amd_disc_parse(B,sizeof B)==0); CK(amd_ipver(AMD_IP_GC)==AMD_IPV(9,3,0)); CK(amd_ipver(AMD_IP_DCN)==AMD_IPV(2,1,0));
        CK(amd_ipver(AMD_IP_MP1)==AMD_IPV(12,0,0)); CK(amd_ipver(AMD_IP_NBIO)==AMD_IPV(7,2,0)); CK(g_amd.ip[AMD_IP_SDMA0].ninst==2);
        CK(g_amd.ip[AMD_IP_DCN].base[2]==0x34C0); CK(g_amd.ip[AMD_IP_DCN].base[5]==0); CK(g_amd.ip[AMD_IP_GC].base[3]==0); CK(g_amd_disc_nips==7);
        CK(!strcmp(amd_asic_name(amd_ipver(AMD_IP_GC),0x1638),"Cezanne")); CK(ac_family(amd_ipver(AMD_IP_GC))==AMD_FAM_RV);
        B[200]^=1; CK(amd_disc_parse(B,sizeof B)==-3); B[200]^=1;
        w16(10,20000); CK(amd_disc_parse(B,sizeof B)==-2); w16(10,n);
        CK(amd_disc_parse(B,8)==-1); }
    srand(1); for(int it=0;it<200000;it++){ u32 n=build(it&1,it&2,it&2); int k=rand()%4; for(int j=0;j<k;j++)B[rand()%n]=(u8)rand();
        u16 s=0; for(u32 i=10;i<n;i++)s=(u16)(s+B[i]); if(it&4)w16(8,s); amd_disc_parse(B,sizeof B); }
    CK(!strcmp(amd_asic_name(AMD_IPV(11,0,1),0x15BF),"Phoenix")); CK(ac_family(AMD_IPV(10,3,3))==AMD_FAM_YC);
    printf(fails?"disc: %d FAILED\n":"disc: all passed\n",fails); return fails!=0; }
