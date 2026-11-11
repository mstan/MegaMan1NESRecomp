"""Native half/full damage, lethal pass-through and boss result paths."""
import argparse,json,os,pathlib,struct,subprocess

def ram(path):
    data=path.read_bytes();assert data[:12]==b'CYCFRAME\x02\0\0\0'
    return data[336:2384]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','donor','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    a=p.parse_args();a.exe=a.exe.resolve();a.rom=a.rom.resolve();a.donor=a.donor.resolve();a.out.mkdir(parents=True,exist_ok=True)
    def route(full,targets,boss=0):
        release=1610 if full else 1460
        lines=['0 -','180 START','181 -','360 START','361 -']
        for slot in range(16,32):lines.append(f'1400 POKE {0x600+slot:04X} F8')
        for addr,value in ((0x480,112),(0x460,0),(0x600,160),(0x420,64),(0x400,9),(0x680,0),(0x660,0),(0x55,0x81),(0x6A,28)):
            lines.append(f'1400 HOLD {addr:04X} {value:02X}')
        lines+=['1400 POKE 0640 00',f'{1500 if full else 1420} B',f'{release} -']
        for slot,x,hp in targets:
            values={0x400:0 if slot==1 else 255,0x420:4 if slot==1 else 6,0x440:0,0x460:0,
                0x480:x,0x4A0:0,0x4C0:0,0x4E0:0,0x600:159,0x620:0,0x640:127,0x660:0,0x680:0,
                0x6A0:1,0x6C0:hp,0x6E0:0 if slot==1 else 0x21}
            lines.extend(f'{release} POKE {addr+slot:04X} {value:02X}' for addr,value in values.items())
        if boss:
            lines += [f'{release} POKE 003E 05',f'{release} POKE 00AC 00',f'{release} POKE 0056 00']
            for addr,value in ((0x481,176),(0x461,0),(0x601,159),(0x421,4)):
                lines += [f'{release} HOLD {addr:04X} {value:02X}',f'{release+25} UNHOLD {addr:04X}']
            lines.append(f'{release+30} HOLD 0480 40')
        return lines
    def run(name,lines,frames=1660,extra=()):
        path=a.out/(name+'.txt');path.write_text('\n'.join(lines)+'\n')
        args=[str(a.exe),str(a.rom),'--frames',str(frames),'--input',str(path),'--mm4-rom',str(a.donor),
            '--charge','on','--protoman','off','--widescreen','off','--no-save','--config',str(a.out/(name+'.ini')),
            '--charge-report',str(a.out/(name+'.jsonl')),'--hash-out',str(a.out/(name+'.hash')),
            '--frame-log',str(a.out/(name+'.frames')),'--frame-log-frames',f'{frames-1}:{frames-1}',*extra]
        env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1',PYTHONDONTWRITEBYTECODE='1')
        with (a.out/(name+'.log')).open('w') as log:
            result=subprocess.run(args,cwd=a.exe.parent,stdout=log,stderr=subprocess.STDOUT,env=env,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        assert result.returncode==0,(name,(a.out/(name+'.log')).read_text())
        rows={r['frame']:r for r in map(json.loads,(a.out/(name+'.jsonl')).read_text().splitlines())}
        return ram(a.out/(name+'.frames')),rows[frames]
    half,_=run('half-enemy',route(False,[(18,176,8)]),1500)
    assert half[0x6D2]==4 and half[0x6F2]==0x21,(half[0x6D2],half[0x6F2])
    full,_=run('full-enemy',route(True,[(18,176,20)]))
    assert full[0x6D2]==8 and full[0x6F2]==0x21,(full[0x6D2],full[0x6F2])
    normal_after,_=run('normal-after-full',route(True,[(18,176,20)])+['1700 B','1701 -'],1760)
    assert normal_after[0x6D2]==4 and normal_after[0x6F2]==0x21,(normal_after[0x6D2],normal_after[0x6F2])
    chain=route(True,[(18,176,8),(20,208,20)])
    passed,stats=run('pass-through',chain)
    assert passed[0x6F2]!=0x21 and passed[0x6D4]==8,(passed[0x6F2],passed[0x6D4])
    assert stats['passes']>=1 and stats['kinds']==[0,0,0],stats
    interpreted,_=run('pass-interpreter',chain,extra=('--interp-only',))
    assert interpreted==passed
    assert (a.out/'pass-through.hash').read_bytes()==(a.out/'pass-interpreter.hash').read_bytes()
    boss,_=run('full-boss',route(True,[(1,176,20)],1))
    assert boss[0x6C1]==11,boss[0x6C1]
    lethal,_=run('lethal-boss-proto',route(True,[(1,176,9)],1),2200,('--protoman','on'))
    assert lethal[0x6C1]==0 and lethal[0x601]==248 and lethal[0x3E]==0
    assert lethal[0x6FF]==0x44 and lethal[0x61F]<248,(lethal[0x6FF],lethal[0x61F])
    summary=dict(result='pass',half_enemy_hp=[8,4],full_enemy_hp=[20,8],full_boss_hp=[20,11],
        lethal_pass_through=True,normal_damage_restored=True,native_interpreter_exact=True,protoman_native_boss_defeat=True)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
