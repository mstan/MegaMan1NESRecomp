"""Real NMI continuations in native guest collision/projectile scopes."""
import argparse,json,os,pathlib,subprocess,sys,wave
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'protoman'))
from validate import pixels

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    lines=['0 -','0 2:-','180 START','181 -','360 START','361 -','1490 POKE 005D FE',
        '1490 POKE 005F 05','1490 POKE 006F 1C','1490 POKE 0055 81']
    pose={0x480:32,0x460:0,0x600:180,0x420:64,0x400:0,0x440:0,0x640:0,
        0x680:0,0x660:0,0x4A0:0,0x620:0}
    for address,value in pose.items():lines.append(f'1490 POKE {address:04X} {value:02X}')
    # Authored density precondition; execution and the NMI remain native.
    for slot in range(16,32):
        actor={0x400:255,0x420:6,0x440:0,0x460:0,0x480:176,0x4A0:0,0x4C0:0,0x4E0:0,
            0x600:176,0x620:0,0x640:127,0x660:0,0x680:0,0x6A0:1,0x6C0:255,0x6E0:0x21}
        for address,value in actor.items():lines.append(f'1490 HOLD {address+slot:04X} {value:02X}')
    for frame in range(1500,1600,30):lines += [f'{frame} B',f'{frame} 2:B',f'{frame+1} -',f'{frame+1} 2:-']
    lines+=['1543 X2:R','1544 X2:-']
    route=a.out/'input.txt';route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
    pose.update({0x480:64,0x55:0x81,0x5F:5,0x6F:28});pokes=[]
    for address,value in pose.items():pokes+=['--coop-test-poke',f'1490:2:{address:X}:{value:X}']
    base=[str(a.exe),str(a.rom.resolve()),'--frames','1600','--coop','on','--weapon-cycle','on','--charge','off',
        '--widescreen','fit','--widescreen-enemies','classic','--present-size','1280x720','--input',str(route),
        *pokes,'--no-save','--config',str(a.out/'test.ini')]
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    def run(name,extra=()):
        with (a.out/(name+'.log')).open('w') as log:
            subprocess.run(base+['--coop-report',str(a.out/(name+'.jsonl')),'--hash-out',str(a.out/(name+'.hash')),
                '--present-out',str(a.out/(name+'.png')),'--wav-out',str(a.out/(name+'.wav')),*extra],
                cwd=a.exe.parent,env=env,check=True,stdout=log,stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
    states=[a.out/'collision.cycstate',a.out/'pool.cycstate']
    run('native',['--save-state',f'1530:{states[0]}','--save-state',f'1542:{states[1]}','--present-every','1530'])
    rows={r['frame']:r for r in map(json.loads,(a.out/'native.jsonl').read_text().splitlines())}
    assert rows[1531]['phase']==2 and rows[1543]['phase']==3,(rows[1531],rows[1543])
    assert rows[1531]['owner']==rows[1543]['owner']==1 and rows[1600]['failures']==0
    assert rows[1600]['weapon']==[5,6],rows[1600]
    def pcm(path):
        with wave.open(str(path),'rb') as f:return f.readframes(f.getnframes())
    original=pcm(a.out/'native.wav')
    for name,state,frame in zip(('collision','pool'),states,(1530,1542)):
        run(name,['--load-state',str(state),'--present-every','1542'])
        tail=[s for s in (a.out/'native.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>frame]
        assert (a.out/(name+'.hash')).read_text().splitlines()==tail
        assert (a.out/(name+'.png')).read_bytes()==(a.out/'native.png').read_bytes()
        resumed=pcm(a.out/(name+'.wav'));assert resumed and original[-len(resumed):]==resumed
    # No frame is simulated: inspect the restored, in-flight pool picture.
    run('pool-picture',['--load-state',str(states[1]),'--frames','1543'])
    assert (a.out/'pool-picture.png').read_bytes()==(a.out/'collision_01542.png').read_bytes()
    for name in ('native_01530.png','pool-picture.png'):
        width,height,rgb=pixels(a.out/name);assert (width,height)==(426,240)
        colors={rgb[y][x*3:x*3+3] for y in range(24,220) for x in range(341,426)}
        assert len(colors)>5,(name,len(colors))
    run('interpreter',['--interp-only','--present-every','1530'])
    assert (a.out/'native.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
    assert (a.out/'native.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
    assert (a.out/'native_01530.png').read_bytes()==(a.out/'interpreter_01530.png').read_bytes()
    summary=dict(result='pass',injected_wait=False,native_collision_nmi=True,native_pool_nmi=True,
        current_state_hash_pixels_pcm_exact=True,fit_margins_during_both_scopes=True,
        personal_lr_deferred_across_native_nmi=True,native_interpreter_exact=True)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
