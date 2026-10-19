import struct,sys
class Fat:
    def __init__(s,img,off=0):
        s.d=img; s.o=off; b=s.sec(0); s.err=[]
        if b[510:512]!=b'\x55\xaa': s.err.append('boot signature missing')
        s.bps,s.spc,s.rsvd,s.nf,s.re,ts16,s.media,fz16=struct.unpack_from('<HBHBHHBH',b,11)
        s.ts=ts16 or struct.unpack_from('<I',b,32)[0]; s.fz=fz16 or struct.unpack_from('<I',b,36)[0]
        s.rs=(s.re*32+s.bps-1)//s.bps; s.data=s.rsvd+s.nf*s.fz+s.rs; s.ncl=(s.ts-s.data)//s.spc
        s.type=12 if s.ncl<4085 else 16 if s.ncl<65525 else 32
        s.cb=s.spc*s.bps
        fats=[s.d[s.o+(s.rsvd+i*s.fz)*512:s.o+(s.rsvd+(i+1)*s.fz)*512] for i in range(s.nf)]
        for i in range(1,s.nf):
            if fats[i]!=fats[0]: s.err.append(f'FAT copy {i} differs from FAT 0')
        s.fat=fats[0]
        if s.type==32:
            s.rootc=struct.unpack_from('<I',b,44)[0]; fsi=struct.unpack_from('<H',b,48)[0]; bk=struct.unpack_from('<H',b,50)[0]
            if bk and s.sec(bk)!=b: s.err.append('backup boot sector differs')
            f=s.sec(fsi)
            if struct.unpack_from('<I',f,0)[0]!=0x41615252 or struct.unpack_from('<I',f,484)[0]!=0x61417272 or struct.unpack_from('<I',f,508)[0]!=0xAA550000: s.err.append('FSInfo signatures bad')
            s.fsi_free=struct.unpack_from('<I',f,488)[0]
        if s.ent(0)&0xFF!=s.media: s.err.append('FAT[0] does not carry media byte')
        s.used={}
    def sec(s,n): return s.d[s.o+n*512:s.o+(n+1)*512]
    def ent(s,c):
        if s.type==32: return struct.unpack_from('<I',s.fat,c*4)[0]&0x0FFFFFFF
        return struct.unpack_from('<H',s.fat,c*2)[0]
    def eoc(s,v): return v>=(0x0FFFFFF8 if s.type==32 else 0xFFF8)
    def chain(s,c,owner):
        out=[]; seen=set()
        while True:
            if not (2<=c<s.ncl+2): s.err.append(f'{owner}: cluster {c} out of range'); break
            if c in seen: s.err.append(f'{owner}: chain loops'); break
            if c in s.used: s.err.append(f'{owner}: cluster {c} cross-linked with {s.used[c]}')
            seen.add(c); s.used[c]=owner; out.append(c); n=s.ent(c)
            if n==0: s.err.append(f'{owner}: chain runs into a free cluster'); break
            if s.eoc(n): break
            c=n
        return out
    def cdata(s,c): a=s.data+(c-2)*s.spc; return s.d[s.o+a*512:s.o+(a+s.spc)*512]
    def dirbytes(s,c,owner):
        if c==0 and s.type!=32: return s.d[s.o+(s.rsvd+s.nf*s.fz)*512:s.o+s.data*512]
        return b''.join(s.cdata(x) for x in s.chain(c,owner))
    def entries(s,c,owner):
        raw=s.dirbytes(c,owner); res=[]; lfn={}; lchk=None; lexp=0
        for i in range(0,len(raw),32):
            e=raw[i:i+32]
            if e[0]==0: break
            if e[0]==0xE5: lfn={}; lexp=0; continue
            if e[11]&0x3F==0x0F:
                o=e[0]&0x1F
                if e[0]&0x40: lfn={}; lexp=o; lchk=e[13]
                elif not lexp or e[13]!=lchk: s.err.append(f'{owner}: orphan/broken LFN entry'); lfn={}; lexp=0; continue
                lfn[o]=e[1:11]+e[14:26]+e[28:32]; continue
            if e[11]&0x08: continue
            sn=e[0:11]; ck=0
            for x in sn: ck=((ck>>1)|((ck&1)<<7))+x&0xFF
            name=None
            if lexp:
                if len(lfn)!=lexp or ck!=lchk: s.err.append(f'{owner}: LFN checksum/sequence mismatch before {sn}')
                else:
                    u=b''.join(lfn[k] for k in range(1,lexp+1)).decode('utf-16-le',errors='replace'); name=u.split('\x00')[0]
            if name is None:
                bn=sn[0:8].decode('latin1').rstrip(); ex=sn[8:11].decode('latin1').rstrip()
                if e[12]&0x08: bn=bn.lower()
                if e[12]&0x10: ex=ex.lower()
                name=bn+('.'+ex if ex else '')
            lfn={}; lexp=0
            clus=(struct.unpack_from('<H',e,20)[0]<<16 if s.type==32 else 0)|struct.unpack_from('<H',e,26)[0]
            res.append((name,e[11],clus,struct.unpack_from('<I',e,28)[0]))
        return res
    def walk(s,c=None,path='',parent=0,out=None):
        if out is None: out=[]
        if c is None: c=s.rootc if s.type==32 else 0
        for name,attr,clus,size in s.entries(c,path or '/'):
            if name in ('.','..'):
                want=(c if name=='.' else (0 if parent==(s.rootc if s.type==32 else 0) else parent))
                if clus!=want: s.err.append(f'{path or "/"}: "{name}" points to {clus}, expected {want}')
                continue
            p=path+'/'+name
            if attr&0x10:
                out.append((p,'dir',0))
                if clus<2: s.err.append(f'{p}: directory without cluster'); continue
                s.walk(clus,p,c,out)
            else:
                out.append((p,'file',size)); need=(size+s.cb-1)//s.cb
                if size==0:
                    if clus: s.err.append(f'{p}: empty file owns cluster {clus}')
                else:
                    ch=s.chain(clus,p)
                    if len(ch)!=need: s.err.append(f'{p}: chain {len(ch)} clusters, size needs {need}')
        return out
    def check(s):
        tree=s.walk()
        alloc=[c for c in range(2,s.ncl+2) if s.ent(c)!=0]
        lost=[c for c in alloc if c not in s.used]
        if lost: s.err.append(f'{len(lost)} lost clusters (allocated, unreferenced)')
        free=s.ncl-len(alloc)
        if s.type==32 and s.fsi_free!=0xFFFFFFFF and s.fsi_free!=free: s.err.append(f'FSInfo free={s.fsi_free} but actual free={free}')
        return tree,free
    def read(s,path):
        c=s.rootc if s.type==32 else 0; parts=[p for p in path.split('/') if p]
        for i,p in enumerate(parts):
            hit=[e for e in s.entries(c,'') if e[0].lower()==p.lower()]
            if not hit: return None
            name,attr,clus,size=hit[0]
            if i==len(parts)-1:
                if attr&0x10: return None
                if size==0: return b''
                cl=[]; x=clus
                while 2<=x<s.ncl+2 and len(cl)<(size+s.cb-1)//s.cb: cl.append(x); x=s.ent(x)
                return b''.join(s.cdata(x) for x in cl)[:size]
            c=clus
if __name__=='__main__':
    img=open(sys.argv[1],'rb').read(); off=int(sys.argv[2])*512 if len(sys.argv)>2 else 0
    f=Fat(img,off); s=f.used.clear()
    tree,free=f.check()
    print(f'  FAT{f.type}: {f.ncl} clusters of {f.cb} bytes, {free} free')
    for p,k,sz in tree: print(f'    {p}{"/" if k=="dir" else ""}  {sz if k=="file" else ""}')
    print('  errors:' if f.err else '  NO ERRORS', *f.err, sep='\n    ')
