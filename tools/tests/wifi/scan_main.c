static u8 buf[8192]; static int fl;
static void hdr(int plen){ u32 w0=(u32)plen; *(u32*)buf=w0; }
static void beacon(const u8* bssid,const u8* ies,int il){ u8* f=buf+24; memset(f,0,24); f[0]=0x80; memcpy(f+16,bssid,6); memcpy(f+10,bssid,6); u8* b=f+24; memset(b,0,12); b[10]=0x11; memcpy(b+12,ies,il); hdr(24+12+il+4); }
int main(){ int bad=0;
 u8 b1[6]={1,2,3,4,5,6}; u8 ie1[]={0,9,'H','e','m','m','a','n',0xC3,0xA4,'t', 61,22,44,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
 beacon(b1,ie1,sizeof ie1); rtw_scan_frame(buf,36);
 if(!(g_apn==1&&g_aps[0].ch==44&&strcmp(g_aps[0].ssid,"Hemman\xc3\xa4t")==0)){ bad++; printf("utf8/htop fail apn=%d ch=%d ssid=%s\n",g_apn,g_apn?g_aps[0].ch:-1,g_apn?g_aps[0].ssid:""); }
 u8 b2[6]={7,7,7,7,7,7}; u8 ie2[]={0,3,'a','b','c'}; beacon(b2,ie2,sizeof ie2); int r=rtw_scan_frame(buf,0);
 if(r!=0||g_apn!=1){ bad++; printf("settle fail r=%d apn=%d\n",r,g_apn); }
 u8 b3[6]={8,8,8,8,8,8}; u8 ie3[]={0,3,'o','l','d', 3,1,6, 221,6,0,0x50,0xF2,1,1,0}; beacon(b3,ie3,sizeof ie3); rtw_scan_frame(buf,6);
 if(!(g_apn==2&&g_aps[1].sec==2&&g_aps[1].grp==2)){ bad++; printf("wpa1 fail grp=%d sec=%d\n",g_aps[1].grp,g_aps[1].sec); }
 u8 ie4[]={0,3,'o','l','d', 3,1,6}; beacon(b3,ie4,sizeof ie4); int keep=g_aps[1].rssi; *(u32*)buf|=0; rtw_scan_frame(buf,1);
 if(g_aps[1].ch!=6){ bad++; printf("offch changed ch %d\n",g_aps[1].ch); }
 u8 ie5[]={0,4,0,0,0,0, 3,1,11}; beacon(b1,ie5,sizeof ie5); rtw_scan_frame(buf,11); 
 for(int it=0;it<200000;it++){ int n=(it*7919)%600; for(int i=0;i<n;i++)buf[24+i]=(u8)(it*31+i*17); *(u32*)buf=(u32)((it*13)%3000); buf[24]=0x80; rtw_scan_frame(buf,(u8)(it%14)); }
 printf(bad?"FAIL %d\n":"scan ok\n",bad); return bad; }
