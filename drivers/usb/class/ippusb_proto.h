#ifndef NVX_IPPUSB_PROTO_H
#define NVX_IPPUSB_PROTO_H
struct uprn_if { int ifn, alt, proto, ie, oe, im, om; };
static inline int upp_parse_cfg(const unsigned char* c,int n,struct uprn_if* o,int max){
    int k=-1, cnt=0;
    for(int i=0;i+1<n;){ int bl=c[i]; if(bl<2||i+bl>n)break;
        if(c[i+1]==4&&bl>=9){ k=-1; if(c[i+5]==7&&c[i+6]==1&&cnt<max){ k=cnt++; o[k].ifn=c[i+2]; o[k].alt=c[i+3]; o[k].proto=c[i+7]; o[k].ie=o[k].oe=o[k].im=o[k].om=0; } }
        else if(c[i+1]==5&&bl>=7&&k>=0&&(c[i+3]&3)==2){ int a=c[i+2], m=(c[i+4]|(c[i+5]<<8))&0x7FF; if(a&0x80){ if(!o[k].ie){ o[k].ie=a; o[k].im=m; } } else if(!o[k].oe){ o[k].oe=a; o[k].om=m; } }
        i+=bl; }
    int w=0; for(int j=0;j<cnt;j++) if(o[j].oe&&(o[j].ie||o[j].proto==1))o[w++]=o[j];
    return w; }
static inline int upp_pick(const struct uprn_if* f,int n,int ipp){ for(int j=0;j<n;j++) if(ipp?(f[j].proto==4&&f[j].ie):(f[j].proto!=4))return j; return -1; }
static inline int upp_up(int c){ return c>='a'&&c<='z'?c-32:c; }
static inline int upp_devid(const char* id,const char* key,char* out,int cap){
    int kl=0; while(key[kl])kl++; out[0]=0;
    for(int i=0;id[i];){ while(id[i]==' '||id[i]==';')i++; int s=i; while(id[i]&&id[i]!=':'&&id[i]!=';')i++; if(id[i]!=':')continue;
        int ok=(i-s==kl); for(int q=0;ok&&q<kl;q++) if(upp_up(id[s+q])!=key[q])ok=0; i++;
        int v=i; while(id[i]&&id[i]!=';')i++;
        if(ok){ int n=0; for(int q=v;q<i&&n<cap-1;q++)out[n++]=id[q]; out[n]=0; return n; } }
    return 0; }
static inline int upp_devid_len(const unsigned char* b,int n){ if(n<2)return 0; int l=(b[0]<<8)|b[1]; if(l>n||l<2)l=(b[1]<<8)|b[0]; if(l>n||l<2)return 0; return l-2; }
struct uph { int status, hlen, clen, chunked, blen, close; };
static inline int upp_ci(const unsigned char* a,const char* b,int n){ for(int i=0;i<n;i++) if(upp_up(a[i])!=upp_up(b[i]))return 0; return 1; }
static inline int upp_hdr(const unsigned char* b,int hlen,const char* name,char* out,int cap){
    int nl=0; while(name[nl])nl++; out[0]=0;
    for(int i=0;i<hlen;){ int e=i; while(e<hlen&&b[e]!='\n')e++;
        if(e-i>nl&&b[i+nl]==':'&&upp_ci(b+i,name,nl)){ int v=i+nl+1; while(v<e&&b[v]==' ')v++; int z=e; while(z>v&&(b[z-1]=='\r'||b[z-1]==' '))z--; int n=0; for(int q=v;q<z&&n<cap-1;q++)out[n++]=(char)b[q]; out[n]=0; return 1; }
        i=e+1; }
    return 0; }
