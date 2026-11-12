"""An original Blader chooses the nearest living player and dives toward it."""
import argparse,json,os,pathlib,struct,subprocess,wave

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--near-primary',action='store_true')
    p.add_argument('--withdraw-guest',action='store_true')
    p.add_argument('--dense',action='store_true',help='Check real AI NMI continuation and distinct armor palettes')
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    lines=['0 -','0 2:-','180 START','181 -','360 START','361 -']
    if a.withdraw_guest:lines+=['1390 2:SELECT','1485 2:-']
    if a.dense:lines+=['1380 POKE 005D FE','1400 X1:R','1401 X1:-','1412 X1:R','1413 X1:-']
    if a.dense:
        for frame in range(1500,1600,30):lines += [f'{frame} B',f'{frame} 2:B',f'{frame+1} -',f'{frame+1} 2:-']
    for slot in range(16,32):lines.append(f'1500 POKE {0x600+slot:04X} F8')
    actor={0x400:255,0x420:6,0x440:0,0x460:0,0x480:160,0x4A0:0,0x4C0:1,0x4E0:0,
        0x600:100,0x620:0,0x640:0,0x660:0,0x680:0,0x6A0:1,0x6C0:8,0x6E0:0}
    for slot in range(16,32) if a.dense else (18,):
        for address,value in actor.items():lines.append(f'1500 POKE {address+slot:04X} {value:02X}')
        # Stress the original search/jump calculation on each real update.
        # Target/direction/position and the NMI are still computed natively.
        if a.dense:lines += [f'1500 HOLD {0x640+slot:04X} 00',f'1500 POKE {0x6C0+slot:04X} FF']
    pose={0x460:0,0x480:32,0x600:180,0x400:0,0x440:0,0x420:64,0x640:0,0x680:0,
        0x660:0,0x4A0:0,0x620:0,0x55:255}
    pokes=[]
    for seat in (1,2):
        x=176 if (seat==1)==a.near_primary else 32
        for address,value in {**pose,0x480:x}.items():pokes+=['--coop-test-poke',f'{1380 if a.withdraw_guest else 1499}:{seat}:{address:X}:{value:X}']
        if a.dense:pokes+=['--coop-test-poke',f'1380:{seat}:55:FF']
    route=a.out/'input.txt';route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    frames=1600 if a.dense else 1505
    base=[str(a.exe),str(a.rom.resolve()),'--frames',str(frames),'--input',str(route),'--protoman','off','--coop','on',
        '--charge','off','--slide','off','--weapon-cycle','on' if a.dense else 'off','--widescreen','off',
        '--no-save','--config',str(a.out/'test.ini'),*pokes]
    def run(name,extra=()):
        with (a.out/(name+'.log')).open('w') as log:subprocess.run(base+[
            '--coop-report',str(a.out/(name+'.jsonl')),'--hash-out',str(a.out/(name+'.hash')),
            '--frame-log',str(a.out/(name+'.frames')),'--frame-log-frames','1500:1599' if a.dense else '1504:1504',
            '--present-out',str(a.out/(name+'.png')),'--wav-out',str(a.out/(name+'.wav')),*extra],
            cwd=a.exe.parent,env=env,check=True,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
        return {r['frame']:r for r in map(json.loads,(a.out/(name+'.jsonl')).read_text().splitlines())}
    state=a.out/'targeting.cycstate';rows=run('native',('--save-state',f'1501:{state}'))
    m=(a.out/'native.frames').read_bytes()[336:2384]
    assert rows[frames]['failures']==0 and m[0x6F2]==0,rows[frames]
    checkpoint=1501;ai_frames=[]
    if a.dense:
        rom=a.rom.read_bytes();palette=rom[16+7*16384+0x148F:16+7*16384+0x1491]
        blob=(a.out/'native.frames').read_bytes();offset=12
        while offset<len(blob):
            h=struct.unpack_from('<9I',blob,offset);ram=blob[offset+324:offset+324+2048]
            row=rows[h[0]+1]
            if row['ai'] and row['owner']==1:
                ai_frames.append(h[0]);assert ram[0x3E1:0x3E3]==palette,(h[0],ram[0x3E1:0x3E3],palette)
                assert blob[offset+36+17:offset+36+19]==palette,('Physical armor palette differs',h[0])
            offset+=324+sum(h[3:])
        assert ai_frames,'No real native NMI in the projected enemy scope'
        checkpoint=ai_frames[0];run('producer',('--save-state',f'{checkpoint}:{state}','--present-every',str(checkpoint)))
        assert (a.out/'native.hash').read_bytes()==(a.out/'producer.hash').read_bytes()
        run('instant',('--load-state',str(state),'--frames',str(checkpoint+1)))
        assert (a.out/'instant.png').read_bytes()==(a.out/f'producer_{checkpoint:05d}.png').read_bytes()
    elif a.withdraw_guest:
        assert rows[1505]['withdrawn']==2 and not(m[0x432]&64) and m[0x492]<160,rows[1505]
    else:
        assert m[0x432]&64 and m[0x652]>0 and m[0x492]>=160,('Native Blader ignored the nearby player',m[0x432],m[0x652],m[0x492])
    run('interpreter',('--interp-only',));assert (a.out/'native.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
    assert (a.out/'native.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
    run('resume',('--load-state',str(state)))
    tail=[s for s in (a.out/'native.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>checkpoint]
    assert tail==(a.out/'resume.hash').read_text().splitlines();assert (a.out/'native.png').read_bytes()==(a.out/'resume.png').read_bytes()
    def pcm(name):
        with wave.open(str(a.out/(name+'.wav')),'rb') as f:return f.readframes(f.getnframes())
    continued=pcm('resume');assert continued and pcm('native')[-len(continued):]==continued
    summary=dict(result='pass',nearest_native_blader=True,near_primary=a.near_primary,withdrawn_guest_ignored=a.withdraw_guest,
        actual_ai_nmi_frames=ai_frames,native_interpreter_exact=True,current_state_hash_pixels_pcm_exact=True)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
