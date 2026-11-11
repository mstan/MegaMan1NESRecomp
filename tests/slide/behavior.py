"""Actual terrain, contact, Select and shared-charge slide behavior."""
import argparse,json,os,pathlib,subprocess
from validate import fixture

def actor(lines,frame,x,y):
    values={0x410:255,0x430:2,0x450:0,0x470:0,0x490:x,0x4B0:0,0x4D0:0,0x4F0:0,
            0x610:y,0x630:0,0x650:127,0x670:0,0x690:0,0x6B0:1,0x6D0:1,0x6F0:0x1A}
    lines.extend(f'{frame} POKE {addr:04X} {value:02X}' for addr,value in values.items())

def block(lines,frame,x1,y1,x2,y2):
    values={0x721:1,0x722:0,0x723:x1,0x724:y1,0x725:x2,0x726:y2,0x727:0,0x728:255,0x68:0,0x8E:0}
    lines.extend(f'{frame} POKE {addr:04X} {value:02X}' for addr,value in values.items())

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','donor','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--motion-only',action='store_true',help='Run the terrain/input checks without repeating contact fixtures')
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    base=[str(a.exe),str(a.rom.resolve()),'--mm4-rom',str(a.donor.resolve()),'--slide','on',
          '--protoman','off','--charge','off','--widescreen','off','--no-save','--config',str(a.out/'test.ini')]
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    def run(name,lines,frames=1470,extra=()):
        route=a.out/(name+'.txt');route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
        args=base+['--frames',str(frames),'--input',str(route),'--slide-report',str(a.out/(name+'.jsonl')),
            '--proto-report',str(a.out/(name+'-proto.jsonl')),'--charge-report',str(a.out/(name+'-charge.jsonl')),
            '--present-out',str(a.out/(name+'.png')),'--hash-out',str(a.out/(name+'.hash')),*extra]
        with (a.out/(name+'.log')).open('w') as log:
            subprocess.run(args,cwd=a.exe.parent,env=env,check=True,stdout=log,stderr=subprocess.STDOUT,
                           creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        def rows(suffix):return {r['frame']:r for r in map(json.loads,(a.out/(name+suffix+'.jsonl')).read_text().splitlines())}
        return rows(''),rows('-proto'),rows('-charge')
    if not a.motion_only:
        for proto in (False,True):
            for low in (False,True):
                lines=fixture()+['1400 POKE 0055 00'];actor(lines,1415,42,184 if low else 170)
                s,p,_=run(f'contact-{int(proto)}-{int(low)}',lines,extra=('--protoman','on' if proto else 'off'))
                expected=(24 if proto else 26) if low else 28
                assert p[1470]['hp']==expected,p[1470]
                if low:
                    assert max(r['hurt'] for r in s.values())==(0 if proto else 26),s[1470]
                    assert (s[1470]['x']!=97),s[1470]
    standing=[s for s in fixture() if s not in ('1410 DOWN+A','1411 -')]+['1400 POKE 0055 00']
    actor(standing,1415,38,170);_,p,_=run('standing-contact',standing)
    assert p[1470]['hp']==26,p[1470]
    clear=fixture()+['1415 A','1425 -'];s,_,_=run('jump',clear)
    assert s[1470]['jumps']==1 and min(r['y'] for r in s.values() if r['frame']>1415)<160,s[1470]
    reverse=fixture()+['1415 LEFT','1416 -'];s,_,_=run('reverse',reverse)
    assert s[1470]['reverse']==1 and not s[1420]['active'] and s[1470]['x']<48,s[1470]
    held=[s for s in fixture() if s!='1411 -']+['1450 -'];s,_,_=run('held-a',held)
    assert s[1470]['starts']==1 and s[1470]['jumps']==0 and s[1470]['y']==180,s[1470]
    roof=fixture();block(roof,1413,0,160,120,178)
    roof+=['1440 SELECT','1441 A','1442 -','1450 POKE 0721 00','1455 SELECT','1456 -']
    s,p,_=run('roof-select',roof,1500,('--protoman','on'))
    assert s[1445]['active'] and s[1445]['blocked'] and s[1445]['jumps']==0,s[1445]
    assert p[1445]['swaps']==0 and p[1500]['swaps']==1,(p[1445],p[1500])
    swap=fixture()+['1400 POKE 0055 00','1415 SELECT','1416 -','1447 POKE 0610 F8']
    actor(swap,1418,44,180);s,p,_=run('select-protection',swap,1470,('--protoman','on'))
    assert p[1440]['teleport'] and p[1470]['swaps']==1 and p[1470]['hp']==28,p[1470]
    assert not s[1420]['active'],s[1420]
    pending=fixture()+['1400 B','1410 B+DOWN+A','1411 B','1420 -']
    s,_,c=run('pending-charge',pending,1470,('--charge','on'))
    assert s[1421]['active'] and c[1421]['half']==0,(s[1421],c[1421])
    # Charge has only twenty frames at release, so repeat with a full pre-charge.
    pending=[line for line in pending if line!='1400 B']+['1200 B']
    s,_,c=run('pending-full-charge',pending,1470,('--charge','on'))
    assert c[1421]['full']==0 and c[1470]['full']==1,(c[1421],c[1470])
    for name,flags in (('ladder',('--protoman','on')),):
        ladder=fixture()+['1400 POKE 0480 58','1400 POKE 0600 96','1400 POKE 0400 15','1400 POKE 0420 50']
        s,_,_=run(name,ladder,extra=flags);assert s[1470]['starts']==0,s[1470]
    ledge=fixture()+['1400 POKE 0600 44'];block(ledge,1400,0,80,64,96)
    s,_,_=run('ledge',ledge)
    assert s[1470]['starts']==1 and s[1470]['falls']==1 and not s[1470]['active'],s[1470]
    summary=dict(result='pass',contact_box_and_hurt_checks=not a.motion_only,
        jump_reverse_and_held_input=True,low_ceiling_blocks_stand_jump_and_swap=True,
        slide_select_cancels_and_protects=True,deferred_charge_release=True,ladder_rejects=True,native_ledge_fall=True)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
