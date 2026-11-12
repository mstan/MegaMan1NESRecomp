"""Real character dependency and staged co-op catalog choices."""
import argparse,json,os,pathlib,shutil,subprocess,wave

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','exe','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--baseline',action='store_true')
    a=p.parse_args();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
    route=a.out/'input.txt';lines=['0 -','0 2:-','180 START','181 -','360 START','361 -','1495 POKE 0055 00']
    for slot in range(16,32):lines.append(f'1490 POKE {0x600+slot:04X} F8')
    route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
    base=[str(a.exe),str(a.rom.resolve()),'--frames','1505','--input',str(route),'--no-save','--config',str(a.out/'test.ini'),
        '--widescreen','off','--charge','off','--slide','off','--no-flicker','on']
    def run(name,options):
        with (a.out/(name+'.log')).open('w') as log:subprocess.run(base+list(options)+['--coop-report',str(a.out/(name+'.jsonl')),
            '--hash-out',str(a.out/(name+'.hash')),'--present-out',str(a.out/(name+'.png')),'--wav-out',str(a.out/(name+'.wav'))],
            cwd=a.exe.parent,env=env,check=True,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
        rows=list(map(json.loads,(a.out/(name+'.jsonl')).read_text().splitlines()))
        if rows:assert not rows[-1]['failures'],rows[-1]
        return rows
    cases=[('proto-first',('--protoman','off','--coop','on'),True,1),
        ('coop-first',('--coop','on','--protoman','off'),True,1),
        ('disable',('--protoman','off','--coop','on','--coop','off'),False,0),
        ('retain-solo',('--protoman','on','--coop','on','--coop','off'),True,0)]
    observed={}
    for name,options,wanted,seat in cases:
        rows=run(name,options);art=any(r['special'][seat]['art']['mask'] for r in rows if r['frame']>=1496)
        observed[name]=art
        if not a.baseline:assert art==wanted,(name,art,wanted)
    if a.baseline:
        assert observed=={'proto-first':True,'coop-first':False,'disable':True,'retain-solo':True},observed
        (a.out/'summary.json').write_text(json.dumps(dict(result='observed',character_art=observed),indent=2)+'\n');print(observed);return
    catalog=a.out/'mods';shutil.copytree(a.exe.parent/'mods'/'packages',catalog/'packages',dirs_exist_ok=True)
    (catalog/'state.toml').write_text('''format_version = 1
[[feature]]
package_id = "mega-man-usa.character.protoman"
id = "protoman"
enabled = true
[[feature]]
package_id = "mega-man-usa.coop"
id = "coop"
enabled = true
[feature.values]
coop-view = "split"
player-one = "protoman"
wait-both = "on"
''')
    state=a.out/'catalog.cycstate';options=['--mods-root',str(catalog),'--slowdown-report',str(a.out/'timing.jsonl')]
    # Explicit off switches above test CLI ordering. Omit them for the actual
    # staged catalog, whose fresh defaults include both display enhancements.
    index=base.index('--no-flicker');del base[index:index+2]
    rows=run('catalog',(*options,'--save-state',f'1499:{state}','--present-every','1499'))
    assert rows[-1]['ready'] and rows[-1]['wait_both']==1,rows[-1]
    assert any(r['special'][0]['art']['mask'] for r in rows if r['frame']>=1496),rows[-1]
    assert not any(r['special'][1]['art']['mask'] for r in rows if r['frame']>=1496),rows[-1]
    assert all(r['lines']==262 for r in map(json.loads,(a.out/'timing.jsonl').read_text().splitlines()))
    # The PNG header is sufficient to check the real fixed-width split layout.
    import struct
    assert struct.unpack('>II',(a.out/'catalog.png').read_bytes()[16:24])==(512,240)
    run('interpreter',(*options,'--interp-only'))
    assert (a.out/'catalog.hash').read_bytes()==(a.out/'interpreter.hash').read_bytes()
    assert (a.out/'catalog.png').read_bytes()==(a.out/'interpreter.png').read_bytes()
    run('instant',(*options,'--load-state',str(state),'--frames','1500'))
    assert (a.out/'instant.png').read_bytes()==(a.out/'catalog_01499.png').read_bytes()
    run('resume',(*options,'--load-state',str(state)))
    tail=[s for s in (a.out/'catalog.hash').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>1499]
    assert tail==(a.out/'resume.hash').read_text().splitlines()
    assert (a.out/'catalog.png').read_bytes()==(a.out/'resume.png').read_bytes()
    def pcm(name):
        with wave.open(str(a.out/(name+'.wav')),'rb') as f:return f.readframes(f.getnframes())
    continued=pcm('resume');assert continued and pcm('catalog')[-len(continued):]==continued
    summary=dict(result='pass',character_art=observed,staged_catalog_choices=True,reverse_roster=True,
        native_split_width=512,wait_both=True,default_cpu_lines=262,native_interpreter_exact=True,current_state_pixels_pcm_exact=True)
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
