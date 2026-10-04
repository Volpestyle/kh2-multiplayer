"""Bounded flag409 fault through the existing strict resync transaction.

The CLI write and multi-batch snapshots are not atomic native operations.  This
control brackets them with checked state and append-only lifecycle evidence.
"""
from __future__ import annotations
import hashlib
import json
import re
import struct
import time

LOCATION = [5, 6, 0, 1, 1, 0]
RVA = 0x9ABC8F
BIT = 2
SHARED = (("programs", 0x10, 0x1C80), ("story", 0x1C90, 0x260),
          ("visited", 0x22F8, 0x98), ("chests", 0x23AC, 0x34))
PERSONAL = (("characters", 0x24F0, 0xE04), ("inventory", 0x3580, 0x140),
            ("munny", 0x2440, 4), ("exp", 0x36E0, 4))
APPLY = re.compile(r"\[progresssync\] apply version=(\d+) spans=(\d+) bytes=(\d+) hash=([0-9A-Fa-f]{8}) personal_before=([0-9A-Fa-f]{8}) personal_after=([0-9A-Fa-f]{8}) personal_unchanged=(\d+)")
LOAD = re.compile(r"\[warp\] load complete serial=(\d+) transition=(\d+) room=([0-9A-Fa-f]+)/([0-9A-Fa-f]+) door=(\d+) map=(\d+) btl=(\d+) evt=(\d+)")
LIMITS = [
    "Generic rig-only poke is not an atomic game-thread context-checked write; reads and full progress snapshots are also non-atomic.",
    "Pre-write logs establish session and unchanged membership/attachment history, not numeric connection IDs; exact original connections are joined from the subsequent native plan and strict terminal evidence.",
    "This is an injected native shared-progress fault, not natural progression, chest reward, HP-repair, remote-machine or general dead-enemy acceptance.",
]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def mask(offset):
    return 0xFE if offset == 0x23AC else 0x0F if offset == 0x23DF else 0xFF


def regions(snapshot, peer):
    data = snapshot['instances'][str(peer)]['ranges']
    result = {}
    for name, offset, length in SHARED + PERSONAL:
        row = data[name]
        raw = bytes.fromhex(row['hex'])
        require(row['saveOffset'] == offset and row['length'] == length and len(raw) == length,
                'incomplete native range ' + name)
        require(hashlib.sha256(raw).hexdigest() == row['sha256'], 'range SHA mismatch')
        result[name] = raw
    return result


def khp1(ranges):
    value = 2166136261
    encoded = bytearray(struct.pack('<II', 0x3150484B, 8108))
    for name, offset, length in SHARED:
        for index in range(length):
            at = offset + index
            encoded.extend(struct.pack('<IBB', at, mask(at), ranges[name][index] & mask(at)))
    for byte in encoded:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def shared_equal(left, right):
    return all(all((a & mask(offset + i)) == (b & mask(offset + i))
                   for i, (a, b) in enumerate(zip(left[name], right[name])))
               for name, offset, _ in SHARED)


def check_snapshots(before, after=None):
    baseline = {i: regions(before, i) for i in range(3)}
    require(all(shared_equal(baseline[0], baseline[i]) for i in (1, 2)),
            'pre-fault full shared progress differs')
    if after is not None:
        for i in range(3):
            current = regions(after, i)
            for name, _, _ in SHARED + PERSONAL:
                wanted = bytearray(baseline[i][name])
                if i == 1 and name == 'chests':
                    wanted[-1] ^= BIT
                require(current[name] == wanted, 'fault changed unexpected range/peer: ' + str(i) + '/' + name)
    return {str(i): khp1(baseline[i]) for i in range(3)}


def cleanup_value(current, original):
    return (current & ~BIT) | (original & BIT)


