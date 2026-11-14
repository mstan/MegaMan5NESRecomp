"""Native boss recoil and room bounds from a current, pre-hit arena state."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,wave
p=argparse.ArgumentParser(description=__doc__)
for key in ('exe','rom','state','out'):p.add_argument('--'+key,required=True)
for key in ('at','cut','frames'):p.add_argument('--'+key,required=True,type=int)
a=p.parse_args();exe=Path(a.exe).resolve();rom=Path(a.rom).resolve();seed=Path(a.state).resolve();out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=False)
assert a.at<a.cut<a.frames-1
env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NESRECOMP_NO_LAUNCHER='1')
for mode in ('native','interpreter','cold','instant'):
 d=out/mode;d.mkdir();args=[str(exe),str(rom),'--no-save','--config',str(d/'config.ini'),'--load-state',str(out/'native/cut.cycstate') if mode in ('cold','instant') else str(seed),'--frames',str(a.cut+1 if mode=='instant' else a.frames),'--coop','on','--coop-view','split','--protoman','on','--proto-mode','protoman','--weapon-cycle','on','--widescreen','on','--no-flicker','on','--slowdown-reduction','on','--present-size','1152x540','--mm5-log',str(d/'frames.jsonl'),'--hash-out',str(d/'run.hash'),'--wav-out',str(d/'audio.wav'),'--present-out',str(d/'scene.png'),'--save-state',f'{a.cut}:{d}/cut.cycstate','--save-state',f'{a.frames-1}:{d}/end.cycstate']
 for addr,value in ((0x330,16),(0xB0,0x98),(0x5B8,0)):args+=['--coop-test-poke',f'{a.at}:1:{addr:X}:{value:X}']
 if mode=='native':args+=['--present-every',str(a.cut)]
 if mode=='interpreter':args+=['--interp-only']
 (d/'argv.json').write_text(json.dumps(args,indent=2))
 with(d/'run.log').open('w') as log:ret=subprocess.run(args,cwd=exe.parent,env=env,stdout=log,stderr=subprocess.STDOUT,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0),timeout=180)
 assert ret.returncode==0,(mode,(d/'run.log').read_text()[-1400:]);print(mode,flush=True)
n=out/'native';i=out/'interpreter';c=out/'cold';rows=[json.loads(x) for x in(n/'frames.jsonl').read_text().splitlines()];hurt=[x for x in rows if x['coop']['players'][1][4]==6];first=hurt[0]['frame'] if hurt else 0;page=rows[0]['coop']['pages'][0]
checks={'real-native-hit':bool(hurt) and min(x['coop']['players'][1][2] for x in rows)<24,'hurt-finishes':bool(first) and any(x['frame']>first and x['coop']['players'][1][4]<4 for x in rows),'native-arena-bounds':all(x['coop']['pages'][1]==page and 16<=x['coop']['players'][1][0]<=240 for x in rows),'no-native-scope-failure':all(not x['coop']['failed'] and not x['wide']['failed'] for x in rows),'Native-interpreter-exact':all((n/f).read_bytes()==(i/f).read_bytes() for f in ('run.hash','end.cycstate','scene.png','audio.wav')),'cold-state-picture':all((n/f).read_bytes()==(c/f).read_bytes() for f in ('end.cycstate','scene.png')),'cold-hash-tail':[x for x in(n/'run.hash').read_text().splitlines() if int(x.split()[0])>a.cut]==(c/'run.hash').read_text().splitlines(),'instant-picture':(out/'instant/scene.png').read_bytes()==(n/f'scene_{a.cut:05d}.png').read_bytes()}
with wave.open(str(n/'audio.wav')) as w,wave.open(str(c/'audio.wav')) as tail:pcm=tail.readframes(tail.getnframes());checks['cold-PCM-tail']=bool(pcm) and w.readframes(w.getnframes()).endswith(pcm)
result=dict(passed=all(checks.values()),checks=checks,exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),first_hit=first,scope='A current Big Pets arena state with both live players is the prerequisite. Native P2 position/HP/immunity setup permits a real boss hit; no damage, hurt-state, animation-completion or victory flags are supplied. Native/I and current cut-state PNG/hash/PCM continuation.');(out/'summary.json').write_text(json.dumps(result,indent=2));print(json.dumps(checks));raise SystemExit(not result['passed'])
