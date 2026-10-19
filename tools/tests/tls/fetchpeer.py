import ssl,socket,threading,datetime,ipaddress,os,sys,time,base64,re
from cryptography import x509
from cryptography.x509.oid import NameOID
from cryptography.hazmat.primitives import hashes,serialization
from cryptography.hazmat.primitives.asymmetric import ec,rsa
W=sys.argv[1]; os.makedirs(W,exist_ok=True)
now=datetime.datetime.now(datetime.timezone.utc)
nm=lambda cn: x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,cn)])
ck=ec.generate_private_key(ec.SECP256R1())
ca=x509.CertificateBuilder().subject_name(nm('Fetch Test CA')).issuer_name(nm('Fetch Test CA')).public_key(ck.public_key()).serial_number(1).not_valid_before(now-datetime.timedelta(days=1)).not_valid_after(now+datetime.timedelta(days=30)).add_extension(x509.BasicConstraints(ca=True,path_length=None),critical=True).sign(ck,hashes.SHA256())
open(os.path.join(W,'ca.pem'),'wb').write(ca.public_bytes(serialization.Encoding.PEM))
BIG=bytes(((i*7+(i>>8))&0xFF) for i in range(300000))
def handle(t,d):
    if b'GET /redir ' in d: t.sendall(b'HTTP/1.1 302 Found\r\nLocation: /chunked\r\nContent-Length: 0\r\n\r\n')
    elif b'GET /chunked ' in d:
        out=b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n'; o=0
        while o<len(BIG): n=min(7000,len(BIG)-o); out+=b'%x\r\n'%n+BIG[o:o+n]+b'\r\n'; o+=n
        t.sendall(out+b'0\r\n\r\n')
    elif b'GET /missing ' in d: t.sendall(b'HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nnot found')
    else: t.sendall(b'HTTP/1.0 200 OK\r\nContent-Length: %d\r\n\r\n'%len(BIG)+BIG)
def server(kind,san,port,days=30,start=-1):
    key=ec.generate_private_key(ec.SECP256R1()) if kind=='ec' else rsa.generate_private_key(public_exponent=65537,key_size=2048)
    names=[x509.DNSName(n) if not n[0].isdigit() else x509.IPAddress(ipaddress.ip_address(n)) for n in san]
    leaf=x509.CertificateBuilder().subject_name(nm(san[0])).issuer_name(nm('Fetch Test CA')).public_key(key.public_key()).serial_number(x509.random_serial_number()).not_valid_before(now+datetime.timedelta(days=start)).not_valid_after(now+datetime.timedelta(days=start+days)).add_extension(x509.SubjectAlternativeName(names),critical=False).sign(ck,hashes.SHA256())
    cp=os.path.join(W,'%d.pem'%port); kp=os.path.join(W,'%d.key'%port)
    open(cp,'wb').write(leaf.public_bytes(serialization.Encoding.PEM)); open(kp,'wb').write(key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption()))
    ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.minimum_version=ssl.TLSVersion.TLSv1_2; ctx.maximum_version=ssl.TLSVersion.TLSv1_2; ctx.load_cert_chain(cp,kp)
    s=socket.socket(); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); s.bind(('127.0.0.1',port)); s.listen(8)
    def loop():
        while True:
            c,a=s.accept()
            def h(c):
                try:
                    t=ctx.wrap_socket(c,server_side=True); d=b''
                    while b'\r\n\r\n' not in d:
                        x=t.recv(4096)
                        if not x: break
                        d+=x
                    handle(t,d); t.close()
                except Exception: pass
            threading.Thread(target=h,args=(c,),daemon=True).start()
    threading.Thread(target=loop,daemon=True).start()
base=int(sys.argv[2]) if len(sys.argv)>2 else 19443
server('ec',['localhost','127.0.0.1'],base)
server('rsa',['localhost','127.0.0.1'],base+1)
server('ec',['other.test'],base+2)
server('ec',['localhost'],base+3,days=1,start=-5)
def anchors(src,out):
    def tlv(b,o):
        t=b[o]; l=b[o+1]; h=2
        if l&0x80: n=l&0x7f; l=int.from_bytes(b[o+2:o+2+n],'big'); h=2+n
        return t,o+h,l,o+h+l
    def items(b,o,end):
        while o<end:
            t,vs,l,nx=tlv(b,o); yield t,o,vs,l,nx; o=nx
    lines=['# test anchors']; n=0
    for label,b64 in re.findall(r'(?:# Label: "([^"]*)"\s*\n(?:#[^\n]*\n)*)?-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----',open(src).read(),re.S):
        der=base64.b64decode(''.join(b64.split()))
        t,vs,l,_=tlv(der,0); t,tvs,tl,_=tlv(der,vs); it=list(items(der,tvs,tvs+tl))
        if it[0][0]==0xA0: it=it[1:]
        subj=der[it[4][1]:it[4][4]]; spki=der[it[5][1]:it[5][4]]
        ok=(bytes.fromhex('2a864886f70d010101') in spki) or (bytes.fromhex('2a8648ce3d0201') in spki and (bytes.fromhex('2a8648ce3d030107') in spki or bytes.fromhex('2b81040022') in spki))
        if not ok: continue
        b=lambda x: '\n'.join(base64.b64encode(x).decode()[i:i+64] for i in range(0,len(base64.b64encode(x)),64))
        lines+=['# '+label,'-----BEGIN TRUST ANCHOR NAME-----',b(subj),'-----END TRUST ANCHOR NAME-----','-----BEGIN PUBLIC KEY-----',b(spki),'-----END PUBLIC KEY-----']; n+=1
    open(out,'w').write('\n'.join(lines)+'\n'); return n
if len(sys.argv)>3: print('anchors',anchors(sys.argv[3],os.path.join(W,'anchors.pem')),flush=True)
print('peer up',flush=True)
while True: time.sleep(3600)
