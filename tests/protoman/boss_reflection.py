"""Qualify reflected Buster identity and the real native boss defeat script."""
import argparse,json,os,pathlib,subprocess
from traits import frame_ram

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    a=p.parse_args();a.rom=a.rom.resolve();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    def route(hp):
        lines=['0 -','180 START','181 -','360 START','361 -']
        for slot in range(16,32):lines.append(f'1400 POKE {0x600+slot:04X} F8')
        for addr,value in ((0x480,112),(0x460,0),(0x600,160),(0x420,64),(0x400,9),(0x640,0),(0x680,0),(0x660,0)):
            lines.append(f'1400 HOLD {addr:04X} {value:02X}')
        lines+=['1400 POKE 006A 1C','1400 POKE 0055 00','1400 POKE 005F 05','1400 POKE 0060 00',
                '1400 POKE 003E 05','1400 POKE 00AC 00','1400 POKE 0056 00']
        for slot,x,y,sprite,flags,kind in ((1,160,152,0,4,0),(16,120,152,255,2,0x1A)):
            values={0x400:sprite,0x420:flags,0x440:0,0x460:0,0x480:x,0x4A0:0,0x4C0:0,0x4E0:0,
                    0x600:y,0x620:0,0x640:0x7F,0x660:0,0x680:0,0x6A0:1,0x6C0:hp if slot==1 else 1,0x6E0:kind}
            lines.extend(f'1400 POKE {addr+slot:04X} {value:02X}' for addr,value in values.items())
        # Hold only the setup location, then let the actual boss script own it.
        for addr,value in ((0x481,160),(0x461,0),(0x601,152),(0x421,4)):
            lines+=[f'1400 HOLD {addr:04X} {value:02X}',f'1415 UNHOLD {addr:04X}']
        lines+=['1430 HOLD 0480 40'] # keep away from the naturally spawned reward
        return lines
    def run(name,hp,frames=2000,extra=()):
        path=a.out/(name+'.txt');path.write_text('\n'.join(route(hp))+'\n')
        args=[str(a.exe),str(a.rom),'--frames',str(frames),'--input',str(path),'--protoman','on',
              '--widescreen','off','--weapon-cycle','on','--no-save','--config',str(a.out/(name+'.ini')),
              '--proto-report',str(a.out/(name+'.jsonl')),'--frame-log',str(a.out/(name+'.frames')),
              '--frame-log-frames',f'{frames-1}:{frames-1}','--hash-out',str(a.out/(name+'.hash')),*extra]
        env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
        with (a.out/(name+'.log')).open('w') as log:
            result=subprocess.run(args,cwd=a.exe.parent,stdout=log,stderr=subprocess.STDOUT,env=env,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        assert result.returncode==0,(name,(a.out/(name+'.log')).read_text())
        rows={r['frame']:r for r in map(json.loads,(a.out/(name+'.jsonl')).read_text().splitlines())}
        return rows,frame_ram(a.out/(name+'.frames'))
    ordinary,ram=run('nonlethal',4,1440)
    # USA's actual SBC at C90F addresses FDEE; Cut Man's Buster damage is03.
    assert ordinary[1440]['blocks']==1 and ram[0x6C1]==1,(ordinary[1440],ram[0x6C1])
    assert ram[0x5F]==5 and not ordinary[1440]['continuation']
    state=a.out/'native-defeat.cycstate'
    lethal,ram=run('lethal',3,extra=('--save-state',f'1420:{state}'))
    assert lethal[2000]['boss_kills']==1,lethal[2000]
    assert ram[0x6C1]==0 and ram[0x601]==248 and ram[0x3E]==0,(ram[0x6C1],ram[0x601],ram[0x3E])
    assert ram[0x6FF]==0x44 and ram[0x61F]<248,(ram[0x6FF],ram[0x61F])
    assert ram[0x5F]==5 and not lethal[2000]['continuation']
    _,ram_i=run('lethal-interpreter',3,extra=('--interp-only',))
    assert ram==ram_i
    assert (a.out/'lethal.hash').read_bytes()==(a.out/'lethal-interpreter.hash').read_bytes()
    _,ram_r=run('defeat-resume',3,extra=('--load-state',str(state)))
    assert ram==ram_r
    tail=[s for s in (a.out/'lethal.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1420]
    assert (a.out/'defeat-resume.hash').read_text().splitlines()==tail
    summary=dict(result='pass',nonlethal_buster_damage=[4,1],lethal_boss=True,native_reward_spawn=True,
        original_weapon_restored=5,native_interpreter_exact=True,native_defeat_cold_frames=len(tail))
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
