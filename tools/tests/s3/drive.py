import json, os, shutil, socket, subprocess, sys, time
o = sys.argv[1]; q = os.environ.get('NVX_QEMU') or shutil.which('qemu-system-x86_64')
if not q: print('S3 HOST TEST: SKIP no qemu'); sys.exit(0)
log = os.path.join(o, 'serial.log'); qmp = os.path.join(o, 'qmp')
for p in (log, qmp):
    if os.path.exists(p): os.unlink(p)
pr = subprocess.Popen([q, '-m', '256', '-kernel', os.path.join(o, 's3t.img'), '-global', 'PIIX4_PM.disable_s3=0', '-device', 'isa-debug-exit,iobase=0xf4,iosize=4',
                       '-serial', 'file:' + log, '-display', 'none', '-nic', 'none', '-qmp', 'unix:' + qmp + ',server,nowait'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
def ser():
    try: return open(log, errors='replace').read()
    except OSError: return ''
def wait(t, s):
    e = time.time() + s
    while time.time() < e:
        if t in ser() or pr.poll() is not None: return t in ser()
        time.sleep(0.2)
    return False
try:
    if not wait('s3t: entering S3', 30): print(ser()); print('S3 HOST TEST: FAIL did not reach S3 entry'); sys.exit(1)
    for _ in range(50):
        if os.path.exists(qmp): break
        time.sleep(0.1)
    s = socket.socket(socket.AF_UNIX); s.connect(qmp); f = s.makefile('rw')
    f.readline(); f.write(json.dumps({'execute': 'qmp_capabilities'}) + '\n'); f.flush(); f.readline()
    st = ''
    for _ in range(50):
        f.write(json.dumps({'execute': 'query-status'}) + '\n'); f.flush()
        while True:
            r = json.loads(f.readline())
            if 'return' in r or 'error' in r: break
        st = r.get('return', {}).get('status', '')
        if st == 'suspended' or 'FAIL' in ser(): break
        time.sleep(0.2)
    print('guest status before wakeup: ' + st)
    if st != 'suspended': print(ser()); print('S3 HOST TEST: FAIL guest did not suspend'); sys.exit(1)
    key = os.environ.get('NVX_S3_WAKE') == 'key'
    f.write(json.dumps({'execute': 'send-key', 'arguments': {'keys': [{'type': 'qcode', 'data': 'shift'}]}} if key else {'execute': 'system_wakeup'}) + '\n'); f.flush()
    wait('S3 HOST TEST', 30)
    if key: print('woken by a PS/2 key press' if 'RESUME PASS' in ser() else 'a PS/2 key press did not wake the guest')
    print(ser().strip())
    sys.exit(0 if 'RESUME PASS' in ser() else 1)
finally:
    pr.kill(); pr.wait()
