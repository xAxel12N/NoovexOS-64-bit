import sys,os,subprocess,datetime,ipaddress
from cryptography import x509
from cryptography.x509.oid import NameOID
from cryptography.hazmat.primitives import hashes,serialization
from cryptography.hazmat.primitives.asymmetric import rsa,ec,padding
H=sys.argv[1]; W=sys.argv[2]; os.makedirs(W,exist_ok=True)
now=datetime.datetime(2026,6,1,tzinfo=datetime.timezone.utc)
def name(cn): return x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,cn),x509.NameAttribute(NameOID.ORGANIZATION_NAME,'Noovex Test')])
def cert(subj,key,issuer,ikey,h,ca,san=None,nb=None,na=None):
    b=x509.CertificateBuilder().subject_name(name(subj)).issuer_name(name(issuer)).public_key(key.public_key()).serial_number(x509.random_serial_number())
    b=b.not_valid_before(nb or now-datetime.timedelta(days=30)).not_valid_after(na or now+datetime.timedelta(days=60))
    b=b.add_extension(x509.BasicConstraints(ca=ca,path_length=None),critical=True)
    if san: b=b.add_extension(x509.SubjectAlternativeName(san),critical=False)
    return b.sign(ikey,h)
def save(c,n):
    p=os.path.join(W,n); open(p,'wb').write(c.public_bytes(serialization.Encoding.DER)); return p
r4=rsa.generate_private_key(public_exponent=65537,key_size=4096); r2=rsa.generate_private_key(public_exponent=65537,key_size=2048)
e256=ec.generate_private_key(ec.SECP256R1()); e384=ec.generate_private_key(ec.SECP384R1()); e384b=ec.generate_private_key(ec.SECP384R1()); r2b=rsa.generate_private_key(public_exponent=65537,key_size=3072)
san=[x509.DNSName('a.test'),x509.DNSName('*.wild.test'),x509.IPAddress(ipaddress.ip_address('10.0.2.2'))]
root1=save(cert('Root RSA',r4,'Root RSA',r4,hashes.SHA256(),True),'root1.der')
int1=save(cert('Int RSA',r2,'Root RSA',r4,hashes.SHA384(),True),'int1.der')
leaf1=save(cert('a.test',e256,'Int RSA',r2,hashes.SHA256(),False,san),'leaf1.der')
root2=save(cert('Root EC',e384,'Root EC',e384,hashes.SHA384(),True),'root2.der')
int2=save(cert('Int EC',e384b,'Root EC',e384,hashes.SHA384(),True),'int2.der')
leaf2=save(cert('b.test',r2b,'Int EC',e384b,hashes.SHA384(),False,[x509.DNSName('b.test')]),'leaf2.der')
leaf3=save(cert('c.test',e256,'Int EC',e384b,hashes.SHA512(),False,[x509.DNSName('c.test')]),'leaf3.der')
nonca=save(cert('Int RSA',r2,'Root RSA',r4,hashes.SHA384(),False),'nonca.der')
expl=save(cert('a.test',e256,'Int RSA',r2,hashes.SHA256(),False,san,now-datetime.timedelta(days=90),now-datetime.timedelta(days=1)),'expired.der')
nosan=save(cert('cn.test',e256,'Int RSA',r2,hashes.SHA256(),False),'nosan.der')
d=bytearray(open(leaf1,'rb').read()); d[-5]^=1; open(os.path.join(W,'bad.der'),'wb').write(d); bad=os.path.join(W,'bad.der')
T=str(int(now.timestamp()))
fails=0
def chk(desc,args,want):
    global fails
    out=subprocess.run([H,'chain']+args,capture_output=True,text=True).stdout.strip()
    code=int(out.split()[0]) if out else 999
    ok=(code==want); fails+=not ok
    print(('ok   ' if ok else 'FAIL ')+desc+' -> '+out)
