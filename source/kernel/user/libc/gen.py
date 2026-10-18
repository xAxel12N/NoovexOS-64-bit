import re,subprocess,sys,os
src=open('libc/src/legacy_posix.h').read()
def scan(s):
    out=[]; i=0; n=len(s); depth=0
    while i<n:
        c=s[i]
        if s.startswith('//',i): j=s.find('\n',i); j=n if j<0 else j; out.append(('t',s[i:j])); i=j; continue
        if s.startswith('/*',i): j=s.find('*/',i+2); j=n if j<0 else j+2; out.append(('t',s[i:j])); i=j; continue
        if c in '"\'':
            j=i+1
            while j<n and s[j]!=c:
                j+=2 if s[j]=='\\' else 1
            out.append(('t',s[i:j+1])); i=j+1; continue
        if c=='{': depth+=1
        if c=='}': depth-=1
        out.append(('c',c,depth)); i+=1
    return out
toks=[]; i=0; n=len(src); depth=0; res=[]
def strip_static(s):
    o=[]; i=0; n=len(s); depth=0
    while i<n:
        if s.startswith('//',i): j=s.find('\n',i); j=n if j<0 else j; o.append(s[i:j]); i=j; continue
        if s.startswith('/*',i): j=s.find('*/',i+2); j=n if j<0 else j+2; o.append(s[i:j]); i=j; continue
        c=s[i]
        if c in '"\'':
            j=i+1
            while j<n and s[j]!=c: j+=2 if s[j]=='\\' else 1
            o.append(s[i:j+1]); i=j+1; continue
        if s.startswith('#',i) and (i==0 or s[i-1]=='\n'):
            j=i
            while True:
                k=s.find('\n',j); k=n if k<0 else k
                if k>0 and s[k-1]=='\\': j=k+1; continue
                break
            o.append(s[i:k]); i=k; continue
        if c=='{': depth+=1
        elif c=='}': depth-=1
        if depth==0 and s.startswith('static',i) and (i==0 or not (s[i-1].isalnum() or s[i-1]=='_')) and not (s[i+6:i+7].isalnum() or s[i+6:i+7]=='_'):
            i+=6
            while i<n and s[i]==' ': i+=1
            if s.startswith('inline',i) and not s[i+6].isalnum(): i+=6
            continue
        o.append(c); i+=1
    return ''.join(o)
impl=strip_static(src)
impl=re.sub(r'long\s+__sys\s*\(long n,long a,long b,long c,long d,long e\)\{.*?\n\}','long __sys(long n,long a,long b,long c,long d,long e);',impl,count=1,flags=re.S)
open("libc/src/libc.c","w").write("#define NVX_LIBC_IMPL 1\n"+impl)
def headerize(s):
    o=[]; i=0; n=len(s); depth=0; seg_start=0
    while i<n:
        if s.startswith('//',i): j=s.find('\n',i); j=n if j<0 else j; o.append(s[i:j]); i=j; continue
        if s.startswith('/*',i): j=s.find('*/',i+2); j=n if j<0 else j+2; o.append(s[i:j]); i=j; continue
        c=s[i]
        if c in '"\'':
            j=i+1
            while j<n and s[j]!=c: j+=2 if s[j]=='\\' else 1
            o.append(s[i:j+1]); i=j+1; continue
        if s.startswith('#',i) and (i==0 or s[i-1]=='\n'):
            j=i
            while True:
                k=s.find('\n',j); k=n if k<0 else k
                if k>0 and s[k-1]=='\\': j=k+1; continue
                break
            o.append(s[i:k]); i=k; continue
        if c=='{' and depth==0:
            prev=''.join(o).rstrip()
            if prev.endswith(')'):
                d=1; j=i+1
                while j<n and d:
                    if s[j]=='{': d+=1
                    elif s[j]=='}': d-=1
                    elif s[j] in '"\'':
                        q=s[j]; j+=1
                        while j<n and s[j]!=q: j+=2 if s[j]=='\\' else 1
                    j+=1
                o.append(';'); i=j; continue
        if c=='{': depth+=1
        elif c=='}': depth-=1
        o.append(c); i+=1
    return ''.join(o)
