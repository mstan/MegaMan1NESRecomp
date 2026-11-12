"""Ride a partner's emitted Magnet Beam, then fall when it expires."""
import argparse,json,os,pathlib,subprocess,wave

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--producer',type=int,choices=(1,2),default=1)
    p.add_argument('--roster',choices=('megaman','protoman'),default='megaman')
    p.add_argument('--fit',action='store_true')
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    rider=3-a.producer;key='2:' if a.producer==2 else '';rkey='2:' if rider==2 else ''
    lines=['0 -','0 2:-','180 START','181 -','360 START','361 -','1380 POKE 005D FE',f'1500 {key}B',f'1508 {key}-',
        f'1570 {rkey}A',f'1571 {rkey}-']
    for frame in range(1400,1473,12):lines += [f'{frame} X{a.producer}:R',f'{frame+1} X{a.producer}:-']
    pose={0x460:0,0x480:96,0x600:168,0x400:9,0x440:0,0x420:64,0x640:0,0x680:0,0x660:0,0x4A0:0,0x620:0,0x55:255}
    # Wider enemy activation must not interrupt the setup's quick-switch edges.
    pokes=['--coop-test-poke','1380:1:55:FF','--coop-test-poke','1380:2:55:FF']
    for seat,frame,values in ((a.producer,1498,pose),(rider,1512,{**pose,0x480:104,0x600:130,0x680:254})):
        for address,value in values.items():pokes+=['--coop-test-poke',f'{frame}:{seat}:{address:X}:{value:X}']
    route=a.out/'input.txt';route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    base=[str(a.exe),str(a.rom.resolve()),'--frames','1560','--input',str(route),'--protoman','off','--coop','on',
        '--coop-p1',a.roster,'--charge','off','--slide','off','--weapon-cycle','on',
        '--widescreen','fit' if a.fit else 'off','--present-size','1280x720',
        '--no-save','--config',str(a.out/'test.ini'),*pokes]
    def run(name,extra=()):
        with (a.out/(name+'.log')).open('w') as log:subprocess.run(base+[
            '--coop-report',str(a.out/(name+'.jsonl')),'--hash-out',str(a.out/(name+'.hash')),
            '--frame-log',str(a.out/(name+'.frames')),'--frame-log-frames','1559:1559',
            '--present-out',str(a.out/(name+'.png')),'--wav-out',str(a.out/(name+'.wav')),*extra],
            cwd=a.exe.parent,env=env,check=True,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
        return {r['frame']:r for r in map(json.loads,(a.out/(name+'.jsonl')).read_text().splitlines())}
    state=a.out/'riding.cycstate';rows=run('native',('--save-state',f'1549:{state}'))
    m=(a.out/'native.frames').read_bytes()[336:2384]
    active=[slot for slot in range(5,16) if m[0x5B0+slot]]
    assert rows[1560]['weapon'][a.producer-1]==7 and rows[1560]['weapon'][rider-1]==0 and rows[1560]['energy'][a.producer-1]<28,rows[1560]
    assert rows[1560]['failures']==0 and rows[1560]['phase']==0,rows[1560]
    # The authored setup emits the native beam at Y168; its original 9691
    # landing result is 168-16=152. It does not alter any beam/result fields.
    if a.producer==1:assert active and {m[0x5F0+slot]-16 for slot in active}=={152},active
    assert rows[1560]['y'][rider-1]==152,('Partner fell through actual beam',rows[1560])
    run('interpreter',('--interp-only',));assert (a.out/'native.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
    assert (a.out/'native.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
    run('resume',('--load-state',str(state)))
    tail=[s for s in (a.out/'native.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1549]
    assert tail==(a.out/'resume.hash').read_text().splitlines();assert (a.out/'native.png').read_bytes()==(a.out/'resume.png').read_bytes()
    def pcm(name):
        with wave.open(str(a.out/(name+'.wav')),'rb') as f:return f.readframes(f.getnframes())
    continued=pcm('resume');assert continued and pcm('native')[-len(continued):]==continued
    expiry=run('expired',('--frames','1700','--frame-log-frames','1699:1699'))
    m=(a.out/'expired.frames').read_bytes()[336:2384]
    if a.producer==1:assert not any(m[0x5B5:0x5C0])
    assert min(expiry[f]['y'][rider-1] for f in range(1572,1590))<148,'Native short jump did not leave the beam'
    assert expiry[1700]['y'][rider-1]==180,expiry[1700]
    summary=dict(result='pass',actual_partner_magnet_landing=True,native_expiry_releases_rider=True,
        producer=a.producer,roster=a.roster,fit=a.fit,native_jump_leaves_beam=True,
        rider_weapon_unchanged=True,native_interpreter_exact=True,current_state_hash_pixels_pcm_exact=True)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