def runtime_identity(text, pid):
    lines = [line for line in text.splitlines() if any(key in line for key in (
        'SessionState session=', 'Attached to KH2 process', 'Verified membership;',
        'Network: transport', 'Network: disconnected', 'Network: refused', 'Connecting to ',
        'Network: closed', 'Connection closed', 'Recovery', 'recovery', 'Disconnected', 'invalid roster'))]
    sessions = re.findall(r'SessionState session=([0-9a-f]{32}) actors=(\d+)', text)
    require(sessions and sessions[-1][1] == '3', 'current three-member session unavailable')
    require(len({session for session, _ in sessions}) == 1, 'session changed during owned runtime')
    require(text.count('Verified membership; native bootstrap remains separate') == 1,
            'ambiguous or missing verified membership')
    attached = re.findall(r'Attached to KH2 process \(PID=(\d+)\)', text)
    require(attached == [str(pid)], 'ambiguous runtime attachment')
    peers = re.findall(r'peer_id=(\S+)', text)
    require(len(peers) == 1, 'unique configured peer identity unavailable')
    require(not re.search(r'disconnected|Disconnected|Network: closed|refused by relay|invalid roster|Recovery|recovery', '\n'.join(lines)),
            'membership retirement/recovery observed')
    return {'session': sessions[-1][0], 'peer': peers[0], 'pid': pid, 'transcript': lines}


def native_context(text, arrival_pattern, client, allow_host_plan=False):
    loads = list(LOAD.finditer(text))
    arrivals = list(re.finditer(arrival_pattern, text))
    require(loads and arrivals, 'load/arrival evidence unavailable')
    load, arrival = loads[-1], arrivals[-1]
    require(arrival.group(0).startswith('[enemysync] ' + ('client' if client else 'host') + ' arrived '),
            'completed arrival role differs from required owner')
    require(int(load[1]) > 0 and arrival.start() > load.start(), 'arrival does not follow current load')
    tail = text[arrival.end():]
    if allow_host_plan and not client:
        # The host captures in place; only targets reload. The strict transaction
        # auditor joins this one stage0 plan after collection, never at baseline.
        plans = re.findall(r'\[resync\] plan [^\r\n]*', tail)
        require(len(plans) == 1 and ' phase=0 stage=0 ' in plans[0], 'expected host capture plan unavailable')
        tail = tail.replace(plans[0], '', 1)
    require(not re.search(r'\[enemysync\] (?:session reset|role)|\[resync\] plan|\[warp\] client (?:queued|issued)', tail),
            'completed arrival retired by outstanding plan/reset/role/transition')
    require([int(load[i], 16 if i in (3, 4) else 10) for i in range(3, 9)] == LOCATION,
            'native load full tuple changed')
    a = arrival.groupdict()
    require([int(a[k], 16 if k in ('world', 'room') else 10) for k in ('world', 'room', 'door', 'map', 'btl', 'evt')] == LOCATION,
            'arrival full tuple changed')
    lineage = [line for line in text.splitlines() if any(key in line for key in (
        '[warp]', '[progresssync] client', '[progresssync] apply', '[progresssync] failure',
        '[enemysync] session reset', '[enemysync] role', '[resync] plan'))]
    out = {'load': int(load[1]), 'transition': int(load[2]), 'epoch': int(a['epoch']), 'lineage': lineage}
    require(out['epoch'] > 0, 'zero epoch')
    if client:
        applies = list(APPLY.finditer(text))
        queued = list(re.finditer(r'\[warp\] client queued epoch=(\d+)', text))
        issued = list(re.finditer(r'\[warp\] client issued epoch=(\d+) transition=(\d+)', text))
        versions = re.findall(r'\[progresssync\] client (?:full|delta) version=(\d+)', text)
        require(applies and queued and issued and versions, 'applied client version unavailable')
        apply, queue, issue = applies[-1], queued[-1], issued[-1]
        resets = list(re.finditer(r'\[enemysync\] (?:session reset|role)|\[resync\] plan', text))
        require(not resets or resets[-1].start() < queue.start(), 'client reset/plan does not precede current queued boundary')
        require(queue.start() < apply.start() < issue.start() < load.start() < arrival.start(),
                'latest apply is not attached to current completed boundary')
        require(int(queue[1]) == int(issue[1]) == out['epoch'] and int(issue[2]) == out['transition'],
                'issued/load/arrival context differs')
        require(apply[7] == '1' and apply[5].lower() == apply[6].lower() and apply[1] == versions[-1],
                'applied version/personal check unavailable')
        require('apply failed' not in text[apply.start():] and 'apply-failed' not in text[apply.start():],
                'progress apply failed')
        out.update(version=int(apply[1]), appliedHash=int(apply[4], 16))
    return out


