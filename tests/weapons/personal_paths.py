"""Compare both native personal special-weapon paths with the solo retail path."""
import argparse,json,os,pathlib,subprocess,wave

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--weapon',choices=('cut','bomb'),required=True)
    p.add_argument('--smoke-only',action='store_true')
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    count=5 if a.weapon=='cut' else 1;weapon=1 if a.weapon=='cut' else 3
    pose={0x460:0,0x480:64,0x600:180,0x400:0,0x440:0,0x420:64,0x640:0,
        0x680:0,0x660:0,0x4A0:0,0x620:0,0x55:255}
    def run(name,coop=False,late=False,extra=()):
        lines=['0 -','0 2:-','180 START','181 -','360 START','361 -','1300 POKE 005D FE',
            '1640 POKE 0721 00','1640 POKE 0728 FF','1640 POKE 008E 00']
        for slot in range(16,32):lines.append(f'1640 POKE {0x600+slot:04X} F8')
        for seat in ((1,2) if coop else (1,)):
            for n in range(count):
                frame=1440+n*12;key='X2:' if seat==2 else 'X1:'
                lines += [f'{frame} {key}R',f'{frame+1} {key}-']
        pokes=[]
        if coop:
            lines += ['1650 2:B','1651 2:-','1664 B','1665 -']
            for seat in (1,2):
                for address,value in pose.items():pokes+=['--coop-test-poke',f'1640:{seat}:{address:X}:{value:X}']
        else:
            fire=1664 if late else 1650;lines += [f'{fire} B',f'{fire+1} -']
            lines += [f'1640 POKE {address:04X} {value:02X}' for address,value in pose.items()]
        route=a.out/(name+'.txt');route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
        args=[str(a.exe),str(a.rom.resolve()),'--frames','1790','--input',str(route),'--protoman','off',
            '--coop','on' if coop else 'off','--charge','off','--slide','off','--weapon-cycle','on',
            '--widescreen','off','--no-save','--config',str(a.out/'test.ini'),*pokes,
            '--coop-report',str(a.out/(name+'.jsonl')),'--hash-out',str(a.out/(name+'.hash')),
            '--present-out',str(a.out/(name+'.png')),'--wav-out',str(a.out/(name+'.wav')),*extra]
        with (a.out/(name+'.log')).open('w') as log:subprocess.run(args,cwd=a.exe.parent,env=env,check=True,
            stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
        return {r['frame']:r for r in map(json.loads,(a.out/(name+'.jsonl')).read_text().splitlines())}
    early=run('solo-early');late=run('solo-late',late=True)
    state=a.out/'both.cycstate';both=run('both',True,extra=('--save-state',f'1684:{state}'))
    assert both[1690]['weapon']==[weapon,weapon] and both[1790]['failures']==0,both[1690]
    differences=[];samples=0
    for seat,reference in ((0,late),(1,early)):
        for frame in range(1651,1791):
            want=reference[frame]['special'][0];got=both[frame]['special'][seat]
            if want['actor'][8]>=248 and got['actor'][8]>=248:continue
            samples+=1
            # Pose, position, fractional speed and native lifetime all come
            # from the original weapon routines, independently per player.
            if want['actor']!=got['actor']:differences.append((seat+1,frame,want,got))
    summary=dict(result='fail' if differences else 'pass',weapon=a.weapon,native_active_samples=samples,
        differing_samples=len(differences),first_differences=differences[:4])
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    assert samples>40 and not differences,summary
    if not a.smoke_only:
        run('interpreter',True,extra=('--interp-only',))
        assert (a.out/'both.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
        assert (a.out/'both.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
        run('resume',True,extra=('--load-state',str(state)))
        tail=[s for s in (a.out/'both.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1684]
        assert tail==(a.out/'resume.hash').read_text().splitlines()
        assert (a.out/'both.png').read_bytes()==(a.out/'resume.png').read_bytes()
        def pcm(name):
            with wave.open(str(a.out/(name+'.wav')),'rb') as f:return f.readframes(f.getnframes())
        continued=pcm('resume');assert continued and pcm('both')[-len(continued):]==continued
        summary.update(native_interpreter_exact=True,current_weapon_state_pcm_exact=True)
        (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(summary))

if __name__=='__main__':main()
