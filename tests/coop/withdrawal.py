"""Select hold, safe native arrival and personal inventory retention."""
import argparse,json,os,pathlib,subprocess,sys,wave
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'protoman'))
from validate import pixels

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','donor','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--seat',type=int,choices=(1,2),default=2)
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    key='2:' if a.seat==2 else '';xkey=f'X{a.seat}:'
    lines=['0 -','0 2:-','180 START','181 -','360 START','361 -','1300 POKE 005D FE',
        f'1400 {key}SELECT',f'1489 {key}-',f'1490 {xkey}R',f'1491 {xkey}-',
        f'1500 {key}SELECT',f'1600 {key}-',f'1660 {key}SELECT',f'1661 {key}-']
    for slot in range(16,32):lines.append(f'1300 POKE {0x600+slot:04X} F8')
    route=a.out/'input.txt';route.write_text('\n'.join(sorted(lines,key=lambda x:int(x.split()[0])))+'\n')
    pokes=[]
    for seat in (1,2):
        values={0x460:0,0x480:32 if seat==1 else 64,0x600:180,0x400:0,0x440:0,0x420:64,0x640:0,
            0x4A0:0,0x620:0,0x4C0:0,0x4E0:0,0x680:0,0x660:0,0x55:255,0x6A:20 if seat==1 else 24,0x6F:7 if seat==1 else 19}
        for address,value in values.items():pokes+=['--coop-test-poke',f'1300:{seat}:{address:X}:{value:X}']
    base=[str(a.exe),str(a.rom.resolve()),'--input',str(route),'--coop','on','--mm4-rom',str(a.donor.resolve()),
        '--charge','on','--weapon-cycle','on','--widescreen','off','--no-save','--config',str(a.out/'test.ini'),*pokes]
    def run(name,extra=()):
        args=base+['--frames','1900','--coop-report',str(a.out/(name+'.jsonl')),'--hash-out',str(a.out/(name+'.hash')),
            '--present-out',str(a.out/(name+'.png')),'--screenshot',str(a.out/(name+'-native.png')),
            '--wav-out',str(a.out/(name+'.wav')),*extra]
        with (a.out/(name+'.log')).open('w') as log:subprocess.run(args,cwd=a.exe.parent,env=env,check=True,
            stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        return {r['frame']:r for r in map(json.loads,(a.out/(name+'.jsonl')).read_text().splitlines())}
    state=a.out/'arrival.cycstate';rows=run('native',('--save-state',f'1665:{state}','--present-every','1665'))
    seat=a.seat-1;bit=1<<seat
    assert rows[1489]['withdrawals'][seat]==0,rows[1489]
    assert rows[1589]['hold'][seat]==89 and rows[1589]['withdrawals'][seat]==0,rows[1589]
    assert rows[1590]['withdrawals'][seat]==1 and rows[1590]['withdrawn']==bit,rows[1590]
    assert rows[1635]['teleport'][seat]==2 and not rows[1635]['picture'],rows[1635]
    assert rows[1666]['teleport'][seat]==3,rows[1666]
    final=rows[1900];assert final['failures']==0 and final['withdrawals'][seat]==1 and final['rejoins'][seat]==1,final
    assert not final['withdrawn'] and final['teleport']==[0,0] and final['dead']==[0,0],final
    assert final['hp']==[20,24] and final['lives']==2 and [x[4] for x in final['ammo']]==[7,19],final
    assert final['weapon'][seat]==5 and final['wipes']==0 and final['anchor']==(0 if seat==1 else 1),final
    run('interpreter',('--interp-only','--present-every','1665'));assert (a.out/'native.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
    assert (a.out/'native.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
    assert (a.out/'native_01665.png').read_bytes()==(a.out/'interpreter_01665.png').read_bytes()
    run('arrival-still',('--load-state',str(state),'--frames','1666'))
    assert (a.out/'arrival-still.png').read_bytes()==(a.out/'native_01665.png').read_bytes()
    _,_,shown=pixels(a.out/'arrival-still.png');_,_,raw=pixels(a.out/'arrival-still-native.png')
    x,y=rows[1666]['x'][seat],rows[1666]['y'][seat]
    visible=sum(shown[py][px*3:px*3+3]!=raw[py][px*3:px*3+3] for py in range(y-16,y+16) for px in range(x-16,x+16))
    assert visible>3,visible
    run('resume',('--load-state',str(state)))
    tail=[x for x in (a.out/'native.hash').read_text().splitlines() if x[:1].isdigit() and int(x.split()[0])>1665]
    assert tail==(a.out/'resume.hash').read_text().splitlines()
    assert (a.out/'native.png').read_bytes()==(a.out/'resume.png').read_bytes()
    def pcm(name):
        with wave.open(str(a.out/(name+'.wav')),'rb') as f:return f.readframes(f.getnframes())
    continued=pcm('resume');assert continued and pcm('native')[-len(continued):]==continued
    summary=dict(result='pass',seat=a.seat,hold_89_ignored_90_withdraws=True,hidden_and_safe_native_arrival=True,
        native_interpreter_exact=True,mid_arrival_state_pcm_exact=True,arrival_opaque_pixels=visible,own_health_weapon_and_ammo_retained=True)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
