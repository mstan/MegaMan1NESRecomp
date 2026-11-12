"""Independent native-size views share simulation and preserve shown pictures."""
import argparse,json,os,pathlib,struct,subprocess,sys,wave
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'protoman'))
from validate import pixels

def records(path):
    data=path.read_bytes();offset=12;out={}
    while offset<len(data):
        h=struct.unpack_from('<9I',data,offset)
        out[h[0]+1]=data[offset+324:offset+324+2048]
        offset+=324+sum(h[3:])
    return out

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--native-oracle',action='store_true',help='Compare complete native grids with the retail draw path')
    p.add_argument('--pool-retry',action='store_true',help='A full native pool must retry a peer-view constructor after a slot is free')
    p.add_argument('--projectile-crossing',action='store_true',help='A native player shot must cross the primary window into the peer grid')
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    lines=['0 -','0 2:-','180 START','181 -','360 START','361 -','1300 POKE 005D FE',
        '1420 X2:R','1421 X2:-','1432 X2:R','1433 X2:-','1510 2:B','1511 2:-']
    # Let each native spawn initialize before removing enemies for a stable
    # visible-body comparison. Their native ID reservations remain untouched.
    for slot in range(16,32):lines.append(f'1499 POKE {0x600+slot:04X} F8')
    pose={0x660:0,0x420:64,0x600:180,0x6A:28,0x640:0,0x400:0,
        0x4A0:0,0x680:0,0x440:0,0x55:255,0x620:0}
    pokes=[]
    for seat,x in ((1,104),(2,600)):
        for addr,value in {**pose,0x460:x//256,0x480:x%256}.items():
            pokes+=['--coop-test-poke',f'1490:{seat}:{addr:X}:{value:X}']
        pokes+=['--coop-test-poke',f'1499:{seat}:55:0']
    route=a.out/'input.txt';route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    base=[str(a.exe),str(a.rom.resolve()),'--frames','1540','--input',str(route),
        '--protoman','off','--coop','on','--charge','off','--slide','off',
        '--weapon-cycle','on','--widescreen','off','--no-save','--config',str(a.out/'test.ini'),*pokes]
    def run(name,mode='split',extra=()):
        with (a.out/(name+'.log')).open('w') as log:subprocess.run(base+[
            '--coop-view',mode,'--coop-report',str(a.out/(name+'.jsonl')),
            '--hash-out',str(a.out/(name+'.hash')),'--present-out',str(a.out/(name+'.png')),
            '--wav-out',str(a.out/(name+'.wav')),*extra],cwd=a.exe.parent,env=env,
            check=True,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
        return {r['frame']:r for r in map(json.loads,(a.out/(name+'.jsonl')).read_text().splitlines())}
    if a.projectile_crossing:
        base=base[:base.index('--coop-test-poke')]
        lines=['0 -','0 2:-','180 START','181 -','360 START','361 -','1510 B','1511 -']
        for slot in range(16,32):lines.append(f'1499 POKE {0x600+slot:04X} F8')
        pokes=[]
        for seat,x in ((1,220),(2,400)):
            for addr,value in {**pose,0x460:x//256,0x480:x%256,0x55:0}.items():
                pokes+=['--coop-test-poke',f'1490:{seat}:{addr:X}:{value:X}']
        base+=pokes;route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
        rows=run('crossing',extra=('--frames','1570','--frame-log',str(a.out/'crossing.frames'),
            '--frame-log-frames','1520:1569','--present-every','1559'))
        assert rows[1570]['failures']==0 and rows[1570]['hp']==[28,28],rows[1570]
        memory=records(a.out/'crossing.frames');crossed=[]
        for frame,m in memory.items():
            camera=m[0x1B]*256+m[0x1A]
            for slot in range(2,5):
                x=m[0x460+slot]*256+m[0x480+slot]
                if m[0x600+slot]<248 and x>=camera+256 and 272<=x<528:crossed.append((frame,slot,x,m[0x600+slot]))
        assert crossed,'No real native shot survived beyond the primary window'
        # Choose an actual crossing boundary, then capture it without changing
        # simulation. This is the native shot, not a separately simulated copy.
        frame,slot,x,y=crossed[-1]
        run('shown',extra=('--frames',str(frame),'--frame-log',str(a.out/'shown.frames'),
            '--frame-log-frames',f'{frame-1}:{frame-1}'))
        width,height,actual=pixels(a.out/'shown.png');assert (width,height)==(512,240)
        # A scene-only picture supplies a reference for the actual shot's
        # bounded region. Keep input/time and both players unchanged.
        lines=[s for s in lines if s not in ('1510 B','1511 -')]
        route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
        run('background',extra=('--frames',str(frame)))
        _,_,background=pixels(a.out/'background.png')
        changed=[(px,py) for py in range(max(0,y-16),min(240,y+16)) for px in range(256,512)
            if actual[py][px*3:px*3+3]!=background[py][px*3:px*3+3]]
        assert len(changed)>=4,('The far native shot was not presented',frame,x,y,changed)
        summary=dict(result='pass',native_shot_crosses_primary_window=True,peer_view_shot_pixels=len(changed),
            actual_crossing_frames=len(crossed),last_native_position=[x,y])
        (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary));return
    if a.pool_retry:
        lines=['0 -','0 2:-','180 START','181 -','360 START','361 -','1510 POKE 061F F8']
        actor={0x400:255,0x420:6,0x440:0,0x460:0,0x480:120,0x4A0:0,0x4C0:0,0x4E0:0,
            0x600:100,0x620:0,0x640:127,0x660:0,0x680:0,0x6A0:1,0x6C0:255,0x6E0:0x21}
        for slot in range(16,32):
            for addr,value in actor.items():lines.append(f'1490 POKE {addr+slot:04X} {value:02X}')
            lines.append(f'1490 POKE {0x6B+slot:04X} {224+slot:02X}')
        route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
        state=a.out/'pool.cycstate'
        rows=run('pool',extra=('--frames','1520','--save-state',f'1505:{state}',
            '--frame-log',str(a.out/'pool.frames'),'--frame-log-frames','1500:1519'))
        memory=records(a.out/'pool.frames');before,after=memory[1506],memory[1520]
        assert rows[1520]['failures']==0 and rows[1520]['hp']==[28,28],rows[1520]
        assert all(before[0x600+slot]<248 for slot in range(16,32))
        assert all(v not in before[0x7B:0x8B] for v in (3,4,5))
        assert after[0x8A]==3 and after[0x61F]<248 and after[0x6FF]==0,(after[0x8A],after[0x61F],after[0x6FF])
        assert before[0x1A:0x1C]==after[0x1A:0x1C]==bytes([0,0])
        run('interpreter',extra=('--frames','1520','--interp-only'))
        assert (a.out/'pool.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
        assert (a.out/'pool.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
        run('resume',extra=('--frames','1520','--load-state',str(state)))
        tail=[s for s in (a.out/'pool.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1505]
        assert tail==(a.out/'resume.hash').read_text().splitlines()
        assert (a.out/'pool.png').read_bytes()==(a.out/'resume.png').read_bytes()
        summary=dict(result='pass',native_pool_full_then_retried=True,stationary_primary_camera=True,
            original_far_constructor_identity=3,native_interpreter_exact=True,current_state_exact=True)
        (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary));return
    if a.native_oracle:
        base=base[:base.index('--coop-test-poke')]
        lines=['0 -','0 2:-','180 START','181 -','360 START','361 -',
            '1300 2:SELECT','1390 2:-']
        for addr,value in {**pose,0x460:0,0x480:104,0x55:0}.items():lines.append(f'1495 POKE {addr:04X} {value:02X}')
        for slot in range(16,32):lines.append(f'1497 POKE {0x600+slot:04X} F8')
        route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
        for roster in ('megaman','protoman'):
            common=('--frames','1500','--coop-p1',roster)
            run(roster+'-native','shared',(*common,'--coop','off','--protoman','on' if roster=='protoman' else 'off'))
            rows=run(roster+'-split','split',common)
            assert rows[1500]['withdrawn']==2 and rows[1500]['failures']==0,rows[1500]
            w,h,reference=pixels(a.out/(roster+'-native.png'));sw,sh,actual=pixels(a.out/(roster+'-split.png'))
            assert (w,h,sw,sh)==(256,240,512,240)
            for seat in (0,1):
                different=sum(reference[y][x*3:x*3+3]!=actual[y][(seat*256+x)*3:(seat*256+x)*3+3]
                    for y in range(240) for x in range(256))
                assert different==0,('Original native grid differs',roster,seat,different)
        summary=dict(result='pass',full_native_grid_pixels_exact=4*256*240,both_characters=True,
            native_hud_background_body_and_priority=True)
        (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary));return
    state=a.out/'views.cycstate'
    rows=run('split',extra=('--save-state',f'1529:{state}',
        '--present-every','1529','--frame-log',str(a.out/'native.frames'),'--frame-log-frames','1490:1539'))
    assert rows[1540]['failures']==0 and rows[1540]['hp']==[28,28],rows[1540]
    assert rows[1540]['weapon']==[0,5] and rows[1540]['x']==[104,600],rows[1540]
    width,height,split=pixels(a.out/'split.png');assert (width,height)==(512,240)
    for mode,seat in (('p1',0),('p2',1)):
        run(mode,mode);w,h,one=pixels(a.out/(mode+'.png'));assert (w,h)==(256,240)
        assert all(one[y]==split[y][seat*768:(seat+1)*768] for y in range(240)),mode
        assert (a.out/'split.hash').read_bytes()==(a.out/(mode+'.hash')).read_bytes(),mode
    run('interpreter',extra=('--interp-only',))
    assert (a.out/'split.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
    assert (a.out/'split.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
    run('instant',extra=('--load-state',str(state),'--frames','1530'))
    assert (a.out/'instant.png').read_bytes()==(a.out/'split_01529.png').read_bytes()
    run('resume',extra=('--load-state',str(state)))
    tail=[s for s in (a.out/'split.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1529]
    assert tail==(a.out/'resume.hash').read_text().splitlines()
    assert (a.out/'split.png').read_bytes()==(a.out/'resume.png').read_bytes()
    def pcm(name):
        with wave.open(str(a.out/(name+'.wav')),'rb') as f:return f.readframes(f.getnframes())
    continued=pcm('resume');assert continued and pcm('split')[-len(continued):]==continued
    # This fixture changes only peer position, not the native primary camera.
    # Ordinary far-view entries must have passed through their native constructor.
    rom=a.rom.read_bytes();bank=rom[16+6*16384:16+7*16384]
    pointer=int.from_bytes(bank[0x2452:0x2454],'little')-0x8000
    authored=[]
    for index in range(256):
        screen,x,y,kind=bank[pointer+index*4:pointer+index*4+4]
        if screen>=48:break
        if screen<=3 and y<248 and kind!=255:authored.append((index,screen*256+x,y,kind))
    memory=records(a.out/'native.frames')[1499]
    assert memory[0x1A:0x1C]==bytes([0,0]),memory[0x1A:0x1C]
    reserved=set(memory[0x7B:0x8B])
    far=[v for v in authored if 456<=v[1]<744]
    assert far and any(v[0] in reserved for v in far),(far,reserved)
    gap=[v for v in authored if 272<=v[1]<456]
    assert all(v[0] not in reserved for v in gap),(gap,reserved)
    summary=dict(result='pass',split_native_grids=[256,256],single_views_match_split=True,
        view_modes_share_canonical_simulation=True,peer_only_far_native_spawns=True,gap_spawns_excluded=True,
        native_interpreter_exact=True,current_state_pixels_pcm_exact=True,far_authored_ids=[v[0] for v in far])
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
