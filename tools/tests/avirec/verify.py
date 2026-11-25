import sys,os,subprocess,shutil
d=sys.argv[1]; ok=0; bad=0
def ck(c,m):
    global ok,bad
    print(m,'ok' if c else 'FAIL'); ok+=bool(c); bad+=not c
try:
    from PIL import Image
    import math,io
    for fn,w,h,q in (('t.jpg',64,48,85),('t2.jpg',37,21,50)):
        ref=Image.new('RGB',(w,h))
        for y in range(h):
            for x in range(w): ref.putpixel((x,y),(x*255//(w-1),y*255//(h-1),200 if ((x//8+y//8)&1) else 40))
        def psnr(im):
            se=0
            for y in range(h):
                for x in range(w):
                    p=im.getpixel((x,y)); r=ref.getpixel((x,y)); se+=sum((p[k]-r[k])**2 for k in range(3))
            return 10*math.log10(255*255/(se/(w*h*3)+1e-9))
        im=Image.open(os.path.join(d,fn)).convert('RGB'); ck(im.size==(w,h),'PIL decodes '+fn)
        bio=io.BytesIO(); ref.save(bio,'JPEG',quality=q,subsampling=2); pr=psnr(Image.open(bio).convert('RGB')); po=psnr(im)
        ck(po>pr-1.0 and po>20,'%s PSNR %.1f dB (PIL 4:2:0 same quality %.1f dB)'%(fn,po,pr))
except ImportError: print('PIL missing, skipped')
if shutil.which('ffprobe'):
    o=subprocess.run(['ffprobe','-v','error','-show_entries','stream=codec_name,width,height,sample_rate,channels','-of','csv=p=0',os.path.join(d,'t.avi')],capture_output=True,text=True)
    ck('mjpeg,64,48' in o.stdout and 'pcm_s16le,48000,2' in o.stdout and not o.stderr.strip(),'ffprobe: MJPEG video + PCM audio '+o.stdout.replace('\n',' ')+o.stderr[:200])
if shutil.which('ffmpeg'):
    o=subprocess.run(['ffmpeg','-v','error','-i',os.path.join(d,'t.avi'),'-f','null','-'],capture_output=True,text=True)
    ck(o.returncode==0 and not o.stderr.strip(),'ffmpeg decodes the whole AVI '+o.stderr[:200])
print('verify: all ok' if not bad else 'verify: FAIL'); sys.exit(1 if bad else 0)
