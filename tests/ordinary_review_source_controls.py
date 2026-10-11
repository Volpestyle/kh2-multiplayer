"""Review-boundary source projections. Native producer/consumer checks are separate."""
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = '6472df95bf48028c8255c42a3ad80cd9f36ea058'
checks = []


def check(value, name):
    assert value, name
    checks.append(name)


def source(path):
    return (ROOT / path).read_text(encoding='utf-8')


def original(path):
    return subprocess.check_output(['git', 'show', BASE + ':' + path], cwd=ROOT).decode()


def body(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


sync = source('inject/src/EnemySync.cpp')
impl = source('inject/src/EnemyOrdinaryLifecycle.inl')
prior_impl = original('inject/src/EnemyOrdinaryLifecycle.inl')
native = source('tests/NativeHitClaimTest.cpp')
check(body(impl, 'void TryReceiveOrdinaryLifecycle(').replace('TryReceiveOrdinaryLifecycle', 'ReceiveOrdinaryLifecycle') ==
      body(prior_impl, 'void ReceiveOrdinaryLifecycle('), 'actual native admission body byte-exact except helper name')
for signature in ('bool ReleaseClientClaims(std::uint32_t frame) {', 'bool RequestOrdinaryNativeLeave('):
    check(body(sync, signature) == body(original('inject/src/EnemySync.cpp'), signature), signature + ' unchanged')
for path in ('inject/src/EnemyPopulationRepair.inl', 'inject/src/OrdinaryEnemyBinding.hpp',
             'inject/src/CombatCausalTrace.hpp', 'common/include/kh2coop/OrdinaryLifecycle.hpp',
             'common/include/kh2coop/PopulationCut.hpp', 'CMakeLists.txt', 'tests/PopulationRepairNativeTest.cpp'):
    check(source(path) == original(path), path + ' exact: no repair/death/claim/relabel or test-registration change')
queue = body(impl, 'void ReceiveOrdinaryLifecycle(')
for predicate in ('ValidatePopulationCut(cut)', 'source.sessionId.empty()', 'source.kind!=WorldSourceKind::Native',
                  'cut.epoch!=g_host.epoch', 'cut.location!=PopulationLocation(*g_lastRoomPacket)',
                  'g_pendingOrdinaryCuts.size()>=16', 'history.Admit(cut)', 'source.hostSourceSerial<='):
    check(predicate in queue, 'pending cut authentication/bound: ' + predicate)
pending = body(impl, 'bool PendingOrdinarySourceCurrent(')
for predicate in ('pending.generation==WorldSessionGeneration()', 'pending.cut.epoch==g_host.epoch',
                  'pending.load==warp::LoadSerial()', 'pending.transition==warp::TransitionSerial()',
                  'GetTickCount64()-pending.receivedMs<=30000', 'source.sourceConnectionId',
                  'source.sourceDeliverySerial', 'source.targetConnectionId', 'source.targetDeliverySerial'):
    check(predicate in pending, 'original queued authority: ' + predicate)
room = sync[sync.index('if (type == PacketType::RoomTransition) {'):]
check(room.index('ResetOrdinaryLifecycle();') < room.index('g_lastRoomPacket = t;') and
      room.index('ClearPendingOrdinaryCuts();') < room.index('g_lastRoomPacket = t;'), 'superseding ordered room clears history and queue')
check(sync.index('RetryOrdinaryLifecycle(); // ordered bootstrap') < sync.index('if(!ConsumeOrdinaryRetirements(frame,census))'),
      'tick retries before client retirement/store processing')
check('g_ordinaryLifecycleRequested && !g_populationRequested' in body(impl, 'bool OrdinaryLifecycleRoom('),
      'experimental schema2 and schema1 population mode isolated at all room gates')
check('bool g_ordinaryLifecycleRequested = false;' in sync and 'KH2COOP_ORDINARY_LIFECYCLE_EXPERIMENTAL' in sync,
      'living-removal limitation explicitly defaults off')
host = body(sync, 'bool HostFrame(')
check('(!OrdinaryLifecycleRoom() && now - s.goneSinceMs > DESPAWN_GRACE_MS)' in host and
      'if (refilled && !OrdinaryLifecycleRoom())' in host,
      'only supported experiment forbids default grace and refill supersession')
check('g_ordinaryHostBlocked=true' in impl and 'if(!OrdinaryLifecycleRoom() || g_ordinaryHostBlocked)return true;' in impl,
      'failed experimental proof suspends cuts instead of retrying disposed roots')
for label in ('actual relay RoomTransition then cut retains bootstrap before native arrival',
              'pending relay cut revalidates after recovery without another host event',
              'BC population-enabled manifest HP and motion still publish without schema2 retirement',
              'outside ordinary lifecycle different-allocation living/dead or population metadata refill cannot freeze HP publication',
              'unlink plus invalidated status suspends experimental membership once'):
    check(label in native, 'actual native control present, not executed: ' + label)
print(json.dumps(dict(status='PASS', count=len(checks), checks=checks, nativeExecuted=False,
                      cppControls='PENDING_STEAM', acceptance=False), indent=2))
