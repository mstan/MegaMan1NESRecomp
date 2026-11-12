"""Real NMI continuations in native guest collision/projectile scopes."""
import argparse,json,os,pathlib,shutil,subprocess,sys,wave
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'protoman'))
from validate import pixels

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--slowdown-gate',action='store_true',help='Compare the existing dense native workload with fresh catalog CPU timing')
    p.add_argument('--flicker-gate',action='store_true',help='Compare complete native art packets with the original OAM-limited scene')
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    lines=['0 -','0 2:-','180 START','181 -','360 START','361 -','1490 POKE 005D FE',
        '1490 POKE 005F 05','1490 POKE 006F 1C','1490 POKE 0055 81']
    pose={0x480:32,0x460:0,0x600:180,0x420:64,0x400:0,0x440:0,0x640:0,
        0x680:0,0x660:0,0x4A0:0,0x620:0}
    for address,value in pose.items():lines.append(f'1490 POKE {address:04X} {value:02X}')
    # Authored density precondition; execution and the NMI remain native.
    for slot in range(16,32):
        actor={0x400:255,0x420:6,0x440:0,0x460:0,0x480:176,0x4A0:0,0x4C0:0,0x4E0:0,
            0x600:176,0x620:0,0x640:127,0x660:0,0x680:0,0x6A0:1,0x6C0:255,0x6E0:0x21}
        if a.flicker_gate:actor.update({0x480:48+(slot-16)%4*48,0x600:48+(slot-16)//4*40})
        for address,value in actor.items():lines.append(f'1490 HOLD {address+slot:04X} {value:02X}')
    if not a.flicker_gate:
        for frame in range(1500,1600,30):lines += [f'{frame} B',f'{frame} 2:B',f'{frame+1} -',f'{frame+1} 2:-']
        lines+=['1543 X2:R','1544 X2:-']
    route=a.out/'input.txt';route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
    pose.update({0x480:64,0x55:0x81,0x5F:5,0x6F:28});pokes=[]
    for address,value in pose.items():pokes+=['--coop-test-poke',f'1490:2:{address:X}:{value:X}']
    base=[str(a.exe),str(a.rom.resolve()),'--frames','1600','--coop','on','--weapon-cycle','on','--charge','off',
        '--widescreen','fit','--widescreen-enemies','classic','--present-size','1280x720','--input',str(route),
        *pokes,'--slowdown','off','--no-flicker','off','--no-save','--config',str(a.out/'test.ini')]
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    def run(name,extra=()):
        with (a.out/(name+'.log')).open('w') as log:
            subprocess.run(base+['--coop-report',str(a.out/(name+'.jsonl')),'--hash-out',str(a.out/(name+'.hash')),
                '--slowdown-report',str(a.out/(name+'-timing.jsonl')),
                '--flicker-report',str(a.out/(name+'-sprites.jsonl')),'--screenshot',str(a.out/(name+'-native.png')),
                '--present-out',str(a.out/(name+'.png')),'--wav-out',str(a.out/(name+'.wav')),*extra],
                cwd=a.exe.parent,env=env,check=True,stdout=log,stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
    states=[a.out/'collision.cycstate',a.out/'pool.cycstate']
    run('native',['--save-state',f'1530:{states[0]}','--save-state',f'1542:{states[1]}','--present-every','1530'])
    if a.flicker_gate:
        base[base.index('--no-flicker')+1]='on';state=a.out/'complete.cycstate'
        catalog=a.out/'fresh-mods';package='packages/mega-man-usa.enhancement.no-flicker/1.0.0/manifest.toml'
        manifest=catalog/package;manifest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(a.exe.parent/'mods'/package,manifest)
        index=base.index('--no-flicker');catalog_base=[v for i,v in enumerate(base) if i not in (index,index+1)]
        with (a.out/'catalog.log').open('w') as log:subprocess.run(catalog_base+['--mods-root',str(catalog),'--frames','2',
            '--flicker-report',str(a.out/'catalog-sprites.jsonl')],cwd=a.exe.parent,env=env,check=True,
            stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
        assert all(r['on']==1 for r in map(json.loads,(a.out/'catalog-sprites.jsonl').read_text().splitlines()))
        run('complete',['--save-state',f'1530:{state}','--present-every','1530'])
        packets=list(map(json.loads,(a.out/'complete-sprites.jsonl').read_text().splitlines()))
        last=packets[-1];assert last['ready'] and any(r['overlays'] for r in packets if r['frame']>1490),last
        fullest=max(r['native_parts'][r['anchor']]+r['hud'][r['anchor']] for r in packets)
        assert fullest>64,('The native scene did not exceed its physical OAM capacity',fullest)
        assert (a.out/'native.hash').read_bytes()==(a.out/'complete.hash').read_bytes(),'Native simulation changed'
        assert (a.out/'native-native.png').read_bytes()==(a.out/'complete-native.png').read_bytes(),'Physical OAM picture changed'
        assert (a.out/'native.png').read_bytes()!=(a.out/'complete.png').read_bytes(),'No visible completion in the dense scene'
        width,height,before=pixels(a.out/'native.png');_,_,complete=pixels(a.out/'complete.png');origin=(width-256)//2
        assert all(before[y][:32*3]==complete[y][:32*3] for y in range(16,80)),'Wide health bars moved'
        def differences(slot):
            x=48+(slot-16)%4*48+origin;y=48+(slot-16)//4*40
            return sum(before[py][px*3:px*3+3]!=complete[py][px*3:px*3+3] for py in range(y-16,y+16) for px in range(x-16,x+16))
        missing=max(range(16,32),key=differences);assert differences(missing)>0,'Only unrelated pixels changed'
        def pcm(name):
            with wave.open(str(a.out/(name+'.wav')),'rb') as f:return f.readframes(f.getnframes())
        assert pcm('native')==pcm('complete'),'Native audio changed'
        run('instant',['--load-state',str(state),'--frames','1531'])
        assert (a.out/'instant.png').read_bytes()==(a.out/'complete_01530.png').read_bytes()
        run('resume',['--load-state',str(state)])
        tail=[s for s in (a.out/'complete.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1530]
        assert tail==(a.out/'resume.hash').read_text().splitlines()
        assert (a.out/'complete.png').read_bytes()==(a.out/'resume.png').read_bytes()
        continued=pcm('resume');assert continued and pcm('complete')[-len(continued):]==continued
        run('interpreter',['--interp-only'])
        assert (a.out/'complete.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
        assert (a.out/'complete.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
        assert pcm('complete')==pcm('interpreter')
        # One original actor drawn without the crowded pool is an independent
        # native pixel oracle for the object the full page omitted or cut short.
        original_route=route.read_text();reference=[]
        for line in lines:
            fields=line.split();addr=int(fields[2],16) if len(fields)>3 and fields[1] in ('POKE','HOLD') else -1
            if 0x400<=addr<0x500 or 0x600<=addr<0x700:
                slot=addr&31
                if slot>=16 and slot!=missing:continue
            reference.append(line)
        for slot in range(16,32):
            if slot!=missing:reference.append(f'1490 HOLD {0x600+slot:04X} F8')
        route.write_text('\n'.join(sorted(reference,key=lambda s:int(s.split()[0])))+'\n')
        base[base.index('--no-flicker')+1]='off'
        run('reference');route.write_text(original_route)
        _,_,expected=pixels(a.out/'reference.png');x=48+(missing-16)%4*48+origin;y=48+(missing-16)//4*40
        different=sum(expected[py][px*3:px*3+3]!=complete[py][px*3:px*3+3] for py in range(y-16,y+16) for px in range(x-16,x+16))
        assert different==0,('Completed actor differs from its original native image',missing,different)
        summary=dict(result='pass',native_scene_parts=fullest,physical_capacity=64,native_simulation_oam_audio_unchanged=True,
            actual_visible_completion=True,native_oracle_actor=missing,native_oracle_pixels_exact=1024,
            native_interpreter_exact=True,current_state_pixels_pcm_exact=True)
        (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary));return
    if a.slowdown_gate:
        index=base.index('--slowdown');base[index+1]='on'
        catalog=a.out/'fresh-mods';package='packages/mega-man-usa.enhancement.slowdown/1.0.0/manifest.toml'
        manifest=catalog/package;manifest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(a.exe.parent/'mods'/package,manifest)
        # Headless runs activate no catalog without --mods-root. Use a fresh
        # disposable catalog containing the actual staged package to prove its
        # default separately from the explicit timing A/B in the dense scene.
        catalog_base=[v for i,v in enumerate(base) if i not in (index,index+1)]
        with (a.out/'catalog.log').open('w') as log:subprocess.run(catalog_base+['--mods-root',str(catalog),
            '--frames','2','--slowdown-report',str(a.out/'catalog-timing.jsonl')],cwd=a.exe.parent,env=env,
            check=True,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
        assert all(r['selected']==r['lines']==262 for r in map(json.loads,(a.out/'catalog-timing.jsonl').read_text().splitlines()))
        boosted=a.out/'boosted.cycstate'
        run('default',['--save-state',f'1530:{boosted}','--present-every','1530'])
        timings={name:[r for r in map(json.loads,(a.out/(name+'-timing.jsonl')).read_text().splitlines())
            if 1500<r['frame']<=1600] for name in ('native','default')}
        lag={name:sum(r['wait']!=0 for r in rows) for name,rows in timings.items()}
        assert len(timings['native'])==len(timings['default'])==100 and lag['default']<lag['native'],lag
        assert all(r['lines']==r['selected']==262 for r in timings['default']),timings['default'][-1]
        default_rows=list(map(json.loads,(a.out/'default.jsonl').read_text().splitlines()))
        assert default_rows[-1]['failures']==0 and default_rows[-1]['hp'][0]>0 and default_rows[-1]['hp'][1]>0,default_rows[-1]
        run('instant',['--load-state',str(boosted),'--frames','1531'])
        assert (a.out/'instant.png').read_bytes()==(a.out/'default_01530.png').read_bytes()
        run('resume',['--load-state',str(boosted)])
        tail=[s for s in (a.out/'default.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1530]
        assert tail==(a.out/'resume.hash').read_text().splitlines()
        assert (a.out/'default.png').read_bytes()==(a.out/'resume.png').read_bytes()
        def pcm(name):
            with wave.open(str(a.out/(name+'.wav')),'rb') as f:return f.readframes(f.getnframes())
        continued=pcm('resume');assert continued and pcm('default')[-len(continued):]==continued
        run('interpreter',['--interp-only'])
        assert (a.out/'default.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
        assert (a.out/'default.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
        assert pcm('default')==pcm('interpreter')
        assert abs(len(pcm('native'))-len(pcm('default')))<=4,'CPU budget changed the audio sample rate'
        summary=dict(result='pass',authored_native_density_precondition=True,stock_lag_frames=lag['native'],
            enhanced_lag_frames=lag['default'],observed_frames=100,default_catalog_lines=262,
            native_interpreter_exact=True,current_state_hash_pixels_pcm_exact=True,audio_sample_rate_preserved=True)
        (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary));return
    rows={r['frame']:r for r in map(json.loads,(a.out/'native.jsonl').read_text().splitlines())}
    assert rows[1531]['phase']==2 and rows[1543]['phase']==3,(rows[1531],rows[1543])
    assert rows[1531]['owner']==rows[1543]['owner']==1 and rows[1600]['failures']==0
    assert rows[1600]['weapon']==[5,6],rows[1600]
    def pcm(path):
        with wave.open(str(path),'rb') as f:return f.readframes(f.getnframes())
    original=pcm(a.out/'native.wav')
    for name,state,frame in zip(('collision','pool'),states,(1530,1542)):
        run(name,['--load-state',str(state),'--present-every','1542'])
        tail=[s for s in (a.out/'native.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>frame]
        assert (a.out/(name+'.hash')).read_text().splitlines()==tail
        assert (a.out/(name+'.png')).read_bytes()==(a.out/'native.png').read_bytes()
        resumed=pcm(a.out/(name+'.wav'));assert resumed and original[-len(resumed):]==resumed
    # No frame is simulated: inspect the restored, in-flight pool picture.
    run('pool-picture',['--load-state',str(states[1]),'--frames','1543'])
    assert (a.out/'pool-picture.png').read_bytes()==(a.out/'collision_01542.png').read_bytes()
    for name in ('native_01530.png','pool-picture.png'):
        width,height,rgb=pixels(a.out/name);assert (width,height)==(426,240)
        colors={rgb[y][x*3:x*3+3] for y in range(24,220) for x in range(341,426)}
        assert len(colors)>5,(name,len(colors))
    run('interpreter',['--interp-only','--present-every','1530'])
    assert (a.out/'native.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
    assert (a.out/'native.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
    assert (a.out/'native_01530.png').read_bytes()==(a.out/'interpreter_01530.png').read_bytes()
    summary=dict(result='pass',injected_wait=False,native_collision_nmi=True,native_pool_nmi=True,
        current_state_hash_pixels_pcm_exact=True,fit_margins_during_both_scopes=True,
        personal_lr_deferred_across_native_nmi=True,native_interpreter_exact=True)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
