"""Source projections for the two b0b3 failed controls; native execution is separate."""
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = '6472df95'
FAILED = 'b0b3fadd'
checks = []


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)


def check(value, name):
    assert value, name
    checks.append(name)


for directory in ('common', 'server'):
    check(not git('diff', '--name-only', BASE, FAILED, '--', directory).strip(),
          directory + ' entire product subtree unchanged across suspected regression')
for path in ('CMakeLists.txt', 'tests/WorldWireFixture.hpp', 'tests/WorldSyncTest.cpp'):
    check(git('show', BASE + ':' + path) == git('show', FAILED + ':' + path),
          path + ' Git blob unchanged at failed candidate')
for path in ('inject/src/EnemySync.cpp', 'inject/src/EnemyOrdinaryLifecycle.inl'):
    check(git('show', '2e845b73:' + path) == git('show', FAILED + ':' + path),
          path + ' product unchanged by prior failed-control correction 2e845b73')
cmake = (ROOT / 'CMakeLists.txt').read_text()
check('target_link_libraries(kh2coop_world_test PRIVATE kh2coop_server_core kh2coop_common)' in cmake,
      'world target excludes inject consumer')
native = (ROOT / 'tests/NativeHitClaimTest.cpp').read_text()
bootstrap = native[native.index('const auto relayBootstrapCut='):native.index('for(unsigned delayedCase=')]
check(bootstrap.index('g_host={};') < bootstrap.index('SeedPreparationShadows(1000)'),
      'native actor seeding follows cleared remote manifest')
check(bootstrap.index('native arrival alone cannot admit') < bootstrap.index('encode(relayBootstrapManifest)'),
      'negative missing-manifest control precedes authenticated manifest delivery')
check(bootstrap.index('HostEnvelope(encode(relayBootstrapManifest),52)') < bootstrap.rindex('RetryOrdinaryLifecycle();'),
      'actual consumer receives manifest before same original cut retry')
check(bootstrap.count('encode(relayBootstrapCut)') == 1 and
      'OrdinaryCutCurrent()&&g_pendingOrdinaryCuts.empty()' in bootstrap,
      'original positive admission predicate retained with exactly one cut')
world = (ROOT / 'tests/WorldSyncTest.cpp').read_text()
prime = world[world.index('const auto receivePeriodicHp'):world.index('host.sendEventHold(EventHold {12')]
for predicate in ('periodicDeadline = nowMs() + 3000', 'periodicSends < 60',
                  'nextPeriodicSend = periodicNow + 50', 'periodicHp.sequence = ++hpSequence',
                  'receivedHp.epoch == periodicHp.epoch', 'receivedEntry.netId == expectedEntry.netId',
                  'receivedEntry.hp == expectedEntry.hp', 'receivedEntry.maxHp == expectedEntry.maxHp',
                  'receivedHp.sequence >= firstPeriodicSequence', 'receivedHp.sequence <= hpSequence'):
    check(predicate in prime, 'bounded exact periodic acquisition: ' + predicate)
check('host.setLinkConditions({0,0,1.0f,811}, {});' in prime and
      'host.outbound_.dropped() == droppedHpBefore + 1' in prime,
      'first actual unreliable send is deterministically dropped and asserted')
check(prime.count('receivePeriodicHp(EnemyHp {') == 2 and 'host.setLinkConditions({}, {});' in prime,
      'both epochs recover using normal transport after restoring link')
check('worldfixture::send' not in prime[:prime.index('// Drop the actual')] and 'host.sendEnemyHp(periodicHp)' in prime,
      'positive priming never substitutes reliable fixture delivery')
for label in ('actual unreliable old-room HP is positively received',
              'actual unreliable current-room HP is positively received'):
    check(label in prime, 'original positive assertion retained: ' + label)
print(json.dumps(dict(status='PASS', count=len(checks), checks=checks,
                      nativeExecuted=False, acceptance=False), indent=2))
