"""Actual B holds/releases, donor art, native/interpreter and charge states."""
import argparse,json,os,pathlib,subprocess,wave,struct,zlib

def red_body_pixels(path):
    blob=path.read_bytes();width,height=struct.unpack('>II',blob[16:24]);pos=8;compressed=b''
    while pos<len(blob):
        size=struct.unpack('>I',blob[pos:pos+4])[0]
        if blob[pos+4:pos+8]==b'IDAT':compressed+=blob[pos+8:pos+8+size]
        pos+=size+12
    raw=zlib.decompress(compressed);stride=width*3+1;count=0
    for y in range(130,min(185,height)):
        assert raw[y*stride]==0
        for x in range(95,125):
            r,g,b=raw[y*stride+1+x*3:y*stride+4+x*3]
            count+=r>80 and r>g*1.5 and r>b*2
    return count

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','donor','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--pcm',action='store_true')
    a=p.parse_args();a.exe=a.exe.resolve();a.rom=a.rom.resolve();a.donor=a.donor.resolve();a.out.mkdir(parents=True,exist_ok=True)
    route=['0 -','180 START','181 -','360 START','361 -']
    for slot in range(16,32):route.append(f'1400 POKE {0x600+slot:04X} F8')
    for addr,value in ((0x480,112),(0x460,0),(0x600,160),(0x420,64),(0x400,9),(0x640,0),(0x680,0),(0x660,0),(0x55,0x81),(0x6A,28)):
        route.append(f'1400 {"POKE" if addr==0x640 else "HOLD"} {addr:04X} {value:02X}')
    route+=['1420 B','1460 -','1500 B','1610 -']
    path=a.out/'input.txt';path.write_text('\n'.join(route)+'\n')
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    base=[str(a.exe),str(a.rom),'--input',str(path),'--mm4-rom',str(a.donor),'--charge','on',
        '--protoman','off','--widescreen','off','--weapon-cycle','on','--no-save','--config',str(a.out/'test.ini')]
    def run(name,frames,extra):
        args=base+['--frames',str(frames),'--charge-report',str(a.out/(name+'.jsonl')),
            '--hash-out',str(a.out/(name+'.hash')),'--screenshot',str(a.out/(name+'-native.png')),
            '--present-out',str(a.out/(name+'.png')),'--wav-out',str(a.out/(name+'.wav')),*extra]
        with (a.out/(name+'.log')).open('w') as log:
            result=subprocess.run(args,cwd=a.exe.parent,stdout=log,stderr=subprocess.STDOUT,env=env,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        assert result.returncode==0,(name,(a.out/(name+'.log')).read_text())
        return {r['frame']:r for r in map(json.loads,(a.out/(name+'.jsonl')).read_text().splitlines())}
    half=run('half',1470,[])
    assert half[1470]['half']==1 and half[1470]['full']==0,half[1470]
    assert any(half[1470]['kinds']),half[1470]
    assert (a.out/'half.png').read_bytes()!=(a.out/'half-native.png').read_bytes()
    state=a.out/'charging.cycstate'
    shot_state=a.out/'full-shot.cycstate'
    full=run('full',1620,['--save-state',f'1550:{state}','--save-state',f'1614:{shot_state}'])
    assert full[1620]['half']==1 and full[1620]['full']==1,full[1620]
    assert 2 in full[1620]['kinds'],full[1620]
    assert full[1551]['counter']>=32,full[1551]
    run('interpreter',1620,['--interp-only'])
    assert (a.out/'full.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
    assert (a.out/'full.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
    run('resume',1620,['--load-state',str(state)])
    tail=[s for s in (a.out/'full.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1550]
    assert (a.out/'resume.hash').read_text().splitlines()==tail
    assert (a.out/'full.png').read_bytes()==(a.out/'resume.png').read_bytes()
    def pcm(name):
        with wave.open(str(a.out/(name+'.wav')),'rb') as w:return w.readframes(w.getnframes())
    full_pcm,resume_pcm=pcm('full'),pcm('resume')
    assert full_pcm,'No audio samples produced'
    pcm_exact=full_pcm[-len(resume_pcm):]==resume_pcm
    if a.pcm:assert pcm_exact,'Audio continuation differs'
    run('shot-resume',1620,['--load-state',str(shot_state)])
    shot_tail=[s for s in (a.out/'full.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1614]
    assert (a.out/'shot-resume.hash').read_text().splitlines()==shot_tail
    assert (a.out/'full.png').read_bytes()==(a.out/'shot-resume.png').read_bytes()
    shot_pcm=pcm('shot-resume');shot_pcm_exact=full_pcm[-len(shot_pcm):]==shot_pcm
    if a.pcm:assert shot_pcm_exact,'Active-shot audio continuation differs'
    proto=run('protoman',1620,['--protoman','on'])
    assert proto[1620]['half']==1 and proto[1620]['full']==1,proto[1620]
    assert red_body_pixels(a.out/'protoman.png')>20,'Proto body disappeared while firing'
    menu_path=a.out/'menu-input.txt'
    menu_path.write_text('\n'.join(s for s in route if s not in ('1420 B','1460 -','1610 -'))+
        '\n1550 B+START\n1551 B\n1560 -\n1600 START\n1601 -\n')
    menu_report=a.out/'menu-weapons.jsonl'
    menu=run('menu-cancel',1660,['--input',str(menu_path),'--weapon-report',str(menu_report)])
    guards={r['frame']:r for r in map(json.loads,menu_report.read_text().splitlines())}
    assert not guards[1555]['safe'] and guards[1660]['safe'],(guards[1555],guards[1660])
    assert menu[1660]['counter']==0 and menu[1660]['half']==0 and menu[1660]['full']==0,menu[1660]
    summary=dict(result='pass',native_half_and_full=True,protoman_shares_ability=True,
        native_interpreter_exact=True,current_build_charge_continuation=len(tail),presented_pixels_exact=True,
        pcm_continuation_exact=pcm_exact,pcm_qualification_required=a.pcm)
    summary.update(active_shot_continuation=len(shot_tail),active_shot_pcm_exact=shot_pcm_exact)
    summary.update(native_menu_cancels_charge=True,air_fire_body_visible=True)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
