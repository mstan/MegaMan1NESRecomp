"""MM1 adapter of the existing MM5 production LAN rollback check."""
from pathlib import Path
import argparse,os,subprocess,time,json,hashlib,re,socket
p=argparse.ArgumentParser()
for key in ('root','rom','wire','bind','peer'):p.add_argument('--'+key,required=True)
p.add_argument('--seat',type=int,required=True)
p.add_argument('--catalog-source')
p.add_argument('--catalog-view',choices=('shared','split'),default='shared')
p.add_argument('--ui-probe',action='store_true')
p.add_argument('--require-restore',action='store_true')
v=p.parse_args();r=Path(v.root);r.mkdir(parents=True,exist_ok=True);seat=v.seat
exe=r/'MegaManRecomp.exe';name=f'physical-lan-seat{seat}'
(r/'config.ini').write_text('[Game]\nWidescreen = fit\n')
env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1',NES_NETPLAY='1',NES_NET_SLOT=str(seat),NES_NET_SLOTS='2',NES_NET_OCCUPIED='3',NES_NET_TRANSPORT='lan',NES_NET_BIND=v.bind,NES_NET_PEER=v.peer,NES_NET_SESSION_ID='7790113',NES_NET_DELAY='2',NES_NET_MODE='rollback',NES_NET_MATCH_TICKS='2500',NES_NET_SHOT_TICK='2400',NES_NET_SHOT_PATH=str(r/(name+'.png')),NES_NET_TEST_INPUT=v.wire,NES_NET_FINAL_STATE=str(r/(name+'.state')),RNET_SIM_LATENCY_MS='25',RNET_SIM_JITTER_MS='10',RNET_SIM_SEED=str(60+seat),NES_RB_FORCE_MISPREDICT='97')
argv=[str(exe),v.rom,'--no-save','--hidden','--frames','3200','--config',str(r/'config.ini'),'--coop','on','--coop-view','split' if seat==0 else 'p2','--protoman','on','--weapon-cycle','on','--widescreen','fit','--no-flicker','on','--slowdown','on','--charge','off','--slide','off','--coop-report',str(r/(name+'.jsonl'))]+(['--interp-only'] if seat else [])
if v.catalog_source:
 state='format_version = 1\n'
 for package,feature in [('mega-man-usa.coop','coop'),('mega-man-usa.character.protoman','protoman'),('mega-man-usa.enhancement.quick-weapons','quick-weapons'),('mega-man-usa.enhancement.slowdown','slowdown'),('mega-man-usa.enhancement.no-flicker','no-flicker'),('mega-man-usa.backport.mm4','charge'),('mega-man-usa.backport.mm4','slide')]:
  state+=f'\n[[feature]]\npackage_id = "{package}"\nid = "{feature}"\nenabled = '+('true' if seat==0 or feature in ('charge','slide') else 'false')+'\n'
  if feature=='coop':state+='[feature.values]\ncoop-view = "'+v.catalog_view+'"\nplayer-one = "protoman"\nwait-both = "on"\n'
 state+='\n[[resource]]\npackage_id = "mega-man-usa.backport.mm4"\nid = "mm4-usa"\npath = "'+Path(v.catalog_source).as_posix()+'"\n'
 (r/'mods/state.toml').write_text(state)
 wire=r/'abilities.input';wire.write_text(('0 00 0\n300 10 0\n302 00 0\n600 10 0\n602 00 0\n' if seat==0 else '0 00 0\n')+'1240 40 0\n1470 00 0\n1510 40 0\n1600 00 0\n1640 84 0\n1642 00 0\n1700 84 0\n1702 00 0\n1760 84 0\n1762 00 0\n'+('1950 10 0\n1952 00 0\n' if seat==0 else '1900 10 0\n1902 00 0\n2000 10 0\n2002 00 0\n'))
 env['NES_NET_TEST_INPUT']=str(wire);env['NES_NET_SESSION_ID']='7790114'
 argv=[str(exe),v.rom,'--no-save','--hidden','--frames','3200','--config',str(r/'config.ini'),'--mods-root',str(r/'mods'),'--coop-report',str(r/(name+'.jsonl')),'--charge-report',str(r/'charge.jsonl'),'--slide-report',str(r/'slide.jsonl')]+(['--interp-only'] if seat else [])
started=time.monotonic()
ui={}
if v.ui_probe:
 env.pop('SDL_VIDEODRIVER',None)
 with socket.socket() as s:s.bind(('127.0.0.1',0));tcp=s.getsockname()[1]
 at=argv.index('--frames');argv[at:at+2]=['--exit-after','3200'];argv+=['--tcp',str(tcp)]
