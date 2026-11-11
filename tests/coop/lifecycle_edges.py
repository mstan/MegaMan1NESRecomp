"""Unavailable teammates, unsafe arrival and positional checkpoint revival.

Checkpoint setup teleports the survivor into the authored checkpoint page;
it bypasses traversal, never writes the checkpoint or revival result.
"""
import argparse,json,os,pathlib,subprocess,wave
import sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'protoman'))
from validate import pixels

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','donor','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--case',choices=('roof','last-hidden','last-arriving','both-select','checkpoint','checkpoint-retry'),required=True)
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    lines=['0 -','0 2:-','180 START','181 -','360 START','361 -','1300 POKE 005D FE']
    for slot in range(16,32):lines.append(f'1300 POKE {0x600+slot:04X} F8')
    pokes=[]
    def personal(frame,seat,values):
        for address,value in values.items():pokes.extend(('--coop-test-poke',f'{frame}:{seat}:{address:X}:{value:X}'))
    for seat in (1,2):
        personal(1300,seat,{0x460:0,0x480:32 if seat==1 else 64,0x600:180,0x400:0,0x420:64,
            0x55:255,0x6A:2 if seat==1 else 24,0x6F:7 if seat==1 else 19})
    def bullet(frame,seat=1,x=32,screen=0,lethal_hp=0):
        personal(frame-1,seat,{0x55:0,**({0x6A:lethal_hp} if lethal_hp else {})})
        values={0x400:255,0x420:2,0x440:0,0x460:screen,0x480:x,0x4A0:0,0x4C0:0,0x4E0:0,
            0x600:180,0x620:0,0x640:127,0x660:0,0x680:0,0x6A0:1,0x6C0:1,0x6E0:0x1A}
        for address,value in values.items():lines.append(f'{frame} POKE {address+18:04X} {value:02X}')
        lines.append(f'{frame+10} POKE 0612 F8')
    frames=1950;snapshot=1665
    if a.case.startswith('checkpoint'):
        bullet(1499);lines+=['1510 SELECT','1511 -','1700 POKE 0027 0A','1700 POKE 0028 0A',
            '1700 POKE 001A 00','1700 POKE 001B 0A']
        personal(1700,2,{0x460:10,0x480:104,0x600:180,0x400:0,0x420:64,0x680:0,0x660:0,0x55:255})
        snapshot=1705
        if a.case=='checkpoint-retry':
            bullet(1800,x=80,screen=10,lethal_hp=2);bullet(1820,seat=2,x=104,screen=10,lethal_hp=4)
            frames=2700;snapshot=2000
    else:
        lines+=['1490 X2:R','1491 X2:-','1500 2:SELECT','1600 2:-']
        if a.case=='both-select':lines+=['1500 SELECT','1600 -','1680 2:SELECT','1800 2:-']
        elif a.case=='last-hidden':bullet(1640);frames=2450;snapshot=1800
        else:
            lines+=['1660 2:SELECT','1661 2:-']
            if a.case=='last-arriving':bullet(1665)
            else:
                values={0x721:1,0x722:0,0x723:0,0x724:160,0x725:120,0x726:178,0x727:0,0x728:255,0x68:0,0x8E:0}
                lines += [f'1650 POKE {k:04X} {v:02X}' for k,v in values.items()]
                lines+=['1680 POKE 0721 00','1690 2:SELECT','1691 2:-'];snapshot=1695
    route=a.out/'input.txt';route.write_text('\n'.join(sorted(lines,key=lambda x:int(x.split()[0])))+'\n')
    base=[str(a.exe),str(a.rom.resolve()),'--input',str(route),'--coop','on','--mm4-rom',str(a.donor.resolve()),
        '--charge','on','--weapon-cycle','on','--widescreen','fit','--present-size','1280x720','--widescreen-enemies','classic','--no-save','--config',str(a.out/'test.ini'),*pokes]
    def run(name,extra=()):
        args=base+['--frames',str(frames),'--coop-report',str(a.out/(name+'.jsonl')),'--hash-out',str(a.out/(name+'.hash')),
            '--present-out',str(a.out/(name+'.png')),'--wav-out',str(a.out/(name+'.wav')),*extra]
        with (a.out/(name+'.log')).open('w') as log:subprocess.run(args,cwd=a.exe.parent,env=env,check=True,
            stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        return {r['frame']:r for r in map(json.loads,(a.out/(name+'.jsonl')).read_text().splitlines())}
    state=a.out/'lifecycle.cycstate';rows=run('native',('--save-state',f'{snapshot}:{state}','--present-every',str(snapshot)))
    final=rows[frames];assert final['failures']==0,final
    width,height,_=pixels(a.out/'native.png');assert (width,height)==(426,240),(width,height)
    if a.case=='last-hidden':
        assert final['wipes']==1 and final['lives']==1 and final['hp']==[28,28] and final['retries']==1,final
    elif a.case=='both-select':
        assert final['withdrawn']==1 and final['withdrawals']==[1,0] and final['anchor']==1 and final['wipes']==0,final
    elif a.case.startswith('checkpoint'):
        assert rows[1600]['dead']==[1,0] and rows[1600]['revivals']==[0,0],rows[1600]
        assert rows[1790]['checkpoint']==1 and rows[1790]['dead']==[0,0] and rows[1790]['revivals']==[1,0] and rows[1790]['hp']==[28,24],rows[1790]
        assert [x[4] for x in final['ammo']]==[7,19],final
        if a.case=='checkpoint-retry':
            first=next(row for row in rows.values() if row['frame']>1820 and row['ready'] and row['retries']==1)
            assert first['hp']==[28,28] and first['dead']==[0,0] and all(x//256==10 for x in first['x']),first
            assert final['lives']==1 and final['retries']==1 and all(x//256==10 for x in final['x']),final
        else:assert final['lives']==2 and final['retries']==0,final
    else:
        if a.case=='roof':assert rows[1670]['teleport'][1]==2 and rows[1670]['rejoins'][1]==0,rows[1670]
        else:assert rows[1670]['dead']==[1,0] and rows[1670]['teleport'][1] in (3,4),rows[1670]
        assert final['rejoins'][1]==1 and final['hp'][1]==24 and final['weapon'][1]==3 and final['lives']==2 and final['wipes']==0,final
        if a.case=='last-arriving':assert final['anchor']==1 and final['dead']==[1,0],final
        if a.case=='last-arriving':
            width,height,image=pixels(a.out/f'native_{snapshot:05d}.png')
            colors={image[y][x*3:x*3+3] for y in range(240) for x in range(85)}
            assert width==426 and len(colors)>2,(width,len(colors))
    run('interpreter',('--interp-only',));assert (a.out/'native.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
    assert (a.out/'native.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
    run('resume',('--load-state',str(state)))
    tail=[x for x in (a.out/'native.hash').read_text().splitlines() if x[:1].isdigit() and int(x.split()[0])>snapshot]
    assert tail==(a.out/'resume.hash').read_text().splitlines()
    assert (a.out/'native.png').read_bytes()==(a.out/'resume.png').read_bytes()
    def pcm(name):
        with wave.open(str(a.out/(name+'.wav')),'rb') as f:return f.readframes(f.getnframes())
    continued=pcm('resume');assert continued and pcm('native')[-len(continued):]==continued
    summary=dict(result='pass',case=a.case,native_interpreter_exact=True,current_state_pcm_exact=True,
        checkpoint_traversal_bypassed=a.case.startswith('checkpoint'))
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
