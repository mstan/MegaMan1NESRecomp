"""Shared camera/leash, native terrain and current-build continuation."""
import argparse,json,os,pathlib,subprocess,sys,wave
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'protoman'))
from validate import pixels
p=argparse.ArgumentParser(description=__doc__)
for key in ('exe','baseline','rom','out'):p.add_argument('--'+key,type=pathlib.Path,required=True)
p.add_argument('--edges-only',action='store_true')
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
route=a.out/'input.txt'
lines=['0 -','0 2:-','180 START','181 -','360 START','361 -']
for slot in range(16,32):lines.append(f'1500 POKE {0x600+slot:04X} F8')
route.write_text('\n'.join(lines)+'\n')
pokes=[]
for seat,x in ((1,104),(2,500)):
 for addr,value in {0x460:x//256,0x480:x%256,0x600:180,0x6A:28,0x55:255,0x420:64,0x400:0,0x640:0,0x660:0,0x680:0,0x620:0,0x4A0:0}.items():
  pokes+=['--coop-test-poke',f'1490:{seat}:{addr:X}:{value:X}']
base=[str(a.rom.resolve()),'--frames','1510','--input',str(route),'--coop','on','--coop-view','shared','--widescreen','16:9','--charge','off','--slide','off','--no-save','--config',str(a.out/'test.ini'),*pokes]
def run(name,exe,flicker='on',extra=()):
 args=[str(exe.resolve()),*base,'--no-flicker',flicker,'--coop-report',str(a.out/(name+'.jsonl')),'--hash-out',str(a.out/(name+'.hash')),'--screenshot',str(a.out/(name+'-native.png')),'--present-out',str(a.out/(name+'.png')),'--wav-out',str(a.out/(name+'.wav')),*extra]
 with (a.out/(name+'.log')).open('w') as log:subprocess.run(args,cwd=exe.resolve().parent,env=env,check=True,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
 rows=[json.loads(s) for s in (a.out/(name+'.jsonl')).read_text().splitlines()]
 if rows:assert not rows[-1]['failures'],rows[-1]
 return rows
def pcm(name):
 with wave.open(str(a.out/(name+'.wav')),'rb') as w:return w.readframes(w.getnframes())
if not a.edges_only:
 before=run('before',a.baseline)
 state=a.out/'camera.cycstate'
 for flicker in ('on','off'):
  name='after-'+flicker;rows=run(name,a.exe,flicker,('--save-state',f'1499:{state}','--present-every','1499') if flicker=='on' else ())
  assert before[-1]['x']==[104,500],before[-1]
  assert rows[-1]['x']==[104,482],rows[-1]
  w,h,b=pixels(a.out/'before-native.png');aw,ah,c=pixels(a.out/(name+'.png'));assert (w,h,aw,ah)==(256,240,426,240)
  # The common focus is293, the426-wide view starts80. This clear terrain
  # band overlaps the original physical native PPU window at world112..255.
  errors=sum(c[y][x*3:x*3+3]!=b[y][(x+80)*3:(x+80)*3+3] for y in range(82,145) for x in range(32,176))
  assert errors==0,('native terrain alignment',flicker,errors)
 run('interpreter',a.exe,'on',('--interp-only',));assert (a.out/'interpreter.hash').read_bytes()==(a.out/'after-on.hash').read_bytes();assert (a.out/'interpreter.png').read_bytes()==(a.out/'after-on.png').read_bytes()
 run('instant',a.exe,'on',('--load-state',str(state),'--frames','1500'));assert (a.out/'instant.png').read_bytes()==(a.out/'after-on_01499.png').read_bytes()
 run('resume',a.exe,'on',('--load-state',str(state)))
 tail=[s for s in (a.out/'after-on.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1499]
 assert tail==(a.out/'resume.hash').read_text().splitlines();assert (a.out/'resume.png').read_bytes()==(a.out/'after-on.png').read_bytes();assert pcm('after-on')[-len(pcm('resume')):]==pcm('resume')
 summary=dict(result='pass',native_terrain_pixels_exact=18144,horizontal_span=378,positions_before=[104,500],positions_after=[104,482],shared_left=80,no_flicker_on_off=True,native_interpreter=True,instant_cold_png_pcm_exact=True)
 (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))
else:
 def case(name,xs,wide='16:9',actions=(),frames=1560):
  global base
  camera=max(0,xs[0]-128)
  setup=[f'1490 POKE 001A {camera&255:02X}',f'1490 POKE 001B {camera>>8:02X}'] if camera else []
  route=a.out/(name+'-input.txt');route.write_text('\n'.join(sorted(lines+setup+list(actions),key=lambda s:int(s.split()[0])))+'\n')
  pp=[]
  for seat,x in enumerate(xs,1):
   for addr,value in {0x460:x//256,0x480:x%256,0x600:180,0x6A:28,0x55:255,0x420:64,0x400:0,0x640:0,0x660:0,0x680:0,0x620:0,0x4A0:0}.items():
    pp+=['--coop-test-poke',f'1490:{seat}:{addr:X}:{value:X}']
  base=[str(a.rom.resolve()),'--frames',str(frames),'--input',str(route),'--coop','on','--coop-view','shared','--widescreen',wide,'--charge','off','--slide','off','--no-save','--config',str(a.out/'test.ini'),*pp]
  return {r['frame']:r for r in run(name,a.exe)}
 reverse=case('reverse',[852,104],actions=('1510 LEFT','1510 2:RIGHT','1530 -','1530 2:-'))
 assert reverse[1500]['x']==[852,474],reverse[1500]
 assert reverse[1560]['x'][0]<852 and reverse[1560]['x'][1]>474,reverse[1560]
 assert all(abs(r['x'][0]-r['x'][1])<=378 for f,r in reverse.items() if f>=1491),reverse[1560]
 stock=case('stock',[104,500],wide='off',frames=1510)
 assert stock[1510]['x']==[104,312],stock[1510]
 assert pixels(a.out/'stock.png')[:2]==(256,240)
 withdrawn=case('primary-withdraw',[104,482],actions=('1505 SELECT','1600 -'),frames=1680)
 assert withdrawn[1680]['withdrawn']==1 and withdrawn[1680]['anchor']==1,withdrawn[1680]
 assert withdrawn[1680]['teleport']==[2,0] and withdrawn[1680]['hp']==[28,28],withdrawn[1680]
 summary=dict(result='pass',reverse_group_and_inward_movement=True,stock_width_span=208,far_visible_primary_withdrawal=True,native_survivor_handoff=True)
 (a.out/'edges-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))
