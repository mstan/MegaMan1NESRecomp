"""Both native arrival bodies, exact guest Mega pixels and unchanged READY."""
import argparse,json,os,pathlib,subprocess,sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'protoman'))
from validate import pixels

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    route=a.out/'input.txt';route.write_text('0 -\n0 2:-\n180 START\n181 -\n360 START\n361 -\n')
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    base=[str(a.exe),str(a.rom.resolve()),'--input',str(route),'--coop','on','--coop-p1','protoman',
        '--widescreen','off','--charge','off','--no-save','--config',str(a.out/'test.ini')]
    def run(name,frames,extra=()):
        with (a.out/(name+'.log')).open('w') as log:
            subprocess.run(base+['--frames',str(frames),'--coop-report',str(a.out/(name+'.jsonl')),
                '--screenshot',str(a.out/(name+'-native.png')),'--present-out',str(a.out/(name+'.png')),*extra],
                cwd=a.exe.parent,env=env,check=True,stdout=log,stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        return {r['frame']:r for r in map(json.loads,(a.out/(name+'.jsonl')).read_text().splitlines())}
    saves=[]
    for frame in (970,972,975,984):saves+=['--save-state',f'{frame}:{a.out/f"arrival-{frame}.cycstate"}']
    rows=run('producer',1010,saves)
    options=[f for f in (970,972,975) if rows[f+1]['intro'] and rows[f+1]['picture']]
    assert options,[(f,rows[f+1]) for f in (970,972,975)]
    frame=options[0];state=a.out/f'arrival-{frame}.cycstate';run('arrival',frame+1,('--load-state',str(state)))
    _,_,raw=pixels(a.out/'arrival-native.png');_,_,actual=pixels(a.out/'arrival.png')
    # Both rectangles are sky. Exclude the score's own native sprite rows.
    sky=raw[0][:3];visible=0
    for y in range(60):
        if 16<=y<25:continue
        assert raw[y][116*3:140*3]==actual[y][92*3:116*3],y
        visible+=sum(raw[y][x*3:x*3+3]!=sky for x in range(116,140))
    assert visible>5,visible
    run('fit',frame+1,('--widescreen','fit','--widescreen-enemies','classic','--present-size','1280x720'))
    width,height,wide=pixels(a.out/'fit.png');assert (width,height)==(426,240)
    assert all(wide[y][(85+90)*3:(85+142)*3]==actual[y][90*3:142*3] for y in range(60))
    ready=run('ready',850);assert not ready[850]['ready'] and not ready[850]['intro']
    assert (a.out/'ready-native.png').read_bytes()==(a.out/'ready.png').read_bytes()
    run('opening',985,('--load-state',str(a.out/'arrival-984.cycstate')))
    reference=a.out/'reference-input.txt';reference.write_text(route.read_text()+'850 POKE 0480 68\n')
    run('opening-reference',985,('--input',str(reference),'--coop','off','--protoman','off'))
    _,_,opening=pixels(a.out/'opening.png');_,_,expected=pixels(a.out/'opening-reference-native.png')
    assert all(opening[y][92*3:116*3]==expected[y][92*3:116*3] for y in range(128,164)),'Native partner opening differs'
    summary=dict(result='pass',native_guest_mega_arrival_pixels_exact=True,arrival_opaque_pixels=visible,
        first_arrival_frame=frame,fit_native_grid_exact=True,ready_text_preserved=True,initial_state_picture_exact=True,
        native_guest_opening_pixels_exact=True)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
