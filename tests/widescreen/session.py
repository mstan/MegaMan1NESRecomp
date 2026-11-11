"""Paused hidden cycle game session for exact rendering repros."""
import json,os,pathlib,socket,subprocess,time

def hidden_options():
 if not hasattr(subprocess,'STARTUPINFO'):return {}
 si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=0
 return dict(startupinfo=si,creationflags=subprocess.CREATE_NO_WINDOW)

def prepare(exe,rom,route,frame,out):
 state=out/'base.cycstate'
 with (out/'prepare.log').open('w') as log:
  r=subprocess.run([str(exe),str(rom),'--frames',str(frame+1),'--input',str(route),
      '--widescreen','32:9','--save-state',f'{frame}:{state}','--no-save','--config',str(out/'prepare.ini')],cwd=exe.parent,
      stdout=log,stderr=subprocess.STDOUT,**hidden_options())
 assert r.returncode==0,(out/'prepare.log').read_text()
 return state

class Session:
 def __init__(self,exe,rom,out,mode):
  self.out=out;self.seq=0;self.sock=None;self.stream=None
  with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
  self.log=(out/(mode.replace(':','-')+'.log')).open('w')
  self.proc=subprocess.Popen([str(exe),str(rom),'--hidden','--no-save','--config',str(out/'config.ini'),
      '--tcp',str(port),'--widescreen',mode],cwd=exe.parent,env=dict(os.environ,NESRECOMP_NO_LAUNCHER='1'),
      stdout=self.log,stderr=subprocess.STDOUT,**hidden_options())
  try:
   until=time.monotonic()+15
   while time.monotonic()<until:
    try:self.sock=socket.create_connection(('127.0.0.1',port),timeout=1);break
    except OSError:time.sleep(.05)
   assert self.sock,'TCP startup failed'
   self.sock.settimeout(30);self.stream=self.sock.makefile('rwb');self.cmd('menu',open=True)
  except BaseException:self.close();raise
 def cmd(self,name,**args):
  self.seq+=1;self.stream.write((json.dumps(dict(cmd=name,id=self.seq,**args))+'\n').encode());self.stream.flush()
  reply=json.loads(self.stream.readline());assert reply['ok'],(name,reply);return reply
 def poke(self,a,v):self.cmd('ws_poke',addr=a,value=v&255)
 def ram(self,a,n):return bytes.fromhex(self.cmd('read_ram',addr=a,len=n)['hex'])
 def step(self,n=1,pad=0):self.cmd('ws_step',frames=n,pad=pad)
 def load(self,p):self.cmd('load_state',path=str(p))
 def state(self):return self.cmd('ws_state',verify=True)
 def shot(self,name):
  p=self.out/(name+'.png');self.cmd('screenshot',path=str(p));return p
 def close(self):
  if self.proc.poll() is None:
   try:
    if self.stream:self.cmd('quit');self.proc.wait(timeout=5)
   except (OSError,AssertionError,ValueError,subprocess.TimeoutExpired):pass
   if self.proc.poll() is None:self.proc.terminate();self.proc.wait(timeout=5)
  if self.stream:self.stream.close()
  if self.sock:self.sock.close()
  self.log.close()
 def __enter__(self):return self
 def __exit__(self,*args):self.close()
