#include "include/nvx/base.h"
#include "include/nvx/sys/bpf/bpf_attach.h"

struct cbpf_insn { u16 code; u8 jt, jf; u32 k; };

int cbpf_check(const void* insns,u32 n){
    const struct cbpf_insn* f=(const struct cbpf_insn*)insns;
    if(!f||n==0||n>4096)return -22;
    for(u32 i=0;i<n;i++){
        u16 c=f[i].code; u32 k=f[i].k; u32 cl=c&7;
        switch(cl){
        case 0: { u32 m=c&0xe0, sz=c&0x18; if(sz==0x18||c>0xffu)return -22;
            if(m==0x00||m==0x80){ if(sz)return -22; }
            else if(m==0x60){ if(sz||k>=16)return -22; }
            else if(m!=0x20&&m!=0x40)return -22;
            break; }
        case 1: if(c!=0x01&&c!=0x61&&c!=0x81&&c!=0xb1)return -22; if(c==0x61&&k>=16)return -22; break;
        case 2: case 3: if(c!=cl||k>=16)return -22; break;
        case 4: { u32 op=c&0xf0; if(c>0xffu||op>0xa0)return -22;
            if(op==0x80){ if(c!=0x84)return -22; break; }
            if(!(c&8)&&(op==0x30||op==0x90)&&k==0)return -22;
            if(!(c&8)&&(op==0x60||op==0x70)&&k>=32)return -22;
            break; }
        case 5: { u32 op=c&0xf0;
            if(c>0xffu)return -22;
            if(op==0x00){ if(c!=0x05||k>=n-i-1)return -22; break; }
            if(op>0x40)return -22;
            if((u32)f[i].jt>=n-i-1||(u32)f[i].jf>=n-i-1)return -22;
            break; }
        case 6: if(c!=0x06&&c!=0x0e&&c!=0x16)return -22; break;
        case 7: if(c!=0x07&&c!=0x87)return -22; break;
        }
    }
    u16 l=f[n-1].code; if((l&7)!=6)return -22;
    return 0;
}

static int cbpf_ld(const u8* p,u32 len,u32 off,u32 sz,u32* out){
    if(off>len||sz>len-off)return 0;
    if(sz==4)*out=((u32)p[off]<<24)|((u32)p[off+1]<<16)|((u32)p[off+2]<<8)|p[off+3];
    else if(sz==2)*out=((u32)p[off]<<8)|p[off+1];
    else *out=p[off];
    return 1;
}

u32 cbpf_run(const void* insns,u32 n,const u8* pkt,u32 len){
    const struct cbpf_insn* f=(const struct cbpf_insn*)insns;
    u32 A=0, X=0, M[16]; for(int i=0;i<16;i++)M[i]=0;
    for(u32 pc=0;pc<n;pc++){
        const struct cbpf_insn* I=&f[pc]; u16 c=I->code; u32 k=I->k, v;
        switch(c&7){
        case 0: { u32 m=c&0xe0, sz=(c&0x18)==0?4:(c&0x18)==8?2:1;
            if(m==0x00)A=k; else if(m==0x80)A=len; else if(m==0x60)A=M[k&15];
            else { u32 off=m==0x40?X+k:k; if(m==0x40&&off<X)return 0; if(!cbpf_ld(pkt,len,off,sz,&A))return 0; }
            break; }
        case 1: if(c==0x01)X=k; else if(c==0x61)X=M[k&15]; else if(c==0x81)X=len; else { if(!cbpf_ld(pkt,len,k,1,&v))return 0; X=(v&0xf)<<2; } break;
        case 2: M[k&15]=A; break;
        case 3: M[k&15]=X; break;
        case 4: { u32 s=(c&8)?X:k;
            switch(c&0xf0){
            case 0x00: A+=s; break; case 0x10: A-=s; break; case 0x20: A*=s; break;
            case 0x30: if(!s)return 0; A/=s; break;
            case 0x40: A|=s; break; case 0x50: A&=s; break;
            case 0x60: A=s<32?A<<s:0; break; case 0x70: A=s<32?A>>s:0; break;
            case 0x80: A=(u32)-(int)A; break;
            case 0x90: if(!s)return 0; A%=s; break;
            case 0xa0: A^=s; break;
            default: return 0; }
            break; }
        case 5: { u32 s=(c&8)?X:k; int t;
            switch(c&0xf0){
            case 0x00: if(k>=n-pc-1)return 0; pc+=k; continue;
            case 0x10: t=A==s; break; case 0x20: t=A>s; break; case 0x30: t=A>=s; break; case 0x40: t=(A&s)!=0; break;
            default: return 0; }
            u32 j=t?I->jt:I->jf; if(j>=n-pc-1)return 0; pc+=j; break; }
        case 6: return (c&0x18)==0x10?A:(c&0x18)==0x08?X:k;
        case 7: if(c==0x07)X=A; else A=X; break;
        }
    }
    return 0;
}
