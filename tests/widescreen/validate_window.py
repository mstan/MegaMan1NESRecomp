"""Hidden SDL/TCP checks for margins, resize, save/load and activation lifetime."""
import argparse,json,os,pathlib,socket,subprocess,time
from validate import REPO,TARGET,PACKAGE

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--rom',type=pathlib.Path,required=True)
 p.add_argument('--exe',type=pathlib.Path,default=REPO/'build-wide/Release'/TARGET)
 p.add_argument('--out',type=pathlib.Path,default=REPO/'wide-evidence/window-regression')
 a=p.parse_args();a.rom=a.rom.resolve();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
 mm=TARGET.startswith('Mega');frame=1500 if mm else 1000;opts={}
 if hasattr(subprocess,'STARTUPINFO'):
  si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=0
  opts=dict(startupinfo=si,creationflags=subprocess.CREATE_NO_WINDOW)
 state=a.out/'base.cycstate';route=pathlib.Path(__file__).with_name('route.txt')
 with (a.out/'prepare.log').open('w') as log:
  r=subprocess.run([str(a.exe),str(a.rom),'--frames',str(frame+1),'--input',str(route),'--widescreen','32:9',
                    '--save-state',f'{frame}:{state}','--no-save','--config',str(a.out/'prepare.ini')],
                    cwd=a.exe.parent,stdout=log,stderr=subprocess.STDOUT,**opts)
 assert r.returncode==0,(a.out/'prepare.log').read_text()
 results=[]
 for mode in ('off','16:9','21:9','32:9','fit'):
  with socket.socket() as port_socket:port_socket.bind(('127.0.0.1',0));port=port_socket.getsockname()[1]
  label=mode.replace(':','-');log=(a.out/(label+'.log')).open('w')
  proc=subprocess.Popen([str(a.exe),str(a.rom),'--hidden','--no-save','--config',str(a.out/'config.ini'),
      '--tcp',str(port),'--widescreen',mode],cwd=a.exe.parent,env=dict(os.environ,NESRECOMP_NO_LAUNCHER='1'),
      stdout=log,stderr=subprocess.STDOUT,**opts)
  sock=None;stream=None
  try:
   deadline=time.monotonic()+15
   while time.monotonic()<deadline:
    try:sock=socket.create_connection(('127.0.0.1',port),timeout=1);break
    except OSError:time.sleep(.05)
   assert sock is not None,'TCP startup failed'
   sock.settimeout(30);stream=sock.makefile('rwb');request_id=0
   def cmd(command,**fields):
    nonlocal request_id
    request_id+=1;stream.write((json.dumps(dict(cmd=command,id=request_id,**fields))+'\n').encode());stream.flush()
    reply=json.loads(stream.readline());assert reply['ok'],(command,reply);return reply
   def ram(addr,n):return bytes.fromhex(cmd('read_ram',addr=addr,len=n)['hex'])
   def poke(addr,value):cmd('ws_poke',addr=addr,value=value&255)
   def step(n=1,pad=0):cmd('ws_step',frames=n,pad=pad)
   def info():return cmd('ws_state',verify=True)
   def load(path=state):cmd('load_state',path=str(path))
   def shot(name):cmd('screenshot',path=str(a.out/(name+'.png')))
   cmd('menu',open=True);load()
   actor=next((i+16 for i,y in enumerate(ram(0x610,16)) if y<248),None) if mm else next((i for i,y in enumerate(ram(0x661,8)) if y),None)
   assert actor is not None,'route did not reach an active actor'
   step(2);s=info();results.append(dict(test='geometry',mode=mode,state=s))
   assert s['terrain_errors']==0 and s['failures']==0 and s['overflow']==0,s
   if mode=='fit':
    for w,h,expected in ((1280,720,426),(1920,720,640),(640,480,320),(640,720,256),(2560,720,854)):
     cmd('window_size',w=w,h=h);time.sleep(.05);step(2);s=info()
     assert s['width']==expected,(w,h,s)
     results.append(dict(test='resize',size=[w,h],state=s))
    if not mm:
     load();start=info()['spawns']
     for i,value in enumerate((0,0x72,30,0x19,255)):poke(0x7b40+i,value)
     for i in range(48):poke(0x7b10+i,0)
     for i in range(5):poke(0x661+i,0)
     poke(0x7b01,0)
     step(2);s=info();assert s['spawns']==start+1,s
     index=next(i for i in range(5) if ram(0x661+i,1)[0] and ram(0x659+i,1)[0]==0)
     poke(0x661+index,0);poke(0x7b10,0);step(2)
     assert info()['spawns']==s['spawns'],'respawned while still visible'
     cmd('window_size',w=640,h=720);time.sleep(.05);step(2)
     assert info()['width']==256
     cmd('window_size',w=2560,h=720);time.sleep(.05);step(2)
     assert info()['spawns']==s['spawns']+1,'record did not re-enter after resize'
     results.append(dict(test='resize_activation_lifetime',reentered=True))
   if mode in ('off','16:9','32:9'):
    for name,delta in (('right',320),('left',-80),('outside',900)):
     load();cam=info()['camera']
     if cam+delta<0:
      cam=256
      for addr,val in ((0x1a,0),(0x1b,1),(0x480,128),(0x460,1)) if mm else ((0xfd,0),(0x12,1),(0x90,128),(0x75,1)):poke(addr,val)
     x=cam+delta
     for addr,val in ((0x480+actor,x),(0x460+actor,x>>8),(0x6a0+actor,100),(0x440+actor,1)) if mm else ((0x91+actor,x),(0x76+actor,x>>8)):poke(addr,val)
     step(8);alive=ram((0x600 if mm else 0x661)+actor,1)[0]
     alive=alive<248 if mm else alive!=0
     pos=ram((0x480 if mm else 0x91)+actor,1)[0]+ram((0x460 if mm else 0x76)+actor,1)[0]*256
     if mode!='off' and name!='outside':assert alive,(mode,name,info())
     if name=='outside':assert not alive or abs(pos-x)>64,(mode,name,pos,x)
     results.append(dict(test='boundary',mode=mode,edge=name,alive=alive,x=pos,target=x))
   if mode=='32:9':
    load();step(3);saved=a.out/'roundtrip.cycstate';cmd('save_state',path=str(saved))
    before=ram(0,2048)+ram(0x6000,4096)+ram(0x7000,4096);shot('before')
    step(10);load(saved);after=ram(0,2048)+ram(0x6000,4096)+ram(0x7000,4096);shot('after')
    assert before==after
    assert (a.out/'before.png').read_bytes()==(a.out/'after.png').read_bytes()
    results.append(dict(test='save_roundtrip',ram_equal=True,picture_equal=True))
    if not mm:
     load();start=info()['spawns']
     for i,value in enumerate((0,0x72,30,0x19,0xd3,30,0,255)):poke(0x7b40+i,value)
     for i in range(48):poke(0x7b10+i,0)
     poke(0x7b01,0);step(2);s=info();flags=list(ram(0x7b10,2))
     assert s['spawns']==start+1 and flags==[128,0],(s,flags)
     ids=ram(0x671,8);states=ram(0x661,8)
     index=next(i for i in range(5) if ids[i]==0x72 and states[i] and ram(0x659+i,1)[0]==0)
     poke(0x661+index,0);poke(0x7b10,0);step(5);assert info()['spawns']==s['spawns']
     cmd('save_state',path=str(saved));step(3);load(saved);step(3);assert info()['spawns']==s['spawns']
     results.append(dict(test='controller_and_no_respawn',controller_flag=flags[1],spawned=1))
   cmd('quit');proc.wait(timeout=5)
  finally:
   if proc.poll() is None:proc.terminate();proc.wait(timeout=5)
   if stream:stream.close()
   if sock:sock.close()
   log.close()
 (a.out/'summary.json').write_text(json.dumps(results,indent=2));print(f'{len(results)} hidden window checks passed')

if __name__=='__main__':main()
