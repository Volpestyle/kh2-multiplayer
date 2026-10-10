"""Offline retained replay/source preservation and actual acquisition controls.
Native copied-facts C++ controls remain for Steam's governed build slot.
"""
import argparse
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import threading
import sys
from practice_player_retained_fixture import practice_fixture

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/scenario'))
import causal_summary_acquisition as acquisition
BASE = '2f6e53160ea886533385221067423d57f2824a99'

def load(path):
    spec = importlib.util.spec_from_file_location('retained_source05_oracle', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def section(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]

def run(evidence, oracle_path):
    checks = []
    def check(value, name):
        assert value, name
        checks.append(name)
    def original(path):
        return subprocess.check_output(['git','show',BASE+':'+path],cwd=ROOT).decode().replace('\r\n','\n')
    def current(path):
        return (ROOT/path).read_text(encoding='utf-8')
    old, new = original('inject/src/CombatCausalTrace.hpp'), current('inject/src/CombatCausalTrace.hpp')
    check(section(old,'inline bool PlayerEnemyHit','// Separate host-only') ==
          section(new,'inline bool PlayerEnemyHit','// Separate host-only'), 'original PlayerEnemyHit unchanged')
    check(section(old,'inline bool HostCompanionEnemyHit','inline std::uint64_t Digest') ==
          section(new,'inline bool HostCompanionEnemyHit','// Canonical host player'), 'companion witness unchanged')
    guard='if(l&&!r.locallyQualified&&e.before.victim.hp!=e.after.victim.hp)Retire(s,now,Reason::UnknownDelta);'
    check(guard in old and guard in new, 'UnknownDelta guard unchanged')
    for path in ['inject/src/EntityHook.cpp','inject/src/DamagePolicy.hpp','inject/src/DamagePolicy.cpp']:
        check(original(path)==current(path), path+' unchanged')
    old, new = original('inject/src/EnemySync.cpp'), current('inject/src/EnemySync.cpp')
    projected = new.replace('#include "CausalDisappearance.hpp"\n','')
    start=projected.index('// At most one diagnostic per original actor,')
    end=projected.index('void ObserveCausalPopulation(bool complete) noexcept {',start)
    projected=projected[:start]+projected[end:]
    projected=projected.replace('    ProbeCausalDisappearance(census,before);\n','')
    projected=projected.replace('    if(combatcausal::engine.Admitted())g_causalDisappearance.Arm(after,rows,count);\n','')
    check(projected==old, 'EnemySync gameplay bytes unchanged after exact read-only diagnostic projection')
    check('if(!combatcausal::Requested() || !g_log || !nativehittrace::IsOwnerThread())return;' in new,
          'additional disappearance reads default-off and owner-thread only')
    with tempfile.TemporaryDirectory() as tmp:
        out=Path(tmp)/'fixture.hpp';practice_fixture(evidence/'inject-0.log',out)
        check(out.read_bytes()==(ROOT/'tests/PracticePlayerRetainedFixture.hpp').read_bytes(), 'practice05 copied event regenerates exact')
        check(out.with_suffix('.json').read_bytes()==(ROOT/'tests/PracticePlayerRetainedFixture.json').read_bytes(), 'practice05 provenance regenerates exact')
    friend=(evidence/'inject-1.log').read_text(encoding='utf-8')
    check('spawn 0 @7FF7D5DB7550 left the list (last hp 20)' in friend and
          'serial=1096 qpc=759474556636800 kind=11 reason=3' in friend,
          'retained practice05 live-HP missing target and hard Mapping refusal preserved')
    oracle=load(oracle_path)
    retained=[]
    for name in ['sample-004-1-host_activate_1.json','sample-005-0-host_activate_2.json']:
        sample=json.loads((evidence/name).read_text(encoding='utf-8-sig'))
        check(not acquisition._rows(sample['injectAfter'][len(sample['injectBefore']):],acquisition.SUMMARY),
              name+' actually has no fresh summary')
        oracle.activation_snapshot(sample,sample['ownedBefore'],sample['readFinishedNs'])
        retained.append(sample)

    class Clock:
        def __init__(self):self.value=retained[0]['readFinishedNs']+1
        def __call__(self):return self.value
    class Cancel:
        def __init__(self,clock):self.clock=clock;self.set=False;self.waits=0
        def is_set(self):return self.set
        def wait(self,seconds):self.waits+=1;self.clock.value+=int(seconds*1e9)
    # Positive successor summary is deliberately a constructed producer fixture;
    # original practice05 summaries do not have the new owner-frame fields.
    def successor(sample, admitted=1):
        sample=copy.deepcopy(sample);sample['injectBefore']=sample['injectAfter']
        admissions=[r for r in acquisition._rows(sample['injectAfter'],acquisition.RECEIPT) if r['kind']=='0']
        row=dict(admissions[0]);row.update(admitted=str(admitted),retired='0',dropped='0',loss='0',
                                         ownerFrame=str(max([int(r['frame']) for r in admissions]+[int(r.get('ownerFrame','0')) for r in acquisition._rows(sample['injectBefore'],acquisition.SUMMARY)])+1),ownerAvailable='1')
        sample['injectAfter']+='\n'+acquisition.SUMMARY+' '.join(k+'='+v for k,v in row.items())+'\n'
        return sample
    def execute(samples, mutate=None):
        clock=Clock();cancel=Cancel(clock);seen=[]
        def read(deadline):
            check(deadline<=clock.value+3_000_000_000,'read receives capped original QPC deadline')
            sample=copy.deepcopy(samples[min(len(seen),len(samples)-1)])
            elapsed=sample['readFinishedNs']-sample['readStartedNs']
            sample['readStartedNs']=clock.value
            clock.value+=elapsed
            sample['readFinishedNs']=clock.value
            seen.append(sample)
            if mutate:mutate(clock,cancel,sample)
            return sample
        validate=lambda s,n:oracle.activation_snapshot(s,s['ownedBefore'],n)
        result=acquisition.acquire(read,validate,cancel,clock.value+4_000_000_000,_clock_ns=clock)
        return result,seen,cancel
    result,seen,cancel=execute([retained[0],successor(retained[0])])
    check(len(seen)==2 and cancel.waits==1 and result[1]['dispatched'] is False,
          'actual acquisition waits after retained no-summary then returns original fresh admitted sample without dispatch')
    waiting=successor(retained[0],0);ready=successor(waiting)
    result,seen,cancel=execute([waiting,ready])
    check(len(seen)==2 and result[1]['attempts'][0]['status']=='WAIT_NOT_ADMITTED','admitted0 waits instead of retiring Admission')
    bad_cases={
        'native retirement':lambda s:s.update(injectAfter=s['injectAfter']+'\n[combat-causal] receipt kind=11 loss=0\n'),
        'scope replacement':lambda s:s['peekAfter']['samples'][0].update({'0x717009':0}),
        'log truncation':lambda s:s.update(injectBefore='foreign prefix'),
        'summary owner unavailable':lambda s:s.update(injectAfter=s['injectAfter'].replace('ownerAvailable=1','ownerAvailable=0')),
        'owner frame regression':lambda s:s.update(injectAfter=s['injectAfter'].replace('ownerFrame=', 'ownerFrame=0 ignored=')),
        'summary loss':lambda s:s.update(injectAfter=s['injectAfter'].replace('retired=0','retired=1')),
    }
    for name,mutation in bad_cases.items():
        bad=successor(retained[0]);mutation(bad)
        try:execute([bad])
        except acquisition.AcquisitionFailed:checks.append(name+' hard refuses')
        else:raise AssertionError(name)
    for name,mutate in [
        ('fresh summary epoch replacement',lambda s:s.update(injectAfter=s['injectAfter'].replace('ownerAvailable=1','ownerAvailable=1 epoch=99'))),
        ('fresh owner fields missing',lambda s:s.update(injectAfter=s['injectAfter'].replace('ownerAvailable=1','missingOwner=1'))),
    ]:
        bad=successor(retained[0]);mutate(bad)
        try:execute([retained[0],bad])
        except acquisition.AcquisitionFailed:checks.append(name+' during waiting refuses')
        else:raise AssertionError(name)
    for name,mutate in [('retirement while waiting',bad_cases['native retirement']),
                        ('scope replacement while waiting',bad_cases['scope replacement'])]:
        bad=successor(retained[0]);mutate(bad)
        try:execute([retained[0],bad])
        except acquisition.AcquisitionFailed:checks.append(name+' hard refuses')
        else:raise AssertionError(name)
    for name,mutation in [
        ('cancel after read',lambda c,e,s:setattr(e,'set',True)),
        ('exact deadline after read',lambda c,e,s:setattr(c,'value',c.value+3_000_000_000)),
        ('cached native sample',lambda c,e,s:s.update(readStartedNs=s['readStartedNs']-1_000_000_000)),
        ('mixed native clock',lambda c,e,s:s.update(readStartedNs=1,readFinishedNs=2)),
    ]:
        try:execute([successor(retained[0])],mutation)
        except acquisition.AcquisitionFailed as exc:
            check(exc.last_sample is not None,name+' failure retains last raw sample')
            checks.append(name+' zero dispatch refuses')
        else:raise AssertionError(name)
    for mode in ['deadline without summary','cancel before read','cancel in validation','deadline in validation']:
        clock=Clock();cancel=Cancel(clock);read_count=[]
        sample=copy.deepcopy(retained[0]);sample['injectBefore']=sample['injectAfter']
        if mode=='cancel before read':cancel.set=True
        def read(deadline):
            read_count.append(deadline);clock.value+=100_000_000
            row=copy.deepcopy(sample if mode=='deadline without summary' else successor(retained[0]))
            row['readStartedNs']=clock.value
            clock.value+=100_000_000
            row['readFinishedNs']=clock.value
            return row
        def validate(s,n):
            oracle.activation_snapshot(s,s['ownedBefore'],n)
            if mode=='cancel in validation':cancel.set=True
            if mode=='deadline in validation':clock.value+=3_000_000_000
        try:acquisition.acquire(read,validate,cancel,clock.value+3_000_000_000,_clock_ns=clock)
        except acquisition.AcquisitionFailed as exc:
            check(('cancelled' in str(exc)) if 'cancel' in mode else ('deadline expired' in str(exc)), mode+' refuses by exact reason')
            if mode=='cancel before read':check(not read_count,'pre-cancel performs no native read')
        else:raise AssertionError(mode)
    own=retained[0]['ownedBefore'];admission=oracle.Admission('practice05','friend',own,'aa')
    try:admission.check(retained[0],retained[0]['readFinishedNs'])
    except oracle.Unqualified as exc:check(str(exc)=='fresh owner-frame mapping summary missing','original Admission reproduces retained cadence failure')
    try:admission.check(successor(retained[0]),retained[0]['readFinishedNs'])
    except oracle.Unqualified as exc:check(str(exc)=='oracle permanently retired; no reanchor','acquisition cannot revive already retired Admission')
    print(json.dumps(dict(ok=True,checks=checks,count=len(checks),nativeExecuted=False,
                         cppControls='PENDING_STEAM_NATIVE_BUILD',positiveSuccessorSummary='CONSTRUCTED_NOT_RETAINED'),indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('evidence',type=Path);p.add_argument('--oracle',type=Path,required=True)
    a=p.parse_args();run(a.evidence,a.oracle)