with (r/(name+'.log')).open('w') as log:
 child=subprocess.Popen(argv,cwd=r,env=env,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
 try:
  if v.ui_probe:
   until=time.monotonic()+20;s=None
   while time.monotonic()<until:
    try:s=socket.create_connection(('127.0.0.1',tcp),timeout=1);break
    except OSError:time.sleep(.05)
   assert s,'Hidden window TCP startup failed';s.settimeout(15);stream=s.makefile('rwb');seq=0
   def cmd(name,**args):
    global seq
    seq+=1;stream.write((json.dumps(dict(cmd=name,id=seq,**args))+'\n').encode());stream.flush();return json.loads(stream.readline())
   until=time.monotonic()+100
   while cmd('ping')['frame']<1500:
    assert time.monotonic()<until,'Native gameplay startup exceeded bound';time.sleep(.05)
   ui['direct-video-refused']=cmd('video');assert not ui['direct-video-refused']['ok']
   ui['initial']=cmd('state');assert ui['initial']['picture'][0]==256,ui
   assert cmd('menu',open=True)['ok']
   for _ in range(5):assert cmd('menu',nav='down')['ok']
   assert cmd('menu',nav='accept')['ok']
   assert cmd('screenshot',layer='ui',path=str(r/'online-view-row.png'))['ok']
   for label,nav,width in [('p1','left',256),('split','left',512),('shared-refused','left',512),('p1-again','right',256),('p2','right',256)]:
    assert cmd('menu',nav=nav)['ok'];time.sleep(.06);ui[label]=cmd('state');assert ui[label]['picture'][0]==width,(label,ui)
   assert cmd('window_size',w=1280,h=720)['ok'];time.sleep(.08);assert cmd('state')['picture'][0]==256
   assert cmd('screenshot',layer='ui',path=str(r/'online-view-final.png'))['ok']
   ui['save-refused']=cmd('save_state',path=str(r/'refused-online.cycstate'));assert not ui['save-refused']['ok'] and not (r/'refused-online.cycstate').exists()
   ui['qa-poke-refused']=cmd('ws_poke',addr=85,value=0);assert not ui['qa-poke-refused']['ok']
   ui['qa-step-refused']=cmd('ws_step',frames=1,pad=0);assert not ui['qa-step-refused']['ok']
   assert cmd('menu',open=False)['ok'];stream.close();s.close()
  child.wait(timeout=max(1,180-(time.monotonic()-started)))
 finally:
  if child.poll() is None:child.terminate();child.wait(timeout=10)
lines=(r/(name+'.log')).read_text(errors='replace').splitlines()
report=r/(name+'.jsonl');rows=[json.loads(x) for x in report.read_text().splitlines()] if report.exists() else []
caps=[x for x in lines if x.startswith(('NET_HOST_CAPS','NET_PROFILE_SEALED','NETPLAY_DRIVER'))]
summary=next((x for x in reversed(caps) if x.startswith('NETPLAY_DRIVER')),'')
driver={k:int(n) for k,n in re.findall(r'(\w+)=(\d+)',summary)}
live=[x for x in rows if x['ready']]
checks={'production-exit':child.returncode==0,'both-native-controllers':bool(live) and live[-1]['failures']==0 and live[-1]['updates']>100 and all(max(x['x'][s] for x in live)-min(x['x'][s] for x in live)>12 for s in (0,1)), 'confirmed-shot':any(x.startswith('NET_TICK_DIGEST tick=2400 ') for x in lines) and driver.get('confirmed',0)>=2400,'real-rollback-no-desync':driver.get('episodes',0)>0 and driver.get('resim_ticks',0)>0 and driver.get('desyncs',-1)==0}
restore=next((dict(re.findall(r'(\w+)=(\S+)',x)) for x in lines if x.startswith('MM1_NET_RESTORE ')),{})
if v.require_restore:checks['offline-settings-and-next-offer-restored']=bool(restore) and restore['applied']=='1' and all(restore[a]==restore[b] for a,b in [('expected','actual'),('expected_offer','next_offer'),('expected_view','actual_view'),('expected_lines','actual_lines')])
if v.catalog_source:
 charge=[json.loads(x) for x in (r/'charge.jsonl').read_text().splitlines()];slide=[json.loads(x) for x in (r/'slide.jsonl').read_text().splitlines()]
 checks['both-real-half-full-and-slide']=all(any(x['seat']==s and x['full'] and x['half'] for x in charge) and any(x['seat']==s and x['starts'] for x in slide) for s in (0,1))
result=dict(checks=checks,ui=ui,restore=restore,seat=seat,exit=child.returncode,seconds=time.monotonic()-started,exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),argv=argv,capabilities=caps,driver=driver,digest=[x for x in lines if x.startswith('NET_TICK_DIGEST tick=2400 ')],final=rows[-1] if rows else None,scope='Actual desktop/SERVER UDP, identical production binary, native/interpreter, 25ms latency/10ms jitter, controller-only reset route, no RAM/state admission')
(r/(name+'-result.json')).write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
assert all(checks.values()),checks
