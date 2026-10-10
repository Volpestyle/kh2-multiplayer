"""Generate copied-fact C++ cases from explicitly supplied retained host logs.
No native execution. New applySequence is established by actual test callbacks,
not retroactively asserted as a recorded field in the historical event.
"""
import argparse,hashlib,json,re
from pathlib import Path
def fields(line):return dict(re.findall(r'(\w+)=([^\s]*)',line))
HEX={'actor','objentry','status','namePrefix','hit','attack','owner','canonicalPlayer','head','tracked','callerRva'}
BOOL={'available','syncDrop','manualFilterOn','manualDrop','ownerThread','returned','unwound','nested','overflow','coverageStable','contextStable','metadataStable','lossStable','witness','callerAvailable','recorded','contextAvailable','supported','revalidationAttempted','revalidationPassed','zeroAttempted','claimAttempted','claimQueued','matching'}
def assign(prefix,row,names):
    return ''.join(prefix+'.'+k+'='+('true' if row[k]=='1' else 'false' if row[k]=='0' else row[k])+';' if k in BOOL else prefix+'.'+k+'='+str(int(row[k],16 if k in HEX else 10))+('ULL' if int(row[k],16 if k in HEX else 10)>0x7fffffff else '')+';' for k in names)
def context(prefix,row):
    out=assign(prefix,row,['frame','generation','epoch','transitionSerial','loadSerial','connectionId','hostConnectionId','readMask','role','slot','available'])
    return out+''.join(f'{prefix}.location[{i}]={int(row["location"][i*4:i*4+4],16)};' for i in range(6))
def actor(prefix,row):return assign(prefix,row,['actor','objentry','status','objectId','readMask','team','hp','maxHp','namePrefix','type'])
def hit(prefix,row):return assign(prefix,row,['hit','attack','owner','canonicalPlayer','head','tracked','flags','attackHandle','atkpHandle','ownerHandle','attackId','readMask','damage','stat','kind','syncDrop','manualFilterOn','manualDrop'])
def generate(paths,destination):
    out=['// Generated from retained causal08/10 text. Regenerate with companion_retained_fixture.py.\ninline ht::Event RetainedCompanion(unsigned index) { ht::Event e{}; switch(index) {']
    manifest=[]
    for index,path in enumerate(paths):
        lines=path.read_text(encoding='utf-8').splitlines()
        def one(prefix,**wanted):
            candidates=[fields(l) for l in lines if prefix in l and all(fields(l).get(k)==str(v) for k,v in wanted.items())]
            assert len(candidates)==1,(prefix,wanted,len(candidates))
            return candidates[0]
        event=one('[hittrace] event ',seq=1);p=one('[damagepolicy] event ',seq=1)
        out.append(f'case {index}: {{')
        out.append('e.sequence='+event['seq']+';e.parentSequence='+event['parent']+';')
        out.append(assign('e',event,['depth','coverageSerial','lossSerial','callerRva','rawResult','coverageMask','takeCalls','statCalls','childCount','ownerThread','callerAvailable','returned','unwound','nested','overflow','coverageStable','contextStable','metadataStable','lossStable','witness']))
        for phase,side in [(0,'before'),(1,'after')]:
            out.append(context('e.'+side+'.context',one('[hittrace] context ',seq=1,phase=phase)))
            for subject,name in [(0,'victim'),(1,'source')]:out.append(actor('e.'+side+'.'+name,one('[hittrace] actor ',seq=1,phase=phase,subject=subject)))
            out.append(hit('e.'+side+'.hit',one('[hittrace] input ',seq=1,phase=phase)))
        out.append('auto& p=e.policy;'+assign('p',p,['recorded','revalidationAttempted','revalidationPassed','zeroAttempted','claimAttempted','claimQueued']))
        out.append(f'p.decision.action=static_cast<kh2coop::inject::damagepolicy::Action>({p["action"]});p.decision.reason=static_cast<kh2coop::inject::damagepolicy::Reason>({p["reason"]});p.decision.supported='+('true' if p['supported']=='1' else 'false')+';')
        out.append(f'p.facts.role=static_cast<kh2coop::inject::damagepolicy::Role>({p["role"]});p.facts.source=static_cast<kh2coop::inject::damagepolicy::ActorClass>({p["sourceClass"]});p.facts.victim=static_cast<kh2coop::inject::damagepolicy::ActorClass>({p["victimClass"]});'+assign('p.facts',p,['ownerThread','contextAvailable']))
        for key,source in [('flags','flags'),('readMask','readMask'),('amount','amount'),('stat','stat'),('kind','kind')]:out.append(f'p.facts.hit.{key}={p[source]};')
        for i in range(3):out.append(f'p.roster[{i}]={p["roster"+str(i)]};')
        out.append(context('p.authority.context',one('[damagepolicy] context ',seq=1,phase=0)))
        for subject,name in [(0,'victim'),(1,'source')]:out.append(actor('p.authority.'+name,one('[damagepolicy] actor ',seq=1,phase=0,subject=subject)))
        out.append(hit('p.authority.hit',one('[damagepolicy] input ',seq=1,phase=0)))
        c=one('[hittrace] child ',seq=1,index=0)
        out.append('auto& c=e.children[0];c.sequence='+c['childSeq']+';c.takeSequence='+c['takeSeq']+';c.kind=ht::ChildKind::Stat;'+assign('c',c,['actor','callerRva','delta','stat','react','result','callerAvailable','matching','returned','unwound']))
        out.append(actor('c.before',one('[hittrace] actor ',seq=1,phase=0,subject=2)))
        out.append(actor('c.after',one('[hittrace] actor ',seq=1,phase=1,subject=2)))
        out.append('return e;}')
        manifest.append(dict(case=index,log=path.name,sha256=hashlib.sha256(path.read_bytes()).hexdigest(),actualGate=dict(sourceClass=p['sourceClass'],reason=p['reason'],supported=p['supported'])))
    out.append('default:return e;}}')
    destination.write_text('\n'.join(out)+'\n',encoding='utf-8')
    destination.with_suffix('.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('logs',nargs=2,type=Path);p.add_argument('--out',required=True,type=Path);args=p.parse_args();generate(args.logs,args.out)
