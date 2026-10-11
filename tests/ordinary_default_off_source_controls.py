"""Default-off absence/refill projection against main; native controls need the operator."""
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
checks = []


def original(revision, path):
    return subprocess.check_output(['git', 'show', revision + ':' + path], cwd=ROOT).decode()


def check(value, name):
    assert value, name
    checks.append(name)


def body(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def normalized(text):
    return re.sub(r'\s+', '', re.sub(r'//[^\n]*', '', text))


sync = (ROOT / 'inject/src/EnemySync.cpp').read_text()
main = original('3c0f8e4', 'inject/src/EnemySync.cpp')
parent = original('2e845b73', 'inject/src/EnemySync.cpp')
start = '// Despawns: refilled at the same point'
def despawns(text):
    segment = text[text.index(start):]
    return segment[:segment.index('    EnemyHp hp;')]

projected = despawns(sync)
check('if (refilled && !OrdinaryLifecycleRoom())' in projected, 'main refill supersession fenced outside supported experiment')
check('(!OrdinaryLifecycleRoom() && now - s.goneSinceMs > DESPAWN_GRACE_MS)' in projected,
      'main grace fenced outside supported experiment')
check('if(OrdinaryLifecycleRoom() && s.lastHp>0 && !g_ordinaryHostBlocked)return false;' in projected,
      'experiment retains its original hard proof guard')
# Project the OFF branch. The added lifecycleRetired skip only excludes rows
# already retired by schema2; that authority cannot be created with the mode OFF.
projected = projected.replace(' || s.lifecycleRetired', '')
projected = projected.replace('        if(OrdinaryLifecycleRoom() && s.lastHp>0 && !g_ordinaryHostBlocked)return false;\n', '')
projected = projected.replace('if (refilled && !OrdinaryLifecycleRoom())', 'if (refilled)')
projected = projected.replace('(!OrdinaryLifecycleRoom() && now - s.goneSinceMs > DESPAWN_GRACE_MS)',
                              'now - s.goneSinceMs > DESPAWN_GRACE_MS')
check(normalized(projected) == normalized(despawns(main)),
      'default-off despawn statements exactly match main after mode/quarantine projection')
for signature in ('bool ReleaseClientClaims(std::uint32_t frame) {', 'bool RequestOrdinaryNativeLeave(',
                  'bool ResolveOrdinaryBindings(', 'bool ReceiveWorldPackets('):
    check(body(sync, signature) == body(parent, signature), 'unchanged original hard boundary: ' + signature)
check((ROOT / 'inject/src/EnemyOrdinaryLifecycle.inl').read_text().replace('\r\n', '\n') ==
      original('2e845b73', 'inject/src/EnemyOrdinaryLifecycle.inl').replace('\r\n', '\n'),
      'experimental proof/suspension/pending/retirement implementation unchanged')
check('constexpr std::uint64_t DESPAWN_GRACE_MS = 3000;' in sync, 'original 3000ms grace constant unchanged')
native = (ROOT / 'tests/NativeHitClaimTest.cpp').read_text()
control = native[native.index('for(unsigned absenceMode='):native.index('for(auto& removalSpawn:')]
for marker in ('absenceMode<2', 'g_ordinaryLifecycleRequested=absenceMode!=0',
               'GetTickCount64()-DESPAWN_GRACE_MS-1', 'encode(EnemyDeath{9,4})',
               'absenceBootstrapPackets', 'absenceBeforeGracePackets', 'absenceTerminalPackets',
               'HostEnvelope(absencePacket,++absenceSourceSerial)',
               '!ReleaseClientClaims(41)', 'ReleaseClientClaims(42)==(absenceMode==0)',
               'g_ordinaryHostBlocked&&OrdinaryCutCurrent()', 'nativeCalls==0&&deathCalls==0'):
    check(marker in control, 'actual host/client default-OFF vs ON control: ' + marker)
check('only schema1 population sends terminal history' in native,
      'different-allocation/refill/metadata fixture checks main terminal disposition')
print(json.dumps(dict(status='PASS', count=len(checks), checks=checks, nativeExecuted=False, acceptance=False), indent=2))
