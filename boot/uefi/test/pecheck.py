import struct,sys
def check(path):
    d=open(path,'rb').read(); errs=[]
    if d[:2]!=b'MZ': return ['no MZ header']
    pe=struct.unpack_from('<I',d,0x3c)[0]
    if d[pe:pe+4]!=b'PE\0\0': return ['no PE signature']
    mach,nsec,_,_,_,optsz,ch=struct.unpack_from('<HHIIIHH',d,pe+4); o=pe+24
    magic=struct.unpack_from('<H',d,o)[0]; entry=struct.unpack_from('<I',d,o+16)[0]; ib=struct.unpack_from('<Q',d,o+24)[0]
    sa,fa=struct.unpack_from('<II',d,o+32); soi,soh=struct.unpack_from('<II',d,o+56); sub=struct.unpack_from('<H',d,o+68)[0]; nd=struct.unpack_from('<I',d,o+108)[0]
    if mach!=0x8664: errs.append('machine is not x86-64')
    if magic!=0x20b: errs.append('not PE32+')
    if sub!=10: errs.append('subsystem is not EFI application (10)')
    if not ch&2: errs.append('not marked executable')
    if soi%sa: errs.append('SizeOfImage not a multiple of SectionAlignment')
    secs=[]; st=o+optsz
    for i in range(nsec):
        nm=d[st+i*40:st+i*40+8].rstrip(b'\0').decode(); vs,va,rs,rp=struct.unpack_from('<IIII',d,st+i*40+8); f=struct.unpack_from('<I',d,st+i*40+36)[0]
        secs.append((nm,va,vs,rs,rp,f))
        if va<soh: errs.append(f'section {nm} at VA {va:#x} overlaps the headers (SizeOfHeaders {soh:#x})')
        if va%sa: errs.append(f'section {nm} VA {va:#x} not aligned to {sa:#x}')
        if va+max(vs,rs)>soi: errs.append(f'section {nm} extends past SizeOfImage')
        if rp%fa: errs.append(f'section {nm} raw data not file-aligned')
        if rp+rs>len(d): errs.append(f'section {nm} raw data past end of file')
    for a,b in zip(secs,secs[1:]):
        if a[1]+max(a[2],a[3] if a[3]<=((a[2]+sa-1)//sa*sa) else a[2])>b[1]: errs.append(f'sections {a[0]} and {b[0]} overlap')
    es=[s for s in secs if s[1]<=entry<s[1]+s[2]]
    if not es: errs.append('entry point is outside every section')
    elif not es[0][5]&0x20000000: errs.append('entry point section is not executable')
    if nd>5:
        rva,rsz=struct.unpack_from('<II',d,o+112+5*8)
        if rsz:
            rs=[s for s in secs if s[1]<=rva<s[1]+max(s[2],1)]
            if not rs: errs.append('relocation directory outside sections')
            else:
                s=rs[0]; off=s[4]+(rva-s[1]); end=off+rsz
                while off<end:
                    pg,bs=struct.unpack_from('<II',d,off)
                    if bs<8 or bs%2 or off+bs>end+0: errs.append(f'bad relocation block size {bs}'); break
                    off+=bs
    return errs,dict(entry=entry,soh=soh,soi=soi,sa=sa,secs=secs)
if __name__=="__main__":
    errs,info=check(sys.argv[1])
    for s in info['secs']: print(f"  {s[0]:8s} VA {s[1]:#7x} size {s[2]:#7x} file@{s[4]:#7x}")
    print('  PE check:', 'VALID' if not errs else 'INVALID'); [print('   -',e) for e in errs]