def check_hashes(result, expected_rows, expected_hashes, epoch):
    require(result['ready'] and not result['problems'] and len(result['matchingSamples']) == 2,
            'two complete fresh mismatch samples unavailable')
    prior = None
    for sample in result['matchingSamples']:
        require(sample['epoch'] == epoch and set(sample['instances']) == {'0', '1', '2'}, 'hash epoch/denominator changed')
        frames = []
        for peer, row in sample['instances'].items():
            require(row['location'] == LOCATION and row['count'] == 5 and row['unmatched'] == 0
                    and row['liveRows'] == expected_rows and row['progress'] == expected_hashes[peer],
                    'mismatch sample does not describe exact native five Shadows/progress')
            frames.append(int(row['frame']))
        require(all(frame > 0 for frame in frames) and (prior is None or all(b > a for a, b in zip(prior, frames))),
                'nonincreasing mismatch frames')
        prior = frames
    require(any(row['peer'] == 'peer1' and int(row['fields']) == 4 and int(row['epoch']) == epoch
                for row in result['relayDesync']), 'fresh exact progress-only relay mismatch unavailable')


def check_repair_chain(bundle, before, hashes):
    require(bundle['audit']['complete'], 'strict native/relay transaction incomplete')
    acknowledgments = bundle['audit']['acknowledgments']
    ack = acknowledgments.get(1, acknowledgments.get('1'))
    text = bundle['native']['1']
    plans = list(re.finditer(r'\[resync\] plan ([^\r\n]+)', text))
    require(len(plans) == 1, 'progress control requires one Bootstrap plan, not checkpoint')
    fields = dict(item.split('=', 1) for item in plans[0][1].split())
    require(fields['session'] == before['runtime']['1']['session'], 'post-plan session does not match observed pre-fault session')
    require(fields['stage'] == '1' and fields['phase'] == '0' and all(fields[k] == ack[k] for k in ('session', 'host', 'request')),
            'repair apply not under matching stage1 Bootstrap plan')
    require(ack['phase'] == '0' and int(ack['loadBefore']) == before['native']['1']['load'], 'repair load baseline changed')
    for slot in (1, 2):
        target_ack = acknowledgments.get(slot, acknowledgments.get(str(slot)))
        require(target_ack is not None and int(target_ack['loadBefore']) == before['native'][str(slot)]['load'],
                'original target load baseline differs from pre-fault observation')
    applies = list(APPLY.finditer(text))
    resets = list(re.finditer(r'\[enemysync\] session reset: host epoch and pending target cleared', text))
    full = list(re.finditer(r'\[progresssync\] client full version=(\d+) spans=\d+ bytes=8108 complete=1', text))
    queued = list(re.finditer(r'\[warp\] client queued epoch=(\d+) target=05/06 door=0 map=1 btl=1 evt=0', text))
    issues = list(re.finditer(r'\[warp\] client issued epoch=(\d+) transition=(\d+)', text))
    loads = list(LOAD.finditer(text))
    require(len(applies) == len(issues) == len(loads) == 1, 'ambiguous repair boundary')
    require(len(resets) == len(full) == len(queued) == 1 and text.count('session reset') == 1,
            'requires exactly one expected Bootstrap reset/full/queue')
    apply, issue, load = applies[0], issues[0], loads[0]
    require(plans[0].end() < resets[0].start() < full[0].start() < queued[0].start() < apply.start()
            < issue.start() < load.start() < text.index('[resync] ack '),
            'repair stage/apply/issue/load/ACK ordering unavailable')
    require(int(apply[1]) == before['native']['1']['version'] and apply[2] == apply[3] == '1'
            and int(apply[4], 16) == hashes['0'] and apply[5].lower() == apply[6].lower() and apply[7] == '1',
            'repair must apply exactly one span/byte of expected version/hash with personal equality')
    require(int(issue[1]) == before['native']['1']['epoch'] and int(issue[2]) == int(load[2])
            and int(load[1]) == int(ack['loadAfter']) > int(ack['loadBefore']), 'repair issued/load mismatch')
    require(full[0][1] == apply[1] and queued[0][1] == issue[1] and
            not re.search(r'apply.failed|\[enemysync\] role|\[progresssync\] client delta', text),
            'repair failure/role/version change observed')
    return {'apply': apply.group(0), 'plan': fields, 'ack': ack}


