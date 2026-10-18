import os,sys,ssl,socket,struct,fcntl,threading,time,datetime,ipaddress
TAP=os.environ.get('NVX_TAP_NAME','nvxtap0').encode()
W=sys.argv[1] if len(sys.argv)>1 else '/tmp/nvx_netpeer'
os.makedirs(W,exist_ok=True)
LOG=open(os.path.join(W,'peer.log'),'a')
def L(*a): LOG.write(' '.join(map(str,a))+'\n'); LOG.flush()
def mktap():
    fd=os.open('/dev/net/tun',os.O_RDWR)
    fcntl.ioctl(fd,0x400454ca,struct.pack('16sH',TAP,0x0002|0x1000)); fcntl.ioctl(fd,0x400454cb,1); os.close(fd)
    s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
    def setaddr(ifn,ip):
        fcntl.ioctl(s,0x8916,struct.pack('16s',ifn)+struct.pack('HH4s8x',socket.AF_INET,0,socket.inet_aton(ip)))
        fcntl.ioctl(s,0x891c,struct.pack('16s',ifn)+struct.pack('HH4s8x',socket.AF_INET,0,socket.inet_aton('255.255.255.0')))
        fl=struct.unpack('16sH',fcntl.ioctl(s,0x8913,struct.pack('16sH',ifn,0)))[1]
        fcntl.ioctl(s,0x8914,struct.pack('16sH',ifn,fl|1|0x40))
    setaddr(TAP,'10.0.2.2'); setaddr(TAP+b':1','10.0.2.3')
def pat(i): return (i*7+(i>>8))&0xFF
BIG=bytes(pat(i) for i in range(300000))
def listen(port):
    s=socket.socket(); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); s.bind(('10.0.2.2',port)); s.listen(16); return s
def http_reply(c,d):
    if b'GET /redir ' in d:
        c.sendall(b'HTTP/1.1 302 Found\r\nLocation: /big\r\nContent-Length: 0\r\nConnection: close\r\n\r\n'); return
    if b'GET /chunked ' in d:
        out=b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n'; o=0
        while o<len(BIG): n=min(7000,len(BIG)-o); out+=b'%x\r\n'%n+BIG[o:o+n]+b'\r\n'; o+=n
        c.sendall(out+b'0\r\n\r\n'); return
    body=BIG if b'/big' in d else b'<html><body>hello from the NoovexOS test peer</body></html>\n'
    c.sendall(b'HTTP/1.0 200 OK\r\nContent-Length: %d\r\n\r\n'%len(body)+body)
def read_req(c):
    d=b''
    while b'\r\n\r\n' not in d:
        x=c.recv(4096)
        if not x: break
        d+=x
    return d
def serve(port,handler):
    s=listen(port)
    while True:
        c,a=s.accept()
        threading.Thread(target=handler,args=(c,a),daemon=True).start()
def h_http(c,a):
    try: d=read_req(c); L('http',port_of(c),d[:40]); http_reply(c,d)
    finally: c.close()
def port_of(c): return c.getsockname()[1]
def h_echo(c,a):
    n=0
    while True:
        x=c.recv(65536)
        if not x: break
        c.sendall(x); n+=len(x)
    L('echo',n); c.close()
def udp_echo():
    s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.bind(('10.0.2.2',7778))
    while True:
        d,a=s.recvfrom(2048); L('udp',d); s.sendto(d.upper(),a)
def dns():
    s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.bind(('10.0.2.3',53))
    while True:
        d,a=s.recvfrom(2048); tid=d[:2]; q=d[12:]; i=0
        while q[i]: i+=1+q[i]
        qd=q[:i+5]; qt=struct.unpack('>H',q[i+1:i+3])[0]
        name=[]; j=0
        while q[j]: name.append(q[j+1:j+1+q[j]].decode()); j+=1+q[j]
        L('dns','.'.join(name),qt)
        if qt==1: r=tid+b'\x81\x80'+struct.pack('>HHHH',1,1,0,0)+qd+b'\xc0\x0c'+struct.pack('>HHIH',1,1,60,4)+socket.inet_aton('10.0.2.2')
        else: r=tid+b'\x81\x80'+struct.pack('>HHHH',1,0,0,0)+qd
        s.sendto(r,a)
