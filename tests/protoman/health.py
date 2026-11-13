"""Native solo character HP ownership through Select, damage, healing and retry."""
from pathlib import Path
import argparse,os,json,shutil,subprocess,hashlib,wave
p=argparse.ArgumentParser(description=__doc__)
for name in ('rom','exe','out'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();o=a.out.resolve();o.mkdir(parents=True,exist_ok=False)
mods=o/'mods';mods.mkdir()
packages=a.exe.resolve().parent/'mods/packages'
if not packages.is_dir():packages=Path(__file__).resolve().parents[2]/'mods/preloaded/packages'
shutil.copytree(packages,mods/'packages')
env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
lines=['0 -','180 START','181 -','360 START','361 -']
for slot in range(16,32):lines.append(f'1400 POKE {0x600+slot:04X} F8')
for addr,value in ((0x480,112),(0x460,0),(0x600,160),(0x420,64),(0x400,0),(0x640,31),(0x680,0),(0x660,0)):
 lines.append(f'1400 HOLD {addr:04X} {value:02X}')
lines+=['1400 POKE 006A 1C','1400 POKE 0055 00','1400 POKE 005F 00','1400 POKE 0060 00']
def actor(at,kind=0x1A):
 lines.append(f'{at} POKE 0055 00')
 values={0x400:255,0x420:2,0x440:0,0x460:0,0x480:112,0x4A0:0,0x4C0:0,0x4E0:0,0x600:160,0x620:0,0x640:127,0x660:0,0x680:0,0x6A0:1,0x6C0:1,0x6E0:kind}
 for addr,value in values.items():lines.append(f'{at} POKE {addr+16:04X} {value:02X}')
actor(1400)
lines+=['1450 POKE 0610 F8']
for at in (1500,1800,1870):lines.extend([f'{at} SELECT',f'{at+1} -'])
for at in range(1560,1661,20):actor(at)
actor(1700,0x40)
lines+=['1940 UNHOLD ALL','1940 POKE 006A 01','1940 POKE 0600 A0']
actor(1940)
route=o/'native.input';route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
exe=a.exe.resolve();rom=a.rom.resolve();runner=exe.parent
def run(name,extra=(),load=None,frames=2601):
 d=o/name;d.mkdir();a=[str(exe),str(rom),'--frames',str(frames),'--input',str(route),'--protoman','on','--coop','off','--weapon-cycle','on','--widescreen','off','--no-flicker','on','--charge','off','--slide','off','--no-save','--mods-root',str(mods),'--config',str(d/'profile.ini'),'--proto-report',str(d/'proto.jsonl'),'--coop-report',str(d/'coop.jsonl'),'--hash-out',str(d/'hash.txt'),'--present-out',str(d/'end.png'),'--wav-out',str(d/'audio.wav'),'--save-state',f'2600:{d}/end.cycstate',*extra]
 if load:a+=['--load-state',str(load)]
 with(d/'log.txt').open('w') as log:p=subprocess.run(a,cwd=runner,env=env,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
 assert p.returncode==0,(name,(d/'log.txt').read_text()[-2400:])
 return {row['frame']:row for row in map(json.loads,(d/'proto.jsonl').read_text().splitlines())}
rows=run('native',('--save-state',f'1710:{o}/refill.cycstate','--save-state',f'1815:{o}/swap.cycstate','--present-every','5'))
samples={f:rows[f] for f in (1450,1540,1680,1760,1840,1920,1941,2000,2200,2400,2600)}
(o/'samples.json').write_text(json.dumps(samples,indent=2))
checks={}
for frame,hp,health,owner in [(1450,24,[28,24],1),(1540,28,[28,24],0),(1680,16,[16,24],0),(1760,26,[26,24],0),(1840,24,[26,24],1),(1920,26,[26,24],0),(2600,28,[28,28],0)]:
 row=rows[frame];checks[f'native-health-{frame}']=(row['hp'],row['health'],row['health_owner'])==(hp,health,owner)
coop={row['frame']:row for row in map(json.loads,(o/'native/coop.jsonl').read_text().splitlines())}
checks['native-death-spends-one-life']=coop[1940]['lives']-coop[2600]['lives']==1
(o/'preliminary.json').write_text(json.dumps(dict(checks=checks,samples=samples),indent=2));assert all(checks.values()),checks
other=run('interpreter',('--interp-only',))
for f in ('hash.txt','end.png','end.cycstate','audio.wav'):checks['Native-I-'+f]=(o/'native'/f).read_bytes()==(o/'interpreter'/f).read_bytes()
def pcm(path):
 with wave.open(str(path),'rb') as w:return w.readframes(w.getnframes())
for name,at in [('refill',1710),('swap',1815)]:
 run('cold-'+name,load=o/(name+'.cycstate'))
 for f in ('end.png','end.cycstate'):checks[name+'-cold-'+f]=(o/'native'/f).read_bytes()==(o/('cold-'+name)/f).read_bytes()
 tail=[s for s in (o/'native/hash.txt').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>at];checks[name+'-cold-hash']=tail==(o/('cold-'+name)/'hash.txt').read_text().splitlines()
 cold=pcm(o/('cold-'+name)/'audio.wav');checks[name+'-cold-pcm']=bool(cold) and pcm(o/'native/audio.wav')[-len(cold):]==cold
 run('instant-'+name,load=o/(name+'.cycstate'),frames=at+1);checks[name+'-instant-picture']=(o/('instant-'+name)/'end.png').read_bytes()==(o/'native'/f'end_{at:05}.png').read_bytes()
result=dict(passed=all(checks.values()),checks=checks,exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),scope='Actual Select swaps, native hostile bullet damage, native health pickup refill and native death/retry; only execution prerequisites supplied. Current cold refill/mid-swap state, instant picture, PCM and Native/I checked.')
(o/'summary.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True);assert result['passed'],checks
