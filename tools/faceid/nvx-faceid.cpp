#include <dlib/dnn.h>
#include <dlib/image_processing/frontal_face_detector.h>
#include <dlib/image_processing.h>
#include <dlib/image_io.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <sys/random.h>
#include "fid_policy.h"

using namespace dlib;

template <template <int,template<typename>class,int,typename> class block, int N, template<typename>class BN, typename SUBNET>
using residual = add_prev1<block<N,BN,1,tag1<SUBNET>>>;
template <template <int,template<typename>class,int,typename> class block, int N, template<typename>class BN, typename SUBNET>
using residual_down = add_prev2<avg_pool<2,2,2,2,skip1<tag2<block<N,BN,2,tag1<SUBNET>>>>>>;
template <int N, template <typename> class BN, int stride, typename SUBNET>
using block = BN<con<N,3,3,1,1,relu<BN<con<N,3,3,stride,stride,SUBNET>>>>>;
template <int N, typename SUBNET> using ares = relu<residual<block,N,affine,SUBNET>>;
template <int N, typename SUBNET> using ares_down = relu<residual_down<block,N,affine,SUBNET>>;
template <typename SUBNET> using alevel0 = ares_down<256,SUBNET>;
template <typename SUBNET> using alevel1 = ares<256,ares<256,ares_down<256,SUBNET>>>;
template <typename SUBNET> using alevel2 = ares<128,ares<128,ares_down<128,SUBNET>>>;
template <typename SUBNET> using alevel3 = ares<64,ares<64,ares<64,ares_down<64,SUBNET>>>>;
template <typename SUBNET> using alevel4 = ares<32,ares<32,ares<32,SUBNET>>>;
using anet_type = loss_metric<fc_no_bias<128,avg_pool_everything<alevel0<alevel1<alevel2<alevel3<alevel4<
    max_pool<3,3,2,2,relu<affine<con<32,7,7,2,2,input_rgb_image_sized<150>>>>>>>>>>>>>;

typedef matrix<float,0,1> fprint;

static const uint32_t Q_MAGIC=0x3151464E, R_MAGIC=0x3152464E;
enum { C_VERIFY=1, C_ADD=2, C_SAVE=3, C_RESET=4, C_QUIT=9 };
enum { S_NOFACE=0, S_FACE=1, S_MATCH=2, S_NOMATCH=3, S_ADDED=4, S_SAVED=5, S_NOFILE=6, S_ERROR=7, S_SMALL=8, S_MANY=9, S_TURN=10, S_LIVE=11, S_SPOOF=12 };
static const float MATCH_DIST=FP_T_MATCH;
static const int MIN_FACE=96;

struct req { uint32_t magic, seq, cmd, w, h; char user[32]; uint32_t pad[3]; };
struct res { uint32_t magic, seq, status; int32_t dist, x, y, w, h, pts[10], count, faces; };

struct engine {
    frontal_face_detector det;
    shape_predictor sp;
    anet_type net;
    std::string loaded_user;
    std::vector<fprint> stored;
    fp_enrol en;
    fp_live live;
    fprint last;
    bool have_last=false;
    int skipped=0;
};

struct shot { float pts[10], yaw, luma, eye; };

static std::string g_dir="/usr/share/noovex/faceid";
static std::string g_store="/etc/noovex/faceid";

