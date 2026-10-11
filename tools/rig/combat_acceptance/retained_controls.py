"""Read-only replay of retained practice A data. Never imports native readers."""
import argparse,copy,hashlib,json
from pathlib import Path
from . import health,preposition,population

def main(argv=None):
    p=argparse.ArgumentParser();p.add_argument('--evidence',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args(argv)
    h=json.loads((a.evidence/'fresh-health.json').read_text(encoding='utf-8-sig'))
    files=sorted(a.evidence.glob('sample-*-1-preposition-*.json'))
    samples=[json.loads(f.read_text(encoding='utf-8-sig')) for f in files]
    s=samples[0];canonical=next(x for x in h if x['peer']==1 and x.get('ok') and x['session']==s['ownedBefore'])
    preposition.proven_a_start(s,canonical)
    checks=['actual retained friend start pose and original canonical binding']
    world=s['avatarObserve']['samples'][0]['worldBefore'];session=population.records(s['runtimeAfter'],'[runtime-identity] ')[-1]['session']
    moves=preposition.PreActivationMovement(s['ownedBefore'],world,session)
    for i, sample in enumerate(samples[:3]):
        moves.check(f'friend_position_{i}',population.MOVE,sample,sample['readFinishedNs'],population,lambda:None);moves.ack(f'friend_position_{i}')
    checks.append('actual original no-enemy movement guards exact3 ACKs')
    moves.mark_activation_issuing()
    try:moves.check('friend_position_0',population.MOVE,s,s['readFinishedNs'],population,lambda:None)
    except preposition.Refused:checks.append('activation permanently closes movement')
    else:raise AssertionError('movement reopened')
    shifted=copy.deepcopy(s);shifted['stateAfter']['actors']['player']['position']['x']+=100
    try:preposition.proven_a_start(shifted,canonical)
    except preposition.Refused:checks.append('wrong start pose refuses')
    else:raise AssertionError('wrong pose accepted')
    # Replay actual canonical pointer/metadata using an in-memory RPM leaf.
    class RAM:
        base=int(canonical['session']['moduleBase'],16)
        def __init__(self,hp):
            x=canonical['after'];self.values={self.base+health.NATIVE_PLAYER:x['actor'],self.base+health.ACTIVE_HEAD:x['head'],x['actor']+0x918:x['objentry'],x['actor']+0x5C0:x['status'],x['objentry']:84,x['objentry']+4:0,x['actor']+0x4DC:1,x['status']:hp,x['status']+4:24,self.base+0x2A23598:hp,self.base+0x2A2359C:24}
            self.values.update({self.base+int(rva,16):expected for _,rva,_,expected in health.FLAGS})
        def value(self,address,fmt):return self.values[address]
    for hp in (24,16,12,8,0):
        guard=health.CanonicalHealth()
        try:guard.read(RAM(hp),canonical['session'],lambda:canonical['session'],clock=lambda:1)
        except health.HealthRefusal:assert hp<=12
        else:assert hp>12
    checks.append('actual canonical guard retained pointers:24/16 allowed,12/8/0 STOP')
    result={'passed':len(checks),'checks':checks,'nativeCalls':0,'inputDispatches':0,'scope':'retained data and in-memory RPM leaf; no transport/native/product qualification','inputHashes':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in files+[a.evidence/'fresh-health.json']}}
    a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps({'passed':len(checks)}))
if __name__=='__main__':main()
