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
control = native[native.index('// Same native living leave with experiment OFF/ON.'):native.index('for(auto& removalSpawn:')]
for marker in ('absenceMode<2', 'g_ordinaryLifecycleRequested=absenceMode!=0',
               'GetTickCount64()-DESPAWN_GRACE_MS-1', 'encode(EnemyDeath{9,4})',
               'absenceBootstrapPackets', 'absenceBeforeGracePackets', 'absenceTerminalPackets',
               'HostEnvelope(absencePacket,++absenceSourceSerial)',
               '!ReleaseClientClaims(absenceBeforeGraceFrame)', 'ReleaseClientClaims(absenceExpiryFrame)==(absenceMode==0)',
               'g_ordinaryHostBlocked&&OrdinaryCutCurrent()', 'nativeCalls==0&&deathCalls==0'):
    check(marker in control, 'actual host/client default-OFF vs ON control: ' + marker)
check(not subprocess.check_output(['git', 'diff', '5170d399', '--', 'inject', 'common', 'server', 'CMakeLists.txt'], cwd=ROOT).strip(),
      'fixture correction changes no product or build inputs')
interval = int(re.search(r'HP_INTERVAL_FRAMES = (\d+);', sync).group(1))
main_interval = int(re.search(r'HP_INTERVAL_FRAMES = (\d+);', main).group(1))
check(interval == main_interval == 6, 'main and candidate use the same actual six-frame HP cadence')
check(40 % interval != 0 and 41 % interval != 0 and 42 % interval == 0,
      'former40/41 fixture cannot publish baseline/pregrace HP;42 can')
check('absenceInitialFrame=7*HP_INTERVAL_FRAMES' in control and
      'absenceBeforeGraceFrame=absenceInitialFrame+HP_INTERVAL_FRAMES' in control and
      'absenceExpiryFrame=absenceBeforeGraceFrame+HP_INTERVAL_FRAMES' in control and
      all(frame % interval == 0 for frame in (7*interval, 8*interval, 9*interval)),
      'corrected baseline/pregrace/expired calls are actual HP publication frames42/48/54')
check('static_assert(absenceInitialFrame%HP_INTERVAL_FRAMES==0' in control and
      'HostFrame(absenceInitialFrame+1' in control and '!HasHp(1,1000)' in control,
      'compiled fixture asserts cadence and distinguishes adjacent nonpublication frame43')
check('absencePointSpacing=100.0f' in control and 'static_assert(absencePointSpacing>SPAWN_POINT_TOLERANCE)' in control and
      'offsets::entity::POS_X,absencePointSpawn.spawnPos.x' in control and
      '!SamePoint(g_inst.spawns[absenceLeft].spawnPos,g_inst.spawns[absenceRight].spawnPos)' in control,
      'actual native and historical spawn points are spread and independently checked against false refill')
check(control.count('seedAbsencePoints();') == 2 and 'SamePoint(absenceNative->position' in control,
      'both owned host/client fixtures use matching checked native point layout')
check('HostFrame(absenceInitialFrame,absenceFresh,absenceCensus)&&HasHp(1,1000)&&HasHp(4,1000)' in control,
      'original bootstrap assertion now proves actual baseline HP publication before client admission')
check('if (frame % HP_INTERVAL_FRAMES == 0 && !hp.entries.empty())' in main and
      't.spawnIndex > s.spawnIndex && t.objectId == s.objectId && SamePoint(t.spawnPos, s.spawnPos)' in main,
      'both missing prerequisites are exact main policy, not new experimental product behavior')
check('only schema1 population sends terminal history' in native,
      'different-allocation/refill/metadata fixture checks main terminal disposition')
print(json.dumps(dict(status='PASS', count=len(checks), checks=checks, nativeExecuted=False, acceptance=False), indent=2))
