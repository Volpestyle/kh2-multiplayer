"""Retained clip04 short-mode metrics and strict v2 cross-mode schema controls."""
import argparse,copy,json
from pathlib import Path
from .adapter import read,validate
from . import metrics
from rig.pc2_clip import metrics as legacy_metrics

def run(retained):
    original=read(retained/'hb_arrival_peer0_clip.json');frames=read(retained/'hb_arrival_peer0_frames.json');checks=[]
    s=copy.deepcopy(original);s['schema']='pc2-owned-clip-v2';s['capture'].update(seconds=3,frameCount=90)
    schema=read(Path(__file__).with_name('pc2-owned-clip-v2.schema.json'));validate(s,schema);checks.append('retained-short-success')
    f=copy.deepcopy(frames);f.update(schema='pc2-decoded-frames-v2',seconds=3,frameCount=90)
    fs=read(Path(__file__).with_name('pc2-decoded-frames-v2.schema.json'));validate(f,fs);checks.append('retained-short-frames')
    assert metrics.summarize(f,s['capture']['encodedFps'])==legacy_metrics.summarize(frames,s['capture']['encodedFps']);checks.append('unchanged-short-metrics')
    def refused(label,value,schema):
        try:validate(value,schema)
        except (ValueError,TypeError,KeyError):checks.append(label)
        else:raise AssertionError(label)
    for field,value in [('seconds',16),('frameCount',480),('seconds',12),('frameCount',True)]:
        bad=copy.deepcopy(s);bad['capture'][field]=value;refused('short-cross-'+field+str(value),bad,schema)
    for seconds,count in [(3,90),(16,480)]:
        valid=copy.deepcopy(f);valid.update(seconds=seconds,frameCount=count);valid['rows']=[dict(copy.deepcopy(frames['rows'][i%90]),index=i,pts=i) for i in range(count)];validate(valid,fs);checks.append('frames-'+str(count))
        bad=copy.deepcopy(valid);bad['frameCount']=90 if count==480 else 480;refused('frame-count-cross-'+str(count),bad,fs)
        bad=copy.deepcopy(valid);bad['rows'].pop();refused('missing-frame-'+str(count),bad,fs)
    return dict(passed=len(checks),failed=0,checks=checks,nativeBuilt=False,pc2Contact=False,limit='Retained90 metrics and synthetic480 schema shape only')

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--retained',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();result=run(a.retained);a.out.write_text(json.dumps(result,indent=2));print(json.dumps(result))
