import os, sys, re, time, json, socket, shutil, subprocess, tempfile, fcntl, atexit

SLOTS = int(os.environ.get('NVXQA_SLOTS', '2'))
BAD = re.compile(r'(?i)kernel panic|panic: |page fault at|general protection|double fault|stack overflow (at|in|on cpu)|see the crash report|freeze report|watchdog: main loop')
KEYS = {' ': 'spc', '\n': 'ret', '\t': 'tab', '-': 'minus', '/': 'slash', '.': 'dot', ',': 'comma', ';': 'semicolon', "'": 'apostrophe', '=': 'equal',
        '[': 'bracket_left', ']': 'bracket_right', '\\': 'backslash', '`': 'grave_accent',
        '>': 'shift-dot', '<': 'shift-comma', '&': 'shift-7', ':': 'shift-semicolon', '|': 'shift-backslash', '_': 'shift-minus', '+': 'shift-equal',
        '"': 'shift-apostrophe', '?': 'shift-slash', '!': 'shift-1', '@': 'shift-2', '#': 'shift-3', '$': 'shift-4', '%': 'shift-5', '^': 'shift-6',
        '*': 'shift-8', '(': 'shift-9', ')': 'shift-0', '{': 'shift-bracket_left', '}': 'shift-bracket_right', '~': 'shift-grave_accent'}
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))


def _slot():
    while True:
        for i in range(SLOTS):
            f = open(f'/tmp/nvxqa-slot{i}.lock', 'w')
            try:
                fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB); return i, f
            except OSError:
                f.close()
        time.sleep(5)


