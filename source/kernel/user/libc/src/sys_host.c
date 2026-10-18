static long lx(long n,long a,long b,long c,long d){ long r; register long r10 __asm__("r10")=d; __asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c),"r"(r10):"rcx","r11","memory"); return r; }
char** __host_envp;
static unsigned char g_dbuf[65536];
long __sys(long n,long a,long b,long c,long d,long e){
    (void)e;
    switch(n){
    case 0: lx(231,a,0,0,0); for(;;){}
    case 2: return lx(57,0,0,0,0);
    case 12: return lx(12,a,0,0,0);
    case 16: { long ts[2]={a/1000,(a%1000)*1000000}; return lx(35,(long)ts,0,0,0); }
    case 17: return lx(62,a,b,0,0);
    case 18: { long r=lx(79,a,b,0,0); return r<0?r:r; }
    case 19: return lx(80,a,0,0,0);
    case 21: return lx(83,a,0755,0,0);
    case 30: return lx(257,-100,a,b,0644);
    case 31: return lx(0,a,b,c,0);
    case 32: return lx(1,a,b,c,0);
    case 33: return lx(3,a,0,0,0);
    case 34: { unsigned char st[144]; long r=lx(5,a,(long)st,0,0); if(r<0)return r; return *(long*)(st+48); }
    case 36: return lx(8,a,b,c,0);
    case 37: return lx(32,a,0,0,0);
    case 38: return lx(87,a,0,0,0);
    case 39: return lx(39,0,0,0,0);
    case 120: { const char* nm=(const char*)a; char* out=(char*)b; long cap=c;
        for(char** e2=__host_envp;e2&&*e2;e2++){ const char* s=*e2; int i=0; while(nm[i]&&s[i]==nm[i])i++; if(!nm[i]&&s[i]=='='){ s+=i+1; long k=0; while(s[k]&&k<cap-1){ out[k]=s[k]; k++; } out[k]=0; return k; } }
        return -1; }
    case 130: return lx(102,0,0,0,0);
    case 131: return lx(105,a,0,0,0);
    case 132: return lx(110,0,0,0,0);
    case 133: { long ts[2]; lx(228,0,(long)ts,0,0); return ts[0]*1000+ts[1]/1000000; }
    case 134: return lx(82,a,b,0,0);
    case 135: return lx(84,a,0,0,0);
    case 136: return lx(76,a,b,0,0);
    case 137: return lx(77,a,b,0,0);
    case 138: return lx(74,a,0,0,0);
    case 139: { long fd=lx(257,-100,a,0x10000,0); if(fd<0)return fd; long idx=0, got=-1;
        for(;;){ long nr=lx(217,fd,(long)g_dbuf,sizeof g_dbuf,0); if(nr<=0)break; long off=0;
            while(off<nr){ unsigned short rl=*(unsigned short*)(g_dbuf+off+16); unsigned char ty=g_dbuf[off+18]; const char* nm=(const char*)(g_dbuf+off+19);
                if(idx==b){ char* o=(char*)c; int k=0; while(nm[k]&&k<63){ o[k]=nm[k]; k++; } o[k]=0; if(d)*(int*)d=ty; got=1; break; } idx++; off+=rl; }
            if(got>0)break; }
        lx(3,fd,0,0,0); return got>0?1:0; }
    case 140: { long ts[2]; lx(228,1,(long)ts,0,0); return (ts[0]*1000+ts[1]/1000000)/10; }
    case 144: return lx(33,a,b,0,0);
    case 145: return lx(22,a,0,0,0);
    case 146: return lx(72,a,b,c,0);
    case 147: return lx(95,a,0,0,0);
    case 148: return lx(104,0,0,0,0);
    case 156: return lx(61,a,b,c,0);
    case 157: return lx(59,a,b,(long)__host_envp,0);
    case 160: return lx(86,a,b,0,0);
    case 161: return lx(88,a,b,0,0);
    case 162: return lx(89,a,b,c,0);
    case 163: { unsigned char st[144]; long r=lx(c?4:6,a,(long)st,0,0); if(r<0)return r; unsigned* o=(unsigned*)b;
        o[0]=*(unsigned*)(st+24); o[1]=*(unsigned*)(st+28); o[2]=*(unsigned*)(st+32); o[3]=(unsigned)*(long*)(st+16); o[4]=(unsigned)*(long*)(st+48);
        o[5]=(unsigned)*(long*)(st+8); o[6]=(unsigned)*(long*)(st+88); o[9]=((o[0]&0170000)==0040000); return 0; }
    case 165: return lx(92,a,b,c,0);
    case 166: return lx(90,a,b,0,0);
    case 167: return lx(132,a,b,0,0);
    case 172: { unsigned char tio[64]; return lx(16,a,0x5401,(long)tio,0)==0?1:0; }
    case 170: { unsigned char u[390]; lx(63,(long)u,0,0,0); char* o=(char*)a; long k=0; while(u[65+k]&&k<b-1){ o[k]=(char)u[65+k]; k++; } o[k]=0; return 0; }
    }
    return -38;
}
