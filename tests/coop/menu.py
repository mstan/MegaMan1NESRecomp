"""Both menu owners, personal choices and original native panel pixels."""
import argparse,json,os,pathlib,subprocess,sys,wave
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'protoman'))
from validate import pixels

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--owner',type=int,choices=(1,2),default=2)
    p.add_argument('--smoke-only',action='store_true')
    p.add_argument('--fit',action='store_true')
    p.add_argument('--reopen',action='store_true')
    p.add_argument('--withdraw-guard',action='store_true')
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    boot=['0 -','0 2:-','180 START','181 -','360 START','361 -','1300 POKE 005D FE']
    key='2:' if a.owner==2 else '';other='' if a.owner==2 else '2:'
    route=boot+[f'1400 {key}START',f'1401 {key}-','1420 DOWN','1421 -','1430 A','1431 -',
        '1440 2:DOWN','1441 2:-','1450 2:DOWN','1451 2:-','1460 2:A','1461 2:-',
        f'1470 {other}START',f'1471 {other}-',f'1500 {key}START',f'1501 {key}-']
    if a.reopen:
        route += [f'1600 {other}START',f'1601 {other}-',f'1620 {other}DOWN',f'1621 {other}-',
            f'1630 {other}A',f'1631 {other}-',f'1640 {key}START',f'1641 {key}-',f'1660 {other}START',f'1661 {other}-']
        if a.owner==1:route += ['1624 2:DOWN','1625 2:-']
    if a.withdraw_guard:route=boot+['1500 2:SELECT','1589 2:SELECT+START','1590 2:-']
    pokes=[]
    for seat in (1,2):
        for address,value in {0x460:0,0x480:128 if seat==1 else 104,0x600:148 if seat==1 else 180,0x400:0,0x440:0,0x420:64,
            0x640:0,0x680:0,0x660:0,0x55:255,0x6A:20 if seat==1 else 24,0x6F:7 if seat==1 else 19}.items():
            pokes+=['--coop-test-poke',f'1300:{seat}:{address:X}:{value:X}']
    if a.withdraw_guard:pokes+=['--coop-test-poke','1490:2:55:FF']
    def run(name,extra=(),reference=False):
        path=a.out/(name+'.txt')
        lines=boot+['1400 START','1401 -',f'1300 POKE 006F {7 if a.owner==1 else 19:02X}'] if reference else route
        if reference and a.owner==1:lines+=['1420 DOWN','1421 -']
        path.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
        args=[str(a.exe),str(a.rom.resolve()),'--frames','1430' if reference else '1650' if a.withdraw_guard else '1680' if a.reopen else '1520','--input',str(path),
            '--protoman','on' if reference and a.owner==2 else 'off','--coop','off' if reference else 'on',
            '--charge','off','--slide','off','--weapon-cycle','on',
            '--widescreen','fit' if a.fit else 'off','--widescreen-enemies','classic','--present-size','1280x720',
            '--no-save','--config',str(a.out/'test.ini'),*([] if reference else pokes),
            '--coop-report',str(a.out/(name+'.jsonl')),'--hash-out',str(a.out/(name+'.hash')),
            '--present-out',str(a.out/(name+'.png')),'--screenshot',str(a.out/(name+'-native.png')),
            '--wav-out',str(a.out/(name+'.wav')),*extra]
        with (a.out/(name+'.log')).open('w') as log:subprocess.run(args,cwd=a.exe.parent,env=env,check=True,
            stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        return {r['frame']:r for r in map(json.loads,(a.out/(name+'.jsonl')).read_text().splitlines())}
    if a.withdraw_guard:
        rows=run('guard');final=rows[1650]
        assert not final['menu'] and final['menu_opens']==[0,0] and final['withdrawn']==2 and final['teleport'][1]==2,final
        assert final['menu_failures']==0 and final['failures']==0,final
        (a.out/'summary.json').write_text(json.dumps(dict(result='pass',withdrawal_start_does_not_open_peer_menu=True),indent=2)+'\n');return
    state=a.out/'menu.cycstate';rows=run('native',('--save-state',f'1449:{state}','--present-every','1429'))
    assert rows[1410]['menu']==1 and rows[1410]['menu_owner']==a.owner-1,rows[1410]
    choices=[3,0] if a.owner==1 else [0,5]
    assert rows[1475]['menu']==1 and rows[1475]['weapon']==choices and rows[1475]['menu_failures']==0,rows[1475]
    assert rows[1475]['x']==rows[1410]['x'] and rows[1475]['updates']==rows[1410]['updates']
    if a.reopen:
        assert rows[1610]['menu'] and rows[1610]['menu_owner']==(a.owner%2) and rows[1610]['cursor'][a.owner%2]==0,rows[1610]
        assert rows[1645]['menu'] and rows[1645]['menu_opens']==[1,1],rows[1645]
    final=rows[1680 if a.reopen else 1520]
    assert not final['menu'] and final['weapon']==([3,5] if a.reopen else choices) and final['hp']==[20,24],final
    assert [x[4] for x in final['ammo']]==[7,19] and final['menu_failures']==0 and final['failures']==0,final
    if a.smoke_only:print(json.dumps(dict(result='pass',shared_owner_inventory=True,owner=a.owner)));return
    run('reference',reference=True)
    width,height,expected=pixels(a.out/'reference.png');actual_width,_,actual=pixels(a.out/'native_01429.png')
    assert (width,height)==(426 if a.fit else 256,240) and actual_width==width
    offset=(width-256)//2
    different=sum(expected[y+32][(x+offset+128)*3:(x+offset+128)*3+3]!=actual[y+32][(x+offset+128)*3:(x+offset+128)*3+3]
        for y in range(160) for x in range(96))
    assert different==0,('Native menu panel differs',different)
    assert all(expected[y][(offset+120)*3:(offset+128)*3]==actual[y][(offset+120)*3:(offset+128)*3] for y in range(120,160)),'Menu backdrop retained player sprites'
    run('interpreter',('--interp-only',));assert (a.out/'native.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
    assert (a.out/'native.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
    run('resume',('--load-state',str(state)))
    tail=[s for s in (a.out/'native.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1449]
    assert tail==(a.out/'resume.hash').read_text().splitlines();assert (a.out/'native.png').read_bytes()==(a.out/'resume.png').read_bytes()
    def pcm(name):
        with wave.open(str(a.out/(name+'.wav')),'rb') as f:return f.readframes(f.getnframes())
    continued=pcm('resume');assert continued and pcm('native')[-len(continued):]==continued
    summary=dict(result='pass',owner=a.owner,fit=a.fit,reopen=a.reopen,native_panel_pixels_exact=15360,shared_view_owner_inventory_and_close=True,
        world_frozen=True,original_background_without_player_sprites=True,own_health_ammo_retained=True,current_state_pcm_exact=True,native_interpreter_exact=True)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