decl=headerize(strip_static(src))
open('libc/include/nvx/_all.h','w').write(decl)
print('libc.c and _all.h generated:', len(impl), len(decl))
GINC=subprocess.check_output(['gcc','-print-file-name=include']).decode().strip()
open('/tmp/_h.c','w').write(open('libc/include/nvx/_all.h').read())
subprocess.run(['gcc','-c','-w','-fno-common','-ffreestanding','-nostdinc','-isystem',GINC,'-o','/tmp/_h.o','/tmp/_h.c'],check=True)
data=set(l.split()[2] for l in subprocess.check_output(['nm','/tmp/_h.o']).decode().splitlines() if len(l.split())==3 and l.split()[1] in 'BDRCbdr')
def externize(s,names):
    o=[]; i=0; n=len(s); stmt_start=0; depth=0; par=0
    out=[]; buf=[]
    i=0
    while i<n:
        if s.startswith('//',i): j=s.find('\n',i); j=n if j<0 else j; buf.append(s[i:j]); i=j; continue
        if s.startswith('/*',i): j=s.find('*/',i+2); j=n if j<0 else j+2; buf.append(s[i:j]); i=j; continue
        c=s[i]
        if c in '"\'':
            j=i+1
            while j<n and s[j]!=c: j+=2 if s[j]=='\\' else 1
            buf.append(s[i:j+1]); i=j+1; continue
        if s.startswith('#',i) and (i==0 or s[i-1]=='\n') and depth==0:
            j=i
            while True:
                k=s.find('\n',j); k=n if k<0 else k
                if k>0 and s[k-1]=='\\': j=k+1; continue
                break
            out.append(''.join(buf)); buf=[]; out.append(s[i:k]); i=k; continue
        if c in '{(': depth+=1
        elif c in '})': depth-=1
        buf.append(c); i+=1
        if c==';' and depth==0:
            st=''.join(buf); buf=[]
            ids=set(re.findall(r'[A-Za-z_][A-Za-z0-9_]*',st))
            if ids & names and not st.lstrip().startswith(('typedef','extern')):
                lead=re.match(r'\s*',st).group(0); body=st[len(lead):-1]
                parts=[]; d=0; cur=''; q=None
                for ch in body:
                    if q: cur+=ch; q=None if ch==q else q; continue
                    if ch in '"\'': q=ch; cur+=ch; continue
                    if ch in '{([': d+=1
                    elif ch in '})]': d-=1
                    if ch==',' and d==0: parts.append(cur); cur=''; continue
                    cur+=ch
                parts.append(cur)
                def noinit(p):
                    d=0; q=None
                    for k,ch in enumerate(p):
                        if q: q=None if ch==q else q; continue
                        if ch in '"\'': q=ch; continue
                        if ch in '{([': d+=1
                        elif ch in '})]': d-=1
                        elif ch=='=' and d==0: return p[:k].rstrip()
                    return p.rstrip()
                st=lead+'extern '+', '.join(noinit(p).strip() for p in parts)+';'
            out.append(st)
    out.append(''.join(buf))
    return ''.join(out)
