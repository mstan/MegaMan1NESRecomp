"""Stock parity, opt-in package activation and real-stage widescreen smoke tests.

python tests/widescreen/validate.py --rom <original ROM> [--exe <cycle build>]
Native processes are hidden on Windows. Evidence stays in wide-evidence/regression.
"""
import argparse,json,pathlib,shutil,struct,subprocess

REPO=pathlib.Path(__file__).resolve().parents[2]
TARGET='MegaManRecomp.exe'
PACKAGE='mega-man.enhancement.widescreen'

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--rom',type=pathlib.Path,required=True)
 p.add_argument('--exe',type=pathlib.Path,default=REPO/'build-wide/Release'/TARGET)
 p.add_argument('--out',type=pathlib.Path,default=REPO/'wide-evidence/regression')
 a=p.parse_args();a.rom=a.rom.resolve();a.exe=a.exe.resolve();a.out.mkdir(parents=True,exist_ok=True)
 route=pathlib.Path(__file__).with_name('route.txt');options={}
 if hasattr(subprocess,'STARTUPINFO'):
  info=subprocess.STARTUPINFO();info.dwFlags|=subprocess.STARTF_USESHOWWINDOW;info.wShowWindow=0
  options=dict(startupinfo=info,creationflags=subprocess.CREATE_NO_WINDOW)
 def run(label,extra):
  args=[str(a.exe),str(a.rom),'--frames','1800','--input',str(route),'--hash-out',str(a.out/(label+'.hash')),
        '--screenshot',str(a.out/(label+'-native.png')),'--present-out',str(a.out/(label+'-wide.png')),
        '--widescreen-report',str(a.out/(label+'.jsonl')),'--no-save',
        '--config',str(a.out/(label+'.ini')),*map(str,extra)]
  with (a.out/(label+'.log')).open('w') as log:
   result=subprocess.run(args,cwd=a.exe.parent,stdout=log,stderr=subprocess.STDOUT,**options)
  assert result.returncode==0,(label,(a.out/(label+'.log')).read_text())
  rows=[json.loads(s) for s in (a.out/(label+'.jsonl')).read_text().splitlines()]
  assert all(r['failures']==0 and r['overflow']==0 for r in rows),(label,rows)
  return rows
 stock=run('stock',['--widescreen','off'])
 for name,args in [('interpreted',['--widescreen','off','--interp-only']),
                   ('classic',['--widescreen','32:9','--widescreen-enemies','classic'])]:
  run(name,args)
  assert (a.out/(name+'.hash')).read_bytes()==(a.out/'stock.hash').read_bytes(),name
 viewport=run('viewport',['--widescreen','32:9'])
 assert any(r['terrain_samples']>10000 and r['terrain_errors']==0 for r in viewport)
 assert viewport[-1]['spawns']>0,viewport
 catalog=a.out/'mods';shutil.copytree(REPO/'mods/preloaded',catalog,dirs_exist_ok=True)
 (catalog/'state.toml').write_text('')
 run('default',['--mods-root',catalog])
 assert (a.out/'default.hash').read_bytes()==(a.out/'stock.hash').read_bytes()
 (catalog/'state.toml').write_text(f'[[feature]]\npackage_id = "{PACKAGE}"\nid = "widescreen"\nenabled = true\n'
                                 '[feature.values]\naspect = "21-9"\nenemy_activation = "classic"\n')
 run('package',['--mods-root',catalog])
 assert (a.out/'package.hash').read_bytes()==(a.out/'stock.hash').read_bytes()
 width=struct.unpack('>I',(a.out/'package-wide.png').read_bytes()[16:20])[0]
 assert width==560,width
 summary=dict(stock_equals_interpreter=True,classic_equals_stock=True,default_off=True,
              package_activation_width=width,viewport_spawns=viewport[-1]['spawns'],
              native_terrain_checks=[dict(frame=r['frame'],samples=r['terrain_samples'],errors=r['terrain_errors']) for r in viewport if r['terrain_samples']])
 (a.out/'summary.json').write_text(json.dumps(summary,indent=2))
 print(json.dumps(summary,indent=2))

if __name__=='__main__':main()
