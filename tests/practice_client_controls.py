"""Offline preservation/retained-fact checks; C++ execution is Steam-owned."""
import argparse
import json
import subprocess
import tempfile
from pathlib import Path
from companion_retained_fixture import fields
from practice_client_retained_fixture import client_fixture

ROOT = Path(__file__).resolve().parents[1]
BASE = '713cd6f4f366216119ef4a23bf10c23889b56bd5'


def run(evidence):
    checks = []
    def check(value, name):
        assert value, name
        checks.append(name)
    def old(path):
        return subprocess.check_output(['git', 'show', BASE+':'+path], cwd=ROOT).decode().replace('\r\n', '\n')
    def now(path):
        return (ROOT/path).read_text(encoding='utf-8')
    def section(s, a, b):
        return s[s.index(a):s.index(b, s.index(a))]
    before, after = old('inject/src/CombatCausalTrace.hpp'), now('inject/src/CombatCausalTrace.hpp')
    for a, b in [('inline bool PlayerEnemyHit', '// Separate host-only'),
                 ('inline bool HostCompanionEnemyHit', '// Canonical host player')]:
        check(section(before, a, b) == section(after, a, b), a+' unchanged')
    check(section(before, 'inline bool HostPlayerDirectStatEnemyHit', 'inline std::uint64_t Digest') ==
          section(after, 'inline bool HostPlayerDirectStatEnemyHit', '// Client direct Stat'), 'host direct witness unchanged')
    guard = 'if(l&&!r.locallyQualified&&e.before.victim.hp!=e.after.victim.hp)Retire(s,now,Reason::UnknownDelta);'
    check(guard in before and guard in after, 'UnknownDelta guard unchanged')
    for path in ['inject/src/EntityHook.cpp', 'inject/src/EnemySync.cpp', 'inject/src/NativeHitTrace.cpp',
                 'inject/src/NativeHitTrace.hpp', 'inject/src/DamagePolicy.cpp', 'inject/src/DamagePolicy.hpp',
                 'tools/scenario/causal_summary_acquisition.py', 'tests/PracticeCombinedControls.hpp']:
        check(old(path) == now(path), path+' unchanged')
    log = evidence/'inject-1.log'
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp)/'fixture.hpp'
        client_fixture(log, out)
        check(out.read_bytes() == (ROOT/'tests/PracticeClientRetainedFixture.hpp').read_bytes(), 'both real client cases regenerate exact')
        check(out.with_suffix('.json').read_bytes() == (ROOT/'tests/PracticeClientRetainedFixture.json').read_bytes(), 'client provenance regenerates exact')
    lines = log.read_text(encoding='utf-8').splitlines()
    for seq in (7, 9):
        def row(prefix, **expected):
            rows = [fields(line) for line in lines if prefix in line and
                    all(fields(line).get(k) == str(v) for k, v in expected.items())]
            assert len(rows) == 1
            return rows[0]
        policy = row('[damagepolicy] event ', seq=seq)
        check(all(policy[k] == v for k, v in dict(role='2', sourceClass='1', victimClass='5',
              action='2', reason='13', supported='1', recorded='1', ownerThread='1', contextAvailable='1',
              claimAttempted='1', claimQueued='1', revalidationAttempted='1', revalidationPassed='1',
              zeroAttempted='1', zeroResult='2').items()), f'event{seq} actual ClientClaim decision and attempted suppression')
        before_hit = row('[hittrace] input ', seq=seq, phase=0)
        after_hit = row('[hittrace] input ', seq=seq, phase=1)
        authority = row('[damagepolicy] input ', seq=seq, phase=0)
        check(before_hit['syncDrop'] == after_hit['syncDrop'] == '1' and authority['syncDrop'] == '0',
              f'event{seq} real predecision/observer syncDrop phase difference')
        check(after_hit['damage'] == '0' and before_hit['damage'] == policy['amount'] == '3',
              f'event{seq} client native hit amount zeroed')
        victim_before = row('[hittrace] actor ', seq=seq, phase=0, subject=0)
        victim_after = row('[hittrace] actor ', seq=seq, phase=1, subject=0)
        child = row('[hittrace] child ', seq=seq, index=0)
        check(victim_before['hp'] == victim_after['hp'] == child['result'] and child['delta'] == '0',
              f'event{seq} local enemy HP unchanged and direct Stat delta zero')
        event = row('[hittrace] event ', seq=seq)
        check(event['callerRva'] == '410EFF' and event['takeCalls'] == '0' and event['statCalls'] == '1'
              and child['callerRva'] == '3D37D2' and child['matching'] == '0', f'event{seq} direct Apply/Stat family')
    check('copy.count==1 && copy.qualified && copy.frame==e.before.context.frame' in after and
          'p={};' in after and 'e.sequence>lastClientDirectEvent_' in after,
          'bounded one-shot queue association and event replay guard present')
    check('if(!pending){r.locallyQualified=false;Retire(s,now,Reason::Capacity);}' in after, 'association capacity fail closed')
    print(json.dumps(dict(ok=True, count=len(checks), checks=checks, nativeExecuted=False,
                         cppControls='PENDING_STEAM_NATIVE_BUILD', retroactiveAcceptance=False), indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('evidence', type=Path)
    run(parser.parse_args().evidence)
