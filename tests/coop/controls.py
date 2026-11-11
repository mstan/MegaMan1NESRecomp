"""Development controller slice: independent native movement and shared world."""
import argparse,json,os,pathlib,subprocess

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    boot=['0 -','0 2:-','180 START','181 -','360 START','361 -',
          '1400 HOLD 0055 81','1400 HOLD 006A 1C']
    moving=boot+['1420 2:LEFT','1460 2:-']
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    base=[str(a.exe),str(a.rom.resolve()),'--frames','1480','--protoman','off','--coop','on',
          '--widescreen','off','--weapon-cycle','on','--no-save','--config',str(a.out/'test.ini')]
    def run(name,lines,extra=()):
        route=a.out/(name+'.txt');route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
        with (a.out/(name+'.log')).open('w') as log:
            subprocess.run(base+['--input',str(route),'--coop-report',str(a.out/(name+'.jsonl')),
                '--hash-out',str(a.out/(name+'.hash')),'--present-out',str(a.out/(name+'.png')),
                '--frame-log',str(a.out/(name+'.frames')),'--frame-log-frames','1479:1479',*extra],
                cwd=a.exe.parent,env=env,check=True,stdout=log,stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        return {r['frame']:r for r in map(json.loads,(a.out/(name+'.jsonl')).read_text().splitlines())}
    state=a.out/'moving.cycstate'
    rows=run('native',moving,('--save-state',f'1440:{state}'))
    end=rows[1480];assert end['ready'] and end['failures']==0 and end['updates']>100,end
    assert end['x'][0]==rows[1410]['x'][0] and end['x'][1]<rows[1410]['x'][1]-40,(rows[1410],end)
    assert end['picture'],end
    run('interpreter',moving,('--interp-only',))
    assert (a.out/'native.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
    assert (a.out/'native.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
    run('resume',moving,('--load-state',str(state)))
    tail=[s for s in (a.out/'native.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1440]
    assert (a.out/'resume.hash').read_text().splitlines()==tail
    assert (a.out/'native.png').read_bytes()==(a.out/'resume.png').read_bytes()
    idle=run('idle',boot)
    assert (a.out/'native.frames').read_bytes()==(a.out/'idle.frames').read_bytes()
    assert (a.out/'native.hash').read_bytes()==(a.out/'idle.hash').read_bytes()
    assert idle[1480]['x'][1]==rows[1410]['x'][1],(idle[1480],rows[1410])
    assert (a.out/'native.png').read_bytes()!=(a.out/'idle.png').read_bytes()
    summary=dict(result='pass',independent_native_movement=True,controller_failures=0,
        native_world_ram_and_hash_unchanged_by_passive_guest_movement=True,
        native_interpreter_exact=True,current_build_continuation=len(tail),second_body_presented=True,
        development_slice_only=True)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
