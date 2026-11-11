"""Native personal hurt/refill ownership and shared pickups."""
import argparse,json,os,pathlib,subprocess,wave

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    def run(name,kind,roster='megaman',extra=()):
        # Global HOLD cheats also run across a real NMI while guest RAM is
        # projected. Set the main player's precondition once instead.
        lines=['0 -','0 2:-','180 START','181 -','360 START','361 -','1400 POKE 0055 81','1400 POKE 006A 1C']
        for slot in range(16,32):lines.append(f'1490 POKE {0x600+slot:04X} F8')
        values={0x480:104,0x460:0,0x600:180,0x420:64,0x400:0,0x440:0,0x640:0,
            0x680:0,0x660:0,0x4A0:0,0x620:0,0x55:0,0x6A:10 if kind=='health' else 28,
            0x5F:5 if kind=='weapon' else 0,0x6F:10}
        pokes=[]
        for address,value in values.items():pokes+=['--coop-test-poke',f'1490:2:{address:X}:{value:X}']
        if kind=='weapon':lines+=['1490 POKE 005D FE','1490 POKE 006F 07']
        if kind=='health':lines+=['1490 POKE 005D FE','1508 X2:R','1509 X2:-']
        type_id={'hurt':0x1A,'health':0x40,'weapon':0x3F,'life':0x41}[kind]
        actor={0x400:255,0x420:2,0x440:0,0x460:0,0x480:104,0x4A0:0,0x4C0:0,0x4E0:0,
            0x600:180,0x620:0,0x640:127,0x660:0,0x680:0,0x6A0:1,0x6C0:1,0x6E0:type_id}
        for address,value in actor.items():lines.append(f'1499 POKE {address+18:04X} {value:02X}')
        route=a.out/(name+'.txt');route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
        args=[str(a.exe),str(a.rom.resolve()),'--frames','1570','--input',str(route),'--coop','on',
            '--coop-p1',roster,'--weapon-cycle','on','--widescreen','off','--charge','off','--slide','off','--no-save',
            '--config',str(a.out/'test.ini'),*pokes,'--coop-report',str(a.out/(name+'.jsonl')),
            '--frame-log',str(a.out/(name+'.frames')),'--frame-log-frames','1569:1569',
            '--hash-out',str(a.out/(name+'.hash')),'--present-out',str(a.out/(name+'.png')),*extra]
        with (a.out/(name+'.log')).open('w') as log:
            subprocess.run(args,cwd=a.exe.parent,env=env,check=True,stdout=log,stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        rows={r['frame']:r for r in map(json.loads,(a.out/(name+'.jsonl')).read_text().splitlines())}
        assert rows[1570]['failures']==0,rows[1570]
        return rows
    for roster,hp in (('megaman',24),('protoman',26)):
        rows=run('hurt-'+roster,'hurt',roster)
        assert rows[1570]['hp']==[28,hp],rows[1570]
        inv=[r['invul'][1] for f,r in rows.items() if f>1500]
        assert max(inv)>50 and inv[-1]<max(inv)-20,inv[-10:]
    state=a.out/'refill.cycstate'
    health=run('health','health',extra=('--save-state',f'1508:{state}','--wav-out',str(a.out/'health.wav')))
    assert health[1570]['hp']==[28,20] and health[1570]['anchor']==0,health[1570]
    assert health[1570]['weapon']==[0,0],health[1570]
    assert any(r['refill'] and r['owner']==1 and r['hp'][0]==28 for r in health.values())
    run('health-resume','health',extra=('--load-state',str(state),'--wav-out',str(a.out/'health-resume.wav')))
    tail=[s for s in (a.out/'health.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1508]
    assert (a.out/'health-resume.hash').read_text().splitlines()==tail
    assert (a.out/'health.png').read_bytes()==(a.out/'health-resume.png').read_bytes()
    def pcm(path):
        with wave.open(str(path),'rb') as f:return f.readframes(f.getnframes())
    original=pcm(a.out/'health.wav');resumed=pcm(a.out/'health-resume.wav')
    assert resumed and original[-len(resumed):]==resumed
    run('health-interpreter','health',extra=('--interp-only',))
    assert (a.out/'health.hash').read_bytes()==(a.out/'health-interpreter.hash').read_bytes()
    assert (a.out/'health.png').read_bytes()==(a.out/'health-interpreter.png').read_bytes()
    weapon=run('weapon','weapon');assert weapon[1570]['weapon']==[0,5] and weapon[1570]['energy'][1]==20,weapon[1570]
    assert (a.out/'weapon.frames').read_bytes()[336+0x6F]==7
    life=run('life','life');assert life[1570]['lives']==life[1498]['lives']+1,(life[1498],life[1570])
    summary=dict(result='pass',guest_native_damage=[4,2],personal_invulnerability_ticks=True,
        guest_health_refill=[10,20],guest_weapon_refill=[10,20],shared_life_once=True,
        native_refill_owner_and_anchor=True,refill_current_state_pcm_exact=True,native_interpreter_exact=True)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
