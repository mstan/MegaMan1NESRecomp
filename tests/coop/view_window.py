"""Hidden actual windows preserve fixed co-op grids and local Fit selection."""
import argparse,json,pathlib,subprocess,sys,time
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'widescreen'))
from session import Session
from views import pixels

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--state',type=pathlib.Path)
    a=p.parse_args();a.exe=a.exe.resolve();a.rom=a.rom.resolve();a.out.mkdir(parents=True,exist_ok=True)
    if not a.state:
        fixture=a.out/'menu';subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name('menu.py')),
            '--rom',str(a.rom),'--exe',str(a.exe),'--out',str(fixture),'--view','split','--fit','--smoke-only'],
            check=True,creationflags=subprocess.CREATE_NO_WINDOW)
        a.state=fixture/'menu.cycstate'
    results=[]
    for order in ('coop-first','fit-first'):
        folder=a.out/order;folder.mkdir(exist_ok=True)
        coop=['--coop','on','--coop-view','split'];wide=['--widescreen','fit']
        args=['--protoman','off','--charge','off','--slide','off','--weapon-cycle','on']+(coop+wide if order=='coop-first' else wide+coop)
        with Session(a.exe,a.rom,folder,'fit',args=args) as g:
            def video(width):
                time.sleep(.08);v=g.cmd('video');assert v['mode']=='fit' and v['width']==width,(order,v);return v
            video(512)
            # Before gameplay, clearing a layout follows the selected aspect.
            g.cmd('window_size',w=1280,h=720);time.sleep(.08)
            g.cmd('coop_view',view='shared');video(426)
            g.cmd('coop_view',view='split');video(512)
            g.load(a.state.resolve());video(512)
            ram=g.ram(0,2048);split=pixels(g.shot('split'))
            assert split[:2]==(512,240)
            for w,h in ((1920,720),(640,480),(1280,720)):
                g.cmd('window_size',w=w,h=h);video(512)
            g.cmd('video',mode='21:9');time.sleep(.08)
            assert g.cmd('video')['width']==512
            g.cmd('video',mode='fit');video(512)
            for mode,seat in (('p1',0),('p2',1)):
                g.cmd('coop_view',view=mode);video(256)
                width,height,one=pixels(g.shot(mode));assert (width,height)==(256,240)
                assert all(one[y]==split[2][y][seat*768:(seat+1)*768] for y in range(240)),(order,mode)
                assert g.ram(0,2048)==ram,'Paused view change advanced native gameplay'
            g.cmd('coop_view',view='split');video(512)
            assert pixels(g.shot('split-again'))==split
            results.append(dict(order=order,fit_preserved=True,fixed_resize=True,paused_own_panels_exact=True,ram_unchanged=True))
    summary=dict(result='pass',actual_hidden_windows=True,normal_audio_emulation_dummy_output=True,cases=results)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
