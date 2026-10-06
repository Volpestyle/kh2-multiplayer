"""Exercise the actual profile receipt writer under concurrent large appends; offline only."""
import argparse,json,threading,time
from pathlib import Path
from types import SimpleNamespace
from run_profile import Profile

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);args=ap.parse_args()
    args.out.mkdir(exist_ok=False,parents=True)
    p=Profile(SimpleNamespace(kh2ctl=None),{},time.monotonic()+10)
    p.ctx=SimpleNamespace(run_dir=args.out)
    barrier=threading.Barrier(2)
    def producer(owner):
        barrier.wait()
        for i in range(64):p.emit('concurrent_control',{'owner':owner,'i':i,'payload':'x'*16000})
    threads=[threading.Thread(target=producer,args=(i,)) for i in range(2)]
    for t in threads:t.start()
    for t in threads:t.join(timeout=5)
    assert not any(t.is_alive() for t in threads)
    rows=[json.loads(line) for line in (args.out/'party-receipts.jsonl').read_text().splitlines()]
    assert len(rows)==len(p.rows)==128 and rows==p.rows
    assert {(r['data']['owner'],r['data']['i']) for r in rows}=={(a,b) for a in range(2) for b in range(64)}
    assert all(len(r['data']['payload'])==16000 for r in rows)
    print(json.dumps({'ok':True,'rows':128,'producerThreads':2,'payloadBytesPerRow':16000,'actualProfileEmit':True,'noMissingDuplicatePartialRows':True}))
if __name__=='__main__':main()