class VM:
    def __init__(self, name, out, iso=None, mem=1536, smp=1, extra=(), disk_mb=64, w=1280, h=800):
        self.name, self.out, self.w, self.h = name, os.path.abspath(out), w, h
        os.makedirs(self.out, exist_ok=True)
        self.iso = iso or os.environ.get('NVXQA_ISO') or os.path.join(ROOT, 'tools', 'out', 'check.iso')
        self.mem, self.smp, self.extra, self.disk_mb = mem, smp, list(extra), disk_mb
        self.n = 0; self.S = {}; self.pr = None; self.lock = None; self.notes = []
        self.log_md = open(os.path.join(self.out, 'steps.md'), 'a')

    def start(self, wait=420):
        self.slot, self.lock = _slot()
        self.work = tempfile.mkdtemp(prefix=f'nvxqa_{self.name}_')
        self.serial_path = os.path.join(self.work, 'serial.log')
        self.mon = os.path.join(self.work, 'mon'); self.qmp = os.path.join(self.work, 'qmp')
        disk = os.path.join(self.work, 'disk.img')
        with open(disk, 'wb') as f: f.truncate(self.disk_mb << 20)
        iso2 = os.path.join(self.work, 'boot.qcow2'); subprocess.run(['qemu-img', 'create', '-q', '-f', 'qcow2', '-F', 'raw', '-b', self.iso, iso2], check=True)
        q = os.environ.get('NVX_QEMU') or shutil.which('qemu-system-x86_64')
        cmd = [q, '-m', str(self.mem), '-smp', str(self.smp), '-accel', 'tcg,thread=multi',
               '-drive', f'file={disk},format=raw,if=none,id=d0', '-device', 'ide-hd,drive=d0,bus=ide.0,unit=0',
               '-drive', f'file={iso2},format=qcow2,if=none,id=c0', '-device', 'ide-hd,drive=c0,bus=ide.1,unit=0,bootindex=0',
               '-serial', f'file:{self.serial_path}', '-display', 'none',
               '-monitor', f'unix:{self.mon},server,nowait', '-qmp', f'unix:{self.qmp},server,nowait'] + self.extra
        self.pr = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        atexit.register(self.stop)
        self.step(f'boot (slot {self.slot}, {self.mem} MB, {self.smp} CPU)')
        ok = self.wait_for('entering main loop', wait)
        time.sleep(3)
        self.check('boots to the main loop', ok)
        return ok

    def stop(self):
        if self.pr and self.pr.poll() is None:
            try: self.hmp('quit')
            except OSError: pass
            try: self.pr.wait(10)
            except subprocess.TimeoutExpired: self.pr.kill()
        if self.pr:
            try: shutil.copy(self.serial_path, os.path.join(self.out, 'serial.log'))
            except OSError: pass
        self.pr = None
        shutil.rmtree(self.work, ignore_errors=True)
        if self.lock: self.lock.close(); self.lock = None

    def serial(self):
        try: return open(self.serial_path, 'rb').read().decode('latin-1')
        except OSError: return ''

    def mark(self): return len(self.serial())

    def wait_for(self, pat, secs, start=0):
        t = time.time(); rx = re.compile(pat) if any(c in pat for c in '\\[]()|*+?') else None
        while time.time() - t < secs:
            s = self.serial()[start:]
            if (rx.search(s) if rx else pat in s): return True
            if self.pr and self.pr.poll() is not None: return False
            time.sleep(1)
        return False

    def crashed(self, start=0):
        txt = '\n'.join(l for l in self.serial()[start:].split('\n') if 'crash and freeze reports' not in l and 'cmd: ' not in l)
        m = BAD.search(txt); return m.group(0) if m else None

    def hmp(self, c):
        if 'm' not in self.S:
            s = socket.socket(socket.AF_UNIX); s.connect(self.mon); self.S['m'] = s; time.sleep(0.5)
        self.S['m'].sendall((c + '\n').encode()); time.sleep(0.3)

    def qmpc(self):
        if 'q' not in self.S:
            s = socket.socket(socket.AF_UNIX); s.connect(self.qmp); s.settimeout(5); s.recv(4096)
            s.sendall(b'{"execute":"qmp_capabilities"}\n'); time.sleep(0.3); s.recv(4096); self.S['q'] = s
        return self.S['q']

    def qmp_cmd(self, execute, **args):
        self.qmpc().sendall((json.dumps({'execute': execute, 'arguments': args} if args else {'execute': execute}) + '\n').encode()); time.sleep(0.3)
        try: return self.qmpc().recv(65536).decode('latin-1')
        except OSError: return ''

    def ev(self, events, pause=0.2):
        self.qmpc().sendall((json.dumps({'execute': 'input-send-event', 'arguments': {'events': events}}) + '\n').encode()); time.sleep(pause)
        try: self.qmpc().recv(65536)
        except OSError: pass

    def key(self, combo, hold=None):
        self.hmp('sendkey ' + combo + (f' {hold}' if hold else '')); time.sleep(0.2)

    def type(self, txt):
        for ch in txt:
            self.hmp('sendkey ' + (KEYS.get(ch) or ('shift-' + ch.lower() if ch.isupper() else ch)))

    def run(self, c, secs=3, wait=40):
        m = self.mark(); self.type(c + '\n'); t = time.time()
        while time.time() - t < wait and ('cmd: ' + c.split('>')[0].strip()) not in self.serial()[m:]: time.sleep(1)
        time.sleep(secs); return self.serial()[m:]

    def kmsg(self, c, secs=3, wait=40):
        return self.run(c + ' >/dev/kmsg', secs, wait)

    def terminal(self):
        self.key('meta_l-r', 400); time.sleep(2.5)

    def move(self, x, y):
        self.ev([{'type': 'abs', 'data': {'axis': 'x', 'value': x * 32767 // (self.w - 1)}}, {'type': 'abs', 'data': {'axis': 'y', 'value': y * 32767 // (self.h - 1)}}], 0.6)

    def click(self, x, y, button='left', settle=1.5, double=False):
        self.move(x, y)
        for _ in range(2 if double else 1):
            self.ev([{'type': 'btn', 'data': {'down': True, 'button': button}}], 0.12)
            self.ev([{'type': 'btn', 'data': {'down': False, 'button': button}}], 0.12)
        time.sleep(settle)

    def rclick(self, x, y, settle=1.5): self.click(x, y, 'right', settle)

    def dclick(self, x, y, settle=2.0): self.click(x, y, settle=settle, double=True)

    def drag(self, x0, y0, x1, y1, steps=8, settle=1.5):
        self.move(x0, y0); self.ev([{'type': 'btn', 'data': {'down': True, 'button': 'left'}}], 0.2)
        for i in range(1, steps + 1): self.move(x0 + (x1 - x0) * i // steps, y0 + (y1 - y0) * i // steps)
        self.ev([{'type': 'btn', 'data': {'down': False, 'button': 'left'}}], 0.2); time.sleep(settle)

    def wheel(self, x, y, n, settle=1.5):
        self.move(x, y); b = 'wheel-up' if n > 0 else 'wheel-down'
        for _ in range(abs(n)):
            self.ev([{'type': 'btn', 'data': {'down': True, 'button': b}}], 0.1); self.ev([{'type': 'btn', 'data': {'down': False, 'button': b}}], 0.3)
        time.sleep(settle)

    def shot(self, label):
        from PIL import Image
        self.n += 1; base = f'{self.n:03d}-' + re.sub(r'[^a-z0-9]+', '-', label.lower()).strip('-')[:60]
        p = os.path.join(self.work, base + '.ppm')
        self.hmp('screendump ' + p); t = time.time()
        while time.time() - t < 15 and not os.path.exists(p): time.sleep(0.5)
        time.sleep(1)
        try: im = Image.open(p).convert('RGB')
        except Exception as e:
            self.note(f'screenshot {label} failed: {e}'); return None
        png = os.path.join(self.out, base + '.png'); im.save(png); os.remove(p)
        self.log_md.write(f'- shot `{os.path.basename(png)}` — {label}\n'); self.log_md.flush()
        return im

    @staticmethod
    def diff(a, b, box=None):
        if a is None or b is None: return 0
        if box: a, b = a.crop(box), b.crop(box)
        return sum(1 for p, q in zip(a.getdata(), b.getdata()) if abs(p[0] - q[0]) + abs(p[1] - q[1]) + abs(p[2] - q[2]) > 30)

    @staticmethod
    def colours(im, box=None):
        if im is None: return 0
        return len(set((im.crop(box) if box else im).getdata()))

    def step(self, what):
        self.log_md.write(f'- {what}\n'); self.log_md.flush()

    def note(self, what):
        self.notes.append(what); self.log_md.write(f'- NOTE: {what}\n'); self.log_md.flush()

    def check(self, what, ok, detail=''):
        line = f"{'PASS' if ok else 'FAIL'} {what}" + (f' — {detail[:200]}' if detail and not ok else '')
        print('  ' + line, flush=True); self.log_md.write(f'- **{line}**\n'); self.log_md.flush()
        with open(os.path.join(self.out, 'results.txt'), 'a') as f: f.write(line.replace('\n', ' ') + '\n')
        return ok
