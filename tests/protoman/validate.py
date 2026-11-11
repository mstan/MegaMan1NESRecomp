"""Exercise real Select swaps and native presentation/state continuation."""
import argparse,json,os,pathlib,subprocess,struct,zlib

def pixels(path):
    data=path.read_bytes();w,h=struct.unpack('>II',data[16:24]);idat=b'';pos=8
    while pos<len(data):
        n=struct.unpack('>I',data[pos:pos+4])[0]
        if data[pos+4:pos+8]==b'IDAT':idat+=data[pos+8:pos+8+n]
        pos+=n+12
    raw=zlib.decompress(idat);stride=w*3+1
    assert data[24:26]==bytes([8,2]) and all(raw[y*stride]==0 for y in range(h))
    return w,h,[raw[y*stride+1:(y+1)*stride] for y in range(h)]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    a=p.parse_args();a.rom=a.rom.resolve();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    route=a.out/'input.txt'
    route.write_text('\n'.join(['0 -','180 START','181 -','360 START','361 -',
        '500 SELECT','501 -','1400 HOLD 0055 81','1400 HOLD 006A 1C',
        '1408 SELECT','1409 -','1470 SELECT','1480 -',
        '1560 RIGHT','1590 LEFT','1620 -'])+'\n')
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    base=[str(a.exe),str(a.rom),'--frames','1700','--input',str(route),'--protoman','on',
          '--widescreen','off','--weapon-cycle','off','--no-save','--config',str(a.out/'test.ini')]
    def run(name,extra):
        with (a.out/(name+'.log')).open('w') as log:
            result=subprocess.run(base+extra,cwd=a.exe.parent,stdout=log,stderr=subprocess.STDOUT,
                env=env,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        assert result.returncode==0,(name,(a.out/(name+'.log')).read_text())
    state=a.out/'teleport.cycstate'
    run('native',['--hash-out',str(a.out/'native.hash'),'--proto-report',str(a.out/'proto.jsonl'),
        '--save-state',f'1477:{state}','--screenshot',str(a.out/'native.png'),
        '--present-out',str(a.out/'presented.png')])
    rows={r['frame']:r for r in map(json.loads,(a.out/'proto.jsonl').read_text().splitlines())}
    for frame,selected,swaps in ((1100,1,0),(1460,0,1),(1540,1,2),(1700,1,2)):
        row=rows[frame];assert (row['selected'],row['swaps'],row['teleport'])==(selected,swaps,0),row
    assert any(r['mask'] for r in rows.values()),'No actual native body OAM ownership'
    assert rows[1478]['teleport'],rows[1478]
    run('interpreter',['--interp-only','--hash-out',str(a.out/'interpreter.hash'),
        '--present-out',str(a.out/'interpreter.png')])
    assert (a.out/'native.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
    assert (a.out/'presented.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
    run('resume',['--load-state',str(state),'--hash-out',str(a.out/'resume.hash'),
        '--present-out',str(a.out/'resume.png')])
    tail=[s for s in (a.out/'native.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1477]
    assert (a.out/'resume.hash').read_text().splitlines()==tail
    assert (a.out/'presented.png').read_bytes()==(a.out/'resume.png').read_bytes()
    assert (a.out/'native.png').read_bytes()!=(a.out/'presented.png').read_bytes(),'Native art was not replaced'
    run('fit',['--widescreen','fit','--widescreen-enemies','classic','--present-size','1280x720',
        '--hash-out',str(a.out/'fit.hash'),'--present-out',str(a.out/'fit.png')])
    assert (a.out/'fit.hash').read_bytes()==(a.out/'native.hash').read_bytes()
    w,h,wide=pixels(a.out/'fit.png');_,_,stock=pixels(a.out/'presented.png')
    assert (w,h)==(426,240),(w,h)
    origin=(w-256)//2
    assert all(wide[y][(origin+85)*3:(origin+140)*3]==stock[y][85*3:140*3] for y in range(145,200))
    run('native-select',['--protoman','off','--proto-report',str(a.out/'native-select.jsonl')])
    pause={r['frame']:r['native_pause'] for r in map(json.loads,(a.out/'native-select.jsonl').read_text().splitlines())}
    assert pause[1450]==1 and pause[1540]==0,(pause[1450],pause[1540])
    summary=dict(result='pass',swaps=2,ready_select_discarded=True,native_interpreter_exact=True,
        current_build_teleport_continuation=len(tail),presented_pixels_exact=True,
        fit_width=w,fit_body_pixels_exact=True,native_select_pause_preserved=True,
        observed_pose_ids=sorted({r['pose'] for r in rows.values() if r['mask']}))
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
