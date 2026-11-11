"""Injected-yield snapshot test; requires MM1_TEST_NMI_CONTINUATION=ON build.

The test build inserts one original native NextFrame call into the reflection
continuation. This tests a real NMI and save/load with that context in flight;
it does not establish natural NMI coverage in an ordinary production run.
"""
import argparse,json,os,pathlib,subprocess

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    lines=['0 -','180 START','181 -','360 START','361 -','1400 POKE 005D FE','1405 X1:R','1406 X1:-']
    for addr,value in ((0x480,112),(0x460,0),(0x600,160),(0x420,64),(0x400,9),(0x640,0),(0x680,0),(0x660,0),(0x6A,28)):
        lines.append(f'1400 HOLD {addr:04X} {value:02X}')
    def actor(frame,slot,x,y,kind,flags,hp):
        values={0x400:255,0x420:flags,0x440:0,0x460:0,0x480:x,0x4A0:0,0x4C0:0,0x4E0:0,
            0x600:y,0x620:0,0x640:127,0x660:0,0x680:0,0x6A0:1,0x6C0:hp,0x6E0:kind}
        lines.extend(f'{frame} POKE {addr+slot:04X} {value:02X}' for addr,value in values.items())
    for slot in range(17,32):actor(1400,slot,160+(slot-17)*4,152,0x21,6,120)
    for frame in range(1410,1490,5):actor(frame,16,120,152,0x1A,2,1)
    route=a.out/'input.txt';route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
    base=[str(a.exe),str(a.rom.resolve()),'--frames','1510','--input',str(route),'--protoman','on',
        '--weapon-cycle','on','--widescreen','off','--no-save','--config',str(a.out/'test.ini')]
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    def run(name,extra):
        with (a.out/(name+'.log')).open('w') as log:
            subprocess.run(base+['--proto-report',str(a.out/(name+'.jsonl')),'--hash-out',str(a.out/(name+'.hash')),
                '--present-out',str(a.out/(name+'.png')),*extra],cwd=a.exe.parent,env=env,check=True,
                stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        return {r['frame']:r for r in map(json.loads,(a.out/(name+'.jsonl')).read_text().splitlines())}
    extra=[]
    for frame in range(1410,1417):extra+=['--save-state',f'{frame}:{a.out/(str(frame)+".cycstate")}']
    rows=run('producer',extra);crossed=[r for r in rows.values() if r['continuation']]
    assert crossed,'No actual in-flight snapshot; verify the test-build option'
    chosen=crossed[0]['frame'];assert crossed[0]['effective_weapon']==5,crossed[0]
    resumed=run('resume',['--load-state',str(a.out/(str(chosen)+'.cycstate'))])
    tail=[s for s in (a.out/'producer.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>chosen]
    assert (a.out/'resume.hash').read_text().splitlines()==tail
    assert (a.out/'producer.png').read_bytes()==(a.out/'resume.png').read_bytes()
    assert resumed[1510]['effective_weapon']==5,resumed[1510]
    run('interpreter',['--interp-only'])
    assert (a.out/'producer.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
    assert (a.out/'producer.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
    summary=dict(result='pass',actual_inflight_snapshot_frame=chosen,nmi_crossings=len(crossed),
        injected_native_yield=True,natural_nmi_coverage=False,selected_special_preserved=True,
        cold_continuation_frames=len(tail),hash_and_pixels_exact=True,native_interpreter_exact=True)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
