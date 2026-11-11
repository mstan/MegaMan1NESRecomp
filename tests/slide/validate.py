"""Native slide distance, both characters, art and current-build continuation."""
import argparse,json,os,pathlib,subprocess

def fixture():
    lines=['0 -','180 START','181 -','360 START','361 -']
    for slot in range(16,32):lines.append(f'1400 POKE {0x600+slot:04X} F8')
    for addr,value in ((0x480,32),(0x460,0),(0x4A0,0),(0x600,180),(0x620,0),
                       (0x420,64),(0x400,0),(0x640,0),(0x680,0),(0x660,0),(0x55,0x81),(0x6A,28)):
        lines.append(f'1400 POKE {addr:04X} {value:02X}')
    return lines+['1410 DOWN+A','1411 -']

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','donor','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    route=a.out/'input.txt';route.write_text('\n'.join(fixture())+'\n')
    base=[str(a.exe),str(a.rom.resolve()),'--frames','1470','--input',str(route),
          '--mm4-rom',str(a.donor.resolve()),'--slide','on','--protoman','off','--charge','off',
          '--widescreen','off','--no-save','--config',str(a.out/'test.ini')]
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    def run(name,extra):
        with (a.out/(name+'.log')).open('w') as log:
            subprocess.run(base+['--slide-report',str(a.out/(name+'.jsonl')),'--hash-out',str(a.out/(name+'.hash')),
                '--present-out',str(a.out/(name+'.png')),'--screenshot',str(a.out/(name+'-native.png')),*extra],
                cwd=a.exe.parent,env=env,check=True,stdout=log,stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        return {r['frame']:r for r in map(json.loads,(a.out/(name+'.jsonl')).read_text().splitlines())}
    state=a.out/'sliding.cycstate'
    native=run('native',['--save-state',f'1420:{state}'])
    assert native[1470]['starts']==1 and native[1470]['timed']==1,native[1470]
    assert native[1470]['x']==97 and native[1470]['y']==180,native[1470]
    assert native[1420]['active'] and native[1420]['timer']>0,native[1420]
    run('interpreter',['--interp-only'])
    assert (a.out/'native.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
    assert (a.out/'native.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
    run('resume',['--load-state',str(state)])
    tail=[s for s in (a.out/'native.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1420]
    assert (a.out/'resume.hash').read_text().splitlines()==tail
    assert (a.out/'native.png').read_bytes()==(a.out/'resume.png').read_bytes()
    for name,extra in (('mega-slide',[]),('proto-slide',['--protoman','on'])):
        rows=run(name,['--frames','1421',*extra]);assert rows[1421]['active'],rows[1421]
        assert (a.out/(name+'.png')).read_bytes()!=(a.out/(name+'-native.png')).read_bytes()
    run('art-resume',['--load-state',str(state),'--frames','1421'])
    assert (a.out/'mega-slide.png').read_bytes()==(a.out/'art-resume.png').read_bytes()
    proto=run('proto',['--protoman','on'])
    assert proto[1470]['x']==97 and proto[1470]['timed']==1,proto[1470]
    summary=dict(result='pass',distance_pixels=65,moving_frames=26,both_characters=True,
                 native_interpreter_exact=True,current_build_continuation=len(tail),actual_slide_art=True)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
