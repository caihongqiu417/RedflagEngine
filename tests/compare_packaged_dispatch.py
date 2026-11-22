"""同一句文本在两个游戏包中从登记到交给各自请求客户端。记录游戏自己的时间与帧号。"""
import argparse
from datetime import datetime
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/validation/runtime-ai'


def percentile(samples, fraction):
    values=sorted(samples)
    index=(len(values)-1)*fraction
    low=int(index)
    return round(values[low]+(values[min(low+1,len(values)-1)]-values[low])*(index-low),3)


def main():
    cli=argparse.ArgumentParser(description=__doc__)
    cli.add_argument('--windows',type=Path,required=True)
    cli.add_argument('--delivery-tools',type=Path,required=True)
    cli.add_argument('--trials',type=int,default=5)
    opt=cli.parse_args()
    redflag=json.loads((OUT/'forge-dispatch-latency.json').read_bytes())
    ue=[]
    for i in range(opt.trials):
        raw=ROOT/'tmp/ue-npc-latency'/f'dispatch-{i}.json'
        subprocess.run([sys.executable,str(ROOT/'tests/benchmark_packaged_ue.py'),'--windows',str(opt.windows),
                        '--delivery-tools',str(opt.delivery_tools),'--provider','fixture','--dispatch-only',
                        '--seconds','18','--out',str(raw)],cwd=ROOT,check=True)
        run=json.loads(raw.read_bytes())
        listening=next(m for m in run['markers'] if 'ResidentConversation accepted:' in m['message'])
        player_requests=[m for m in run['markers'] if m['utc']>=listening['utc'] and 'stage=http_start player=1' in m['message']]
        sample={'accepted':listening,'request':player_requests[0] if player_requests else None}
        if player_requests:
            request=sample['request']
            sample.update(input_to_request_ms=round(1000*(datetime.fromisoformat(request['utc'])-
                            datetime.fromisoformat(listening['utc'])).total_seconds(),3),
                          frames=(request['engine_frame']-listening['engine_frame'])%1000,
                          request_id=re.search(r'request=(\w+)',request['message'])[1])
        ue.append(sample)
    samples={name:[trial['input_to_request_ms'] for trial in trials if 'input_to_request_ms' in trial]
             for name,trials in [('redflag',redflag['trials']),('unreal',ue)]}
    report=dict(stage='input accepted to handoff to the host request client; excludes model, audio and action',
                text=run['prompt'],window=[640,360],unreal_exe_sha256=run['exe_sha256'],unreal_provider='fixture',
                unreal=ue,redflag_file='forge-dispatch-latency.json',
                stats={key:dict(n=len(values),p50_ms=percentile(values,.5),p95_ms=percentile(values,.95))
                       for key,values in samples.items() if values})
    (OUT/'packaged-dispatch.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(report['stats'])


if __name__=='__main__':
    main()
