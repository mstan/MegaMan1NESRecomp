"""Short authored checkpoint/last-link/real-attack content check."""
import argparse,pathlib,sys,os,subprocess,json,time
p=argparse.ArgumentParser()
for key in ('exe','rom','out'):p.add_argument('--'+key,type=pathlib.Path,required=True)
p.add_argument('--stage',type=int,choices=range(10),required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent));from views import records
rom=a.rom.read_bytes()[16:];stage=a.stage;bank=stage if stage<6 else stage-6;final_frame=7500 if stage==9 else 6000
def byte(bank,addr):return rom[bank*0x4000+(addr&0x3FFF)]
cp=byte(7,0xC2D4+stage);first=byte(7,0xC2EB+stage);stripes=[];screen=0
for order in range(40):
 b=byte(bank,0x8C71+order);end=screen+(b&31);stripes.append((screen,end,order,b));screen=end+1
 if b==255 or screen>=48:break
checkpoint=next(s for s in stripes if s[0]<=cp<=s[1])
def tile_type(x,y,page=cp):
 room=byte(bank,0x8C00+page);ptr=byte(bank,0x8C30+room*2)|(byte(bank,0x8C31+room*2)<<8)
 block=byte(bank,ptr+(x//32)*8+y//32);quad=(2 if x&16 else 0)+(1 if y&16 else 0)
 return byte(7,0xCD46+stage*4+(byte(bank,0x8000+block*4+quad)>>6))
def checkpoint_y(x):
 for floor in range(32,240,16):
  if all(tile_type(x+dx,floor) in (1,4) for dx in (-6,6)) and all(tile_type(x+dx,floor+dy) in (0,2) for dx in (-6,6) for dy in (-4,-12,-20)):
   return floor-12
 return 180
feet={1:checkpoint_y(128),2:checkpoint_y(104)}
boss=next(s for s in stripes if s[0]>=first and not (s[3]&224));source=stripes[boss[2]-1]
direction='UP' if source[3]&128 else 'DOWN' if source[3]&64 else 'RIGHT'
ladder=next((x for x in range(8,256,16) if tile_type(x,0 if direction=='UP' else 224,source[1])==2),216)
lines=['0 -','0 2:-',f'179 POKE 00AC {stage:02X}','179 POKE 00BC 01','180 START','181 -','500 POKE 00BC 00']
pokes=[]
def raw(frame,values):
 for addr,value in values.items():lines.append(f'{frame} POKE {addr:04X} {value&255:02X}')
def personal(frame,seat,values):
 for addr,value in values.items():pokes.extend(('--coop-test-poke',f'{frame}:{seat}:{addr:X}:{value&255:X}'))
def bounds(frame,s):raw(frame,{0x1A:0,0x1B:s[1],0x27:s[0],0x28:s[1],0x29:s[2],0x8D:1})
def bullet(frame,seat):
 values={0x400:255,0x420:2,0x440:0,0x460:cp,0x480:128 if seat==1 else 104,0x4A0:0,0x4C0:0,0x4E0:0,0x600:feet[seat],0x620:0,0x640:127,0x660:0,0x680:0,0x6A0:1,0x6C0:1,0x6E0:0x1A}
 raw(frame,{addr+18:value for addr,value in values.items()})
bounds(800,checkpoint)
for seat in (1,2):personal(800,seat,{0x460:cp,0x480:128 if seat==1 else 104,0x600:feet[seat],0x400:0,0x420:64,0x6A:1,0x55:0})
for slot in range(16,32):raw(800,{0x600+slot:248})
bullet(820,1);raw(845,{0x612:248});bullet(870,2);raw(895,{0x612:248})
bounds(1750,source)
for seat in (1,2):
 values={0x460:source[1],0x480:(248 if seat==1 else 200) if direction=='RIGHT' else ladder,0x600:180 if direction=='RIGHT' else 8 if direction=='UP' else 232,0x400:0 if direction=='RIGHT' else 21,0x420:64 if direction=='RIGHT' else 80,0x55:255}
 if direction!='RIGHT':values[0x30]=12
 personal(1750,seat,values);personal(2450,seat,{0x6A:28,0x55:255})
for slot in range(16,32):raw(1750,{0x600+slot:248})
lines += [f'1751 {direction}','1785 -']
# Vulnerability/HP and position are prerequisites, never a defeat/clear flag.
attack=2855 if stage==6 else 3155 if stage in (7,9) else 2451
target_y=108 if stage==6 else 180
prerequisites={0x6C1:1,0x481:160,0x601:target_y,0x681:0,0x661:0,0x480:144,0x600:target_y,0x420:64,0x55:255,0x6A:28}
if stage==8:
 # The native CWU orb owns its own HP. Boss1 is an invisible shared meter.
 for addr in (0x481,0x601,0x681,0x661):prerequisites.pop(addr)
 # Retail subtracts exactly4 per orb; a1-unit meter is an invalid cheat
 # that wraps to253. The real attack must remove the last native4-unit orb.
 prerequisites[0x6C1]=4
 prerequisites[0x6D0]=1
raw(attack-1,prerequisites)
lines += [f'{attack} B',f'{attack+1} -']
if stage==6:personal(2850,2,{0x6A:28,0x55:255})
if stage==9:
 raw(4054,{0x6C1:1,0x481:160,0x601:164,0x681:0,0x661:0,0x480:144,0x600:164,0x420:64,0x55:255,0x6A:28})
 lines+=['4055 B','4056 -']
raw(2800,{0x480:168 if stage==2 else 128,0x600:180,0x55:255})
route=a.out/'input.txt';route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
argv=[str(a.exe),str(a.rom),'--frames',str(final_frame),'--input',str(route),'--no-save','--config',str(a.out/'config.ini'),'--coop','on','--widescreen','16:9','--no-flicker','on','--slowdown','on','--charge','off','--slide','off',*pokes,*(['--save-state',f'3000:{a.out/"boss.cycstate"}'] if stage==9 else []),'--coop-report',str(a.out/'coop.jsonl'),'--frame-log',str(a.out/'native.frames'),'--frame-log-frames',f'750:{final_frame-1}','--present-out',str(a.out/'final.png'),'--present-every','1000','--wav-out',str(a.out/'native.wav')]
started=time.monotonic()
with (a.out/'runtime.log').open('w') as log:r=subprocess.run(argv,cwd=a.exe.parent,env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1'),stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW,timeout=110)
memory=records(a.out/'native.frames');rows={r['frame']:r for r in map(json.loads,(a.out/'coop.jsonl').read_text().splitlines())}
def defeated(m):return m[0x601]==248 and m[0x6C1]==0 and (stage>=6 or m[0x6FF]==0x44 and m[0x61F]<248)
reward=[f for f,m in memory.items() if attack+1<=f<=attack+120 and m[0x31]==stage and defeated(m)]
real_attacks=[]
if stage==9:
 for shot,boss_id in ((3155,9),(4055,10)):
  zero=next((f for f,m in memory.items() if shot+1<=f<=shot+120 and m[0x31]==9 and m[0x6C1]==0),None)
  real_attacks.append(dict(input_tick=shot,pre_attack_hp=memory[shot][0x6C1],native_boss=memory[shot][0xAC],zero_after=zero))
 reward=[r['zero_after'] for r in real_attacks] if all(r['pre_attack_hp']==1 and r['native_boss']==boss and r['zero_after'] for r,boss in zip(real_attacks,(9,10))) else []
bit=[32,16,2,64,4,8][stage] if stage<6 else 0
clear=(memory[final_frame][0x5D]&bit)!=0 and memory[final_frame][0x31]==0 if stage<6 else any(m[0x31]==(11 if stage==9 else stage+1) for f,m in memory.items() if f>2452)
checks={'original-stage':memory[751][0x31]==stage,('authored-checkpoint-area' if stage==9 else 'native-checkpoint-retry'):memory[801][0x1B]==cp if stage==9 else any(r['retries'] and r['checkpoint'] and all(h>0 for h in r['hp']) for f,r in rows.items() if 1000<f<1750),'native-boss-arrival':any(m[0x31]==stage and m[0x3E]>=5 and m[0x6C1]==28 for f,m in memory.items() if 1900<f<attack),'real-shot-native-defeat':bool(reward),'native-clear-and-nextscene':clear,'no-scope-failures':rows[final_frame]['failures']==0}
summary=dict(real_attacks=real_attacks,checks=checks,stage=stage,checkpoint=list(checkpoint),last_link=list(source),boss=list(boss),direction=direction,seconds=time.monotonic()-started,reward_frame=reward[0] if reward else None,final_stage=memory[final_frame][0x31],owned=memory[final_frame][0x5D],last=rows[final_frame],scope='Authored main/checkpoint/boss areas; native HP/damage/position prerequisites and real Buster attacks; no clear/victory flag written')
(a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary),flush=True);assert all(checks.values()),checks