def execute(io, step):
    """Production orchestration, with isolated provider seam for failure controls."""
    receipt = {'complete': False, 'limits': LIMITS, 'commands': [], 'cleanup': {}, 'resyncCalls': 0}
    io.receipt = receipt
    started = time.monotonic()
    before = None
    write_attempted = False
    try:
        require(step.get('instance', 0) == 0 and step.get('slot', 'all') == 'all' and step.get('as', 'forced_resync') == 'forced_resync',
                'control is fixed to one host0 both-target transaction')
        before = io.sample('before')
        receipt['before'] = before
        hashes = check_snapshots(before['snapshot'])
        rows = io.expected_rows()
        require(len(rows) == 5 and len({r[0] for r in rows}) == 5 and all(r[0] > 0 and r[1:] == [302, 17] for r in rows),
                'requires exact five original typed Shadow bindings at HP17')
        require(all(before['native'][str(i)]['appliedHash'] == hashes[str(i)] for i in (1, 2)), 'baseline is not applied authoritative progress')
        original = before['bytes']['1']
        require(all(before['bytes'][str(i)] & BIT == original & BIT for i in range(3)), 'baseline selected bit differs')
        # Final immediate guard before write is independent of the multi-batch snapshots.
        io.guard(before, expected_byte=original)
        require(time.monotonic() - started < 120, 'fault preparation deadline exceeded')
        receipt['faultIntent'] = {'instance': 1, 'rva': RVA, 'width': 'u8', 'mask': BIT,
                                  'before': original, 'requested': original ^ BIT}
        io.persist(receipt)
        write_attempted = True
        io.write(original ^ BIT, 'fault')
        fault = io.sample('fault')
        receipt['fault'] = fault
        require(fault['native'] == before['native'] and fault['runtime'] == before['runtime'], 'context changed around fault')
        check_snapshots(before['snapshot'], fault['snapshot'])
        require(fault['bytes']['1'] == original ^ BIT, 'fault readback failed')
        changed_hashes = {str(i): khp1(regions(fault['snapshot'], i)) for i in range(3)}
        require(changed_hashes['1'] != hashes['0'], 'fault checksum did not change')
        mismatch = io.hashes()
        receipt['mismatch'] = mismatch
        check_hashes(mismatch, rows, changed_hashes, before['native']['1']['epoch'])
        prequeue = io.sample('prequeue')
        receipt['prequeue'] = prequeue
        require(prequeue['native'] == before['native'] and prequeue['runtime'] == before['runtime'], 'prequeue context changed')
        check_snapshots(before['snapshot'], prequeue['snapshot'])
        require(prequeue['bytes']['1'] == original ^ BIT, 'fault repaired before resync')
        io.guard(before, expected_byte=original ^ BIT)
        require(time.monotonic() - started < 120, 'fault-to-queue deadline exceeded')
        io.persist(receipt)
        receipt['resyncCalls'] += 1
        io.resync(step)
        repaired = io.sample('repair')
        receipt['repair'] = repaired
        require(repaired['runtime'] == before['runtime'], 'membership changed through repair')
        require(repaired['bytes']['1'] & BIT == original & BIT, 'resync failed to repair selected bit before cleanup')
        require(all(shared_equal(regions(before['snapshot'], 0), regions(repaired['snapshot'], i)) for i in range(3)),
                'repaired full native shared progress differs')
        receipt['repairChain'] = check_repair_chain(io.bundle(), before, hashes)
        acknowledgments = io.bundle()['audit']['acknowledgments']
        for slot in (1, 2):
            ack = acknowledgments.get(slot, acknowledgments.get(str(slot)))
            require(repaired['native'][str(slot)]['load'] == int(ack['loadAfter']) and
                    repaired['native'][str(slot)]['epoch'] == before['native'][str(slot)]['epoch'] and
                    repaired['native'][str(slot)]['version'] == before['native'][str(slot)]['version'],
                    'post-repair sample is outside witnessed target load/epoch/version')
        require(repaired['native']['0']['load'] == before['native']['0']['load'] and
                repaired['native']['0']['epoch'] == before['native']['0']['epoch'], 'host context changed during repair')
        require(time.monotonic() - started < 180, 'control evidence deadline exceeded')
        receipt['complete'] = True
        return receipt
    except Exception as error:
        receipt['error'] = type(error).__name__ + ': ' + str(error)
        raise
    finally:
        if write_attempted and before is not None:
            try:
                # Only original exact pre-fault context can authorize a cleanup write.
                current = io.guard(before)
                value = cleanup_value(current, before['bytes']['1'])
                if value != current:
                    io.write(value, 'cleanup')
                receipt['cleanup'] = {'available': True, 'before': current, 'after': value, 'wrote': value != current}
            except Exception as error:
                receipt['cleanup'] = {'available': False, 'error': str(error), 'action': 'canonical owned-run teardown; no unsafe write'}
        else:
            receipt['cleanup'] = {'available': True, 'wrote': False, 'reason': 'fault not attempted'}
        receipt['elapsedSeconds'] = time.monotonic() - started
        io.persist(receipt)


