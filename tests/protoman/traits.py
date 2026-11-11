"""Native damage, shot admission and real shield collision fixtures."""
import argparse,json,os,pathlib,struct,subprocess

def frame_ram(path):
    data=path.read_bytes();assert data[:12]==b'CYCFRAME\x02\0\0\0'
    head=struct.unpack_from('<9I',data,12);assert head[3]==2048
    return data[12+36+288:12+36+288+2048]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--reflection-only',action='store_true')
    p.add_argument('--core-only',action='store_true')
    a=p.parse_args();a.rom=a.rom.resolve();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    boot=['0 -','180 START','181 -','360 START','361 -']
    def fixture(air=False):
        lines=boot.copy()
        for slot in range(16,32):lines.append(f'1400 POKE {0x600+slot:04X} F8')
        for addr,value in ((0x480,112),(0x460,0),(0x600,160),(0x420,64),(0x400,9 if air else 0),
                           (0x640,0),(0x680,0),(0x660,0)):
            lines.append(f'1400 HOLD {addr:04X} {value:02X}')
        lines+=['1400 POKE 006A 1C','1400 POKE 0055 00','1400 POKE 005F 00','1400 POKE 0060 00']
        return lines
    def actor(lines,slot,x,y,kind=0x1A,flags=2,hp=1):
        values={0x400:255,0x420:flags,0x440:0,0x460:0,0x480:x,0x4A0:0,0x4C0:0,0x4E0:0,
                0x600:y,0x620:0,0x640:0x7F,0x660:0,0x680:0,0x6A0:1,0x6C0:hp,0x6E0:kind}
        lines.extend(f'1400 POKE {addr+slot:04X} {value:02X}' for addr,value in values.items())
    def run(name,lines,mode='on',extra=()):
        route=a.out/(name+'.txt');route.write_text('\n'.join(lines)+'\n')
        args=[str(a.exe),str(a.rom),'--frames','1440','--input',str(route),'--protoman',mode,
              '--widescreen','off','--weapon-cycle','on','--no-save','--config',str(a.out/(name+'.ini')),
              '--proto-report',str(a.out/(name+'.jsonl')),'--frame-log',str(a.out/(name+'.frames')),
              '--frame-log-frames','1439:1439','--hash-out',str(a.out/(name+'.hash')),*extra]
        env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
        with (a.out/(name+'.log')).open('w') as log:
            result=subprocess.run(args,cwd=a.exe.parent,stdout=log,stderr=subprocess.STDOUT,env=env,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        assert result.returncode==0,(name,(a.out/(name+'.log')).read_text())
        rows={r['frame']:r for r in map(json.loads,(a.out/(name+'.jsonl')).read_text().splitlines())}
        return rows,frame_ram(a.out/(name+'.frames'))
    if not a.reflection_only:
        damage=fixture();damage.append('1400 HOLD 0640 1F');actor(damage,16,112,160)
        real,ram=run('damage-proto',damage);skin,_=run('damage-reskin',damage,'reskin')
        assert real[1440]['hp']==24 and real[1440]['damage_hits']==1,real[1440]
        assert skin[1440]['hp']==26 and skin[1440]['damage_hits']==0,skin[1440]
        firing=fixture()
        for slot,x in ((2,64),(3,96)):
            for addr,value in ((0x400,0x1A),(0x420,64),(0x460,0),(0x480,x),(0x600,100),(0x6A0,1),(0x4C0,0),(0x4E0,0)):
                firing.append(f'1400 HOLD {addr+slot:04X} {value:02X}')
        firing+=['1405 B','1406 -']
        limited,_=run('two-shot',firing);normal,_=run('three-shot-reskin',firing,'reskin')
        assert limited[1410]['limited']>=1 and limited[1410]['shots']==3,limited[1410]
        assert normal[1410]['limited']==0 and normal[1410]['shots']==7,normal[1410]
        front=fixture(True);actor(front,16,120,152)
        guarded,_=run('front-shield',front)
        assert guarded[1440]['blocks']==1 and guarded[1440]['reflections']==1 and guarded[1440]['hp']==28,guarded[1440]
        assert guarded[1440]['passes']>0,guarded[1440]
        rear=fixture(True);actor(rear,16,104,152)
        unguarded,_=run('rear-unprotected',rear)
        assert unguarded[1440]['blocks']==0 and unguarded[1440]['hp']==24,unguarded[1440]
    if a.core_only:
        summary=dict(result='pass',double_damage=[26,24],two_vs_three_shots=True,front_only_shield=True)
        (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary));return
    enemy=fixture(True);actor(enemy,16,120,152);actor(enemy,18,160,152,0x21,6,8)
    target,ram=run('reflected-enemy',enemy)
    assert target[1440]['blocks']==1 and target[1440]['passes']>0,target[1440]
    # Native fixed-bank Buster table FC51 + Suzy type21 = FC72, value04.
    assert ram[0x6D2]==4 and ram[0x6F2]==0x21,(ram[0x6D2],ram[0x6F2],target[1440])
    target_i,ram_i=run('reflected-enemy-interpreter',enemy,extra=('--interp-only',))
    assert ram==ram_i
    assert (a.out/'reflected-enemy.hash').read_bytes()==(a.out/'reflected-enemy-interpreter.hash').read_bytes()
    special=enemy+['1400 POKE 005F 05']
    _,ram_s=run('reflected-enemy-special',special)
    assert ram_s[0x5F]==5 and ram_s[0x6D2]==4 and ram_s[0x6F2]==0x21
    lethal=fixture(True);actor(lethal,16,120,152);actor(lethal,18,160,152,0x21,6,4)
    _,ram_l=run('reflected-enemy-lethal',lethal)
    assert ram_l[0x6F2]==0x1B,ram_l[0x6F2]
    summary=dict(result='pass',core_traits_checked=not a.reflection_only,
        reflected_native_enemy_hp=[8,4],lethal_native_enemy=True,special_selection_buster_identity=True,
        native_interpreter_reflection_exact=True)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
