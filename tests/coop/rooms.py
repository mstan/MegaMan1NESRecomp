"""Either player takes authored Cut Man links through the original scripts.

The fixture supplies source-room bounds/order and body positions, bypassing
earlier traversal. Controller input must trigger the real exit and destination.
"""
import argparse,json,os,pathlib,subprocess,wave
from views import records

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--leader',type=int,choices=(1,2),default=1)
    p.add_argument('--view',choices=('shared','split','p1','p2'),default='split')
    p.add_argument('--smoke-only',action='store_true')
    p.add_argument('--distant-partner',action='store_true',help='Leave the other body in the starting page before the real link')
    p.add_argument('--pit',action='store_true',help='An invalid native downward exit kills only that personal body')
    p.add_argument('--link',choices=('ladder','down-ladder','first-door','second-door'),default='ladder')
    p.add_argument('--wait-both',action='store_true')
    p.add_argument('--partner-pit',action='store_true',help='A waiting player must proceed after the partner takes a real native pit death')
    p.add_argument('--battle',action='store_true',help='High-level native boss/nextscene gate: HP1 prerequisite and one real P2 Buster tap')
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    assert not a.pit or a.link=='ladder'
    assert not a.partner_pit or a.wait_both and a.link=='ladder'
    assert not a.battle or a.link=='second-door' and not a.wait_both and a.leader==1
    vertical=a.link in ('ladder','down-ladder');down=a.link=='down-ladder'
    source,begin,order,target,end=(3,0,0,4,4) if a.link=='ladder' else (4,4,1,0,3) if down else (19,18,11,20,22) if a.link=='first-door' else (22,20,12,23,23)
    arrival=end if down else target
    direction='DOWN' if down else 'UP' if vertical else 'RIGHT'
    key='2:' if a.leader==2 else ''
    lines=['0 -','0 2:-','180 START','181 -','360 START','361 -','1490 POKE 001A 00',f'1490 POKE 001B {source:02X}',
        f'1491 {key}{direction}',f'1520 {key}-']
    if a.link!='ladder':lines+= [f'1490 POKE 0027 {begin:02X}',f'1490 POKE 0028 {source:02X}',f'1490 POKE 0029 {order:02X}']
    if a.wait_both:
        assert not a.pit and not a.distant_partner and a.link!='second-door'
        other='2:' if a.leader==1 else ''
        lines=[s for s in lines if not s.startswith('1520 ')]
        if not a.partner_pit:lines += [f'1580 {other}{direction}',f'1640 {other}-']
        lines += [f'1640 {key}-']
    if a.pit:
        other='2:' if a.leader==1 else ''
        lines=lines[:8]+[f'1580 {other}LEFT',f'1610 {other}-']
    if a.battle:
        lines += ['2200 POKE 06C1 01','2208 2:B','2209 2:-',
            '4800 2:RIGHT','4801 2:-','4840 RIGHT','4841 -','4900 START','4901 -']
    for slot in range(16,32):lines.append(f'1490 POKE {0x600+slot:04X} F8')
    pokes=[]
    for seat in (1,2):
        leading=seat==a.leader
        values={0x460:begin if a.distant_partner and not leading else source,0x480:104 if a.distant_partner and not leading else 216 if vertical else 248,
            0x600:(232 if down else 8) if leading and vertical else 180 if vertical else 148,0x400:21 if leading and vertical else 0,
            0x420:80 if leading and vertical else 64,0x440:0,0x640:0,0x680:0,0x660:0,0x4A0:0,0x620:0,
            0x30:12 if leading and vertical else 0,0x55:255,0x6A:20 if seat==1 else 24,0x6F:7 if seat==1 else 19}
        if a.pit:values.update({0x460:2 if leading else 0,0x480:176 if leading else 64,
            0x600:236 if leading else 180,0x400:9 if leading else 0,0x420:64,0x30:0,0x680:254 if leading else 0})
        if a.wait_both and not leading:
            values.update({0x480:216 if vertical else 176,0x600:(200 if down else 40) if vertical else 148,
                0x400:21 if vertical else 0,0x420:80 if vertical else 64,0x30:12 if vertical else 0})
        if a.wait_both or a.battle:
            for addr in (0x440,0x4A0,0x620,0x640):values.pop(addr)
        if a.battle:values.pop(0x6F)
        for addr,value in values.items():pokes+=['--coop-test-poke',f'1490:{seat}:{addr:X}:{value:X}']
    if a.partner_pit:
        for addr,value in {0x460:2,0x480:176,0x600:236,0x400:9,0x420:64,0x680:254,0x660:0}.items():
            pokes+=['--coop-test-poke',f'1580:{3-a.leader}:{addr:X}:{value:X}']
    if a.battle:
        # Position/protect only; never write defeat, reward or scene results.
        pokes+=['--coop-test-poke','2200:1:480:A0','--coop-test-poke','2200:1:55:FF']
        for addr,value in {0x480:28,0x600:148,0x400:0,0x420:64,0x680:0,0x660:0,0x55:255}.items():
            pokes+=['--coop-test-poke',f'2200:2:{addr:X}:{value:X}']
        # The native reward falls below the arena's raised Guts blocks. Bypass
        # that short walk to its floor, then let the original contact collect it.
        for addr,value in {0x480:128,0x600:180,0x55:255}.items():
            pokes+=['--coop-test-poke',f'2600:2:{addr:X}:{value:X}']
    route=a.out/'input.txt';route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    final_frame=6000 if a.battle else 1830 if a.wait_both else 1750
    base=[str(a.exe),str(a.rom.resolve()),'--frames',str(final_frame),'--input',str(route),'--coop','on','--coop-view',a.view,
        '--coop-wait-both','on' if a.wait_both else 'off',
        '--charge','off','--slide','off','--weapon-cycle','on','--widescreen','off','--no-save',
        '--config',str(a.out/'test.ini'),*pokes]
    def run(name,extra=()):
        with (a.out/(name+'.log')).open('w') as log:subprocess.run(base+[
            '--coop-report',str(a.out/(name+'.jsonl')),
            *(['--frame-log',str(a.out/(name+'.frames')),'--frame-log-frames',f'{2190 if a.battle else 1490}:{final_frame-1}'] if name=='native' else []),
            '--hash-out',str(a.out/(name+'.hash')),
            '--present-out',str(a.out/(name+'.png')),'--wav-out',str(a.out/(name+'.wav')),*extra],
            cwd=a.exe.parent,env=env,check=True,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
        return {r['frame']:r for r in map(json.loads,(a.out/(name+'.jsonl')).read_text().splitlines())}
    pit_state=a.out/'pit.cycstate'
    rows=run('native',('--save-state',f'3000:{a.out/"clear.cycstate"}','--present-every','3000') if a.battle else
        ('--save-state',f'1490:{pit_state}','--present-every','1490') if a.pit else ())
    parts=records(a.out/'native.frames',True);memory={f:v[0] for f,v in parts.items()};m=memory[final_frame]
    assert rows[final_frame]['failures']==0,rows[final_frame]
    if a.battle:
        fighting=[f for f,m in memory.items() if m[0x31]==0 and m[0x3E]>=5 and m[0x601]<248 and m[0x6C1]>0]
        defeated=[f for f,m in memory.items() if m[0x601]==248 and not m[0x6C1] and m[0x6FF]==0x44 and m[0x61F]<248]
        summary=dict(result='observed',native_boss_max_hp=max((memory[f][0x6C1] for f in fighting),default=0),
            native_boss_min_hp=min((memory[f][0x6C1] for f in fighting),default=0),
            actual_boss_frames=len(fighting),reward_first_frame=defeated[0] if defeated else None,
            final_stage=m[0x31],final_owned=m[0x5D],final_anchor=rows[final_frame]['anchor'],
            menu_stages={f:memory[f][0x31] for f in (4799,4802,4839,4842,4899,5100)},
            lives=rows[final_frame]['lives'],deaths=rows[final_frame]['deaths'])
        assert fighting and max(memory[f][0x6C1] for f in fighting)==28 and defeated,summary
        assert memory[2201][0x6C1]==1 and defeated[0]>=2209,'Boss must die after the actual Buster input'
        assert rows[defeated[0]]['anchor']==1,summary
        assert summary['final_owned']&0x20 and summary['deaths']==[0,0] and summary['lives']==2,summary
        if not a.smoke_only:
            assert summary['menu_stages']=={4799:0,4802:0,4839:0,4842:5,4899:5,5100:5},summary
            assert rows[4700]['ready']==0 and rows[4700]['anchor']==0 and rows[6000]['ready']==1,rows[4700]
            assert rows[6000]['hp']==[28,28] and summary['final_stage']==5,rows[6000]
            state=a.out/'clear.cycstate'
            run('instant',('--load-state',str(state),'--frames','3001'))
            assert (a.out/'instant.png').read_bytes()==(a.out/'native_03000.png').read_bytes()
            run('resume',('--load-state',str(state)))
            tail=[s for s in (a.out/'native.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>3000]
            assert tail==(a.out/'resume.hash').read_text().splitlines()
            assert (a.out/'native.png').read_bytes()==(a.out/'resume.png').read_bytes()
            def pcm(name):
                with wave.open(str(a.out/(name+'.wav')),'rb') as f:return f.readframes(f.getnframes())
            continued=pcm('resume');assert continued and pcm('native')[-len(continued):]==continued
            run('menu',('--load-state',str(state),'--frames','4701','--screenshot',str(a.out/'menu-native.png')))
            assert (a.out/'menu.png').read_bytes()==(a.out/'menu-native.png').read_bytes(),'Global menu differs from the native picture'
            run('interpreter',('--interp-only',))
            assert (a.out/'native.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
            assert (a.out/'native.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
            assert pcm('native')==pcm('interpreter')
            summary.update(result='pass',real_guest_buster_kill=True,native_reward_pickup_and_clear=True,
                global_p1_input_and_native_pixels=True,next_stage_both_players=True,
                current_clear_pixels_pcm_exact=True,native_interpreter_exact=True)
        (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))
        return
    if a.pit:
        victim=a.leader-1;peer=victim^1;last=rows[1750]
        assert last['dead'][victim] and not last['dead'][peer] and last['hp'][victim]==0,last
        assert last['hp'][peer]==(20 if peer==0 else 24) and last['lives']==2 and last['wipes']==0,last
        assert rows[1620]['x'][peer]<rows[1579]['x'][peer]-15,(rows[1579],rows[1620])
        run('interpreter',('--interp-only',))
        assert (a.out/'native.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
        assert (a.out/'native.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
        run('instant',('--load-state',str(pit_state),'--frames','1491'))
        assert (a.out/'instant.png').read_bytes()==(a.out/'native_01490.png').read_bytes()
        run('resume',('--load-state',str(pit_state)))
        tail=[s for s in (a.out/'native.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1490]
        assert tail==(a.out/'resume.hash').read_text().splitlines()
        assert (a.out/'native.png').read_bytes()==(a.out/'resume.png').read_bytes()
        def pcm(name):
            with wave.open(str(a.out/(name+'.wav')),'rb') as f:return f.readframes(f.getnframes())
        continued=pcm('resume');assert continued and pcm('native')[-len(continued):]==continued
        summary=dict(result='pass',victim=a.leader,view=a.view,native_invalid_downward_exit=True,
            personal_pit_death_survivor_control=True,one_living_partner_no_life_used=True,
            current_pit_state_pixels_pcm_exact=True,native_interpreter_exact=True)
        (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary));return
    assert (m[0x27],m[0x28],m[0x29])==(target,end,order+(-1 if down else 1)),('Native room script did not complete',m[0x27:0x2A],rows[final_frame])
    if a.partner_pit:
        assert rows[final_frame]['x'][a.leader-1]//256==arrival and rows[final_frame]['dead'][2-a.leader],rows[final_frame]
        assert rows[final_frame]['lives']==2 and rows[final_frame]['wipes']==0,rows[final_frame]
    else:assert [x//256 for x in rows[final_frame]['x']]==[arrival,arrival],('Partner did not follow the native scroll',rows[final_frame])
    expected_hp=[20,24]
    if a.partner_pit:expected_hp[2-a.leader]=0
    assert rows[final_frame]['hp']==expected_hp and [v[4] for v in rows[final_frame]['ammo']]==[7,19],rows[final_frame]
    if a.wait_both:
        pending=rows[1550];assert tuple(memory[1550][0x27:0x2A])==(begin,source,order),pending
        if vertical:assert pending['waiting']==1<<(a.leader-1) and pending['y'][a.leader-1]==248,pending
        else:assert not pending['waiting'] and pending['x'][a.leader-1]==source*256+232,pending
        assert rows[final_frame]['waiting']==0 and rows[final_frame]['wait_both']==1,rows[final_frame]
    level=a.rom.read_bytes()[16:16+16384]
    def native_grid(frame,screen):
        nt=parts[frame][1];base=(screen&1)*1024;room=level[0xC00+screen]
        ptr=(level[0xC30+room*2]|level[0xC31+room*2]<<8)-0x8000
        different=[]
        for y in range(30):
            for x in range(32):
                wx=x*8;wy=y*8;block=level[ptr+(wx//32)*8+wy//32]
                quad=(2 if wx&16 else 0)+(1 if wy&16 else 0)
                tile=(level[block*4+quad]*4+(2 if wx&8 else 0)+(1 if wy&8 else 0))&255
                shift=(4 if y&2 else 0)+(2 if x&2 else 0)
                palette=(level[0x300+block]>>shift)&3
                actual_palette=(nt[base+960+y//4*8+x//4]>>shift)&3
                if nt[base+y*32+x]!=tile or actual_palette!=palette:different.append((x,y))
        assert not different,('Native tile/palette ring differs',frame,screen,len(different),different[:8])
    if vertical:native_grid(final_frame,arrival)
    first_native=None
    if a.distant_partner and a.leader==2 and a.view=='shared':
        first_native=next((f for f,m in memory.items() if m[0x1A:0x1C]==bytes((0,source)) and
            m[0x27:0x29]==bytes((begin,source)) and not m[0x1E] and rows[f]['shown_owner']==1),None)
        assert first_native,'No actual new-owner native source frame was observed'
        if vertical:native_grid(first_native,source)
    scrolling=[frame for frame,m in memory.items() if m[0x26] and (m[0x1E] if vertical else m[0x1A])]
    assert scrolling,'No actual native scroll observed'
    if not vertical:
        start=1 if a.link=='first-door' else 10
        assert any(start<=v[0x59]<start+8 and v[0x5A] and v[0x6A0] for v in memory.values()),'No native shutter animation observed'
    if a.smoke_only:print(json.dumps(dict(result='pass',leader=a.leader,link=a.link,native_scroll=True)));return
    checkpoint=1549 if a.wait_both else scrolling[len(scrolling)//2]-1;state=a.out/'scroll.cycstate'
    run('producer',('--save-state',f'{checkpoint}:{state}','--present-every',str(checkpoint)))
    assert (a.out/'native.hash').read_bytes()==(a.out/'producer.hash').read_bytes()
    run('instant',('--load-state',str(state),'--frames',str(checkpoint+1)))
    assert (a.out/'instant.png').read_bytes()==(a.out/f'producer_{checkpoint:05d}.png').read_bytes()
    run('resume',('--load-state',str(state)))
    tail=[s for s in (a.out/'native.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>checkpoint]
    assert tail==(a.out/'resume.hash').read_text().splitlines()
    assert (a.out/'native.png').read_bytes()==(a.out/'resume.png').read_bytes()
    def pcm(name):
        with wave.open(str(a.out/(name+'.wav')),'rb') as f:return f.readframes(f.getnframes())
    continued=pcm('resume');assert continued and pcm('native')[-len(continued):]==continued
    run('interpreter',('--interp-only',))
    assert (a.out/'native.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
    assert (a.out/'native.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
    summary=dict(result='pass',leader=a.leader,view=a.view,link=a.link,wait_both=a.wait_both,partner_pit=a.partner_pit,distant_partner=a.distant_partner,native_link_and_living_follow=True,
        own_health_ammo_retained=True,earlier_traversal_bypassed=True,injected_scroll=False,
        actual_scroll_frames=len(scrolling),native_tile_and_palette_cells_exact=960 if vertical else 0,
        first_new_native_source_frame=first_native,native_interpreter_exact=True,current_scroll_pixels_pcm_exact=True)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
