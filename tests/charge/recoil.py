"""A real enemy bullet starts recoil; charging pauses and resumes afterward."""
import argparse,json,os,pathlib,subprocess

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','donor','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    lines=['0 -','180 START','181 -','360 START','361 -']
    for slot in range(16,32):lines.append(f'1400 POKE {0x600+slot:04X} F8')
    for addr,value in ((0x480,112),(0x460,0),(0x600,180),(0x400,0),(0x680,0),(0x660,0)):
        lines.append(f'1400 {"HOLD" if addr in (0x480,0x460) else "POKE"} {addr:04X} {value:02X}')
    lines+=['1400 POKE 0420 40','1400 POKE 0055 00','1400 POKE 006A 1C',
            '1400 POKE 0640 00','1420 B','1540 -']
    actor={0x410:255,0x430:2,0x450:0,0x470:0,0x490:112,0x4B0:0,0x4D0:0,0x4F0:0,
           0x610:180,0x630:0,0x650:127,0x670:0,0x690:0,0x6B0:1,0x6D0:1,0x6F0:0x1A}
    lines.extend(f'1450 POKE {addr:04X} {value:02X}' for addr,value in actor.items())
    route=a.out/'input.txt';route.write_text('\n'.join(lines)+'\n')
    args=[str(a.exe),str(a.rom.resolve()),'--frames','1560','--input',str(route),
          '--mm4-rom',str(a.donor.resolve()),'--charge','on','--protoman','off',
          '--widescreen','off','--no-save','--config',str(a.out/'test.ini'),
          '--charge-report',str(a.out/'hurt.jsonl'),'--frame-log',str(a.out/'hurt.frames'),
          '--frame-log-frames','1459:1459']
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    with (a.out/'hurt.log').open('w') as log:
        subprocess.run(args,cwd=a.exe.parent,env=env,check=True,stdout=log,stderr=subprocess.STDOUT,
                       creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
    rows={r['frame']:r for r in map(json.loads,(a.out/'hurt.jsonl').read_text().splitlines())}
    hurt=[r for r in rows.values() if r['hurt']]
    assert len(hurt)>=8 and len({r['counter'] for r in hurt[2:-2]})==1,hurt
    ram=(a.out/'hurt.frames').read_bytes()[336:2384]
    assert ram[0x6A]==26,ram[0x6A]
    assert rows[1560]['half']==1 and rows[1560]['full']==0,rows[1560]
    summary=dict(result='pass',native_projectile_damage=2,ordinary_recoil_freezes_charge=True,
                 recoil_frames=len(hurt),preserved_counter=hurt[3]['counter'],release_after_recoil_half=True)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
