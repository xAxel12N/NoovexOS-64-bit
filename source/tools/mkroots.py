import sys,re,base64,hashlib
def tlv(b,o):
    t=b[o]; l=b[o+1]; h=2
    if l&0x80:
        n=l&0x7f; l=int.from_bytes(b[o+2:o+2+n],'big'); h=2+n
    return t,o+h,l,o+h+l
def items(b,o,end):
    while o<end:
        t,vs,l,nx=tlv(b,o); yield t,o,vs,l,nx; o=nx
def anchor(der):
    t,vs,l,_=tlv(der,0); t,tvs,tl,_=tlv(der,vs)
    it=list(items(der,tvs,tvs+tl))
    if it[0][0]==0xA0: it=it[1:]
    issuer,validity,subject,spki=it[2],it[3],it[4],it[5]
    return der[subject[1]:subject[4]],der[spki[1]:spki[4]],spki
EC=bytes.fromhex('2a8648ce3d0201'); RSA=bytes.fromhex('2a864886f70d010101')
P256=bytes.fromhex('2a8648ce3d030107'); P384=bytes.fromhex('2b81040022')
def supported(spki):
    s=spki
    if RSA in s:
        return True
    if EC in s: return P256 in s or P384 in s
    return False
src=sys.argv[1]; out=sys.argv[2]
pem=open(src).read()
blocks=re.findall(r'(?:# Label: "([^"]*)"\s*\n(?:#[^\n]*\n)*)?-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----',pem,re.S)
recs=[];names=[];skipped=[]
for label,b64 in blocks:
    der=base64.b64decode(''.join(b64.split()))
    subj,spki,_=anchor(der)
    if not supported(spki): skipped.append(label); continue
    recs.append((subj,spki)); names.append(label)
blob=bytearray(); offs=[]
for s,k in recs:
    offs.append(len(blob)); blob+=len(s).to_bytes(2,'big')+s+len(k).to_bytes(2,'big')+k
with open(out,'w') as f:
    f.write('#include "include/nvx/base.h"\n#include "include/nvx/lib/crypto/nvxx509.h"\n#include "include/nvx/lib/crypto/nvxroots.h"\n')
    f.write('#define NVX_NROOTS %d\n'%len(recs))
    f.write('const char* const nvx_root_names[NVX_NROOTS]={\n'+',\n'.join('"%s"'%''.join(ch for ch in n if 32<=ord(ch)<127 and ch not in '\\"') for n in names)+'};\n')
    f.write('const unsigned int nvx_root_off[NVX_NROOTS]={'+','.join(map(str,offs))+'};\n')
    f.write('const unsigned char nvx_root_blob[%d]={\n'%len(blob))
    for i in range(0,len(blob),24): f.write(','.join('0x%02x'%x for x in blob[i:i+24])+',\n')
    f.write('};\n')
    f.write('int nvx_root_get(int i,x509_anchor* a){ if(i<0||i>=NVX_NROOTS)return 0; const unsigned char* p=nvx_root_blob+nvx_root_off[i]; a->subjlen=(p[0]<<8)|p[1]; a->subj=p+2; p+=2+a->subjlen; a->spkilen=(p[0]<<8)|p[1]; a->spki=p+2; return 1; }\n')
print('roots:',len(recs),'skipped:',skipped,'blob bytes:',len(blob))
