import os,subprocess,sys,tempfile
xf,out,ir,fir,main=sys.argv[1:6]
here=os.path.dirname(os.path.abspath(__file__))
e=tempfile.NamedTemporaryFile('w',suffix='.c',delete=False); e.close(); b=e.name+'.base.c'
subprocess.check_call(['python3',os.path.join(here,'..','uvc','gen.py'),xf,b,e.name])
src=open(b).read(); os.unlink(b); os.unlink(e.name)
pre='u16 uvc_vid=0x04f2,uvc_pid=0xb6d9; static u64 hr_ns(void){ return 0; }\nstatic int uvc_req(u8 rt,u8 req,u16 val,u16 idx,u16 len,int in,u8* data);\n'
f=open(fir).read(); f=f[f.index('static const char* FIR_CFG'):]; f=f[:f.index('static int fir_load(')]+f[f.index('int fir_live('):]
open(out,'w').write(src+pre+open(ir).read()+'\n'+f+'\n'+open(main).read())
