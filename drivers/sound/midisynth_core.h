#ifndef NVX_MIDISYNTH_CORE_H
#define NVX_MIDISYNTH_CORE_H
#define MS_NV 16
struct ms_voice { u8 on, rel, note, ch, vel, held, stg; u32 ph, inc; int env, age; };
struct ms_state { struct ms_voice v[MS_NV]; int rate, sus[16], vol[16], bend[16], age; };
static const u32 ms_mhz[12]={8176,8662,9177,9723,10301,10913,11562,12250,12978,13750,14568,15434};
static u32 ms_inc(int note,int rate,int bend){
    note=note<0?0:note>127?127:note;
    u64 f=(u64)ms_mhz[note%12]<<(note/12);
    if(bend){ int b=bend<0?-bend:bend; u64 num=1000000+(u64)b*122462/8192; f=bend>0?f*num/1000000:f*1000000/num; }
    return (u32)((f<<32)/((u64)(rate>0?rate:48000)*1000)); }
static void ms_init(struct ms_state* s,int rate){ for(int i=0;i<MS_NV;i++){ s->v[i].on=0; s->v[i].env=0; } for(int c=0;c<16;c++){ s->sus[c]=0; s->vol[c]=100; s->bend[c]=0; } s->rate=rate>0?rate:48000; s->age=0; }
static void ms_off(struct ms_state* s,int ch,int note){ for(int i=0;i<MS_NV;i++){ struct ms_voice* v=&s->v[i]; if(v->on&&!v->rel&&v->ch==ch&&v->note==note){ if(s->sus[ch])v->held=1; else v->rel=1; } } }
static void ms_event(struct ms_state* s,u8 st,u8 d1,u8 d2){
    int ch=st&15, ty=st&0xF0; d1&=0x7F; d2&=0x7F;
    if(ty==0x90&&d2){ int k=-1; for(int i=0;i<MS_NV;i++) if(!s->v[i].on){ k=i; break; }
        if(k<0){ int best=0; for(int i=1;i<MS_NV;i++){ int a=s->v[i].rel*1000000-s->v[i].age, b=s->v[best].rel*1000000-s->v[best].age; if(a>b)best=i; } k=best; }
        struct ms_voice* v=&s->v[k]; v->on=1; v->rel=0; v->held=0; v->note=d1; v->ch=(u8)ch; v->vel=d2; v->ph=0; v->inc=ms_inc(d1,s->rate,s->bend[ch]); v->env=0; v->stg=0; v->age=++s->age; }
    else if(ty==0x80||ty==0x90) ms_off(s,ch,d1);
    else if(ty==0xB0){
        if(d1==64){ s->sus[ch]=d2>=64; if(!s->sus[ch]) for(int i=0;i<MS_NV;i++) if(s->v[i].on&&s->v[i].ch==ch&&s->v[i].held){ s->v[i].held=0; s->v[i].rel=1; } }
        else if(d1==7)s->vol[ch]=d2;
        else if(d1==120||d1==123){ for(int i=0;i<MS_NV;i++) if(s->v[i].on&&(s->v[i].ch==ch||st==0xB0)){ if(d1==120)s->v[i].on=0; else s->v[i].rel=1; } } }
    else if(ty==0xE0){ s->bend[ch]=(int)((u32)d1|((u32)d2<<7))-8192; for(int i=0;i<MS_NV;i++) if(s->v[i].on&&s->v[i].ch==ch)s->v[i].inc=ms_inc(s->v[i].note,s->rate,s->bend[ch]); } }
static int ms_active(const struct ms_state* s){ for(int i=0;i<MS_NV;i++) if(s->v[i].on)return 1; return 0; }
static void ms_render(struct ms_state* s,short* out,int frames){
    int atk=65535/(s->rate/200+1)+1, dec=(65535-40000)/(s->rate/3+1)+1, rel=65535/(s->rate/5+1)+1;
    for(int f=0;f<frames;f++){ int acc=0;
        for(int i=0;i<MS_NV;i++){ struct ms_voice* v=&s->v[i]; if(!v->on)continue;
            if(v->rel){ v->env-=rel; if(v->env<=0){ v->env=0; v->on=0; continue; } }
            else if(!v->stg){ v->env+=atk; if(v->env>=65535){ v->env=65535; v->stg=1; } }
            else if(v->env>40000){ v->env-=dec; if(v->env<40000)v->env=40000; }
            u32 p=v->ph; int tri=(int)(p>>16); tri=tri<32768?tri*2-32768:(65535-tri)*2-32768; int sq=(p&0x80000000u)?-8192:8192;
            int smp=(tri*3+sq)/4; v->ph+=v->inc;
            acc+=(int)(((long long)smp*v->env/65536)*v->vel/127*s->vol[v->ch]/127); }
        acc/=4; if(acc>32767)acc=32767; if(acc<-32768)acc=-32768; out[f*2]=(short)acc; out[f*2+1]=(short)acc; } }
#endif