def make_pki():
    from cryptography import x509
    from cryptography.x509.oid import NameOID
    from cryptography.hazmat.primitives import hashes,serialization
    from cryptography.hazmat.primitives.asymmetric import ec,rsa
    now=datetime.datetime.now(datetime.timezone.utc)
    nm=lambda cn: x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,cn)])
    ck=ec.generate_private_key(ec.SECP256R1())
    ca=x509.CertificateBuilder().subject_name(nm('Noovex Test CA')).issuer_name(nm('Noovex Test CA')).public_key(ck.public_key()).serial_number(1).not_valid_before(now-datetime.timedelta(days=1)).not_valid_after(now+datetime.timedelta(days=3650)).add_extension(x509.BasicConstraints(ca=True,path_length=None),critical=True).sign(ck,hashes.SHA256())
    open(os.path.join(W,'ca.pem'),'wb').write(ca.public_bytes(serialization.Encoding.PEM))
    out={}
    for kind,key in (('ec',ec.generate_private_key(ec.SECP256R1())),('rsa',rsa.generate_private_key(public_exponent=65537,key_size=2048))):
        leaf=x509.CertificateBuilder().subject_name(nm('noovex.test')).issuer_name(nm('Noovex Test CA')).public_key(key.public_key()).serial_number(x509.random_serial_number()).not_valid_before(now-datetime.timedelta(days=1)).not_valid_after(now+datetime.timedelta(days=30)).add_extension(x509.SubjectAlternativeName([x509.DNSName('noovex.test'),x509.IPAddress(ipaddress.ip_address('10.0.2.2'))]),critical=False).sign(ck,hashes.SHA256())
        cp=os.path.join(W,kind+'.pem'); kp=os.path.join(W,kind+'.key')
        open(cp,'wb').write(leaf.public_bytes(serialization.Encoding.PEM))
        open(kp,'wb').write(key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption()))
        ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.minimum_version=ssl.TLSVersion.TLSv1_3; ctx.load_cert_chain(cp,kp); out[kind]=ctx
        c12=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); c12.minimum_version=ssl.TLSVersion.TLSv1_2; c12.maximum_version=ssl.TLSVersion.TLSv1_2; c12.load_cert_chain(cp,kp); out[kind+'12']=c12
    return out
def tls_server(port,ctx):
    s=listen(port)
    while True:
        c,a=s.accept()
        def h(c):
            try:
                t=ctx.wrap_socket(c,server_side=True); d=read_req(t); L('https',port,d[:40]); http_reply(t,d); t.close()
            except Exception as e: L('tls-err',port,repr(e)[:120])
        threading.Thread(target=h,args=(c,),daemon=True).start()
def inbound():
    while True:
        time.sleep(1)
        try:
            c=socket.create_connection(('10.0.2.15',9200),timeout=3); c.sendall(b'ping from host\n'); r=b''
            while not r.endswith(b'\n'):
                x=c.recv(256)
                if not x: break
                r+=x
            c.close(); L('inbound',r.decode(errors='replace').strip())
        except Exception: pass
if __name__=='__main__':
    if os.environ.get('NVX_TAP_CREATE','1')=='1': mktap()
    ctxs=make_pki()
    for f,a in ((serve,(80,h_http)),(serve,(8080,h_http)),(serve,(7777,h_echo)),(udp_echo,()),(dns,()),(tls_server,(443,ctxs['ec'])),(tls_server,(8443,ctxs['rsa'])),(tls_server,(9443,ctxs['ec12'])),(tls_server,(9444,ctxs['rsa12'])),(inbound,())):
        threading.Thread(target=f,args=a,daemon=True).start()
    L('peer up'); print('peer up',flush=True)
    while True: time.sleep(3600)
