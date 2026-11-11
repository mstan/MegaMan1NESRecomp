"""Native guest sprites, complementary rosters, shared abilities and priority."""
import argparse,json,os,pathlib,subprocess,sys,wave
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'protoman'))
from validate import pixels

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','donor','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--priority-only',action='store_true',help='run only the native sprite and priority checks')
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    boot=['0 -','0 2:-','180 START','181 -','360 START','361 -','1400 HOLD 0055 81','1400 HOLD 006A 1C']
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    base=[str(a.exe),str(a.rom.resolve()),'--protoman','off','--coop','on','--widescreen','off',
          '--weapon-cycle','on','--charge','off','--slide','off','--no-save','--config',str(a.out/'test.ini')]
    def pokes(frame,values):
        result=[]
        for address,value in values.items():result+=['--coop-test-poke',f'{frame}:2:{address:X}:{value:X}']
        return result
    def run(name,lines,frames,extra=()):
        route=a.out/(name+'.txt');route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
        args=base+['--frames',str(frames),'--input',str(route),'--coop-report',str(a.out/(name+'.jsonl')),
            '--charge-report',str(a.out/(name+'-charge.jsonl')),'--slide-report',str(a.out/(name+'-slide.jsonl')),
            '--hash-out',str(a.out/(name+'.hash')),'--present-out',str(a.out/(name+'.png')),
            '--screenshot',str(a.out/(name+'-native.png')),'--frame-log',str(a.out/(name+'.frames')),
            '--frame-log-frames',f'{frames-1}:{frames-1}',*extra]
        with (a.out/(name+'.log')).open('w') as log:
            subprocess.run(args,cwd=a.exe.parent,env=env,check=True,stdout=log,stderr=subprocess.STDOUT,
                           creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        def rows(suffix,personal=False):
            data=list(map(json.loads,(a.out/(name+suffix+'.jsonl')).read_text().splitlines()))
            return {(r['seat'],r['frame']) if personal else r['frame']:r for r in data}
        coop=rows('');assert coop[frames]['failures']==0,coop[frames]
        return coop,rows('-charge',True),rows('-slide',True)
    flat=boot.copy()
    for address,value in ((0x480,32),(0x460,0),(0x600,180),(0x420,64),(0x400,0),(0x440,0),
                          (0x640,0),(0x680,0),(0x660,0),(0x4A0,0),(0x620,0)):
        flat.append(f'1495 POKE {address:04X} {value:02X}')
    for slot in range(16,32):flat.append(f'1497 POKE {0x600+slot:04X} F8')
    # Let the actual landing and the OAM/DMA pipeline settle after positioning.
    pose=pokes(1495,{0x480:64,0x460:0,0x600:180,0x420:64,0x400:0,0x440:0,0x640:0,0x680:0,0x660:0,0x4A0:0,0x620:0,0x55:0})
    reference,_,_=run('native-mega',flat,1500,('--coop-p1','protoman',*pose,
        '--save-state',f'1499:{a.out/"native-mega.cycstate"}'))
    assert reference[1500]['x']==[32,64] and reference[1500]['y']==[180,180],reference[1500]
    _,_,native=pixels(a.out/'native-mega-native.png');_,_,presented=pixels(a.out/'native-mega.png')
    # This flat gray background is disjoint from both sprite palettes, so
    # background subtraction keeps their opaque black outlines and eyes.
    assert {native[y][x*3:x*3+3] for y in range(165,192) for x in range(48,80)}=={bytes((174,174,174))}
    # Use the same world coordinates: native background tiles differ even
    # between adjacent body-sized rectangles in this room.
    solo=[s.replace('0480 20','0480 40') for s in flat]
    run('native-mega-reference',solo,1500,('--coop','off'))
    _,_,solo_pixels=pixels(a.out/'native-mega-reference-native.png')
    assert all(solo_pixels[y][52*3:76*3]==presented[y][52*3:76*3] for y in range(165,192)),'Native Mega pixels differ'
    if not a.priority_only:
        lr,_,_=run('personal-lr',boot+['1400 POKE 005D FE','1405 X2:R','1406 X2:-'],1430)
        assert lr[1430]['weapon']==[0,3],lr[1430]
        donor=['--mm4-rom',str(a.donor.resolve())]
        slide_pose=pokes(1400,{0x480:104,0x460:0,0x600:180,0x420:0,0x400:0,0x440:0,0x640:0,0x680:0,0x660:0,0x4A0:0,0x620:0})
        slide=boot+['1420 2:DOWN+A+LEFT','1421 2:-']
        for roster in ('megaman','protoman'):
            extra=['--coop-p1',roster,*donor,'--slide','on',*slide_pose]
            rows,_,s=run('slide-'+roster,slide,1480,extra)
            assert s[1,1480]['starts']==1 and s[1,1480]['timed']==1 and s[1,1480]['x']==39,s[1,1480]
            assert s[0,1480]['starts']==0,s[0,1480]
            _,_,shown=run('slide-art-'+roster,slide,1426,extra)
            assert shown[1,1426]['active'],shown[1,1426]
        _,_,off=run('slide-off',slide,1430,slide_pose)
        assert off[1,1430]['starts']==0 and min(r['y'] for (seat,frame),r in off.items() if seat==1 and frame>1420)<180
        charge=boot+['1300 2:B','1500 2:-']
        for roster in ('megaman','protoman'):
            name='charge-'+roster;charging=a.out/(name+'.cycstate');shot=a.out/(name+'-shot.cycstate')
            extra=['--coop-p1',roster,*donor,'--charge','on']
            _,c,_=run(name,charge,1516,[*extra,'--save-state',f'1490:{charging}','--save-state',f'1505:{shot}',
                                     '--wav-out',str(a.out/(name+'.wav'))])
            assert c[1,1516]['full']==1 and 2 in c[1,1516]['kinds'],c[1,1516]
            assert c[0,1516]['counter']==0 and c[0,1516]['full']==0,c[0,1516]
            if roster=='megaman':
                def pcm(path):
                    with wave.open(str(path),'rb') as f:return f.readframes(f.getnframes())
                original=pcm(a.out/(name+'.wav'))
                for suffix,state,frame in (('charging',charging,1490),('shot',shot,1505)):
                    resume=name+'-'+suffix
                    run(resume,charge,1516,[*extra,'--load-state',str(state),'--wav-out',str(a.out/(resume+'.wav'))])
                    tail=[s for s in (a.out/(name+'.hash')).read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>frame]
                    assert (a.out/(resume+'.hash')).read_text().splitlines()==tail
                    assert (a.out/(name+'.png')).read_bytes()==(a.out/(resume+'.png')).read_bytes()
                    resumed=pcm(a.out/(resume+'.wav'));assert resumed and original[-len(resumed):]==resumed
                run('charge-interpreter',charge,1516,[*extra,'--interp-only'])
                assert (a.out/(name+'.hash')).read_bytes()==(a.out/'charge-interpreter.hash').read_bytes()
                assert (a.out/(name+'.png')).read_bytes()==(a.out/'charge-interpreter.png').read_bytes()
    # The actual native OAM establishes which side of the player the object
    # occupies. Check both alternating orders against the hardware picture.
    orders=set();conflicts=0
    for frame in (1500,1501):
        if frame!=1500:
            run(f'native-mega-{frame}',flat,frame,('--coop-p1','protoman',*pose))
            _,_,presented=pixels(a.out/f'native-mega-{frame}.png')
        name=f'priority-{frame}';values={0x412:255,0x432:0,0x452:0,0x472:0,0x492:64,0x4B2:0,
            0x4D2:0,0x4F2:0,0x612:180,0x632:0,0x652:127,0x672:0,0x692:0,0x6B2:1,0x6D2:1,0x6F2:0x41}
        overlap=flat+[f'1498 HOLD {address:04X} {value:02X}' for address,value in values.items()]
        run(name,overlap,frame,('--coop-p1','protoman',*pose))
        _,_,raw=pixels(a.out/(name+'-native.png'));_,_,actual=pixels(a.out/(name+'.png'))
        oam=(a.out/(name+'.frames')).read_bytes()[80:336]
        player=[];item=[]
        for slot in range(64):
            y,tile,attr,x=oam[slot*4:slot*4+4]
            if 160<=y<192:
                if 16<=x<48:player.append(slot)
                if 48<=x<80:item.append(slot)
        assert player and item,(player,item)
        front=min(item)<min(player);orders.add(front)
        def compare(label):
            nonlocal conflicts
            for y in range(165,192):
                for x in range(48,80):
                    bg=native[y][x*3:x*3+3];guest=presented[y][x*3:x*3+3];npc=raw[y][x*3:x*3+3]
                    conflicts+=guest!=bg and npc!=bg and guest!=npc
                    expected=npc if front and npc!=bg else guest if front else guest if guest!=bg else npc
                    assert actual[y][x*3:x*3+3]==expected,(label,frame,x,y,front,actual[y][x*3:x*3+3],expected)
        compare('body')
        # With no emitted main body, its native last-used cursor is the only
        # anchor. Check the adjacent world object on each side of that event.
        name+= '-blink';blink=[s.replace('HOLD 0055 81','HOLD 0055 83') for s in overlap]
        run(name,blink,frame,('--coop-p1','protoman',*pose))
        _,_,raw=pixels(a.out/(name+'-native.png'));_,_,actual=pixels(a.out/(name+'.png'))
        oam=(a.out/(name+'.frames')).read_bytes()[80:336]
        assert not any(160<=oam[i*4]<192 and 16<=oam[i*4+3]<48 for i in range(64))
        compare('blink')
    assert orders=={False,True} and conflicts>5,(orders,conflicts)
    ability=True if not a.priority_only else None
    summary=dict(result='pass',priority_only=a.priority_only,native_mega_pixels_exact=True,complementary_rosters=True,personal_lr=ability,
        both_guest_characters_share_slide_and_charge=ability,disabled_slide_preserves_native_jump=ability,
        guest_charge_and_shot_state_pcm_exact=ability,native_interpreter_exact=ability,
        both_native_sprite_orders_exact=True,blink_cursor_priority_exact=True,overlap_color_conflicts=conflicts)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
