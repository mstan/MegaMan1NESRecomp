"""Compare quick-switch patterns with the original native weapon menu."""
import argparse,hashlib,json,os,pathlib,struct,subprocess

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--weapons',default='5,3,6')
    p.add_argument('--quick-order',help='explicit input sequence for a historical baseline')
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    order=(0,3,5,6,2,1,4,7);quick_order=tuple(map(int,a.quick_order.split(','))) if a.quick_order else order;results=[]
    for weapon in map(int,a.weapons.split(',')):
        for native in (True,False):
            count=(order if native else quick_order).index(weapon);name=f'{weapon}-'+('menu' if native else 'quick')
            route=['0 -','180 START','181 -','360 START','361 -','1300 POKE 005D FE','1300 HOLD 0055 81']
            if native:route+=['1400 START','1401 -']
            for i in range(count):
                frame=1440+i*12;route += [f'{frame} DOWN' if native else f'{frame} X1:R',f'{frame+1} -' if native else f'{frame+1} X1:-']
            if native:route+=['1540 START','1541 -']
            route+=['1630 B','1638 -']
            path=a.out/(name+'.txt');path.write_text('\n'.join(sorted(route,key=lambda s:int(s.split()[0])))+'\n')
            args=[str(a.exe),str(a.rom.resolve()),'--frames','1642','--input',str(path),'--coop','off','--protoman','off',
                '--charge','off','--slide','off','--weapon-cycle','on','--widescreen','off','--no-save','--config',str(a.out/'test.ini'),
                '--weapon-report',str(a.out/(name+'.jsonl')),'--frame-log',str(a.out/(name+'.frames')),'--frame-log-frames','1641:1641',
                '--screenshot',str(a.out/(name+'.png'))]
            with (a.out/(name+'.log')).open('w') as log:subprocess.run(args,cwd=a.exe.parent,env=env,check=True,
                stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
            row=json.loads((a.out/(name+'.jsonl')).read_text().splitlines()[-1]);assert row['weapon']==weapon,row
            blob=(a.out/(name+'.frames')).read_bytes();head=struct.unpack_from('<9I',blob,12);offset=336+sum(head[3:6])
            patterns=blob[offset+0xE00:offset+0xF00];assert len(patterns)==256
            if native:expected=patterns
            else:
                different=sum(x!=y for x,y in zip(expected,patterns));results.append(dict(weapon=weapon,different=different,
                    native_sha256=hashlib.sha256(expected).hexdigest(),quick_sha256=hashlib.sha256(patterns).hexdigest()))
                (a.out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
                assert different==0,results[-1]
    print(json.dumps(dict(result='pass',native_selected_weapon_patterns_exact=results)))

if __name__=='__main__':main()