static inline int upp_hex(int c){ return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; }
static inline int upp_http(unsigned char* b,int n,struct uph* h){
    int he=-1; for(int i=0;i+3<n;i++) if(b[i]=='\r'&&b[i+1]=='\n'&&b[i+2]=='\r'&&b[i+3]=='\n'){ he=i+4; break; }
    if(he<0)return 0;
    if(n<12||b[0]!='H'||b[1]!='T'||b[2]!='T'||b[3]!='P')return -1;
    h->status=(b[9]-'0')*100+(b[10]-'0')*10+(b[11]-'0'); h->hlen=he; h->clen=-1; h->chunked=0; h->blen=0; h->close=0;
    char v[32]; if(upp_hdr(b,he,"Content-Length",v,32)){ int x=0; for(int i=0;v[i]>='0'&&v[i]<='9';i++)x=x*10+(v[i]-'0'); h->clen=x; }
    if(upp_hdr(b,he,"Transfer-Encoding",v,32)&&upp_ci((const unsigned char*)v,"chunked",7))h->chunked=1;
    if(upp_hdr(b,he,"Connection",v,32)&&upp_ci((const unsigned char*)v,"close",5))h->close=1;
    if(h->status==204||h->status==304||(h->status>=100&&h->status<200)){ h->blen=0; return 1; }
    if(h->chunked){ for(int pass=0;pass<2;pass++){ int r=he, w=he;
        for(;;){ int sz=0, d=0; while(r<n&&upp_hex(b[r])>=0){ sz=sz*16+upp_hex(b[r]); r++; d++; if(sz>(1<<26))return -1; }
            if(!d)return r<n?-1:0;
            while(r<n&&b[r]!='\n')r++; if(r>=n)return 0; r++;
            if(!sz){ if(pass){ h->blen=w-he; return 1; } break; }
            if(r+sz+2>n)return 0;
            if(pass) for(int i=0;i<sz;i++)b[w+i]=b[r+i];
            w+=sz; r+=sz+2; } } }
    if(h->clen>=0){ if(n-he<h->clen)return 0; h->blen=h->clen; return 1; }
    return 0; }
static inline int upp_http_eof(int n,struct uph* h){ if(h->hlen>0&&h->clen<0&&!h->chunked){ h->blen=n-h->hlen; return 1; } return 0; }
static inline unsigned char* upp_p8(unsigned char* p,unsigned char* e,int v){ if(p<e)*p++=(unsigned char)v; return p; }
static inline unsigned char* upp_p16(unsigned char* p,unsigned char* e,int v){ p=upp_p8(p,e,(v>>8)&255); return upp_p8(p,e,v&255); }
static inline unsigned char* upp_attr(unsigned char* p,unsigned char* e,int tag,const char* nm,const char* v){
    int nl=0, vl=0; while(nm[nl])nl++; while(v[vl])vl++; p=upp_p8(p,e,tag); p=upp_p16(p,e,nl); for(int i=0;i<nl;i++)p=upp_p8(p,e,nm[i]); p=upp_p16(p,e,vl); for(int i=0;i<vl;i++)p=upp_p8(p,e,v[i]); return p; }
static inline int upp_ipp_getattrs(unsigned char* o,int cap,const char* uri,unsigned reqid){
    unsigned char* p=o; unsigned char* e=o+cap;
    p=upp_p16(p,e,0x0200); p=upp_p16(p,e,0x000B); p=upp_p16(p,e,(int)(reqid>>16)); p=upp_p16(p,e,(int)(reqid&0xFFFF)); p=upp_p8(p,e,1);
    p=upp_attr(p,e,0x47,"attributes-charset","utf-8"); p=upp_attr(p,e,0x48,"attributes-natural-language","en"); p=upp_attr(p,e,0x45,"printer-uri",uri);
    p=upp_attr(p,e,0x44,"requested-attributes","printer-make-and-model"); p=upp_attr(p,e,0x44,"","document-format-supported"); p=upp_attr(p,e,0x44,"","printer-state"); p=upp_attr(p,e,0x44,"","printer-state-reasons");
    p=upp_p8(p,e,3); return p<e?(int)(p-o):-1; }
struct uppinfo { int status, state, pwg, urf, pdf, jpeg; char model[96]; char reasons[64]; };
static inline int upp_seq(const unsigned char* a,int n,const char* b){ int i=0; for(;i<n;i++) if(!b[i]||a[i]!=(unsigned char)b[i])return 0; return b[i]==0; }
static inline int upp_ipp_parse(const unsigned char* b,int n,struct uppinfo* pi){
    pi->status=-1; pi->state=0; pi->pwg=pi->urf=pi->pdf=pi->jpeg=0; pi->model[0]=0; pi->reasons[0]=0;
    if(n<9)return -1;
    pi->status=(b[2]<<8)|b[3];
    int i=8; const unsigned char* last=0; int lastn=0;
    while(i<n){ int tag=b[i++]; if(tag==3)return 0; if(tag<0x10)continue; if(i+2>n)return -1;
        int nl=(b[i]<<8)|b[i+1]; i+=2; if(i+nl>n)return -1; if(nl){ last=b+i; lastn=nl; } i+=nl; if(i+2>n)return -1;
        int vl=(b[i]<<8)|b[i+1]; i+=2; if(i+vl>n)return -1; const unsigned char* v=b+i;
        if(last&&upp_seq(last,lastn,"printer-make-and-model")){ int k=0; for(;k<vl&&k<95;k++)pi->model[k]=(char)v[k]; pi->model[k]=0; }
        else if(last&&upp_seq(last,lastn,"printer-state")&&vl==4)pi->state=v[3];
        else if(last&&upp_seq(last,lastn,"printer-state-reasons")&&!pi->reasons[0]){ int k=0; for(;k<vl&&k<63;k++)pi->reasons[k]=(char)v[k]; pi->reasons[k]=0; }
        else if(last&&upp_seq(last,lastn,"document-format-supported")){
            if(upp_seq(v,vl,"image/pwg-raster"))pi->pwg=1; else if(upp_seq(v,vl,"image/urf"))pi->urf=1; else if(upp_seq(v,vl,"application/pdf"))pi->pdf=1; else if(upp_seq(v,vl,"image/jpeg"))pi->jpeg=1; }
        i+=vl; }
    return -1; }
