"""Personal native death, survivor handoff and one shared native retry."""
import argparse,json,os,pathlib,subprocess,sys,wave
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'protoman'))
from validate import pixels

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','donor','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--handoff-only',action='store_true')
    p.add_argument('--palette-handoff',action='store_true',help='use a distinct native guest weapon palette')
    p.add_argument('--reverse-roster',action='store_true')
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    def run(name,victim,team=False,game_over=False,extra=(),stop=0,protect=False):
        frames=stop or (2800 if game_over else 2350 if team else 1700)
        lines=['0 -','0 2:-','180 START','181 -','360 START','361 -','1490 POKE 005D FE']
        for slot in range(16,32):lines.append(f'1490 POKE {0x600+slot:04X} F8')
        if game_over:lines.append('1490 POKE 00A6 00')
        pokes=[]
        def personal(frame,seat,values):
            for address,value in values.items():pokes.extend(('--coop-test-poke',f'{frame}:{seat+1}:{address:X}:{value:X}'))
        personal(1490,0,{0x6A:2 if victim==0 and not protect else 28,0x55:0,0x6F:7})
        personal(1490,1,{0x480:104,0x460:0,0x600:180,0x420:64,0x400:0,0x440:0,0x640:0,
            0x680:0,0x660:0,0x4A0:0,0x620:0,0x55:0,0x6A:4 if victim==1 else 28,0x6F:19})
        def bullet(frame,seat):
            values={0x400:255,0x420:2,0x440:0,0x460:0,0x480:128 if seat==0 else 104,
                0x4A0:0,0x4C0:0,0x4E0:0,0x600:148 if seat==0 else 180,0x620:0,0x640:127,
                0x660:0,0x680:0,0x6A0:1,0x6C0:1,0x6E0:0x1A}
            for address,value in values.items():lines.append(f'{frame} POKE {address+18:04X} {value:02X}')
        bullet(1499,victim);lines+=['1510 POKE 0612 F8']
        if a.palette_handoff:lines+=['1490 X2:R','1491 X2:-']
        if team:
            personal(1540,victim^1,{0x6A:2 if victim==1 else 4,0x55:0})
            bullet(1545,victim^1)
            if game_over:lines+=['2100 2:START','2101 2:-','2300 2:START','2301 2:-']
        else:
            seat='2:' if victim==0 else ''
            lines += [f'1450 {seat}B',f'1560 {seat}-',f'1600 {seat}LEFT',f'1630 {seat}-']
        route=a.out/(name+'.txt');route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
        args=[str(a.exe),str(a.rom.resolve()),'--frames',str(frames),'--input',str(route),'--coop','on',
            '--mm4-rom',str(a.donor.resolve()),'--charge','on','--weapon-cycle','on','--widescreen','off','--no-save',
            '--config',str(a.out/'test.ini'),*pokes,'--coop-report',str(a.out/(name+'.jsonl')),
            '--charge-report',str(a.out/(name+'-charge.jsonl')),'--hash-out',str(a.out/(name+'.hash')),
            '--present-out',str(a.out/(name+'.png')),'--frame-log',str(a.out/(name+'.frames')),
            '--frame-log-frames',f'{frames-1}:{frames-1}',*extra]
        if a.reverse_roster:args+=['--coop-p1','protoman']
        with (a.out/(name+'.log')).open('w') as log:
            subprocess.run(args,cwd=a.exe.parent,env=env,check=True,stdout=log,stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        rows={r['frame']:r for r in map(json.loads,(a.out/(name+'.jsonl')).read_text().splitlines())}
        if rows:assert rows[frames]['failures']==0,rows[frames]
        else:assert '--load-state' in extra and stop
        return rows
    if a.handoff_only:
        state=a.out/'handoff.cycstate'
        rows=run('handoff',0,extra=('--save-state',f'1499:{state}','--present-every','1499','--wav-out',str(a.out/'handoff.wav')))
        first=rows[1500];assert first['anchor']==1 and first['shown_owner']==0 and first['guest_owner']==1 and first['picture'],first
        if a.palette_handoff:assert first['weapon'][1]==3,first
        run('reference',0,protect=True,stop=1500,extra=('--screenshot',str(a.out/'reference-native.png')))
        _,_,expected=pixels(a.out/'reference.png');_,_,actual=pixels(a.out/'handoff_01499.png');_,_,raw=pixels(a.out/'reference-native.png')
        assert all(expected[y][90*3:120*3]==actual[y][90*3:120*3] for y in range(160,192))
        visible=sum(expected[y][x*3:x*3+3]!=raw[y][x*3:x*3+3] for y in range(160,192) for x in range(90,120))
        assert visible>100,visible
        run('handoff-next',0,stop=1501,extra=('--load-state',str(state)))
        run('reference-next',0,protect=True,stop=1501)
        _,_,next_expected=pixels(a.out/'reference-next.png');_,_,next_actual=pixels(a.out/'handoff-next.png')
        assert all(next_expected[y][90*3:116*3]==next_actual[y][90*3:116*3] for y in range(164,192)),'First new primary palette differs'
        run('handoff-still',0,stop=1500,extra=('--load-state',str(state)))
        assert (a.out/'handoff-still.png').read_bytes()==(a.out/'handoff_01499.png').read_bytes()
        def pcm(path):
            with wave.open(str(path),'rb') as f:return f.readframes(f.getnframes())
        for name,victim,team,frame in (('handoff',0,False,1499),('retry',0,True,1620)):
            snapshot=state if not team else a.out/'retry.cycstate'
            if team:run(name,victim,team=True,extra=('--save-state',f'{frame}:{snapshot}','--wav-out',str(a.out/(name+'.wav'))))
            resume=name+'-resume';run(resume,victim,team=team,extra=('--load-state',str(snapshot),'--wav-out',str(a.out/(resume+'.wav'))))
            tail=[s for s in (a.out/(name+'.hash')).read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>frame]
            assert (a.out/(resume+'.hash')).read_text().splitlines()==tail
            assert (a.out/(resume+'.png')).read_bytes()==(a.out/(name+'.png')).read_bytes()
            original=pcm(a.out/(name+'.wav'));continued=pcm(a.out/(resume+'.wav'));assert continued and original[-len(continued):]==continued
        run('handoff-interpreter',0,extra=('--interp-only',))
        assert (a.out/'handoff.hash').read_bytes()==(a.out/'handoff-interpreter.hash').read_bytes()
        assert (a.out/'handoff.png').read_bytes()==(a.out/'handoff-interpreter.png').read_bytes()
        summary=dict(result='pass',actual_first_handoff_keeps_live_player_pixels=True,guest_opaque_pixels=visible,
            first_new_primary_colors_exact=True,distinct_weapon_palette=a.palette_handoff,reverse_roster=a.reverse_roster,
            first_handoff_immediate_state_pixels_exact=True,handoff_and_native_retry_state_pcm_exact=True,native_interpreter_exact=True)
        (a.out/'handoff-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary));return
    for victim in (0,1):
        rows=run(f'personal-{victim}',victim);row=rows[1700]
        assert rows[1510]['dead'][victim]==1 and rows[1510]['dead'][victim^1]==0,rows[1510]
        assert row['hp'][victim]==0 and row['hp'][victim^1]==28 and row['lives']==2,row
        assert row['anchor']==victim^1 and row['wipes']==0,row
        assert not row['picture'] and row['blast']==[0,0],row
        assert row['x'][victim^1]<rows[1599]['x'][victim^1]-20,(rows[1599],row)
        charge={(r['seat'],r['frame']):r for r in map(json.loads,(a.out/f'personal-{victim}-charge.jsonl').read_text().splitlines())}
        assert charge[victim^1,1540]['counter']>=80 and charge[victim^1,1700]['full']==1
    for victim in (0,1):
        rows=run(f'team-{victim}',victim,team=True);row=rows[2350]
        assert row['ready'] and row['dead']==[0,0] and row['hp']==[28,28],row
        assert row['wipes']==1 and row['deaths']==[1,1] and row['lives']==1 and row['retries']==1,row
        assert [x[4] for x in row['ammo']]==[7,19] and row['weapon']==[0,0],row
    rows=run('game-over',0,team=True,game_over=True);row=rows[2800]
    assert not rows[2000]['ready'] and row['ready'] and row['hp']==[28,28] and row['lives']==2,row
    assert row['retries']==0 and row['ammo']==[[28]*7,[28]*7],row
    summary=dict(result='pass',either_personal_death_keeps_partner_playing=True,survivor_charge_retained=True,
        either_team_death_order_native_retry=True,one_shared_life_decrement=True,personal_ammo_retained=True,
        second_controller_native_game_over_and_new_game=True)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
