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
enum { S_NOFACE=0, S_FACE=1, S_MATCH=2, S_NOMATCH=3, S_ADDED=4, S_SAVED=5, S_NOFILE=6, S_ERROR=7, S_SMALL=8, S_MANY=9 };
static const float MATCH_DIST=0.5f, SAME_PERSON=0.4f;
static const int ENROLL_MAX=16;
static const int MIN_FACE=96;

struct req { uint32_t magic, seq, cmd, w, h; char user[32]; uint32_t pad[3]; };
struct res { uint32_t magic, seq, status; int32_t dist, x, y, w, h, pts[10], count, faces; };

struct engine {
    frontal_face_detector det;
    shape_predictor sp;
    anet_type net;
    std::string loaded_user;
    std::vector<fprint> stored, pending;
    int skipped=0;
};

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

static int face_print(engine& e,const matrix<rgb_pixel>& img,fprint& out,res& r){
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
    matrix<rgb_pixel> chip;
    extract_image_chip(img,get_face_chip_details(s,150,0.25),chip);
    out=e.net(chip);
    return 1;
}

static float min_dist(const std::vector<fprint>& v,const fprint& f){
    float best=1e9f; for(const fprint& s:v){ float d=length(s-f); if(d<best) best=d; } return best;
}

static std::string store_path(const char* user){ return g_store+"/"+user+".face"; }

static int load_store(engine& e,const char* user){
    if(e.loaded_user==user) return (int)e.stored.size();
    e.stored.clear(); e.loaded_user=user;
    FILE* f=fopen(store_path(user).c_str(),"r"); if(!f) return 0;
    char tag[16]={0}; int n=0;
    if(fscanf(f,"%15s %d",tag,&n)!=2||strcmp(tag,"NVXFACE1")!=0||n<0||n>64){ fclose(f); return 0; }
    for(int i=0;i<n;i++){ fprint p(128); bool ok=true; for(int k=0;k<128;k++){ float v; if(fscanf(f,"%f",&v)!=1){ ok=false; break; } p(k)=v; } if(!ok) break; e.stored.push_back(p); }
    fclose(f);
    return (int)e.stored.size();
}

static int save_store(engine& e,const char* user){
    if(e.pending.empty()) return -1;
    mkdir("/etc/noovex",0755);
    mkdir(g_store.c_str(),0700); chmod(g_store.c_str(),0700);
    std::string p=store_path(user), t=p+".new";
    int fd=open(t.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600); if(fd<0) return -1;
    FILE* f=fdopen(fd,"w"); if(!f){ close(fd); return -1; }
    fprintf(f,"NVXFACE1 %d\n",(int)e.pending.size());
    for(const fprint& s:e.pending){ for(int k=0;k<128;k++) fprintf(f,k?" %.6f":"%.6f",s(k)); fputc('\n',f); }
    if(fclose(f)!=0){ unlink(t.c_str()); return -1; }
    chmod(t.c_str(),0600);
    if(rename(t.c_str(),p.c_str())!=0){ unlink(t.c_str()); return -1; }
    e.stored=e.pending; e.loaded_user=user;
    return (int)e.pending.size();
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
            if(q.cmd==C_RESET){ e.pending.clear(); e.skipped=0; r.status=S_FACE; r.count=0; }
            else if(q.cmd==C_SAVE){ int n=save_store(e,q.user); r.status=n>0?S_SAVED:S_ERROR; r.count=n; }
            else if(q.cmd==C_VERIFY||q.cmd==C_ADD){
                matrix<rgb_pixel> img(q.h,q.w);
                for(uint32_t y=0;y<q.h;y++) for(uint32_t x=0;x<q.w;x++){ const uint8_t* p=&buf[((size_t)y*q.w+x)*3]; img(y,x)=rgb_pixel(p[0],p[1],p[2]); }
                fprint fp;
                if(!face_print(e,img,fp,r)) r.status=S_NOFACE;
                else if(r.faces>1) r.status=S_MANY;
                else if(r.w<MIN_FACE) r.status=S_SMALL;
                else if(q.cmd==C_VERIFY){
                    int n=load_store(e,q.user);
                    if(!n) r.status=S_NOFILE;
                    else { float d=min_dist(e.stored,fp); r.dist=(int32_t)(d*1000.0f); r.status=d<MATCH_DIST?S_MATCH:S_NOMATCH; }
                } else {
                    float d=e.pending.empty()?0.0f:length(e.pending[0]-fp);
                    if(d>SAME_PERSON){ e.pending.clear(); e.skipped++; r.status=S_FACE; }
                    else if((int)e.pending.size()<ENROLL_MAX){ e.pending.push_back(fp); r.status=S_ADDED; } else r.status=S_FACE;
                    r.dist=(int32_t)(d*1000.0f);
                }
                if(q.cmd==C_ADD) r.count=(int32_t)e.pending.size();
            }
        } catch(std::exception& x){ fprintf(stderr,"nvx-faceid: %s\n",x.what()); r.status=S_ERROR; }
        if(write(fd,&r,sizeof r)<0) break;
    }
    close(fd);
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
    fprintf(stderr,"usage: nvx-faceid serve [device] | nvx-faceid compare IMAGE...\n");
    return 1;
}
