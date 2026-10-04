"""Strict saved evidence joins for forced-resync; no game/process access.

Native ACKs are serialization observations. Only matching relay and host-runtime
terminal records establish transport completion; neither proves native work alone.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import re
import shlex
import struct
from pathlib import Path

ROOM = ('epoch', 'world', 'room', 'door', 'map', 'battle', 'event')
LIMITS = [
    'Native ACK checksMask=63 attests the production full-progress/two-frame census checks; logs do not contain the canonical snapshot bytes for independent SHA recomputation.',
    'A queue receipt, Received/Arrived ACK, serialized=1 or bridgeSent=0 is not native convergence or relay acceptance.',
    'This is bounded local-rig saved evidence, not remote-machine acceptance, creation identity or general dead/nonempty bootstrap support.',
    'Checkpoint acceptance conservatively requires a complete Bootstrap Converged witness from every original target. The relay can request a checkpoint after only one target converges; missing other-target bootstrap evidence remains incomplete, never synthesized from Received/Arrived.',
]


def _fields(line):
    fields = {}
    for item in shlex.split(line):
        if '=' not in item:
            raise ValueError('non-field token')
        key, value = item.split('=', 1)
        if key in fields:
            raise ValueError('duplicate field ' + key)
        fields[key] = value
    return fields


def _number(row, key, bits=64):
    value = row[key]
    if isinstance(value, bool) or not re.fullmatch(r'\d+', str(value)):
        raise ValueError('invalid unsigned ' + key)
    value = int(value)
    if value >= 1 << bits:
        raise ValueError('width overflow ' + key)
    return value


def _digest(value, width=64):
    if not re.fullmatch('[0-9a-fA-F]{%d}' % width, value):
        raise ValueError('invalid digest/session')
    return value.lower()


def _key(row):
    return (_digest(row['session'], 32), _number(row, 'host'), _number(row, 'request'))


def _targets(row):
    count = _number(row, 'targetCount', 8)
    if count not in (1, 2):
        raise ValueError('missing original targets')
    result = []
    for index in range(count):
        if 'target%d' % index in row:
            fields = row['target%d' % index].split(',')
            if len(fields) != 7:
                raise ValueError('malformed terminal target')
            slot, conn, delivery, status, cut, fingerprint, error = fields
            values = {'slot': slot, 'connection': conn, 'delivery': delivery,
                      'status': status, 'cut': cut}
            numeric = [_number(values, field) for field in ('slot', 'connection', 'delivery', 'status', 'cut')]
            _digest(fingerprint)
            if error != '-':
                if len(error) > 512 or not re.fullmatch(r'(?:[0-9a-fA-F]{2})+', error):
                    raise ValueError('invalid encoded error')
                bytes.fromhex(error).decode('utf-8', errors='strict')
            result.append(tuple(numeric[:3]))
        else:
            result.append(tuple(_number(row, 'target%d%s' % (index, field))
                                for field in ('Slot', 'Connection', 'Delivery')))
    if len(set(result)) != count or len({x[0] for x in result}) != count or any(
            slot not in (1, 2) or not conn or not delivery for slot, conn, delivery in result):
        raise ValueError('invalid target identities')
    if len({x[1] for x in result}) != count:
        raise ValueError('duplicate target connection')
    return result


def _records(text, problems, label):
    records = []
    if len(text) > 4 * 1024 * 1024:
        problems.append(label + ': evidence exceeds 4MiB bound')
        return records
    for line in text.splitlines(keepends=True):
        marker = re.search(r'\[resync\] (plan|ack|evidence) |\[resync-result\] ', line)
        if not marker:
            continue
        try:
            if not line.endswith('\n'):
                raise ValueError('truncated final record')
            kind = marker.group(1) or 'result'
            if kind == 'evidence':
                raise ValueError('native evidence suppression')
            row = _fields(line[marker.end():].strip())
            _key(row)
            if kind in ('plan', 'result'):
                _targets(row)
            records.append((kind, row))
        except (ValueError, KeyError) as error:
            problems.append(label + ': ' + str(error))
    return records


def _native_hash(rows):
    encoded = struct.pack('<II', 0x3145484B, len(rows))
    encoded += b''.join(struct.pack('<HIi', *row) for row in rows)
    value = 2166136261
    for byte in encoded:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def _population(bundle, room, selected, acknowledgments, problems):
    """Recompute full native rows and require exact independent census joins."""
    try:
        state = bundle['statehash']
        census = bundle['census']
        peers = ['0'] + [str(slot) for slot in selected]
        if not state['ready'] or state['problems'] or not census['complete']:
            raise ValueError('statehash/census incomplete')
        samples = state['matchingSamples']
        if len(samples) != 2 or len(census['snapshots']) != 2:
            raise ValueError('exactly two fresh saved samples required')
        baseline = None
        for sample in samples:
            if sample['epoch'] != room[0] or set(sample['instances']) != set(peers):
                raise ValueError('sample epoch/denominator mismatch')
            for peer in peers:
                row = sample['instances'][peer]
                if row['location'] != list(room[1:]) or row['unmatched']:
                    raise ValueError('full room or unmatched population')
                live = sorted([[r[k] for k in ('netId', 'objectId', 'hp')]
                               for r in row['nativeRows'] if r['hp'] > 0])
                if live != row['liveRows'] or len(live) != row['count'] or any(
                        n <= 0 or obj <= 0 or hp <= 0 for n, obj, hp in live):
                    raise ValueError('invalid bound typed HP rows')
                if len({r[0] for r in live}) != len(live) or _native_hash(live) != row['enemies']:
                    raise ValueError('duplicate binding or recomputed native hash mismatch')
                identity = (row['enemies'], row['progress'], live)
                if baseline is None:
                    baseline = identity
                if identity != baseline:
                    raise ValueError('population/HP/progress changed across peers or samples')
                if peer != '0':
                    ack = acknowledgments[int(peer)]
                    if row['count'] != _number(ack, 'enemyCount') or _number(ack, 'deadCount'):
                        raise ValueError('ACK/census population mismatch or unsupported dead history')
                    delta = (row['frame'] - _number(ack, 'frame2')) & 0xffffffff
                    if not 0 < delta < 0x80000000:
                        raise ValueError('hash does not follow native convergence frame')
        for peer in peers:
            if samples[0]['instances'][peer]['frame'] == samples[1]['instances'][peer]['frame']:
                raise ValueError('repeated hash frame')
        census_frames = {peer: [] for peer in peers}
        for snap in census['snapshots']:
            if set(snap['peers']) != set(peers):
                raise ValueError('census denominator mismatch')
            for peer in peers:
                data = snap['peers'][peer]
                comparison = data['comparison']
                if not data['complete'] or not comparison['comparisonValid'] or comparison['nativeLivingAbsentFromLatestHash'] or comparison['publishedRowsAbsentFromNativeList']:
                    raise ValueError('independent census incomplete/address mismatch')
                native = sorted((int(r['address']), int(r['objectId']), int(r['hp'])) for r in data['livingCombatRows'])
                logged = [r for r in data['logsAfter']['nativeRows'] if int(r['hp']) > 0]
                joins = sorted((int(r['actor'], 16), int(r['objectId']), int(r['hp'])) for r in logged)
                typed = sorted([int(r[k]) for k in ('netId', 'objectId', 'hp')] for r in logged)
                if native != joins or len({r[0] for r in native}) != len(native) or typed != baseline[2]:
                    raise ValueError('native address/type/HP and hash rows do not join exactly')
                h = data['logsAfter']['hash']
                location = [int(h[k], 16 if k in ('world', 'room') else 10)
                            for k in ('world', 'room', 'door', 'map', 'btl', 'evt')]
                if int(h['epoch']) != room[0] or location != list(room[1:]) or int(h['unmatched']):
                    raise ValueError('independent census hash room/epoch mismatch')
                if int(h['enemies'], 16) != _native_hash(typed) or int(h['progress'], 16) != baseline[1] or int(h['count']) != len(native):
                    raise ValueError('independent census hash/progress/count mismatch')
                frame = int(h['frame'])
                if not 0 <= ((frame - samples[-1]['instances'][peer]['frame']) & 0xffffffff) < 0x80000000:
                    raise ValueError('independent census predates post-resync statehash')
                census_frames[peer].append(frame)
        if any(frames[0] == frames[1] for frames in census_frames.values()):
            raise ValueError('repeated independent census logged frame')
    except (KeyError, TypeError, ValueError, OverflowError, struct.error) as error:
        problems.append('population: ' + str(error))


def _queue_contract(bundle):
    receipt, invocation = bundle['queue'], bundle['invocation']
    if receipt.get('ok') is not True or receipt.get('queued') is not True or receipt.get('nativeConvergence') is not False:
        raise ValueError('missing exact queue receipt')
    if any(not _number(receipt, field) for field in ('processId', 'generation', 'deliverySerial')):
        raise ValueError('unarmed queue receipt')
    mask = _number(invocation, 'targetMask', 8)
    if mask not in (2, 4, 6) or not _number(invocation, 'processId', 32):
        raise ValueError('invalid retained command invocation')
    if _number(receipt, 'processId', 32) != _number(invocation, 'processId', 32) or _number(receipt, 'targetMask', 8) != mask:
        raise ValueError('queue reply PID/mask differs from exact invoked command')
    return mask


def _validated_ack(ack, key, target, room, phase):
    if _key(ack) != key or _number(ack, 'phase', 8) != phase or _number(ack, 'status', 8) != 2:
        raise ValueError('missing same-key phase Converged witness')
    if tuple(_number(ack, field) for field in ('targetSlot', 'connection', 'delivery')) != target:
        raise ValueError('wrong native target identity')
    if tuple(_number(ack, field) for field in ROOM) != room or _number(ack, 'checksMask', 32) != 63 or _number(ack, 'serialized', 8) != 1 or ack['error']:
        raise ValueError('native room/progress/census checks incomplete')
    if ack['bridgeSent'] != '0' or ack['relayAccepted'] != 'unknown':
        raise ValueError('unexpected serialization receipt semantics')
    if not _number(ack, 'cut') or not 0 < _number(ack, 'frame1') < _number(ack, 'frame2'):
        raise ValueError('missing cut/two strictly ordered numeric native frames')
    if not _number(ack, 'loadAfter', 32):
        raise ValueError('unavailable native load serial')
    _number(ack, 'loadBefore', 32)
    _digest(ack['snapshotSHA']); _digest(ack['fingerprint'])
    if int(ack['fingerprint'], 16) == 0 or int(ack['snapshotSHA'], 16) == 0:
        raise ValueError('unavailable fingerprint/SHA')


def audit(bundle, require_population=True):
    problems = list(bundle.get('collectionProblems', []))
    result = {'complete': False, 'nativeConvergence': False, 'problems': problems,
              'limits': LIMITS, 'targets': [], 'populationChecked': False}
    try:
        receipt = bundle['queue']
        mask = _queue_contract(bundle)
        expected = [slot for slot in (1, 2) if mask & (1 << slot)]
        native = {int(slot): _records(text, problems, 'native' + slot) for slot, text in bundle['native'].items()}
        host_plans = [row for kind, row in native.get(0, []) if kind == 'plan']
        if not host_plans:
            raise ValueError('no fresh host plan')
        original = host_plans[0]
        key, targets = _key(original), _targets(original)
        room = tuple(_number(original, field) for field in ROOM)
        if not key[1] or not key[2] or not room[0] or [x[0] for x in targets] != expected or key[1] in {x[1] for x in targets}:
            raise ValueError('original plan key/target denominator invalid')
        if _number(original, 'phase') != 0 or _number(original, 'stage') != 0 or _number(original, 'priorGeneration') != _number(receipt, 'generation'):
            raise ValueError('original host plan not bootstrap queue generation')
        phases = set()
        for slot, records in native.items():
            for kind, row in records:
                if kind not in ('plan', 'ack') or _key(row) != key:
                    raise ValueError('unrelated native transaction within captured interval')
                if kind == 'plan':
                    phase = _number(row, 'phase')
                    if phase not in (0, 1) or _targets(row) != targets or tuple(_number(row, f) for f in ROOM) != room:
                        raise ValueError('changed plan room/targets/phase')
                    if _number(row, 'stage') != (0 if slot == 0 else 1) or not 0 < _number(row, 'remainingMs') <= 30000:
                        raise ValueError('invalid plan stage/deadline')
                    if slot == 0:
                        phases.add(phase)
                elif _number(row, 'status') >= 3:
                    raise ValueError('native unavailable/failed observation retained')
        final_phase = max(phases)
        chosen = {}
        bootstrap_witnesses = {}
        for slot, connection, delivery in targets:
            records = native.get(slot, [])
            if not any(kind == 'plan' and _number(row, 'phase') == final_phase for kind, row in records):
                raise ValueError('missing target final plan')
            candidates = [row for kind, row in records if kind == 'ack' and _number(row, 'phase') == final_phase and _number(row, 'status') == 2]
            if not candidates:
                raise ValueError('no native Converged ACK for target ' + str(slot))
            ack = candidates[0]
            if any(row != ack for row in candidates):
                raise ValueError('conflicting native convergence ACK')
            _validated_ack(ack, key, (slot, connection, delivery), room, final_phase)
            bootstrap = [row for kind, row in records if kind == 'ack' and _number(row, 'phase') == 0 and _number(row, 'status') == 2]
            if not bootstrap:
                raise ValueError('missing complete Bootstrap Converged evidence for original target ' + str(slot))
            baseline = bootstrap[0]
            if any(row != baseline for row in bootstrap):
                raise ValueError('conflicting Bootstrap Converged evidence')
            _validated_ack(baseline, key, (slot, connection, delivery), room, 0)
            if _number(baseline, 'loadBefore', 32) == _number(baseline, 'loadAfter', 32):
                raise ValueError('no genuine target bootstrap load change')
            if final_phase == 1:
                if not (_number(baseline, 'cut') < _number(ack, 'cut') and
                        _number(baseline, 'loadAfter', 32) == _number(ack, 'loadBefore', 32) == _number(ack, 'loadAfter', 32)):
                    raise ValueError('checkpoint cut must advance while preserving verified bootstrap load')
                if _number(ack, 'frame1') <= _number(baseline, 'frame2'):
                    raise ValueError('checkpoint native frames do not follow bootstrap witness')
            bootstrap_witnesses[slot] = baseline
            chosen[slot] = ack
        if len({(_number(row, 'cut'), row['snapshotSHA'].lower(), row['fingerprint'].lower()) for row in bootstrap_witnesses.values()}) != 1:
            raise ValueError('mixed bootstrap cut/SHA/fingerprint witnesses')
        if len({(_number(row, 'cut'), row['snapshotSHA'].lower(), row['fingerprint'].lower()) for row in chosen.values()}) != 1:
            raise ValueError('mixed phase/cut/SHA/fingerprint native witnesses')
        terminals = []
        for component, text in (('relay', bundle['relay']), ('runtime', bundle['runtime'])):
            records = _records(text, problems, component)
            rows = [row for kind, row in records if kind == 'result']
            if not rows:
                raise ValueError('missing ' + component + ' terminal corroboration')
            row = rows[0]
            if any(other != row for other in rows) or _key(row) != key or row['component'] != component or _number(row, 'reason') != 0 or _targets(row) != targets:
                raise ValueError('conflicting/failing terminal or changed denominator')
            observer = (_number(row, 'observerSlot'), _number(row, 'observerConnection'))
            if observer != ((255, 0) if component == 'relay' else (0, key[1])):
                raise ValueError('terminal observer identity mismatch')
            for index, (slot, _, _) in enumerate(targets):
                values = row['target%d' % index].split(',')
                ack = chosen[slot]
                if values[3] != '2' or int(values[4]) != int(ack['cut']) or values[5].lower() != ack['fingerprint'].lower() or values[6] != '-':
                    raise ValueError('terminal does not corroborate exact native target witness')
            terminals.append([row['target%d' % i] for i in range(len(targets))])
        if terminals[0] != terminals[1]:
            raise ValueError('relay/runtime terminal disagreement')
        result.update(key=list(key), room=list(room), targets=[list(x) for x in targets], phase=final_phase,
                      acknowledgments=chosen, nativeConvergence=not problems)
        if require_population:
            _population(bundle, room, expected, chosen, problems)
            result['populationChecked'] = not problems
        result['complete'] = not problems
    except (KeyError, TypeError, ValueError, IndexError) as error:
        problems.append(str(error))
    return result


def register(steps, *, kh2ctl, logs, step_failed, wait_for):
    """Narrow runner integration: one command, no retries; retain all evidence."""
    def forced(ctx, step):
        name = step.get('as', 'forced_resync')
        host = step.get('instance', 0)
        target = str(step.get('slot', 'all'))
        if host != 0 or target not in ('1', '2', 'all'):
            raise step_failed('forced_resync requires explicit host0 and slot1/2/all')
        slots = [0] + ([1, 2] if target == 'all' else [int(target)])
        sources = {'native' + str(i): logs / f'kh2coop_inject_{ctx.inst(i).pid}.log' for i in slots}
        sources.update(relay=ctx.run_dir / 'relay.log', runtime=ctx.run_dir / 'runtime_0.log')
        before = {key: path.read_bytes() if path.exists() else b'' for key, path in sources.items()}
        invocation = {'processId': ctx.inst(host).pid, 'targetMask': 6 if target == 'all' else 1 << int(target), 'slot': target}
        evidence = {'invocation': invocation, 'queue': {}, 'native': {}, 'relay': '', 'runtime': '', 'collectionProblems': [],
                    'sources': {key: {'path': str(path), 'offset': len(before[key]),
                                     'prefixSha256': hashlib.sha256(before[key]).hexdigest()} for key, path in sources.items()}}
        def persist():
            ctx.saved[name] = evidence
            path = ctx.run_dir / (name + '.json')
            path.write_text(json.dumps(evidence, indent=2))
            if path.name not in ctx.artifacts:
                ctx.artifacts.append(path.name)
        def collect():
            for key, path in sources.items():
                raw = path.read_bytes() if path.exists() else b''
                if not raw.startswith(before[key]):
                    issue = key + ': log truncated/replaced during capture'
                    if issue not in evidence['collectionProblems']:
                        evidence['collectionProblems'].append(issue)
                tail = raw[len(before[key]):]
                text = tail.decode('utf-8', errors='replace')
                if key.startswith('native'):
                    evidence['native'][key[6:]] = text
                else:
                    evidence[key] = text
            evidence['audit'] = audit(evidence, require_population=False)
            persist()
            return evidence['audit']['complete']
        try:
            evidence['queue'] = kh2ctl('world-resync', '--slot', target, pid=ctx.inst(host).pid)
            persist()
            try:
                _queue_contract(evidence)
            except (KeyError, TypeError, ValueError) as error:
                raise step_failed(str(error)) from error
            wait_for(ctx, collect, 'forced resync native and relay corroboration', 35, 0.25)
            return evidence['audit']
        finally:
            collect()
    def evidence_step(ctx, step):
        name = step.get('from', 'forced_resync')
        evidence = ctx.saved[name]
        evidence['statehash'] = ctx.saved[step['statehash']]
        evidence['census'] = ctx.saved[step['census']]
        evidence['audit'] = audit(evidence)
        path = ctx.run_dir / (name + '.json')
        path.write_text(json.dumps(evidence, indent=2))
        if not evidence['audit']['complete']:
            raise step_failed('forced resync evidence incomplete: ' + '; '.join(evidence['audit']['problems']))
        return evidence['audit']
    steps['forced_resync'] = forced
    steps['forced_resync_evidence'] = evidence_step


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = audit(json.loads(args.bundle.read_text()))
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    raise SystemExit(0 if result['complete'] else 1)
