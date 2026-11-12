"""Actual aerial fire origins and shown body packets in solo and private P2."""
import argparse,json,os,pathlib,subprocess,sys,wave
from validate import pixels

def wave_data(path):
    with wave.open(str(path),'rb') as f:return f.readframes(f.getnframes())

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--reference',type=pathlib.Path)
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    cases=[('solo-right',False,True,'air'),('solo-left',False,False,'air'),
        ('guest-right',True,True,'air'),('guest-left',True,False,'air'),
        ('guest-ladder',True,True,'ladder'),('guest-special',True,True,'special')]
    observed=[]
    for label,coop,right,kind in cases:
        folder=a.out/label;folder.mkdir(exist_ok=True);seat=1 if coop else 0;key='2:' if coop else ''
        pose={0x460:0,0x480:64,0x600:156 if kind=='ladder' else 56,0x400:21 if kind=='ladder' else 9,
            0x440:0,0x420:(64 if right else 0)|(16 if kind=='ladder' else 0),0x640:0,
            0x680:0,0x660:0,0x4A0:0,0x620:0,0x55:0,0x30:12 if kind=='ladder' else 0}
        lines=['0 -','0 2:-','180 START','181 -','360 START','361 -',f'1500 {key}B',f'1501 {key}-']
        for slot in range(16,32):lines.append(f'1495 POKE {0x600+slot:04X} F8')
        pokes=[]
        if coop:
            for address,value in pose.items():pokes+=['--coop-test-poke',f'1495:2:{address:X}:{value:X}']
            for address,value in {**pose,0x480:16,0x600:180,0x400:0,0x420:64,0x30:0}.items():
                pokes+=['--coop-test-poke',f'1495:1:{address:X}:{value:X}']
        else:lines += [f'1495 POKE {address:04X} {value:02X}' for address,value in pose.items()]
        if kind=='special':
            lines+=['1300 POKE 005D FE']
            for n in range(5):lines += [f'{1440+n*12} X2:R',f'{1441+n*12} X2:-']
        route=folder/'input.txt';route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
        base=[str(a.exe),str(a.rom.resolve()),'--frames','1505','--input',str(route),'--protoman','off' if coop else 'on',
            '--coop','on' if coop else 'off','--charge','off','--slide','off','--weapon-cycle','on',
            '--widescreen','off','--no-save','--config',str(folder/'test.ini'),*pokes]
        def run(name,extra=()):
            with (folder/(name+'.log')).open('w') as log:subprocess.run(base+[
                '--coop-report',str(folder/(name+'.jsonl')),'--hash-out',str(folder/(name+'.hash')),
                '--present-out',str(folder/(name+'.png')),'--screenshot',str(folder/(name+'-native.png')),
                '--wav-out',str(folder/(name+'.wav')),*extra],cwd=a.exe.parent,env=env,check=True,
                stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
            return {r['frame']:r for r in map(json.loads,(folder/(name+'.jsonl')).read_text().splitlines())}
        state=folder/'fire.cycstate';rows=run('native',('--save-state',f'1503:{state}','--present-every','1503'))
        final=rows[1505]['special'][seat];assert rows[1505]['failures']==0,rows[1505]
        if kind=='air':
            assert final['body'][0]==9 and final['body'][1]>>4==1,final
            assert final['art']['pose']==9 and final['art']['mask'] and final['art']['right']==right,final
        birth=next((r['special'][seat] for f,r in rows.items() if f>=1501 and r['special'][seat]['buster'][0][2]<248),None)
        if kind!='special':
            assert birth and birth['buster'][0][0]==0x1A,(label,birth)
            if kind=='ladder':assert birth['buster'][0][2]-birth['body'][4]==-4,(label,birth)
        else:assert final['actor'][0]==0x5B and rows[1505]['weapon'][seat]==1,final
        shifted=0
        if a.reference:
            ref=a.reference/label;before={r['frame']:r for r in map(json.loads,(ref/'native.jsonl').read_text().splitlines())}
            assert all(rows[f]['x']==before[f]['x'] and rows[f]['y']==before[f]['y'] for f in range(1496,1506)),'Body trajectory changed'
            assert wave_data(folder/'native.wav')==wave_data(ref/'native.wav'),('Audio changed',label)
            if kind=='air':
                old_birth=next(r['special'][seat] for f,r in before.items() if f>=1501 and r['special'][seat]['buster'][0][2]<248)
                assert birth['buster'][0][2]==old_birth['buster'][0][2]+8,(label,old_birth,birth)
                assert birth['buster'][0][1]==old_birth['buster'][0][1]
            if coop and kind=='air':
                _,_,native=pixels(ref/'native-native.png');_,_,old=pixels(ref/'native.png');_,_,actual=pixels(folder/'native.png')
                bg=bytes((54,201,255));xs=range(40,88);ys=range(32,96)
                assert {native[y][x*3:x*3+3] for y in ys for x in xs}=={bg},'Oracle needs the original flat sky backdrop'
                for y in ys:
                    for x in xs:
                        expect=old[y-8][x*3:x*3+3] if y-8 in ys else bg
                        assert actual[y][x*3:x*3+3]==expect,(label,x,y)
                        shifted+=expect!=bg
                assert shifted>150,(label,shifted)
            elif kind!='air':assert pixels(folder/'native.png')==pixels(ref/'native.png'),label
            if label in ('guest-right','solo-left'):
                run('interpreter',('--interp-only',));assert (folder/'native.hash').read_bytes()==(folder/'interpreter.hash').read_bytes()
                assert (folder/'native.png').read_bytes()==(folder/'interpreter.png').read_bytes()
                run('instant',('--load-state',str(state),'--frames','1504'))
                assert (folder/'instant.png').read_bytes()==(folder/'native_01503.png').read_bytes()
                run('resume',('--load-state',str(state)))
                tail=[s for s in (folder/'native.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1503]
                assert tail==(folder/'resume.hash').read_text().splitlines();assert (folder/'native.png').read_bytes()==(folder/'resume.png').read_bytes()
                continued=wave_data(folder/'resume.wav');assert continued and wave_data(folder/'native.wav')[-len(continued):]==continued
        observed.append(dict(case=label,body=final['body'],art=final['art'],native_birth= birth,
            shifted_visible_pixels=shifted))
    summary=dict(result='pass',phase='after' if a.reference else 'before',actual_cases=observed)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
