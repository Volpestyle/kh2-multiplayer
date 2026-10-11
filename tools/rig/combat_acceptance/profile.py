"""Source-owned two-PC practice A profile. Importing starts nothing.

The sealed packet provides one local Adapter per role, not a second launcher.
All callbacks use the ORIGINAL run deadline/cancellation/ownership identities.
No QPC subtraction across peers; transport and renderer coverage are posthoc.
"""
import argparse
import copy
import importlib
import json
import time
from pathlib import Path
from . import health, preposition, population

MOVE = population.MOVE
CROSS = population.CROSS
ACTIONS = tuple([(f"friend_position_{i}", "friend", MOVE) for i in range(3)] +
                [("host_activate_0", "host", MOVE), ("friend_claim", "friend", CROSS),
                 ("host_hit", "host", CROSS)])
ENVIRONMENT = {"KH2COOP_ORDINARY_LIFECYCLE_EXPERIMENTAL":"0",
               "KH2COOP_COMBAT_CAUSAL_TRACE":"1"}
MARKER = "VUH1503_PRACTICE_A_INPUTS_COMPLETE_EVIDENCE_PENDING"
SAMPLE_BUDGET_NS = 3_000_000_000

def adapter_contract():
    """Serializable source-owned values; packet manifests must match exactly."""
    return dict(sampleBudgetNs=SAMPLE_BUDGET_NS,
                actions=[[name,role,list(argv)] for name,role,argv in ACTIONS])

class Refused(RuntimeError): pass

def need(ok, reason):
    if not ok:raise Refused(reason)

def authorize(action, role, argv, history):
    i=len(history)
    need(i<len(ACTIONS) and history==[r[0] for r in ACTIONS[:i]], "extra/replaced action history")
    need((action,role,tuple(argv))==ACTIONS[i], "undeclared action/role/argv")