decl2=externize(open('libc/include/nvx/_all.h').read(),data)
open('libc/include/nvx/_all.h','w').write(decl2)
open('/tmp/_h.c','w').write(decl2); r=subprocess.run(['gcc','-c','-w','-fno-common','-ffreestanding','-nostdinc','-isystem',GINC,'-o','/tmp/_h2.o','/tmp/_h.c'],capture_output=True,text=True)
left=[l for l in subprocess.check_output(['nm','/tmp/_h2.o']).decode().splitlines() if len(l.split())==3 and l.split()[1] in 'BDRCbdr'] if r.returncode==0 else r.stderr[:500]
print('data symbols found:',len(data),'| header now defines data:',left)
h=open('libc/include/nvx/_all.h').read()
h=re.sub(r'(?m)^__asm__\(".*?"\);\n','',h)
h=h.replace('DIR __dirs[4]; int __dir_used[4];','extern DIR __dirs[4]; extern int __dir_used[4];')
open('libc/include/nvx/_all.h','w').write(h)
open('/tmp/_h.c','w').write(h); subprocess.run(['gcc','-c','-w','-fno-common','-ffreestanding','-nostdinc','-isystem',GINC,'-o','/tmp/_h3.o','/tmp/_h.c'],check=True)
left=[l for l in subprocess.check_output(['nm','/tmp/_h3.o']).decode().splitlines() if len(l.split())==3]
print('header defines:',left if left else 'NOTHING (pure declarations)')
std=set(open('/tmp/stdnames.txt').read().split())
mine={}
for line in open('/tmp/_hf.aux') if os.path.exists('/tmp/_hf.aux') else []: pass
subprocess.run(['gcc','-fsyntax-only','-w','-ffreestanding','-nostdinc','-isystem',GINC,'-aux-info','/tmp/_hf.aux','/tmp/_h.c'],check=True)
protos={}
for line in open('/tmp/_hf.aux'):
    if not line.startswith('/* /tmp/_h.c'): continue
    decl=line.split('*/',1)[1].strip()
    m=re.search(r'([A-Za-z_][A-Za-z0-9_]*)\s*\((?!\*)',decl.replace('extern ',''))
    names=re.findall(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*\(',decl)
    if names: protos[names[0] if not decl.startswith('extern') else names[0]]=decl
fnames=set()
for line in open('/tmp/_hf.aux'):
    if line.startswith('/* /tmp/_h.c'):
        d=line.split('*/',1)[1]
        mm=re.match(r'\s*(?:extern\s+)?[^(]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*\(',d)
        if mm: fnames.add(mm.group(1))
stdset=sorted(n for n in fnames if n in std)
extset=sorted(n for n in fnames if n not in std and not n.startswith('__'))
print('libc functions:',len(fnames),'| standard (declared by glibc headers):',len(stdset),'| NoovexOS extras:',len(extset))
open('/tmp/stdset.txt','w').write('\n'.join(stdset)); open('/tmp/extset.txt','w').write('\n'.join(extset))
stdset=set(open('/tmp/stdset.txt').read().split())|{'FD_CLR','FD_ISSET','FD_SET','FD_ZERO'}
decls={}
for line in open('/tmp/_hf.aux'):
    if not line.startswith('/* /tmp/_h.c'): continue
    d=line.split('*/',1)[1].strip()
    mm=re.match(r'\s*(?:extern\s+)?[^(]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*\(',d)
    if mm and mm.group(1) in fnames: decls[mm.group(1)]=d.replace('__va_list_tag *','va_list')
def drop_protos(s,names):
    out=[]; buf=[]; i=0; n=len(s); depth=0
    while i<n:
        if s.startswith('/*',i): j=s.find('*/',i+2); j=n if j<0 else j+2; buf.append(s[i:j]); i=j; continue
        c=s[i]
        if c in '"\'':
            j=i+1
            while j<n and s[j]!=c: j+=2 if s[j]=='\\' else 1
            buf.append(s[i:j+1]); i=j+1; continue
        if s.startswith('#',i) and (i==0 or s[i-1]=='\n') and depth==0:
            j=i
            while True:
                k=s.find('\n',j); k=n if k<0 else k
                if k>0 and s[k-1]=='\\': j=k+1; continue
                break
            out.append(''.join(buf)); buf=[]; out.append(s[i:k]); i=k; continue
        if c in '{(': depth+=1
        elif c in '})': depth-=1
        buf.append(c); i+=1
        if c==';' and depth==0:
            st=''.join(buf); buf=[]
            t=st.strip()
            mm=re.match(r'(?:extern\s+)?[^(=;{]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*\(',t)
            if mm and mm.group(1) in names and not t.startswith('typedef') and '(*' not in t[:t.find('(')+2]:
                out.append(st[:len(st)-len(st.lstrip())]); continue
            out.append(st)
    out.append(''.join(buf)); return ''.join(out)
types=drop_protos(open('libc/include/nvx/_all.h').read(),fnames)
open('libc/include/nvx/_types.h','w').write('#ifndef NVX_TYPES_H\n#define NVX_TYPES_H\n#include <stddef.h>\n#include <stdint.h>\n#include <stdarg.h>\n'+types+'\nextern void __nvx_sigtramp(void);\n#endif\n')
std_p='\n'.join(decls[n] for n in sorted(decls) if n in stdset)
ext_p='\n'.join(decls[n] for n in sorted(decls) if n not in stdset and not n.startswith('__'))
open('libc/include/nvx/_std.h','w').write('#ifndef NVX_STD_H\n#define NVX_STD_H\n#include <nvx/_types.h>\n'+std_p+'\n#endif\n')
open('libc/include/nvx/extras.h','w').write('#ifndef NVX_EXTRAS_H\n#define NVX_EXTRAS_H\n#include <nvx/_std.h>\n'+ext_p+'\n#endif\n')
hdrs=['stdio.h','stdlib.h','string.h','strings.h','unistd.h','fcntl.h','errno.h','ctype.h','time.h','signal.h','math.h','dirent.h','termios.h','getopt.h','pthread.h','poll.h','libgen.h','utime.h','spawn.h','dlfcn.h','sys/stat.h','sys/types.h','sys/wait.h','sys/time.h','sys/select.h','sys/socket.h','sys/mount.h','arpa/inet.h','netinet/in.h']
for h in hdrs:
    p='libc/include/'+h; os.makedirs(os.path.dirname(p),exist_ok=True)
    g='NVX_'+re.sub(r'[^A-Z0-9]','_',h.upper())
    open(p,'w').write(f'#ifndef {g}\n#define {g}\n#include <nvx/_std.h>\n#endif\n')
print('headers written:',len(hdrs)+4,'| std prototypes:',std_p.count('\n')+1,'| extras:',ext_p.count('\n')+1)
def remove_funcs(s,names):
    out=[]; i=0; n=len(s); depth=0; start=0; removed=[]
    while i<n:
        if s.startswith('//',i): j=s.find('\n',i); j=n if j<0 else j; i=j; continue
        if s.startswith('/*',i): j=s.find('*/',i+2); j=n if j<0 else j+2; i=j; continue
        c=s[i]
        if c in '"\'':
            j=i+1
            while j<n and s[j]!=c: j+=2 if s[j]=='\\' else 1
            i=j+1; continue
        if s.startswith('#',i) and (i==0 or s[i-1]=='\n') and depth==0:
            j=i
            while True:
                k=s.find('\n',j); k=n if k<0 else k
                if k>0 and s[k-1]=='\\': j=k+1; continue
                break
            i=k; start=i; continue
        if depth==0 and c in ';}': start=i+1
        if c=='{':
            if depth==0:
                hdr=s[start:i]
                mm=re.search(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*\([^()]*(?:\([^()]*\)[^()]*)*\)\s*$',hdr)
                d=1; j=i+1
                while j<n and d:
                    if s.startswith('//',j): jj=s.find('\n',j); j=n if jj<0 else jj; continue
                    if s.startswith('/*',j): jj=s.find('*/',j+2); j=n if jj<0 else jj+2; continue
                    if s[j] in '"\'':
                        q=s[j]; j+=1
                        while j<n and s[j]!=q: j+=2 if s[j]=='\\' else 1
                        j+=1; continue
                    if s[j]=='{': d+=1
                    elif s[j]=='}': d-=1
                    j+=1
                if mm and mm.group(1) in names:
                    out.append((start,j)); removed.append(mm.group(1))
                i=j; start=j; continue
            depth+=1
        elif c=='}': depth-=1
        i+=1
    res=[]; last=0
    for a,b in out: res.append(s[last:a]); last=b
    res.append(s[last:]); return ''.join(res),removed
PF={'isspace','strtol','strtoul','strtoll','strtoull','strtoimax','strtoumax','strtod','strtof','strtold','atof','atoi','atol','atoll','strftime','sscanf','vsscanf','vsscanf_impl','fscanf','scanf','vfscanf','vscanf','printf','fprintf','sprintf','snprintf','vsnprintf','vprintf','vfprintf','vfprintf_f','__stdout_vprintf','dprintf','vdprintf_impl','asprintf','vasprintf','vsprintf','vdprintf'}
lc=open('libc/src/libc.c').read() if os.path.exists('libc/src/libc.c') else open('src/libc.c').read()
lc,rm=remove_funcs(lc,PF)
open('libc/src/libc.c' if os.path.exists('libc/src/libc.c') else 'src/libc.c','w').write(lc)
print('old printf family removed from libc.c:',sorted(rm))
