"""Personal native weapon art and shared Guts-block mutations."""
import argparse,json,os,pathlib,subprocess,sys,wave
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'protoman'))
from validate import pixels

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--mutable-block',action='store_true',help='Keep the authored Guts block and require its native destruction')
    p.add_argument('--guts-lift',action='store_true',help='Use native Guts pickup and its carried block instead of Elec')
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    pose={0x460:0,0x480:64,0x600:180,0x400:0,0x440:0,0x420:64,0x640:0,0x680:0,0x660:0,0x4A0:0,0x620:0,0x55:255}
    if a.guts_lift:
        assert a.mutable_block,'Guts pickup needs the authored active block'
        pose[0x480]=120
    def run(name,coop=False,roster='megaman',extra=()):
        # Keep this art oracle out of the separate mutable Guts block path;
        # an ordinary beam must remain an ordinary beam.
        lines=['0 -','0 2:-','180 START','181 -','360 START','361 -','1300 POKE 005D FE','1629 POKE 0023 00']
        if not a.mutable_block:lines+=['1629 POKE 0721 00','1629 POKE 0728 FF','1629 POKE 008E 00']
        for slot in range(16,32):lines.append(f'1629 POKE {0x600+slot:04X} F8')
        pokes=[]
        if coop:
            lines+=['1440 X2:R','1441 X2:-','1452 X2:R','1453 X2:-','1630 2:B','1638 2:-']
            if a.guts_lift:lines+=['1464 X2:R','1465 X2:-']
            for address,value in pose.items():pokes+=['--coop-test-poke',f'1629:2:{address:X}:{value:X}']
            for address,value in {**pose,0x480:32}.items():pokes+=['--coop-test-poke',f'1629:1:{address:X}:{value:X}']
        else:
            lines+=['1400 START','1401 -','1440 DOWN','1441 -','1452 DOWN','1453 -','1540 START','1541 -','1630 B','1638 -']
            if a.guts_lift:lines+=['1464 DOWN','1465 -']
            lines += [f'1629 POKE {address:04X} {value:02X}' for address,value in pose.items()]
        route=a.out/(name+'.txt');route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
        args=[str(a.exe),str(a.rom.resolve()),'--frames','1642','--input',str(route),'--protoman','off',
            '--coop','on' if coop else 'off','--coop-p1',roster,'--charge','off','--slide','off','--weapon-cycle','on',
            '--widescreen','off','--no-save','--config',str(a.out/'test.ini'),*pokes,
            '--coop-report',str(a.out/(name+'.jsonl')),'--hash-out',str(a.out/(name+'.hash')),
            '--frame-log',str(a.out/(name+'.frames')),'--frame-log-frames','1641:1641',
            '--present-out',str(a.out/(name+'.png')),'--screenshot',str(a.out/(name+'-native.png')),'--wav-out',str(a.out/(name+'.wav')),*extra]
        with (a.out/(name+'.log')).open('w') as log:subprocess.run(args,cwd=a.exe.parent,env=env,check=True,
            stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
    run('reference');_,_,reference=pixels(a.out/'reference-native.png')
    def ram(name):return (a.out/(name+'.frames')).read_bytes()[336:2384]
    if a.mutable_block:assert ram('reference')[0x727]==4,'Original weapon did not remove the authored block'
    aligned={}
    for roster in ('megaman','protoman'):
        name='guest-'+roster;state=a.out/(name+'.cycstate');run(name,True,roster,('--save-state',f'1634:{state}'))
        _,_,actual=pixels(a.out/(name+'.png'));_,_,raw=pixels(a.out/(name+'-native.png'))
        rows=[json.loads(s) for s in (a.out/(name+'.jsonl')).read_text().splitlines()]
        assert rows[-1]['weapon']==[0,6 if a.guts_lift else 5] and rows[-1]['failures']==0,rows[-1]
        if a.mutable_block:
            assert ram(name)[0x727]==4,('Guest did not commit native block destruction',ram(name)[0x727])
            assert (a.out/(name+'.frames')).read_bytes()[2384:4432]==(a.out/'reference.frames').read_bytes()[2384:4432], 'Native tile/attribute transfer differs'
            # Retail tile transfer can stall a hardware frame. Compare native
            # debris at the same actual NMI update count; the guest's existing
            # isolated firing call does not consume the retail call's cycles.
            delta=(ram(name)[0x23]-ram('reference')[0x23])&255
            assert delta<=3,('Unexpected native effect timing difference',delta)
            if delta not in aligned:
                match='reference-phase-'+str(delta)
                run(match,extra=('--frames',str(1642+delta),'--frame-log-frames',f'{1641+delta}:{1641+delta}'))
                assert ram(match)[0x23]==ram(name)[0x23]
                _,_,aligned[delta]=pixels(a.out/(match+'-native.png'))
            reference=aligned[delta]
        xs,ys=(range(96,144),range(128,160)) if a.guts_lift else (range(80,248),range(140,208))
        differences=sum(reference[y][x*3:x*3+3]!=actual[y][x*3:x*3+3] for y in ys for x in xs)
        visible=sum(raw[y][x*3:x*3+3]!=actual[y][x*3:x*3+3] for y in ys for x in xs)
        assert differences==0 and visible>10,(roster,differences,visible)
        if roster=='megaman':
            run('interpreter',True,roster,('--interp-only',));assert (a.out/(name+'.hash')).read_bytes()==(a.out/'interpreter.hash').read_bytes()
            assert (a.out/(name+'.png')).read_bytes()==(a.out/'interpreter.png').read_bytes()
            run('resume',True,roster,('--load-state',str(state)))
            tail=[s for s in (a.out/(name+'.hash')).read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1634]
            assert tail==(a.out/'resume.hash').read_text().splitlines()
            assert (a.out/(name+'.png')).read_bytes()==(a.out/'resume.png').read_bytes()
            def pcm(n):
                with wave.open(str(a.out/(n+'.wav')),'rb') as f:return f.readframes(f.getnframes())
            continued=pcm('resume');assert continued and pcm(name)[-len(continued):]==continued
    summary=dict(result='pass',both_rosters_native_private_weapon_pixels_exact=True,own_pattern_while_anchor_uses_buster=True,
        current_build_state_pcm_exact=True,native_interpreter_exact=True,authored_block_destroyed=a.mutable_block,guts_pickup=a.guts_lift)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
