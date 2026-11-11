"""Exercise actual L/R inputs, native inventory order, guards and state replay.

The route grants equipment and protects/positions the player. It never writes
the selected weapon after its initial setup; selection must follow host inputs.
"""
import argparse,json,os,pathlib,subprocess

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--rom',type=pathlib.Path,required=True)
    p.add_argument('--exe',type=pathlib.Path,required=True)
    p.add_argument('--out',type=pathlib.Path,required=True)
    a=p.parse_args();a.exe=a.exe.resolve();a.rom=a.rom.resolve();a.out.mkdir(parents=True,exist_ok=True)
    route=['0 -','180 START','181 -','360 START','361 -','500 X1:R','501 X1:-','800 X1:R','801 X1:-',
           '700 POKE 005D 02','700 POKE 005F 00','700 HOLD 0055 FF','700 HOLD 006A 1C',
           '700 HOLD 0480 80','700 HOLD 0460 00','700 HOLD 0600 94',
           '700 HOLD 0420 40','700 HOLD 0400 00','700 HOLD 0095 00']
    expected={}
    def press(frame,buttons,weapon,changes,release=1):
        route.append(f'{frame} X1:{buttons}');route.append(f'{frame+release} X1:-')
        expected[frame+4]=(weapon,changes)
    press(710,'R',5,1);press(720,'R',0,2)
    route.append('730 POKE 005D FE')
    for i,weapon in enumerate((5,4,1,6,2,3,7,0)):press(730+i*10,'R',weapon,3+i)
    route.append('815 POKE 0071 00')
    press(820,'L',7,11);press(830,'L+R',7,11)
    press(840,'R',0,12,release=10);expected[849]=(0,12)
    route+=['860 HOLD 0420 43','860 X1:L','870 UNHOLD 0420','870 POKE 0420 40']
    expected[865]=(0,12);expected[875]=(0,12)
    route.append('880 X1:-');press(890,'R',5,13)
    route+=['920 UNHOLD 006A','920 HOLD 006A 00','920 X1:R','930 X1:-']
    expected[925]=(5,13)
    # Finish the original READY/arrival sequence before the isolated controls
    # fixture. These cheats do not skip boot, stage entry or its native script.
    offset=700
    route=[f'{int(line.split()[0])+offset} '+line.split(' ',1)[1]
           if i>=9 else line for i,line in enumerate(route)]
    expected={frame+offset:want for frame,want in expected.items()}
    expected[1100]=(0,0)
    path=a.out/'input.txt';path.write_text('\n'.join(route)+'\n')
    base=[str(a.exe),str(a.rom),'--frames',str(940+offset),'--input',str(path),'--widescreen','off',
          '--weapon-cycle','on','--no-save','--config',str(a.out/'test.ini')]
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    flags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0
    def run(name,extra):
        with (a.out/(name+'.log')).open('w') as log:
            result=subprocess.run(base+extra,cwd=a.exe.parent,stdout=log,stderr=subprocess.STDOUT,
                                  env=env,creationflags=flags)
        assert result.returncode==0,(name,(a.out/(name+'.log')).read_text())
    state=a.out/'held.cycstate'
    run('native',['--hash-out',str(a.out/'native.hash'),'--weapon-report',str(a.out/'weapons.jsonl'),
                  '--save-state',f'{845+offset}:{state}','--screenshot',str(a.out/'native.png')])
    rows={r['frame']:r for r in map(json.loads,(a.out/'weapons.jsonl').read_text().splitlines())}
    assert rows[850]['pending']==0,rows[850]
    for frame,want in expected.items():
        got=rows[frame]
        assert (got['weapon'],got['changes'])==want,(frame,want,got)
    run('interpreter',['--interp-only','--hash-out',str(a.out/'interpreter.hash')])
    assert (a.out/'native.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
    run('resume',['--load-state',str(state),'--hash-out',str(a.out/'resume.hash')])
    tail=[line for line in (a.out/'native.hash').read_text().splitlines()
          if line[:1].isdigit() and int(line.split()[0])>845+offset]
    assert (a.out/'resume.hash').read_text().splitlines()==tail
    summary=dict(result='pass',native_interpreter_exact=True,cold_continuation_frames=len(tail),
                 acquired_only=True,wrap=True,empty_weapon=True,simultaneous_ignored=True,
                 hold_once=True,hurt_press_discarded=True,zero_hp_press_discarded=True,
                 native_ready_press_discarded=True,
                 inventory_cycle=[5,4,1,6,2,3,7,0])
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
