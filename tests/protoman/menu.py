"""Native inventory controls/layout with a character-specific life portrait."""
import argparse,json,os,pathlib,subprocess
from validate import pixels

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    lines=['0 -','180 START','181 -','360 START','361 -','1400 POKE 005D FE',
        '1400 HOLD 0055 81','1400 HOLD 006A 1C','1500 START','1501 -']
    route=a.out/'input.txt';route.write_text('\n'.join(lines)+'\n')
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    base=[str(a.exe),str(a.rom.resolve()),'--frames','1570','--input',str(route),'--protoman','off',
        '--widescreen','off','--no-save','--config',str(a.out/'test.ini')]
    def run(name,extra):
        with (a.out/(name+'.log')).open('w') as log:
            subprocess.run(base+['--present-out',str(a.out/(name+'.png')),'--screenshot',str(a.out/(name+'-native.png')),
                '--hash-out',str(a.out/(name+'.hash')),*extra],cwd=a.exe.parent,env=env,check=True,
                stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
    run('stock',[])
    state=a.out/'inventory.cycstate'
    run('proto',['--protoman','on','--save-state',f'1540:{state}'])
    assert (a.out/'stock.hash').read_bytes()==(a.out/'proto.hash').read_bytes()
    assert (a.out/'stock.png').read_bytes()==(a.out/'proto-native.png').read_bytes()
    _,_,stock=pixels(a.out/'stock.png');_,_,proto=pixels(a.out/'proto.png')
    changed=red=0
    for y in range(240):
        for x in range(256):
            before=stock[y][x*3:x*3+3];after=proto[y][x*3:x*3+3]
            if before!=after:
                assert 168<=x<184 and 161<=y<177,(x,y,before,after);changed+=1
            if 168<=x<184 and 161<=y<177:
                r,g,b=after;red+=r>80 and r>g*1.5 and r>b*2
    assert changed>50 and red>20,(changed,red)
    run('interpreter',['--protoman','on','--interp-only'])
    assert (a.out/'proto.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
    assert (a.out/'proto.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
    run('resume',['--protoman','on','--load-state',str(state)])
    tail=[s for s in (a.out/'proto.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1540]
    assert (a.out/'resume.hash').read_text().splitlines()==tail
    assert (a.out/'proto.png').read_bytes()==(a.out/'resume.png').read_bytes()
    run('fit',['--protoman','on','--widescreen','fit','--widescreen-enemies','classic','--present-size','1280x720'])
    w,h,fit=pixels(a.out/'fit.png');assert (w,h)==(426,240)
    assert all(fit[y][85*3:(85+256)*3]==proto[y] for y in range(240))
    swapped=a.out/'swapped.txt'
    swapped.write_text('\n'.join(sorted(lines+['1405 SELECT','1406 -'],key=lambda s:int(s.split()[0])))+'\n')
    run('mega-selected',['--protoman','on','--input',str(swapped)])
    assert (a.out/'mega-selected.png').read_bytes()==(a.out/'stock.png').read_bytes()
    summary=dict(result='pass',native_layout_outside_portrait_exact=True,native_oam_and_gameplay_hash_exact=True,
        portrait_grid=[168,161,16,16],changed_pixels=changed,red_pixels=red,native_interpreter_exact=True,
        cold_inventory_continuation=len(tail),fit426_native_panel_exact=True,selected_mega_retains_original_portrait=True)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
