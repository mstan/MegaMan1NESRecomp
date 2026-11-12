"""Bounded invalid-source cases using the existing two-process LAN driver."""
from pathlib import Path
import argparse,json,os,subprocess,time,shutil,re
p=argparse.ArgumentParser(description=__doc__)
for key in ('exe','rom','donor','out'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
binary=a.exe.resolve().parent;rom=a.rom.resolve();donor=a.donor.resolve()
results=[]
for case in ('missing','wrong'):
 root=out/case;root.mkdir(exist_ok=True);children=[];logs=[]
 try:
  for seat in (0,1):
   d=root/f'seat{seat}';d.mkdir(exist_ok=True);shutil.copytree(binary/'mods/packages',d/'mods/packages',dirs_exist_ok=True)
   source=donor if seat==0 else root/'unavailable.nes' if case=='missing' else rom
   state='format_version = 1\n'
   for package,feature in [('mega-man-usa.coop','coop'),('mega-man-usa.character.protoman','protoman'),('mega-man-usa.enhancement.slowdown','slowdown'),('mega-man-usa.backport.mm4','charge'),('mega-man-usa.backport.mm4','slide')]:
    state+=f'\n[[feature]]\npackage_id = "{package}"\nid = "{feature}"\nenabled = '+('false' if seat==1 and feature=='slowdown' else 'true')+'\n'
   state+='\n[[resource]]\npackage_id = "mega-man-usa.backport.mm4"\nid = "mm4-usa"\npath = "'+source.as_posix()+'"\n'
   (d/'mods/state.toml').write_text(state)
   env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1',NES_NETPLAY='1',NES_NET_SLOT=str(seat),NES_NET_SLOTS='2',NES_NET_OCCUPIED='3',NES_NET_TRANSPORT='lan',NES_NET_BIND=f'127.0.0.1:{47810+seat}',NES_NET_PEER=f'127.0.0.1:{47811-seat}',NES_NET_SESSION_ID='7790115' if case=='missing' else '7790116',NES_NET_MATCH_TICKS='10',NES_NET_DELAY='2',NES_NET_MODE='rollback')
   argv=[str(binary/'MegaManRecomp.exe'),str(rom),'--no-save','--hidden','--frames','60','--config',str(d/'config.ini'),'--mods-root',str(d/'mods')]
   log=(d/'runtime.log').open('w');logs.append(log);children.append(subprocess.Popen(argv,cwd=binary,env=env,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW))
  until=time.monotonic()+60
  while any(p.poll() is None for p in children):
   if any(p.poll() not in (None,0) for p in children):break
   assert time.monotonic()<until,'Admission exceeded bound';time.sleep(.1)
  client=(root/'seat1/runtime.log').read_text(errors='replace')
  checks={'client-rejects':children[1].poll() not in (None,0),'title-revalidates-explicit-source':'verify the Mega Man 4 USA donor before enabling charge or slide' in client,'no-gameplay-admitted':'RB boot taken at frame=' not in client and 'NET_TICK_DIGEST' not in client}
  restore=next((dict(re.findall(r'(\w+)=(\S+)',x)) for x in client.splitlines() if x.startswith('MM1_NET_RESTORE ')),{})
  checks['partial-admission-restores-offline-timing']=bool(restore) and restore['applied']=='1' and restore['expected']==restore['actual'] and restore['expected_offer']==restore['next_offer'] and restore['actual_lines']==restore['expected_lines']=='0'
  results.append(dict(case=case,checks=checks,exits=[p.poll() for p in children]))
 finally:
  for p in children:
   if p.poll() is None:p.terminate();p.wait(timeout=10)
  for log in logs:log.close()
summary=dict(result='pass' if all(all(r['checks'].values()) for r in results) else 'fail',cases=results)
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary),flush=True)
assert summary['result']=='pass',summary
