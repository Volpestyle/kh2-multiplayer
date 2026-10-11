"""Offline practice proposals only; no dispatch, native reads or product edits."""
import copy,math

class Refused(RuntimeError):pass
def need(ok,reason):
    if not ok:raise Refused(reason)

def proven_a_start(sample,canonical):
    """Lead64: check real BC start before any positioning/activation, never reanchor."""
    target=(-64.36,-260.,1950.);rows=[]
    need(canonical['ok'] is True and canonical['after']['hp']>12,'canonical health unavailable/low')
    need(sample['ownedBefore']==sample['ownedAfter']==canonical['session'],'original friend identity changed')
    for side in ('Before','After'):
        actors=[a for a in sample['entities'+side]['actors'] if int(a['address'],16)==canonical['after']['actor']]
        need(len(actors)==1,'unique original canonical friend unavailable');a=actors[0]
        need(int(a['status'],16)==canonical['after']['status'] and (a['objectId'],a['objectType'],a['team'])==(84,0,1),'canonical friend roots changed')
        pose=tuple(float(a['position'][k]) for k in ('x','y','z'));state=sample['state'+side]['actors']['player'];statepose=tuple(float(state['position'][k]) for k in ('x','y','z'));yaw=float(state['rotationY'])
        need(all(map(math.isfinite,(*pose,*statepose,yaw))),'invalid start pose')
        distance=math.dist(pose,target);state_distance=math.dist(statepose,target);yaw_error=abs(math.atan2(math.sin(yaw-math.pi),math.cos(yaw-math.pi)))
        need(distance<=30 and state_distance<=30,'start outside30-unit proven A radius')
        need(yaw_error<=.05,'start yaw differs from pi by more than0.05rad')
        rows.append({'side':side,'position':a['position'],'statePosition':state['position'],'yaw':yaw,'distance':distance,'stateDistance':state_distance,'yawErrorRadians':yaw_error})
    return {'target':target,'radius':30,'yawToleranceRadians':.05,'observations':rows,'checkedBeforeMovementAndActivation':True}

class PreActivationMovement:
    def __init__(self,owned,world,session):
        self.owned=copy.deepcopy(owned);self.world=copy.deepcopy(world);self.session=session
        self.next=0;self.activated=False
    def mark_activation_issuing(self):self.activated=True
    def check(self,action,argv,sample,now_ns,oracle,canonical_health):
        need(not self.activated,'pre-activation mode permanently closed')
        need(self.next<3 and action==f'friend_position_{self.next}','only next original positioning action')
        need(tuple(argv)==oracle.MOVE,'only original bounded forward movement')
        # Original safety validator is retained; zero enemies makes its family
        # predicate vacuous, never grants attack admission.
        oracle.activation_snapshot(sample,self.owned,now_ns)
        need(sample['injectAfter'].startswith(sample['injectBefore']) and sample['runtimeAfter'].startswith(sample['runtimeBefore']),'log prefix replacement')
        for k in ('entitiesBefore','entitiesAfter'):
            need(not any(a['objectType']==4 or a['name'].startswith(('M_','B_')) for a in sample[k]['actors']),'enemy present before activation')
        need(not any(int(x['kind'])==0 for x in oracle.receipt_rows(sample['injectAfter'])),'enemy admission already exists')
        obs=sample['avatarObserve'];need(obs.get('ok') is True and obs.get('readOnly') is True and obs['processId']==self.owned['pid'],'current world observer unavailable')
        need(len(obs['samples'])==1,'one world observation required');row=obs['samples'][0]
        need(row['complete'] is True and row['identityStable'] is True and row['worldBefore']==row['worldAfter']==self.world,'original room/roster/delivery binding changed')
        runtime=oracle.records(sample['runtimeAfter'],'[runtime-identity] ')
        need(bool(runtime),'authenticated current runtime binding unavailable');r=runtime[-1]
        need(r['session']==self.session and r['pinSession']==self.session and all(r[k]=='1' for k in ('identityCurrent','admitted','transportConnected','bridgeOpen')) and r['quarantine']=='0','original authenticated session changed/lost')
        need(int(r['generation'])==self.world['generation'] and int(r['delivery'])==self.world['deliverySerial'] and int(r['slot'])==self.world['localSlot'] and [int(r[f'roster{i}']) for i in range(3)]==self.world['connectionIds'],'runtime roster/delivery mismatch')
        # Caller must re-read canonical health immediately before spawn and
        # enforce original deadline/STOP on BOTH peers. This callback models it.
        canonical_health()
        return {'mode':'pre-activation-movement','action':action,'enemySummaryRequired':False,'attackAuthorized':False}
    def ack(self,action):
        need(not self.activated and action==f'friend_position_{self.next}' and self.next<3,'wrong/duplicate movement ACK');self.next+=1
