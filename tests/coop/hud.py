"""Shared native meters, physical-state preservation and cold continuation."""
import argparse,json,os,pathlib,subprocess,sys,struct,wave
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'protoman'))
from validate import pixels
p=argparse.ArgumentParser(description=__doc__)
for key in ('exe','baseline','rom','out'):p.add_argument('--'+key,type=pathlib.Path,required=True)
p.add_argument('--independent-only',action='store_true')
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
boot=['0 -','0 2:-','180 START','181 -','360 START','361 -','1300 POKE 005D FE']
lines=boot+['1420 X1:R','1421 X1:-','1432 X2:R','1433 X2:-','1460 X2:R','1461 X2:-']
for slot in range(16,32):lines.append(f'1490 POKE {0x600+slot:04X} F8')
lines+=['1490 POKE 003E 01','1490 POKE 003C FF','1490 POKE 06C1 0C']
route=a.out/'input.txt';route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
pokes=[]
for seat,values in ((1,{0x6A:20,0x6D:7,0x55:255}),(2,{0x6A:8,0x6F:19,0x55:255})):
 pokes+=['--coop-test-poke',f'1400:{seat}:55:FF']
 for addr,value in values.items():pokes+=['--coop-test-poke',f'1490:{seat}:{addr:X}:{value:X}']
