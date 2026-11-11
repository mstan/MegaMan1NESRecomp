"""Real pellet wrapping, enemy wrapping, HUD anchoring and SMB3 border colors.

Requires Pillow; emits exact repro pictures and save states in wide-evidence.
"""
import argparse,json,pathlib
from PIL import Image
from validate import REPO,TARGET
from session import Session,prepare

def rgb(argb):return (argb>>16&255,argb>>8&255,argb&255)

def background_equal(g,picture,x,y,w,h):
 colors=g.cmd('ws_background',x=x,y=y,w=w,h=h)['argb'];x0=(picture.width-256)//2
 for r in range(h):
  for c in range(w):
   actual=picture.getpixel((x0+x+c,y+r));expected=rgb(colors[r*w+c])
   assert actual==expected,dict(x=x+c,y=y+r,actual=actual,expected=expected)

def check_wrap(g,name):
 path=g.shot(name);picture=Image.open(path).convert('RGB');s=g.state()
 oam=g.cmd('ws_oam')['oam'];wrapped=[o for o in oam if o[0]<239 and o[4]==1 and o[5]-s['camera']!=o[3]]
 assert wrapped,(name,s)
 for y,tile,attr,x,kind,world in wrapped:
  if x<248 and y<230:background_equal(g,picture,x,y+1,8,8)
 return dict(test=name,wrapped_tiles=len(wrapped),ghost_pixels_equal_background=True)

def check_borders(g,picture):
 x0=(picture.width-256)//2;data=g.cmd('ws_oam');rectangles=[]
 for y,tile,attr,x,kind,world in data['oam']:
  if y<192:rectangles.append((x,y+1,8,16))
 for world,y,tile,attr in data['packets']:rectangles.append((world-data['camera'],y,8,16))
 checked=0
 for x in (0,240):
  for y,h in ((0,128),(128,64)):
   colors=g.cmd('ws_background',x=x,y=y,w=16,h=h)['argb']
   for r in range(h):
    for c in range(16):
     px=x+c;py=y+r
     if any(sx<=px<sx+w and sy<=py<sy+height for sx,sy,w,height in rectangles):continue
     actual=picture.getpixel((x0+px,py));expected=rgb(colors[r*16+c])
     assert actual==expected,dict(x=px,y=py,actual=actual,expected=expected)
     checked+=1
 assert checked>3500,checked
 return checked

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--rom',type=pathlib.Path,required=True)
 p.add_argument('--exe',type=pathlib.Path,default=REPO/'build-wide/Release'/TARGET)
 p.add_argument('--out',type=pathlib.Path,default=REPO/'wide-evidence/rendering-regression')
 a=p.parse_args();a.rom=a.rom.resolve();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
 # The correct PAL profile reaches an enemy hit by frame 1500. Prepare the
 # initial live scene before that hit, then position on its starting platform.
 mm=TARGET.startswith('Mega');state=prepare(a.exe,a.rom,pathlib.Path(__file__).with_name('route.txt'),990 if mm else 1000,a.out)
 results=[]
 for mode in ('16:9','32:9'):
  with Session(a.exe,a.rom,a.out,mode) as g:
   g.load(state)
   if mm:
    for i in range(1,32):g.poke(0x600+i,248)
    g.poke(0x480,128);g.poke(0x460,0);g.poke(0x600,80)
    g.step(4,1);g.step(4,64);g.step(30)
    results.append(check_wrap(g,'pellet-right-'+mode.replace(':','-')))
    picture=Image.open(g.shot('hud-'+mode.replace(':','-'))).convert('RGB');x0=(picture.width-256)//2
    background_equal(g,picture,24,25,8,56)
    assert any(picture.getpixel((8+c,49+r))!=picture.getpixel((0,49+r)) for c in range(8) for r in range(32))
    results.append(dict(test='hud',mode=mode,x=8,original_bar_erased=True))
    saved=a.out/(mode.replace(':','-')+'.cycstate');g.cmd('save_state',path=str(saved));g.shot('saved')
    g.step(5);g.load(saved);g.shot('loaded')
    assert (a.out/'saved.png').read_bytes()==(a.out/'loaded.png').read_bytes()
    # Place a native major enemy in either margin; its wrapped native tiles
    # must be erased as well as rendering the signed actor at the true position.
    for edge,delta in (('right',320),('left',-48)):
     g.load(state);slot=next(i+16 for i,y in enumerate(g.ram(0x610,16)) if y<248)
     for i in range(1,32):
      if i!=slot:g.poke(0x600+i,248)
     # Keep the live player below the enemy's wrapped-copy rectangle; its
     # legitimate sprite pixels must not be mistaken for uncleared ghosts.
     for addr,val in ((0x1a,0),(0x1b,1),(0x480,128),(0x460,1),(0x600,180)):g.poke(addr,val)
     x=256+delta
     for addr,val in ((0x480+slot,x),(0x460+slot,x>>8),(0x600+slot,100),(0x6a0+slot,100),(0x440+slot,1)):g.poke(addr,val)
     g.step(3);results.append(check_wrap(g,'enemy-'+edge+'-'+mode.replace(':','-')))
   else:
    # Reproduce the scrolling first-pipe scene; inspect full color in the
    # original guard columns, including pixels omitted by the opacity smoke.
    for k in range(6):
     g.poke(0x553,255);g.step(90,193);g.step(5,65)
     picture=Image.open(g.shot(f'scroll-{mode.replace(":","-")}-{k}')).convert('RGB')
     pixels=check_borders(g,picture)
     s=g.state();assert s['failures']==0 and s['overflow']==0,s
     results.append(dict(test='border_colors',mode=mode,checkpoint=k,camera=s['camera'],background_pixels=pixels))
   s=g.state();assert s['failures']==0 and s['overflow']==0,s
 (a.out/'summary.json').write_text(json.dumps(results,indent=2));print(f'{len(results)} rendering regression checks passed')

if __name__=='__main__':main()
