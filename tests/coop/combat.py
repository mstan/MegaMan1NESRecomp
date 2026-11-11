"""Exercise the guest's actual native contact and projectile result paths."""
import argparse,json,os,pathlib,subprocess

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','donor','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--reflection-only',action='store_true')
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    def run(name,kind='normal',hp=8,boss=False,roster='megaman',extra=()):
        release=1610 if kind=='full' else 1500
        lethal=boss and ((hp<=9 and kind=='full') or (hp<=3 and kind=='reflect'))
        frames=2200 if lethal else release+65
        lines=['0 -','0 2:-','180 START','181 -','360 START','361 -','1400 POKE 0055 81','1400 POKE 006A 1C']
        for slot in range(16,32):lines.append(f'1490 POKE {0x600+slot:04X} F8')
        values={0x480:104,0x460:0,0x600:180,0x420:64,0x400:0,0x440:0,0x640:0,
            0x680:0,0x660:0,0x4A0:0,0x620:0,0x55:0x81,0x5F:0}
        if kind=='reflect':values.update({0x600:160,0x400:9,0x55:0,0x5F:5})
        pokes=[]
        for address,value in values.items():pokes+=['--coop-test-poke',f'{1499 if kind=="reflect" else 1490}:2:{address:X}:{value:X}']
        if kind!='reflect':lines += [f'{1505 if kind=="full" else 1500} 2:B',f'{release+1 if kind=="normal" else release} 2:-']
        slot=1 if boss else 18
        target={0x400:0 if boss else 255,0x420:4 if boss else 6,0x440:0,0x460:0,0x480:176,0x4A0:0,
            0x4C0:0,0x4E0:0,0x600:176,0x620:0,0x640:127,0x660:0,0x680:0,
            0x6A0:1,0x6C0:hp,0x6E0:0 if boss else 0x21}
        if kind=='reflect':target.update({0x480:152,0x600:152})
        for address,value in target.items():lines.append(f'{release} POKE {address+slot:04X} {value:02X}')
        if kind=='reflect':
            for address,value in {**target,0x420:2,0x480:112,0x6E0:0x1A,0x6C0:1}.items():
                lines.append(f'{release} POKE {address+16:04X} {value:02X}')
            if lethal:lines+=['1800 2:LEFT','1820 2:-','1850 A','1860 -']
        if boss:
            lines += [f'{release} POKE 003E 05',f'{release} POKE 00AC 00',f'{release} POKE 0056 00']
            for address,value in ((0x481,target[0x480]),(0x461,0),(0x601,target[0x600]),(0x421,4)):
                lines += [f'{release} HOLD {address:04X} {value:02X}',f'{release+30} UNHOLD {address:04X}']
        route=a.out/(name+'.txt');route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
        args=[str(a.exe),str(a.rom.resolve()),'--frames',str(frames),'--input',str(route),'--coop','on',
            '--coop-p1',roster,'--mm4-rom',str(a.donor.resolve()),'--charge','on' if kind=='full' else 'off',
            '--widescreen','off','--no-save','--config',str(a.out/'test.ini'),*pokes,
            '--coop-report',str(a.out/(name+'.jsonl')),'--charge-report',str(a.out/(name+'-charge.jsonl')),
            '--proto-report',str(a.out/(name+'-proto.jsonl')),
            '--frame-log',str(a.out/(name+'.frames')),'--frame-log-frames',f'{frames-1}:{frames-1}',
            '--hash-out',str(a.out/(name+'.hash')),'--present-out',str(a.out/(name+'.png')),*extra]
        with (a.out/(name+'.log')).open('w') as log:
            subprocess.run(args,cwd=a.exe.parent,env=env,check=True,stdout=log,stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        rows={r['frame']:r for r in map(json.loads,(a.out/(name+'.jsonl')).read_text().splitlines())}
        ram=(a.out/(name+'.frames')).read_bytes()[336:2384]
        assert rows[frames]['failures']==0,rows[frames]
        if kind=='reflect':
            proto=list(map(json.loads,(a.out/(name+'-proto.jsonl')).read_text().splitlines()))[-1]
            assert proto['blocks']==1 and proto['passes']>0,proto
            assert rows[frames]['weapon']==[0,5],rows[frames]
            if lethal:
                assert rows[1820]['x'][1]<rows[1799]['x'][1]-12,(rows[1799],rows[1820])
                assert min(r['y'][0] for f,r in rows.items() if 1850<=f<=1870)<rows[1849]['y'][0]-5
        return rows[frames],ram
    if a.reflection_only:
        row,ram=run('reflected-enemy',kind='reflect')
        assert ram[0x6D2]==4 and ram[0x6F2]==0x21,(row,ram[0x6D2],ram[0x6F2])
        row,ram=run('reflected-boss',kind='reflect',hp=3,boss=True)
        assert row['anchor']==1 and ram[0x6C1]==0 and ram[0x601]==248 and ram[0x3E]==0
        assert ram[0x6FF]==0x44 and ram[0x61F]<248
        run('reflected-boss-interpreter',kind='reflect',hp=3,boss=True,extra=('--interp-only',))
        assert (a.out/'reflected-boss.hash').read_bytes()==(a.out/'reflected-boss-interpreter.hash').read_bytes()
        assert (a.out/'reflected-boss.png').read_bytes()==(a.out/'reflected-boss-interpreter.png').read_bytes()
        summary=dict(result='pass',guest_reflected_enemy_buster_damage=True,guest_reflected_native_boss_defeat=True,
            special_weapon_restored=5,both_controllers_after_handoff=True,native_interpreter_exact=True)
        (a.out/'reflection-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary));return
    for roster in ('megaman','protoman'):
        row,ram=run('normal-'+roster,roster=roster)
        assert ram[0x6D2]==4 and ram[0x6F2]==0x21,(row,ram[0x6D2],ram[0x6F2])
        row,ram=run('full-'+roster,kind='full',hp=20,roster=roster)
        assert ram[0x6D2]==8 and ram[0x6F2]==0x21,(row,ram[0x6D2],ram[0x6F2])
    row,ram=run('boss',kind='full',hp=20,boss=True)
    assert ram[0x6C1]==11,(row,ram[0x6C1])
    row,ram=run('boss-lethal',kind='full',hp=9,boss=True)
    assert row['anchor']==1 and ram[0x6C1]==0 and ram[0x601]==248 and ram[0x3E]==0,(row,ram[0x6C1],ram[0x601],ram[0x3E])
    assert ram[0x6FF]==0x44 and ram[0x61F]<248,(ram[0x6FF],ram[0x61F])
    run('boss-lethal-interpreter',kind='full',hp=9,boss=True,extra=('--interp-only',))
    assert (a.out/'boss-lethal.hash').read_bytes()==(a.out/'boss-lethal-interpreter.hash').read_bytes()
    assert (a.out/'boss-lethal.png').read_bytes()==(a.out/'boss-lethal-interpreter.png').read_bytes()
    summary=dict(result='pass',both_guest_characters_native_enemy_damage=True,full_enemy_hp=[20,8],
        native_boss_hp=[20,11],native_boss_defeat_and_reward=True,finisher_native_handoff=True,native_interpreter_exact=True)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
