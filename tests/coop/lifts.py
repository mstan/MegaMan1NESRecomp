"""Both players share a real native Foot Holder's landing and carry speed."""
import argparse,json,os,pathlib,subprocess,wave

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--roster',choices=('megaman','protoman'),default='megaman')
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    lines=['0 -','0 2:-','180 START','181 -','360 START','361 -']
    for slot in range(16,32):lines.append(f'1490 POKE {0x600+slot:04X} F8')
    # One-time native actor precondition. Original AI/movement/StoreLiftData
    # must advance it and build the common lift table; neither is held/poked.
    actor={0x400:255,0x420:70,0x440:0,0x460:0,0x480:96,0x4A0:0,0x4C0:1,0x4E0:0,
        0x600:176,0x620:0,0x640:63,0x660:0,0x680:0,0x6A0:1,0x6C0:8,0x6E0:0x13}
    for address,value in actor.items():lines.append(f'1490 POKE {address+18:04X} {value:02X}')
    pose={0x460:0,0x480:104,0x600:130,0x400:9,0x440:0,0x420:64,0x640:0,0x680:254,
        0x660:0,0x4A0:0,0x620:0,0x55:255}
    pokes=[]
    for seat in (1,2):
        for address,value in pose.items():pokes+=['--coop-test-poke',f'1502:{seat}:{address:X}:{value:X}']
    route=a.out/'input.txt';route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    base=[str(a.exe),str(a.rom.resolve()),'--frames','1530','--input',str(route),'--protoman','off','--coop','on',
        '--coop-p1',a.roster,'--charge','off','--slide','off','--weapon-cycle','off','--widescreen','off',
        '--no-save','--config',str(a.out/'test.ini'),*pokes]
    def run(name,extra=()):
        with (a.out/(name+'.log')).open('w') as log:subprocess.run(base+[
            '--coop-report',str(a.out/(name+'.jsonl')),'--hash-out',str(a.out/(name+'.hash')),
            '--frame-log',str(a.out/(name+'.frames')),'--frame-log-frames','1529:1529',
            '--present-out',str(a.out/(name+'.png')),'--wav-out',str(a.out/(name+'.wav')),*extra],
            cwd=a.exe.parent,env=env,check=True,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
        return {r['frame']:r for r in map(json.loads,(a.out/(name+'.jsonl')).read_text().splitlines())}
    state=a.out/'riding.cycstate';rows=run('native',('--save-state',f'1515:{state}'))
    m=(a.out/'native.frames').read_bytes()[336:2384]
    assert m[0x9A] and m[0x9D]==1 and m[0x492]>120 and m[0x612]==176,('Native lift did not advance',m[0x9A],m[0x9D],m[0x492],m[0x612])
    row=rows[1530];assert row['failures']==0 and row['phase']==0,row
    assert row['x'][0]==row['x'][1] and row['x'][0]>=112 and row['y']==[152,152],('Personal carry differs on one common lift',row)
    run('interpreter',('--interp-only',));assert (a.out/'native.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
    assert (a.out/'native.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
    run('resume',('--load-state',str(state)))
    tail=[s for s in (a.out/'native.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1515]
    assert tail==(a.out/'resume.hash').read_text().splitlines();assert (a.out/'native.png').read_bytes()==(a.out/'resume.png').read_bytes()
    def pcm(name):
        with wave.open(str(a.out/(name+'.wav')),'rb') as f:return f.readframes(f.getnframes())
    continued=pcm('resume');assert continued and pcm('native')[-len(continued):]==continued
    summary=dict(result='pass',roster=a.roster,actual_native_lift_world_once=True,both_personal_landing_and_carry=True,
        native_interpreter_exact=True,current_state_hash_pixels_pcm_exact=True)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
