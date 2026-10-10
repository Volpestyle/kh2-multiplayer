"""Retained ordering + exact source guards; C++ execution belongs to steam."""
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = '03329070db92a3b9dd73386492b5be0890728ec9'
checks = []


def check(ok, label):
    assert ok, label
    checks.append(label)


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


fixture = ROOT / 'tests/fixtures/ordinary_native_first'
manifest = json.loads((fixture / 'manifest.json').read_text())
logs = {}
for row in manifest['files']:
    compressed = (fixture / (row['name'] + '.gz')).read_bytes()
    raw = gzip.decompress(compressed)
    check(len(raw) == row['bytes'] and hashlib.sha256(raw).hexdigest() == row['sha256'] and
          hashlib.sha256(compressed).hexdigest() == row['compressedSha256'], row['name'] + ' exact retained bytes')
    logs[row['name']] = raw.decode('utf-8').splitlines()
host, client, runtime = (logs[n] for n in ('inject-0.log', 'inject-1.log', 'runtime-1.log'))
leave = next(line for line in client if '[combat-disappearance]' in line and 'frame=801 ' in line)
leave_ns = int(re.search(r'startNs=(\d+)', leave)[1])
check('originalNetId=4 ' in leave and 'originalHp=20 ' in leave and 'scopeStable=1 ' in leave and
      'watchedPresent=0 ' in leave, 'actual frame801 positive-HP native leave with complete stable absence')
check(any('serial=198 ' in line and 'kind=11 reason=3 ' in line for line in client), 'original Mapping refusal retained, never retro-pass')
check([int(re.search(r'cut=(\d+)', line)[1]) for line in client if 'action=client-admitted ' in line] == [5],
      'client only admitted cut5')
check(any('action=host-published cut=6 ' in line for line in host) and
      any('action=host-published cut=7 ' in line for line in host), 'host ordered cut6 and cut7 retained')
unknown = [i for i, line in enumerate(runtime) if 'Unknown packet type from host: 48' in line]
check(len(unknown) == 3, 'initial cut plus later two callbacks retained')
left = next(line for line in runtime[:unknown[1]][::-1] if '[combat-causal-runtime]' in line)
right = next(line for line in runtime[unknown[2]+1:] if '[combat-causal-runtime]' in line)
low, high = (int(re.search(r'qpc=(\d+)', line)[1]) for line in (left, right))
check(leave_ns < low < high and 'serial=23 ' in left and 'serial=24 ' in right,
      'runtime processed later cuts after native leave, bracketed by retained QPCs')
check(not any('native-retire ' in line for line in client), 'no native-leaf request occurred in original B')
net = source('common/src/NetworkClient.cpp')
check(net.index('callbacks_.onWorldEnvelope') < net.index('Unknown packet type from host:'),
      'Unknown48 log is after authenticated envelope callback, not a drop')
sync, impl = source('inject/src/EnemySync.cpp'), source('inject/src/EnemyOrdinaryLifecycle.inl')
check(body(sync, 'bool RequestOrdinaryNativeLeave(') == body(original('inject/src/EnemySync.cpp'), 'bool RequestOrdinaryNativeLeave('),
      'native leaf byte-exact from033')
check(body(sync, 'bool ReleaseClientClaims(std::uint32_t frame) {') == body(original('inject/src/EnemySync.cpp'), 'bool ReleaseClientClaims(std::uint32_t frame) {'),
      'complete manifest/native release gate byte-exact')
release = body(sync, 'bool ReleaseClientClaims(std::uint32_t frame) {')
for predicate in ('!g_host.manifestComplete', '!host.hpKnown', 'native->hp != host.hp',
                  'living != census.enemies.size()', 'matches != 1',
                  'revision != g_clientManifestRevision', 'hpSequence != g_hostHpSequence'):
    check(predicate in release, 'unchanged release hard predicate: ' + predicate)
control = source('tests/NativeHitClaimTest.cpp')
check('absent-before-cut retirement cannot weaken full manifest release' not in control and
      'cut6 alone releases only the complete matching four-survivor native census' in control,
      'cut6 current membership positive replaces the incorrect future-birth expectation')
for label in ('already-absent retirement cannot release incomplete manifest',
              'already-absent retirement cannot release unknown survivor HP',
              'already-absent retirement cannot release mismatched survivor HP',
              'retired source ID cannot account for an extra living native body',
              'authenticated cut7 birth alone cannot release an unbound replacement',
              'replacement native binding without admitted HP cannot release claims'):
    check(label in control, 'actual-handler negative control added: ' + label)
check('presence.seen+1!=g_hitTraceFrame' in impl and 'presence.checked+1!=g_hitTraceFrame' in impl and
      'g_hitTraceFrame-presence.absent>120' in impl, 'adjacent living/absence proofs with original bounded wait')
check('refuse-reappeared-or-reused-actor' in impl and 'refuse-presence-bookend' in impl,
      'reappearance/reuse and independent bookend failure poison original pending proof')
check('originals!=1' in impl and 'PopulationSpawnRoots(tracked)==PopulationSpawnRoots(anchor.original)' in impl and
      'tracked.ordinaryBinding.highWater==static_cast<int>(presence.target.netId)' in impl,
      'pending proof requires the unchanged unique tracked original binding')
check('target=original->target' in impl and 'complete-native-absence-before-cut' in impl,
      'cut uses authenticated cached positive target and independently completed native absence')
receiver = body(impl, 'void ReceiveOrdinaryLifecycle(')
check(receiver.index('bookend.watchedActorPresent') < receiver.index('g_ordinaryRetirements.push_back'),
      'independent final absence precedes retirement mutation')
check('localAliases!=1' in impl and 'census.enemies.size()>5' in impl, 'replacement exclusion cannot hide ambiguous/extra bodies')
trace = source('inject/src/CombatCausalTrace.hpp')
check('Kind::MembershipPending' in trace and 'receipt.reason=Reason::Pending;Push(receipt)' in trace and
      'l->nativePending=false' in trace, 'pending diagnostic explicitly unqualified until matching raw retirement')
for method in ('void Hit(', 'void Claim(', 'void HostApply(', 'void Packet(', 'void Applied(', 'bool BindBirth('):
    check(body(trace, method) == body(original('inject/src/CombatCausalTrace.hpp'), method), method + ' exact')
for path in ('common/src/NetworkClient.cpp', 'server/src/SessionHost.cpp', 'CMakeLists.txt',
             'inject/src/OrdinaryEnemyBinding.hpp', 'inject/src/NativeHitTrace.hpp'):
    check(source(path) == original(path), path + ' unchanged')
check(not manifest['nativeConsumerFailureReasonRetained'] and not manifest['acceptance'], 'retained evidence limits explicit')
print(json.dumps(dict(status='PASS', checks=checks, count=len(checks),
                     runtimeCutProcessedQpcBracket=[low, high], clientLeaveNs=leave_ns,
                     nativeExecuted=False, acceptance=False), indent=2))