chk('RSA4096 root, RSA2048 int (sha384), P-256 leaf',['a.test',T,leaf1,int1,'-a',root1],0)
chk('wildcard *.wild.test matches x.wild.test',['x.wild.test',T,leaf1,int1,'-a',root1],0)
chk('wildcard does not match two labels',['y.z.wild.test',T,leaf1,int1,'-a',root1],-12)
chk('wildcard does not match bare domain',['wild.test',T,leaf1,int1,'-a',root1],-12)
chk('IP address SAN 10.0.2.2',['10.0.2.2',T,leaf1,int1,'-a',root1],0)
chk('wrong host name',['evil.test',T,leaf1,int1,'-a',root1],-12)
chk('upper-case host name',['A.TEST',T,leaf1,int1,'-a',root1],0)
chk('unknown root is rejected',['a.test',T,leaf1,int1],-11)
chk('missing intermediate is rejected',['a.test',T,leaf1,'-a',root1],-11)
chk('chain sent out of order with root',['a.test',T,leaf1,root1,int1,'-a',root1],0)
chk('P-384 root, P-384 int, RSA3072 leaf',['b.test',T,leaf2,int2,'-a',root2],0)
chk('P-256 leaf signed ecdsa-sha512 by P-384',['c.test',T,leaf3,int2,'-a',root2],0)
chk('intermediate without CA flag rejected',['a.test',T,leaf1,nonca,'-a',root1],-11)
chk('expired leaf rejected',['a.test',T,expl,int1,'-a',root1],-13)
chk('clock unknown (now=0) skips dates',['a.test','0',expl,int1,'-a',root1],0)
chk('tampered signature rejected',['a.test',T,bad,int1,'-a',root1],-11)
chk('CN fallback when no SAN',['cn.test',T,nosan,int1,'-a',root1],0)
chk('cross-root confusion rejected',['b.test',T,leaf2,int2,'-a',root1],-11)
import hashlib
for kname,key,spkin in (('e256',e256,'s1'),('r2b',r2b,'s2'),('e384',e384,'s3')):
    open(os.path.join(W,spkin+'.der'),'wb').write(key.public_key().public_bytes(serialization.Encoding.DER,serialization.PublicFormat.SubjectPublicKeyInfo))
def cv(desc,spk,scheme,key,hname,want,tamper=False):
    global fails
    th=os.urandom(32); content=b' '*64+b'TLS 1.3, server CertificateVerify\x00'+th
    h={'sha256':hashes.SHA256(),'sha384':hashes.SHA384(),'sha512':hashes.SHA512()}[hname]
    if isinstance(key,rsa.RSAPrivateKey): sig=key.sign(content,padding.PSS(mgf=padding.MGF1(h),salt_length=h.digest_size),h)
    else: sig=key.sign(content,ec.ECDSA(h))
    if tamper: th=bytes([th[0]^1])+th[1:]
    open(os.path.join(W,'sig'),'wb').write(sig); open(os.path.join(W,'th'),'wb').write(th)
    out=subprocess.run([H,'cv',os.path.join(W,spk+'.der'),scheme,os.path.join(W,'sig'),os.path.join(W,'th')],capture_output=True,text=True).stdout.strip()
    ok=out==str(want); fails+=not ok; print(('ok   ' if ok else 'FAIL ')+'CertificateVerify '+desc+' -> '+out)
for i in range(3): cv('ecdsa_secp256r1_sha256 #%d'%i,'s1','0403',e256,'sha256',0)
cv('ecdsa_secp384r1_sha384','s3','0503',e384,'sha384',0)
for i in range(3): cv('rsa_pss_rsae_sha256 #%d'%i,'s2','0804',r2b,'sha256',0)
cv('rsa_pss_rsae_sha384','s2','0805',r2b,'sha384',0)
cv('rsa_pss_rsae_sha512','s2','0806',r2b,'sha512',0)
cv('ecdsa with altered transcript','s1','0403',e256,'sha256',-1,True)
cv('rsa-pss with altered transcript','s2','0804',r2b,'sha256',-1,True)
cv('scheme/key mismatch','s1','0804',e256,'sha256',-1)
cv('pkcs1 scheme refused in TLS 1.3','s2','0401',r2b,'sha256',-1)
print('ALL PASS' if not fails else '%d FAILED'%fails)
sys.exit(1 if fails else 0)