static inline int upp_cpy(char* o,int at,int cap,const char* s){ while(*s&&at<cap-1)o[at++]=*s++; o[at]=0; return at; }
static inline int upp_dec(char* o,int at,int cap,int v){ char t[12]; int n=0; if(v<0)v=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v&&n<11); while(n&&at<cap-1)o[at++]=t[--n]; o[at]=0; return at; }
static inline int upp_escl_job(char* o,int cap,int dpi,int color,const char* fmt,int wmm,int hmm){
    int n=0; int w=wmm*3000/254, h=hmm*3000/254;
    n=upp_cpy(o,n,cap,"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<scan:ScanSettings xmlns:scan=\"http://schemas.hp.com/imaging/escl/2011/05/03\" xmlns:pwg=\"http://www.pwg.org/schemas/2010/12/sm\">\n<pwg:Version>2.0</pwg:Version>\n<pwg:ScanRegions><pwg:ScanRegion><pwg:ContentRegionUnits>escl:ThreeHundredthsOfInches</pwg:ContentRegionUnits><pwg:XOffset>0</pwg:XOffset><pwg:YOffset>0</pwg:YOffset><pwg:Width>");
    n=upp_dec(o,n,cap,w); n=upp_cpy(o,n,cap,"</pwg:Width><pwg:Height>"); n=upp_dec(o,n,cap,h);
    n=upp_cpy(o,n,cap,"</pwg:Height></pwg:ScanRegion></pwg:ScanRegions>\n<pwg:InputSource>Platen</pwg:InputSource><scan:ColorMode>");
    n=upp_cpy(o,n,cap,color?"RGB24":"Grayscale8"); n=upp_cpy(o,n,cap,"</scan:ColorMode><pwg:DocumentFormat>"); n=upp_cpy(o,n,cap,fmt);
    n=upp_cpy(o,n,cap,"</pwg:DocumentFormat><scan:DocumentFormatExt>"); n=upp_cpy(o,n,cap,fmt); n=upp_cpy(o,n,cap,"</scan:DocumentFormatExt><scan:XResolution>"); n=upp_dec(o,n,cap,dpi);
    n=upp_cpy(o,n,cap,"</scan:XResolution><scan:YResolution>"); n=upp_dec(o,n,cap,dpi); n=upp_cpy(o,n,cap,"</scan:YResolution>\n</scan:ScanSettings>\n");
    return n<cap-1?n:-1; }
static inline int upp_xml(const char* x,int n,const char* tag,int nth,char* out,int cap){
    int tl=0; while(tag[tl])tl++; out[0]=0;
    for(int i=0;i+tl+2<n;i++){ if(x[i]!='<'||x[i+1]=='/')continue; int s=i+1, e=s; while(e<n&&x[e]!='>'&&x[e]!=' '&&x[e]!='/')e++; int c=s; for(int q=s;q<e;q++) if(x[q]==':')c=q+1;
        if(e-c!=tl)continue;
        int ok=1; for(int q=0;q<tl;q++) if(x[c+q]!=tag[q])ok=0; if(!ok)continue;
        while(e<n&&x[e]!='>')e++; if(e>=n)return 0; if(x[e-1]=='/')continue; if(nth--)continue;
        int v=e+1, k=0; while(v<n&&x[v]!='<'&&k<cap-1)out[k++]=x[v++]; out[k]=0; return 1; }
    return 0; }
static inline int upp_path(const char* loc,char* out,int cap){
    int i=0; if(loc[0]=='h'&&loc[1]=='t'){ while(loc[i]&&!(loc[i]=='/'&&loc[i+1]=='/'))i++; if(loc[i])i+=2; while(loc[i]&&loc[i]!='/')i++; }
    int n=0; while(loc[i]&&n<cap-1)out[n++]=loc[i++]; out[n]=0; return n; }
#endif
