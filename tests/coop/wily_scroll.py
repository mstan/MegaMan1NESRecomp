"""Wily's native background scroll must preserve native sprite positions."""
import argparse,json,os,pathlib,struct,subprocess,sys,wave
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'protoman'))
from validate import pixels
p=argparse.ArgumentParser(description=__doc__)
for key in ('exe','baseline','rom','out'):p.add_argument('--'+key,type=pathlib.Path,required=True)
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
route=a.out/'input.txt';lines=['0 -','0 2:-','179 POKE 00AC 09','179 POKE 00BC 01','180 START','181 -','500 POKE 00BC 00','800 POKE 001A 00','800 POKE 001B 22','800 POKE 0027 21','800 POKE 0028 22','800 POKE 0029 10','801 RIGHT','835 -','1508 POKE 0055 FE','1508 POKE 006A 1C']
route.write_text('\n'.join(lines)+'\n');pokes=[]
for seat,x in ((1,248),(2,200)):
 for addr,value in {0x460:34,0x480:x,0x600:180,0x400:0,0x420:64,0x55:255}.items():pokes+=['--coop-test-poke',f'800:{seat}:{addr:X}:{value:X}']
base=[str(a.rom.resolve()),'--frames','1510','--input',str(route),'--coop','on','--widescreen','16:9','--slowdown','on','--charge','off','--slide','off','--no-save','--config',str(a.out/'test.ini'),*pokes]
env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
def run(name,exe,flicker='on',extra=()):
 args=[str(exe.resolve()),*base,'--no-flicker',flicker,'--coop-report',str(a.out/(name+'.jsonl')),'--hash-out',str(a.out/(name+'.hash')),'--screenshot',str(a.out/(name+'-native.png')),'--present-out',str(a.out/(name+'.png')),'--frame-log',str(a.out/(name+'.frames')),'--frame-log-frames','1509:1509','--wav-out',str(a.out/(name+'.wav')),*extra]
 with (a.out/(name+'.log')).open('w') as log:subprocess.run(args,cwd=exe.resolve().parent,env=env,check=True,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
 rows=[json.loads(s) for s in (a.out/(name+'.jsonl')).read_text().splitlines()]
 if rows:assert not rows[-1]['failures'],rows[-1]
def pcm(name):
 with wave.open(str(a.out/(name+'.wav')),'rb') as w:return w.readframes(w.getnframes())
total=0;before_wrong=0;state=a.out/'wily.cycstate'
for flicker in ('on','off'):
 before='before-'+flicker;after='after-'+flicker;run(before,a.baseline,flicker)
 run(after,a.exe,flicker,('--save-state',f'1499:{state}','--present-every','1499') if flicker=='on' else ())
 assert (a.out/(before+'-native.png')).read_bytes()==(a.out/(after+'-native.png')).read_bytes()
 assert pcm(before)==pcm(after)
 data=(a.out/(after+'.frames')).read_bytes();h=struct.unpack_from('<9I',data,12);ram=data[336:2384];oam=data[80:336];at=336+sum(h[3:6]);chr=data[at:at+h[6]]
 assert ram[0x31]==9 and ram[0xB4] and ram[0x3E]>=5
 _,_,native=pixels(a.out/(after+'-native.png'));_,_,shown=pixels(a.out/(after+'.png'));_,_,old=pixels(a.out/(before+'.png'))
 x0=(ram[0x480]-ram[0x1A])&255;y0=ram[0x600];oracle={}
 for i in range(64):
  y,tile,attr,x=oam[i*4:i*4+4]
  if attr&3 not in (0,1) or not x0-20<=x<=x0+16 or not y0-24<=y<=y0+8:continue
  for dy in range(8):
   row=7-dy if attr&128 else dy;off=(0x1000 if ram[255]&8 else 0)+tile*16+row
   for dx in range(8):
    bit=dx if attr&64 else 7-dx;v=((chr[off]>>bit)&1)|(((chr[off+8]>>bit)&1)<<1)
    if v and x+dx<256 and y+1+dy<240:oracle[x+dx,y+1+dy]=native[y+1+dy][(x+dx)*3:(x+dx)*3+3]
 assert len(oracle)>100,('No actual native body',flicker,len(oracle))
 wrong=sum(shown[y][(x+85)*3:(x+85)*3+3]!=color for (x,y),color in oracle.items())
 assert wrong==0,('Native body placement',flicker,wrong)
 before_wrong+=sum(old[y][(x+85)*3:(x+85)*3+3]!=color for (x,y),color in oracle.items());total+=len(oracle)
assert before_wrong>100,before_wrong
run('interpreter',a.exe,'on',('--interp-only',));assert (a.out/'interpreter.hash').read_bytes()==(a.out/'after-on.hash').read_bytes();assert (a.out/'interpreter.png').read_bytes()==(a.out/'after-on.png').read_bytes()
run('instant',a.exe,'on',('--load-state',str(state),'--frames','1500'));assert (a.out/'instant.png').read_bytes()==(a.out/'after-on_01499.png').read_bytes()
run('resume',a.exe,'on',('--load-state',str(state)))
tail=[s for s in (a.out/'after-on.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1499];assert tail==(a.out/'resume.hash').read_text().splitlines();assert (a.out/'resume.png').read_bytes()==(a.out/'after-on.png').read_bytes();assert pcm('after-on')[-len(pcm('resume')):]==pcm('resume')
summary=dict(result='pass',native_opaque_body_pixels_exact=total,before_wrong_pixels=before_wrong,no_flicker_on_off=True,native_hardware_audio_unchanged=True,native_interpreter=True,current_state_png_pcm_exact=True)
(a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))