def register(steps, *, kh2ctl, logs, step_failed, arrival_pattern, snapshot):
    class Live:
        def __init__(self, ctx):
            self.ctx = ctx
            self.receipt = {}
            self.prefixes = {}
            self.relay_offset = len((ctx.run_dir / 'relay.log').read_text(errors='replace'))

        def command(self, *args, **kwargs):
            record = {'argv': list(args), 'pid': kwargs.get('pid')}
            self.receipt.setdefault('commands', []).append(record)
            try:
                record['response'] = kh2ctl(*args, **kwargs, check=False, timeout=10)
                require(record['response'].get('ok') is True, 'CLI failure: ' + str(record['response']))
                return record['response']
            except Exception as error:
                record['error'] = type(error).__name__ + ': ' + str(error)
                raise

        def contexts(self, allow_host_plan=False):
            self.ctx.check_all()
            native, runtime = {}, {}
            for i in range(3):
                for kind, path in (('native', logs / f'kh2coop_inject_{self.ctx.inst(i).pid}.log'),
                                   ('runtime', self.ctx.run_dir / f'runtime_{i}.log')):
                    raw = path.read_bytes()
                    key = kind + str(i)
                    prior = self.prefixes.get(key, b'')
                    require(raw.startswith(prior), 'log truncated/replaced: ' + key)
                    self.prefixes[key] = raw
                    text = raw.decode('utf-8', errors='strict')
                    if kind == 'native':
                        native[str(i)] = native_context(text, arrival_pattern, i != 0, allow_host_plan=allow_host_plan)
                    else:
                        runtime[str(i)] = runtime_identity(text, self.ctx.inst(i).pid)
            require(len({row['session'] for row in runtime.values()}) == 1, 'peer sessions differ')
            require(all(runtime[str(i)]['peer'] == 'peer' + str(i) for i in range(3)) and
                    len({row['pid'] for row in runtime.values()}) == 3, 'peer/owned PID denominator ambiguous')
            require(len({row['epoch'] for row in native.values()}) == 1, 'peer epochs differ')
            return native, runtime

        def read_safe(self):
            out = {}
            specs = '0x9A98B0:u32,0x9ABC8F:u8,0x717008:u8,0x717009:u8,0x71700A:u8,0x71700C:u16,0x71700E:u16,0x717010:u16,0x2A171E8:i32,0x9BA8D0:u8,0x7435D0:u8,0xB65210:i32,0x2A11478:u64'
            for i in range(3):
                sample = self.command('peek', '--rva', specs, pid=self.ctx.inst(i).pid)['samples'][0]
                def value(key):
                    raw = sample[key]
                    return int(raw, 0) if isinstance(raw, str) else raw
                require(value('0x9A98B0') == 0x4A32484B, 'SAVE magic unavailable')
                require([value(key) for key in ('0x717008', '0x717009', '0x71700A', '0x71700C', '0x71700E', '0x717010')] == LOCATION,
                        'unsafe or changed full room tuple')
                require(value('0x2A171E8') == value('0xB65210') == value('0x2A11478') == 0
                        and value('0x9BA8D0') != 0 and value('0x7435D0') == 255, 'unsafe gameplay')
                out[str(i)] = value('0x9ABC8F')
            return out

        def guard(self, before, expected_byte=None):
            native, runtime = self.contexts()
            values = self.read_safe()
            after_native, after_runtime = self.contexts()
            self.receipt.setdefault('guards', []).append({'native': after_native, 'runtime': after_runtime, 'bytes': values})
            require(native == after_native == before['native'] and runtime == after_runtime == before['runtime'],
                    'original owned arrival/load/version/runtime context no longer identifiable')
            if expected_byte is not None:
                require(values['1'] == expected_byte, 'immediate selected-byte mismatch')
            return values['1']

        def sample(self, label):
            native, runtime = self.contexts(allow_host_plan=label == 'repair')
            values = self.read_safe()
            observation = {'native': native, 'runtime': runtime, 'bytes': values}
            self.receipt.setdefault('observations', {})[label] = observation
            name = 'progress_resync_' + label
            snapshot(self.ctx, {'as': name, 'instances': [0, 1, 2]}, self.command)
            after_values = self.read_safe()
            after_native, after_runtime = self.contexts(allow_host_plan=label == 'repair')
            require(native == after_native and runtime == after_runtime and values == after_values,
                    'non-atomic snapshot crossed a context/selected-byte change')
            captured = self.ctx.saved[name]
            observation['snapshot'] = captured
            require(all(regions(captured, i)['chests'][-1] == values[str(i)] for i in range(3)), 'snapshot byte differs from bracketed read')
            return {'native': native, 'runtime': runtime, 'bytes': values, 'snapshot': captured}

        def write(self, value, purpose):
            require(0 <= value <= 255, 'invalid byte')
            response = self.command('poke', '--rva', '0x9ABC8F', '--type', 'u8', '--value', str(value), pid=self.ctx.inst(1).pid)
            require(response.get('ok') is True and response.get('processId') == self.ctx.inst(1).pid, 'poke response identity failed')
            observed = self.read_safe()['1']
            require(observed == value, purpose + ' readback failed')

        def expected_rows(self):
            return self.ctx.saved['after_host_damage']['instances']['0']['liveRows']

        def hashes(self):
            return steps['statehash_check'](self.ctx, {'as': 'progress_resync_mismatch', 'instances': [0, 1, 2],
                'expectedFields': 4, 'controlInstance': 1, 'controlPeer': 'peer1', 'minEnemies': 5,
                'consecutiveSamples': 2, 'relayOffset': self.relay_offset, 'timeoutMs': 20000})

        def resync(self, step):
            return steps['forced_resync'](self.ctx, {**step, 'do': 'forced_resync'})

        def bundle(self):
            return self.ctx.saved['forced_resync']

        def persist(self, receipt):
            self.ctx.saved['progress_resync_control'] = receipt
            path = self.ctx.run_dir / 'progress_resync_control.json'
            path.write_text(json.dumps(receipt, indent=2))
            if path.name not in self.ctx.artifacts:
                self.ctx.artifacts.append(path.name)

    def run(ctx, step):
        try:
            return execute(Live(ctx), step)
        except Exception as error:
            raise step_failed('progress fault/resync proof failed: ' + str(error)) from error
    steps['progress_fault_resync'] = run