class Profile:
    def __init__(self, adapter):
        self.a=adapter
        need(getattr(adapter,'contract',None)==adapter_contract(),
             'adapter sample/action contract differs from source profile')
        need(adapter.role in ('host','friend'), 'one local role required')
        need(adapter.protocol==17, 'protocol17 products required')
        need(all(adapter.environment.get(k)==v for k,v in ENVIRONMENT.items()),
             'default-OFF lifecycle + explicit trace environment required')
        self.owned=copy.deepcopy(adapter.owned)
        self.peer_owned=copy.deepcopy(adapter.peer_owned)
        self.runtime_session=adapter.runtime_session
        peer_role="friend" if adapter.role=="host" else "host"
        self.peer_population=population.Admission(adapter.run_id,peer_role,self.peer_owned,self.runtime_session)
        self.guard=health.CanonicalHealth()
        self.population=population.Admission(adapter.run_id,adapter.role,self.owned,self.runtime_session)
        self.moves=None;self.history=[];self.issuing=False
        need(type(adapter.encounter_started_ns) is int and 0<adapter.encounter_started_ns<=adapter.clock_ns(),'original local encounter start missing')
        self.end=min(adapter.deadline_ns, adapter.encounter_started_ns+90_000_000_000)

    def check(self):
        self.a.check_stop_and_ownership(self.owned)
        need(self.a.clock_ns()<self.end, 'original 90s encounter expired')

    def canonical(self):
        self.check()
        return self.guard.read(self.a.ram,self.owned,self.a.original_identity,
                               clock=self.a.clock_ns,stage='acceptance')

    def sample(self, label):
        # One complete bracket retry ONLY for a fully authenticated intervening
        # native hit. Never retry an input, reanchor, or renew the read deadline.
        end=min(self.end,self.a.clock_ns()+SAMPLE_BUDGET_NS)
        for attempt in range(2):
            self.check();row=self.a.sample(end)
            flags=[r['experimental'] for r in population.records(row['injectAfter'],'[ordinary-lifecycle] ') if 'experimental' in r]
            need(bool(flags) and all(v=='0' for v in flags),'actual inject lifecycle experiment not confirmed OFF')
            population.activation_snapshot(row,self.owned,self.a.clock_ns())
            canonical=self.canonical()
            try:health.crosscheck_sample(row,canonical)
            except health.HealthRefusal:
                witness=health.intervening_hit(row,canonical,self.a.inject_log())
                self.a.retain('health-reacquisition',dict(label=label,attempt=attempt,
                    deadlineNs=end,sample=row,canonical=canonical,witness=witness,dispatchAllowed=False))
                if witness is None or attempt or self.a.clock_ns()>=end:raise
                continue
            need(self.a.clock_ns()<=end,'whole native bracket deadline expired')
            result=dict(sample=row,canonical=canonical,role=self.a.role)
            self.a.retain(label,result)
            return result
        raise Refused('persistent health straddle')

    def admission_for(self, action, sample, oracle, now_ns):
        if action.startswith('friend_position_'):
            return None
        if (action!='host_activate_0' or oracle.anchor is not None or
                population.activation_requires_admission(sample)):
            return oracle.check(sample,now_ns)
        return None

    def gate(self, action):
        value=self.sample(action)
        sample=value['sample']
        if action.startswith('friend_position_'):
            if self.moves is None:
                world=sample['avatarObserve']['samples'][0]['worldBefore']
                self.moves=preposition.PreActivationMovement(self.owned,world,self.runtime_session)
            self.moves.check(action,MOVE,sample,self.a.clock_ns(),population,self.canonical)
            value['admission']=None
        else:
            # Local and final gates use the same rule as the peer gate below.
            value['admission']=self.admission_for(action,sample,self.population,self.a.clock_ns())
        return value

    def run(self):
        initial=self.sample('initial-start')
        if self.a.role=='friend':preposition.proven_a_start(initial['sample'],initial['canonical'])
        peer=self.a.exchange('initial-start',initial,self.end)
        need(peer['role']!=self.a.role and peer['sample']['ownedBefore']==peer['sample']['ownedAfter']==peer['canonical']['session']==self.peer_owned,'original peer identity mismatch')
        if self.a.role=='host':preposition.proven_a_start(peer['sample'],peer['canonical'])
        # Existing packet already established one host warp/product follower.
        # Start evidence recording, not an input barrier. Missing media never
        # authorizes another attack and cannot qualify AC2 afterwards.
        self.a.begin_paired_evidence(self.end)
        for action,role,argv in ACTIONS:
            authorize(action,role,argv,self.history)
            if action=='host_activate_0':
                if self.moves:self.moves.mark_activation_issuing()
            local=self.gate(action)
            peer=self.a.exchange(action+':before',local,self.end)
            need(peer['role']!=self.a.role,'peer role mismatch')
            # Validate the peer raw snapshot in its own recorded clock domain.
            population.activation_snapshot(peer['sample'],self.peer_owned,peer['sample']['readFinishedNs'])
            need(peer['canonical']['session']==self.peer_owned,'peer canonical identity replaced')
            health.crosscheck_sample(peer['sample'],peer['canonical'])
            peer['admission']=self.admission_for(action,peer['sample'],self.peer_population,peer['sample']['readFinishedNs'])
            if not action.startswith('friend_position_') and (local['admission'] or peer['admission']):
                need(local['admission'] and peer['admission'],'paired full admission unavailable')
                population.paired(local['admission'] if self.a.role=='host' else peer['admission'],
                                  peer['admission'] if self.a.role=='host' else local['admission'])
            output=None
            if self.a.role==role:
                need(not self.issuing,'dispatch is single-use')
                self.issuing=True
                def final_gate():
                    self.gate(action)
                    self.canonical() # original actor/status and HP>12, last native read
                    self.check()
                # Packet must call final_gate AFTER all blocking transport/media
                # work and BEFORE its own final memory-only STOP/stamp/spawn.
                output=self.a.dispatch_once(action,argv,final_gate,self.end)
                need(output.get('ok') is True,'input ACK missing; never repeat')
                self.issuing=False
            ack=self.a.exchange(action+':ack',dict(role=self.a.role,action=action,output=output),self.end)
            actual=output if self.a.role==role else ack['output']
            need(ack['action']==action and ack['role']!=self.a.role and actual.get('ok') is True,'peer input ACK missing')
            self.history.append(action)
            self.a.action_acked(action,actual)
            if action.startswith('friend_position_'):self.moves.ack(action)
            if action in ('friend_claim','host_hit'):
                self.a.passive_observe(min(self.end,self.a.clock_ns()+SAMPLE_BUDGET_NS))
            self.sample(action+':after')
        result=dict(schema='vuh1503-practice-a-result-v1',marker=MARKER,
            actions=self.history,acceptance=False,criteria={'1':'EVIDENCE_PENDING','2':'EVIDENCE_PENDING','3':'NOT_QUALIFIED'},
            extraInputs=0,originalDeadlineNs=self.end)
        self.a.retain('profile-result',result)
        return result

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--adapter',required=True,help='Reviewed packet module with create_adapter(role, config)')
    p.add_argument('--role',required=True,choices=('host','friend'))
    p.add_argument('--config',required=True,type=Path)
    a=p.parse_args(argv)
    adapter=importlib.import_module(a.adapter).create_adapter(a.role,a.config)
    try:return Profile(adapter).run()
    finally:adapter.stop_owned_and_retain() # original cancellation/cleanup budget, no retry

if __name__=='__main__':main()
