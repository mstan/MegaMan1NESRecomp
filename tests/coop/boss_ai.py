"""Native Fire Man reacts to the nearby player, preserving both domains."""
import argparse,json,os,pathlib,subprocess,sys,wave
from views import records
p=argparse.ArgumentParser(description=__doc__)
for key in ('exe','baseline','rom','out'):p.add_argument('--'+key,type=pathlib.Path,required=True)
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
boot=['0 -','0 2:-','179 POKE 00AC 03','179 POKE 00BC 01','180 START','181 -','500 POKE 00BC 00','800 POKE 001A 00','800 POKE 001B 12','800 POKE 0027 10','800 POKE 0028 12','800 POKE 0029 07','801 RIGHT','835 -','1380 POKE 005D FE','1400 X1:R','1401 X1:-','1420 X1:R','1421 X1:-']
def run(name,exe,near_primary=False,extra=()):
 lines=boot+['1450 POKE 003E 05','1450 POKE 0056 00','1450 POKE 06A1 00','1450 POKE 0481 90','1450 POKE 0421 06','1450 POKE 06C1 1C']
 for slot in range(16,32):lines.append(f'1450 POKE {0x600+slot:04X} F8')
 pokes=[]
 for seat in (1,2):
  for addr,value in {0x460:18,0x480:248 if seat==1 else 200,0x600:180,0x400:0,0x420:64,0x55:255}.items():pokes+=['--coop-test-poke',f'800:{seat}:{addr:X}:{value:X}']
  x=240 if (seat==1)==near_primary else 32
  for addr,value in {0x480:x,0x600:180,0x400:0,0x420:64,0x55:255,0x6A:28,0x680:0,0x660:0,0x6F:7 if seat==1 else 19}.items():pokes+=['--coop-test-poke',f'1450:{seat}:{addr:X}:{value:X}']
 route=a.out/(name+'-input.txt');route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
 args=[str(exe.resolve()),str(a.rom.resolve()),'--frames','1460','--input',str(route),'--coop','on','--widescreen','off','--no-flicker','on','--charge','off','--slide','off','--weapon-cycle','on','--no-save','--config',str(a.out/'test.ini'),*pokes,'--coop-report',str(a.out/(name+'.jsonl')),'--hash-out',str(a.out/(name+'.hash')),'--frame-log',str(a.out/(name+'.frames')),'--frame-log-frames','1449:1459','--present-out',str(a.out/(name+'.png')),'--wav-out',str(a.out/(name+'.wav')),*extra]
 with (a.out/(name+'.log')).open('w') as log:subprocess.run(args,cwd=exe.resolve().parent,env=env,check=True,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
 rows={r['frame']:r for r in map(json.loads,(a.out/(name+'.jsonl')).read_text().splitlines())}
 if rows:assert not rows[max(rows)]['failures'],rows[max(rows)]
 return rows,records(a.out/(name+'.frames'))
before,b=run('before',a.baseline);state=a.out/'boss.cycstate'
rows,m=run('native',a.exe,extra=('--save-state',f'1451:{state}','--present-every','1451'))
assert not b[1452][0x421]&64,('Baseline did not retain the distant anchor',b[1452][0x3E],b[1452][0x421])
assert m[1452][0x421]&64 and m[1460][0x481]>b[1460][0x481],('Native boss ignored nearby P2',m[1452][0x3E],m[1452][0x421])
primary,pm=run('near-primary',a.exe,True)
assert pm[1452][0x421]&64
assert rows[1460]['weapon']==[5,0] and [p[4] for p in rows[1460]['ammo']]==[7,19],rows[1460]
rom=a.rom.read_bytes();palette=rom[16+7*16384+0x148F:16+7*16384+0x1491]
assert m[1452][0x3E1:0x3E3]==palette,(m[1452][0x3E1:0x3E3],palette)
run('interpreter',a.exe,extra=('--interp-only',));assert (a.out/'interpreter.hash').read_bytes()==(a.out/'native.hash').read_bytes();assert (a.out/'interpreter.png').read_bytes()==(a.out/'native.png').read_bytes()
run('instant',a.exe,extra=('--load-state',str(state),'--frames','1452'));assert (a.out/'instant.png').read_bytes()==(a.out/'native_01451.png').read_bytes()
run('resume',a.exe,extra=('--load-state',str(state)))
tail=[s for s in (a.out/'native.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1451];assert tail==(a.out/'resume.hash').read_text().splitlines();assert (a.out/'resume.png').read_bytes()==(a.out/'native.png').read_bytes()
def pcm(name):
 with wave.open(str(a.out/(name+'.wav')),'rb') as w:return w.readframes(w.getnframes())
assert pcm('native')[-len(pcm('resume')):]==pcm('resume')
summary=dict(result='pass',native_boss_search_uses_near_player=True,both_player_orders=True,personal_weapon_ammo_palette_retained=True,native_interpreter=True,current_state_png_pcm_exact=True)
(a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))