env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
base=[str(a.rom.resolve()),'--frames','1510','--input',str(route),'--coop','on','--weapon-cycle','on','--widescreen','16:9','--charge','off','--slide','off','--no-save','--config',str(a.out/'config.ini'),*pokes]
def run(name,exe,flicker,extra=(),custom=None):
 args=[str(exe.resolve()),*(base if custom is None else custom),'--no-flicker',flicker,'--coop-report',str(a.out/(name+'.jsonl')),'--hash-out',str(a.out/(name+'.hash')),'--screenshot',str(a.out/(name+'-native.png')),'--present-out',str(a.out/(name+'.png')),'--wav-out',str(a.out/(name+'.wav')),'--frame-log',str(a.out/(name+'.frames')),'--frame-log-frames','1509:1509',*extra]
 with (a.out/(name+'.log')).open('w') as log:subprocess.run(args,cwd=exe.resolve().parent,env=env,check=True,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
 rows=[json.loads(s) for s in (a.out/(name+'.jsonl')).read_text().splitlines()]
 if rows:assert not rows[-1]['failures'],rows[-1]
 return rows
def pcm(name):
 with wave.open(str(a.out/(name+'.wav')),'rb') as w:return w.readframes(w.getnframes())
if a.independent_only:
 state=a.out/'split.cycstate'
 for flicker in ('off','on'):
  before='before-'+flicker;after='after-'+flicker
  run(before,a.baseline,flicker,('--coop-view','split'))
  run(after,a.exe,flicker,('--coop-view','split',*(['--save-state',f'1499:{state}','--present-every','1499'] if flicker=='off' else [])))
  for suffix in ('.hash','.png','-native.png'):assert (a.out/(before+suffix)).read_bytes()==(a.out/(after+suffix)).read_bytes(),(flicker,suffix)
  assert pcm(before)==pcm(after)
 run('instant',a.exe,'off',('--coop-view','split','--load-state',str(state),'--frames','1500'));assert (a.out/'instant.png').read_bytes()==(a.out/'after-off_01499.png').read_bytes()
 run('resume',a.exe,'off',('--coop-view','split','--load-state',str(state)))
 tail=[s for s in (a.out/'after-off.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1499];assert tail==(a.out/'resume.hash').read_text().splitlines();assert (a.out/'resume.png').read_bytes()==(a.out/'after-off.png').read_bytes();assert pcm('after-off')[-len(pcm('resume')):]==pcm('resume')
 run('interpreter',a.exe,'off',('--coop-view','split','--interp-only'));assert (a.out/'interpreter.hash').read_bytes()==(a.out/'after-off.hash').read_bytes();assert (a.out/'interpreter.png').read_bytes()==(a.out/'after-off.png').read_bytes()
 summary=dict(result='pass',independent_native_grid_pixels_unchanged=245760,no_flicker_on_off=True,native_hardware_oam_audio_unchanged=True,native_interpreter_exact=True,instant_cold_pixels_pcm_exact=True)
 (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary));sys.exit(0)
for flicker in ('off','on'):
 before='before-'+flicker;after='after-'+flicker;run(before,a.baseline,flicker)
 state=a.out/'hud.cycstate';rows=run(after,a.exe,flicker,('--save-state',f'1499:{state}','--present-every','1499') if flicker=='on' else ())
 assert rows[-1]['weapon']==[3,5] and rows[-1]['hp']==[20,8],rows[-1]
 assert (a.out/(before+'.hash')).read_bytes()==(a.out/(after+'.hash')).read_bytes(),'Native hardware changed'
 assert (a.out/(before+'-native.png')).read_bytes()==(a.out/(after+'-native.png')).read_bytes(),'Physical OAM changed'
 assert pcm(before)==pcm(after),'Native audio changed'
 w,h,b=pixels(a.out/(before+'.png'));aw,ah,c=pixels(a.out/(after+'.png'));assert (w,h)==(aw,ah)==(426,240)
 # The old adapter leaves the boss's DB..DF tiles at console x40.
 # Removing that exact original bar is part of moving it to the right edge.
 old_boss=(w-256)//2+40
 outside=sum(b[y][x*3:x*3+3]!=c[y][x*3:x*3+3] for y in range(240) for x in range(w) if not (x<48 and 16<=y<160 or w-16<=x<w-8 and 20<=y<76 or old_boss<=x<old_boss+8 and 25<=y<81))
 assert outside==0,('Pixels changed outside the native meters',flicker,outside)
solo=boot+['1400 POKE 0055 FF','1420 X1:R','1421 X1:-','1450 X1:R','1451 X1:-','1490 POKE 006A 08','1490 POKE 006F 13','1490 POKE 003E 01','1490 POKE 003C FF','1490 POKE 06C1 0C']
sr=a.out/'solo.txt';sr.write_text('\n'.join(sorted(solo,key=lambda s:int(s.split()[0])))+'\n')
run('solo-p2',a.baseline,'off',custom=[str(a.rom.resolve()),'--frames','1510','--input',str(sr),'--coop','off','--weapon-cycle','on','--widescreen','off','--charge','off','--slide','off','--no-save','--config',str(a.out/'solo.ini')])
def bar_oracle(name,x,attr):
 data=(a.out/(name+'.frames')).read_bytes();h=struct.unpack_from('<9I',data,12);ram=data[336:2384];oam=data[80:336];chr_at=336+sum(h[3:6]);pattern=data[chr_at:chr_at+h[6]];_,_,rgb=pixels(a.out/(name+'-native.png'));out={}
 for i in range(64):
  sy,tile,flags,sx=oam[i*4:i*4+4]
  if sx!=x or flags!=attr or not 24<=sy<=72:continue
  for dy in range(8):
   at=(0x1000 if ram[255]&8 else 0)+tile*16+dy
   for dx in range(8):
    v=((pattern[at]>>(7-dx))&1)|(((pattern[at+8]>>(7-dx))&1)<<1)
    if v:out[(dx,sy+1+dy-25)]=rgb[sy+1+dy][(sx+dx)*3:(sx+dx)*3+3]
 assert len(out)>100,(name,x,len(out));return out
native_pixels=0
for name,sx,attr,tx,ty,red in [('before-on',24,1,8,20,False),('before-on',16,0,24,20,False),('before-on',40,2,410,20,False),('solo-p2',24,1,8,96,True),('solo-p2',16,0,24,96,False)]:
 oracle=bar_oracle(name,sx,attr)
 for mode in ('off','on'):
  _,_,actual=pixels(a.out/('after-'+mode+'.png'))
  for (x,y),color in oracle.items():
   if red and any(color):color=bytes(((color[0]*3+255)//4,(color[1]*3+64)//4,(color[2]*3+64)//4))
   assert actual[ty+y][(tx+x)*3:(tx+x)*3+3]==color,(name,mode,x,y,color)
 native_pixels+=len(oracle)
run('interpreter',a.exe,'on',('--interp-only',));assert (a.out/'after-on.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes();assert (a.out/'after-on.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
run('instant',a.exe,'on',('--load-state',str(state),'--frames','1500'));assert (a.out/'instant.png').read_bytes()==(a.out/'after-on_01499.png').read_bytes()
run('resume',a.exe,'on',('--load-state',str(state)))
tail=[s for s in (a.out/'after-on.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1499];assert tail==(a.out/'resume.hash').read_text().splitlines();assert (a.out/'resume.png').read_bytes()==(a.out/'after-on.png').read_bytes();assert pcm('after-on')[-len(pcm('resume')):]==pcm('resume')
summary=dict(result='pass',opaque_native_meter_pixels=native_pixels,no_flicker_on_off=True,world_outside_meters_unchanged=True,native_hardware_oam_audio_unchanged=True,native_interpreter_exact=True,instant_cold_pixels_pcm_exact=True)
(a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))