static bool user_ok(const char* u){
    if(!u[0]) return false;
    for(int i=0;u[i];i++){ char c=u[i]; if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_')) return false; if(i>=31) return false; }
    return true;
}

static void load_models(engine& e){
    e.det=get_frontal_face_detector();
    deserialize(g_dir+"/shape_predictor_5_face_landmarks.dat")>>e.sp;
    deserialize(g_dir+"/dlib_face_recognition_resnet_model_v1.dat")>>e.net;
}

static float patch_luma(const matrix<rgb_pixel>& img,long cx,long cy,long h){
    double s=0; long n=0;
    for(long y=cy-h;y<=cy+h;y++) for(long x=cx-h;x<=cx+h;x++) if(y>=0&&x>=0&&y<img.nr()&&x<img.nc()){ const rgb_pixel& p=img(y,x); s+=0.299*p.red+0.587*p.green+0.114*p.blue; n++; }
    return n?(float)(s/n):0.0f;
}

static int face_print(engine& e,const matrix<rgb_pixel>& img,fprint& out,res& r,shot* sh=0){
    std::vector<rectangle> faces;
    if(img.nc()>=400){
        matrix<rgb_pixel> half(img.nr()/2,img.nc()/2); resize_image(img,half);
        for(const rectangle& h:e.det(half)) faces.push_back(rectangle(h.left()*2,h.top()*2,h.right()*2+1,h.bottom()*2+1));
    }
    if(faces.empty()) faces=e.det(img);
    r.faces=(int32_t)faces.size();
    if(faces.empty()) return 0;
    size_t best=0; for(size_t i=1;i<faces.size();i++) if(faces[i].area()>faces[best].area()) best=i;
    rectangle f=faces[best];
    r.x=(int32_t)f.left(); r.y=(int32_t)f.top(); r.w=(int32_t)f.width(); r.h=(int32_t)f.height();
    full_object_detection s=e.sp(img,f);
    for(unsigned k=0;k<5&&k<s.num_parts();k++){ r.pts[k*2]=(int32_t)s.part(k).x(); r.pts[k*2+1]=(int32_t)s.part(k).y(); }
    if(sh){
        for(int k=0;k<10;k++) sh->pts[k]=(float)r.pts[k];
        float pitch=0; sh->yaw=0; fp_pose(sh->pts,&sh->yaw,&pitch);
        long cx=(f.left()+f.right())/2, cy=(f.top()+f.bottom())/2, io=std::abs(r.pts[4]+r.pts[6]-r.pts[0]-r.pts[2])/2, h=std::max(2L,io/6);
        sh->luma=patch_luma(img,cx,cy,std::max(4L,(long)f.width()/4));
        float el=(patch_luma(img,(r.pts[0]+r.pts[2])/2,(r.pts[1]+r.pts[3])/2,h)+patch_luma(img,(r.pts[4]+r.pts[6])/2,(r.pts[5]+r.pts[7])/2,h))*0.5f;
        sh->eye=sh->luma>1.0f?el/sh->luma:0.0f;
    }
    matrix<rgb_pixel> chip;
    extract_image_chip(img,get_face_chip_details(s,150,0.25),chip);
    out=e.net(chip);
    return 1;
}

static std::string store_path(const char* user){ return g_store+"/"+user+".face"; }

static bool rand_bytes(uint8_t* p,size_t n){
    size_t got=0; while(got<n){ ssize_t k=getrandom(p+got,n-got,0); if(k<=0) break; got+=(size_t)k; }
    if(got==n) return true;
    int fd=open("/dev/urandom",O_RDONLY); if(fd<0) return false; got=0;
    while(got<n){ ssize_t k=read(fd,p+got,n-got); if(k<=0){ close(fd); return false; } got+=(size_t)k; }
    close(fd); return true;
}

static bool store_dir(){ mkdir("/etc/noovex",0755); mkdir(g_store.c_str(),0700); chmod(g_store.c_str(),0700); struct stat st; return stat(g_store.c_str(),&st)==0&&S_ISDIR(st.st_mode)&&st.st_uid==geteuid(); }

static bool machine_key(uint8_t key[32],bool create){
    std::string p=g_store+"/.key";
    int fd=open(p.c_str(),O_RDONLY);
    if(fd>=0){ struct stat st; bool ok=fstat(fd,&st)==0&&st.st_size==32&&st.st_uid==geteuid()&&!(st.st_mode&077)&&read(fd,key,32)==32; close(fd); return ok; }
    if(!create||!store_dir()||!rand_bytes(key,32)) return false;
    std::string t=p+".new"; fd=open(t.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600); if(fd<0){ unlink(t.c_str()); fd=open(t.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600); } if(fd<0) return false;
    bool ok=write(fd,key,32)==32; ok=close(fd)==0&&ok; chmod(t.c_str(),0600);
    if(!ok||rename(t.c_str(),p.c_str())!=0){ unlink(t.c_str()); return false; }
    return true;
}

static void store_aad(const char* user,uint8_t* aad,size_t* na){ memcpy(aad,"NVXFACE2",8); size_t n=strlen(user); if(n>31) n=31; memcpy(aad+8,user,n); *na=8+n; }

static int save_list(engine& e,const char* user,const std::vector<fprint>& list){
    if(list.empty()||list.size()>64) return -1;
    uint8_t key[32]; if(!store_dir()||!machine_key(key,true)) return -1;
    std::vector<uint8_t> pt(4+list.size()*FP_DIM*4); uint32_t n=(uint32_t)list.size(); memcpy(&pt[0],&n,4);
    for(size_t i=0;i<list.size();i++) for(int k=0;k<FP_DIM;k++){ float v=list[i](k); memcpy(&pt[4+(i*FP_DIM+k)*4],&v,4); }
    uint8_t nonce[12], tag[16], aad[40]; size_t na; if(!rand_bytes(nonce,12)){ memset(key,0,32); return -1; }
    store_aad(user,aad,&na); fp_seal(key,nonce,aad,na,pt.data(),pt.size(),tag); memset(key,0,32);
    std::string p=store_path(user), t=p+".new";
    unlink(t.c_str()); int fd=open(t.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600); if(fd<0) return -1;
    bool ok=write(fd,"NVXFACE2",8)==8&&write(fd,nonce,12)==12&&write(fd,pt.data(),pt.size())==(ssize_t)pt.size()&&write(fd,tag,16)==16;
    ok=close(fd)==0&&ok; chmod(t.c_str(),0600);
    if(!ok||rename(t.c_str(),p.c_str())!=0){ unlink(t.c_str()); return -1; }
    e.stored=list; e.loaded_user=user;
    return (int)list.size();
}

static int load_store(engine& e,const char* user){
    if(e.loaded_user==user) return (int)e.stored.size();
    e.stored.clear(); e.loaded_user=user;
    int fd=open(store_path(user).c_str(),O_RDONLY); if(fd<0) return 0;
    struct stat st; if(fstat(fd,&st)!=0||st.st_uid!=geteuid()||(st.st_mode&077)||st.st_size<12||st.st_size>65536){ close(fd); fprintf(stderr,"nvx-faceid: %s.face is not a root-only file - ignored\n",user); return 0; }
    std::vector<uint8_t> d((size_t)st.st_size); bool rd=read(fd,d.data(),d.size())==(ssize_t)d.size(); close(fd); if(!rd) return 0;
    if(d.size()>=8+12+4+16&&!memcmp(d.data(),"NVXFACE2",8)){
        uint8_t key[32], aad[40]; size_t na; if(!machine_key(key,false)) return 0;
        size_t n=d.size()-8-12-16; store_aad(user,aad,&na);
        int bad=fp_open(key,&d[8],aad,na,&d[20],n,&d[20+n]); memset(key,0,32);
        if(bad){ fprintf(stderr,"nvx-faceid: %s.face does not open with this machine's key - ignored\n",user); return 0; }
        uint32_t cnt; memcpy(&cnt,&d[20],4); if(cnt==0||cnt>64||4+(size_t)cnt*FP_DIM*4!=n) return 0;
        for(uint32_t i=0;i<cnt;i++){ fprint p(FP_DIM); for(int k=0;k<FP_DIM;k++){ float v; memcpy(&v,&d[24+(i*FP_DIM+k)*4],4); p(k)=v; } e.stored.push_back(p); }
        return (int)e.stored.size();
    }
    d.push_back(0); const char* c=(const char*)d.data(); int n=0, used=0;
    if(sscanf(c,"NVXFACE1 %d%n",&n,&used)!=1||n<=0||n>64) return 0;
    c+=used;
    for(int i=0;i<n;i++){ fprint p(FP_DIM); bool ok=true; for(int k=0;k<FP_DIM;k++){ float v; int u=0; if(sscanf(c,"%f%n",&v,&u)!=1){ ok=false; break; } c+=u; p(k)=v; } if(!ok) break; e.stored.push_back(p); }
    if(!e.stored.empty()){ std::vector<fprint> keep=e.stored; if(save_list(e,user,keep)>0) fprintf(stderr,"nvx-faceid: old plain-text templates of %s encrypted with the machine key\n",user); else fprintf(stderr,"nvx-faceid: could not encrypt the old templates of %s\n",user); e.stored=keep; e.loaded_user=user; }
    return (int)e.stored.size();
}

static int save_store(engine& e,const char* user){
    if(!fp_enrol_ready(&e.en)) return -1;
    std::vector<fprint> list; for(int i=0;i<e.en.n;i++){ fprint p(FP_DIM); for(int k=0;k<FP_DIM;k++) p(k)=e.en.t[i][k]; list.push_back(p); }
    return save_list(e,user,list);
}

static bool read_all(int fd,void* buf,size_t n){
    uint8_t* p=(uint8_t*)buf; size_t got=0;
    while(got<n){ ssize_t r=read(fd,p+got,n-got); if(r<0){ if(errno==EINTR||errno==EAGAIN){ usleep(20000); continue; } return false; } if(r==0){ usleep(20000); continue; } got+=(size_t)r; }
    return true;
}

static int serve(const char* devpath){
    engine e;
    try { load_models(e); } catch(std::exception& x){ fprintf(stderr,"nvx-faceid: cannot load the face models: %s\n",x.what()); return 2; }
    int fd=open(devpath,O_RDWR); if(fd<0){ fprintf(stderr,"nvx-faceid: cannot open %s\n",devpath); return 2; }
    fprintf(stderr,"nvx-faceid: ready\n");
    std::vector<uint8_t> buf;
    for(;;){
        req q; if(!read_all(fd,&q,sizeof q)) break;
        if(q.magic!=Q_MAGIC) continue;
        q.user[31]=0;
        res r; memset(&r,0,sizeof r); r.magic=R_MAGIC; r.seq=q.seq; r.status=S_ERROR; r.dist=-1;
        if(q.cmd==C_QUIT) break;
        size_t bytes=(size_t)q.w*q.h*3;
        if(q.w>4096||q.h>4096){ if(write(fd,&r,sizeof r)<0) break; continue; }
        buf.resize(bytes);
        if(bytes&&!read_all(fd,buf.data(),bytes)) break;
        if(!user_ok(q.user)){ if(write(fd,&r,sizeof r)<0) break; continue; }
        try {
            if(q.cmd==C_RESET){ memset(&e.en,0,sizeof e.en); fp_live_reset(&e.live); e.have_last=false; e.skipped=0; r.status=S_FACE; r.count=0; }
            else if(q.cmd==C_SAVE){ int n=save_store(e,q.user); r.status=n>0?S_SAVED:S_ERROR; r.count=n; }
            else if(q.cmd==C_VERIFY||q.cmd==C_ADD){
                matrix<rgb_pixel> img(q.h,q.w);
                for(uint32_t y=0;y<q.h;y++) for(uint32_t x=0;x<q.w;x++){ const uint8_t* p=&buf[((size_t)y*q.w+x)*3]; img(y,x)=rgb_pixel(p[0],p[1],p[2]); }
                fprint fp; shot sh; memset(&sh,0,sizeof sh);
                if(!face_print(e,img,fp,r,&sh)) r.status=S_NOFACE;
                else if(r.faces>1) r.status=S_MANY;
                else if(r.w<MIN_FACE) r.status=S_SMALL;
                else if(q.cmd==C_VERIFY){
                    int n=load_store(e,q.user);
                    if(!n) r.status=S_NOFILE;
                    else {
                        std::vector<float> t((size_t)n*FP_DIM), pf(FP_DIM); for(int i=0;i<n;i++) for(int k=0;k<FP_DIM;k++) t[(size_t)i*FP_DIM+k]=e.stored[i](k); for(int k=0;k<FP_DIM;k++) pf[k]=fp(k);
                        float best=-1; int m=fp_match_frame((const float(*)[FP_DIM])t.data(),n,pf.data(),&best);
                        float dl=e.have_last?length(fp-e.last):-1.0f; e.last=fp; e.have_last=true;
                        int v=fp_verify_step(&e.live,m,sh.pts,dl,sh.eye,sh.luma);
                        r.dist=(int32_t)(best*1000.0f);
                        r.status=v==FPV_MATCH?S_MATCH:v==FPV_SPOOF?S_SPOOF:v==FPV_NEEDLIVE?S_LIVE:S_NOMATCH;
                    }
                } else {
                    std::vector<float> pf(FP_DIM); for(int k=0;k<FP_DIM;k++) pf[k]=fp(k);
                    float d=e.en.n?fp_dist(e.en.t[0],pf.data()):0.0f;
                    int a=fp_enrol_add(&e.en,pf.data(),sh.yaw,sh.luma);
                    if(a<0){ memset(&e.en,0,sizeof e.en); e.skipped++; r.status=S_FACE; }
                    else r.status=a?S_ADDED:S_FACE;
                    if(!fp_enrol_ready(&e.en)&&e.en.n>=FP_ENROL_MIN-1) r.status=S_TURN;
                    r.dist=(int32_t)(d*1000.0f);
                }
                if(q.cmd==C_ADD) r.count=(int32_t)fp_enrol_shown(&e.en);
            }
        } catch(std::exception& x){ fprintf(stderr,"nvx-faceid: %s\n",x.what()); r.status=S_ERROR; }
        if(write(fd,&r,sizeof r)<0) break;
    }
    close(fd);
    return 0;
}

static int calibrate(engine& e,const char* dir){
    std::vector<std::vector<fprint>> ids; std::vector<std::string> names;
    DIR* d=opendir(dir); if(!d){ fprintf(stderr,"nvx-faceid: cannot open %s\n",dir); return 1; }
    for(dirent* x;(x=readdir(d));){ if(x->d_name[0]=='.') continue; std::string sub=std::string(dir)+"/"+x->d_name; DIR* s2=opendir(sub.c_str()); if(!s2) continue;
        std::vector<fprint> v; for(dirent* y;(y=readdir(s2));){ if(y->d_name[0]=='.') continue; try { matrix<rgb_pixel> img; load_image(img,sub+"/"+y->d_name); res r; memset(&r,0,sizeof r); fprint f; if(face_print(e,img,f,r)&&r.faces==1) v.push_back(f); } catch(std::exception&){} }
        closedir(s2); if(!v.empty()){ ids.push_back(v); names.push_back(x->d_name); } }
    closedir(d);
    static const float TH[]={0.35f,0.40f,0.45f,0.50f,0.55f,0.60f};
    long gn=0, in=0, ga[6]={0}, ia[6]={0};
    for(size_t i=0;i<ids.size();i++) for(size_t a=0;a<ids[i].size();a++){
        for(size_t b=a+1;b<ids[i].size();b++){ float dd=length(ids[i][a]-ids[i][b]); gn++; for(int q=0;q<6;q++) if(dd<TH[q]) ga[q]++; }
        for(size_t j=i+1;j<ids.size();j++) for(size_t b=0;b<ids[j].size();b++){ float dd=length(ids[i][a]-ids[j][b]); in++; for(int q=0;q<6;q++) if(dd<TH[q]) ia[q]++; } }
    printf("%zu people, %ld same-person pairs, %ld different-person pairs\n",ids.size(),gn,in);
    for(int q=0;q<6;q++) printf("threshold %.2f: false accept %.4f%%  false reject %.2f%%%s\n",TH[q],in?100.0*ia[q]/in:0.0,gn?100.0*(gn-ga[q])/gn:0.0,TH[q]==FP_T_MATCH?"  (in use)":"");
    return 0;
}

static int image_print(engine& e,const char* path,fprint& out){
    matrix<rgb_pixel> img; load_image(img,path); res r; memset(&r,0,sizeof r);
    int ok=face_print(e,img,out,r);
    printf("%s: %d face(s)",path,r.faces);
    if(ok) printf(", largest at %d,%d %dx%d",r.x,r.y,r.w,r.h);
    printf("\n");
    return ok;
}

int main(int argc,char** argv){
    const char* dir=getenv("NVX_FACEID_MODELS"); if(dir&&dir[0]) g_dir=dir;
    const char* st=getenv("NVX_FACEID_STORE"); if(st&&st[0]) g_store=st;
    if(argc>=2&&!strcmp(argv[1],"serve")) return serve(argc>=3?argv[2]:"/dev/nvxface");
    if(argc>=3&&!strcmp(argv[1],"compare")){
        engine e; load_models(e);
        std::vector<fprint> fp(argc-2); std::vector<int> ok(argc-2);
        for(int i=2;i<argc;i++) ok[i-2]=image_print(e,argv[i],fp[i-2]);
        for(int i=0;i<argc-2;i++) for(int j=i+1;j<argc-2;j++) if(ok[i]&&ok[j]){ float d=length(fp[i]-fp[j]); printf("%s %s %.3f %s\n",argv[i+2],argv[j+2],d,d<MATCH_DIST?"same":"different"); }
        return 0;
    }
    if(argc==3&&!strcmp(argv[1],"store-check")){ if(!user_ok(argv[2])) return 1; engine* e=new engine; int n=load_store(*e,argv[2]); printf("%s: %d template(s)\n",argv[2],n); return n>0?0:1; }
    if(argc==3&&!strcmp(argv[1],"calibrate")){ engine e; load_models(e); return calibrate(e,argv[2]); }
    fprintf(stderr,"usage: nvx-faceid serve [device] | nvx-faceid compare IMAGE... | nvx-faceid calibrate DIR-OF-PERSON-DIRS\n");
    return 1;
}
