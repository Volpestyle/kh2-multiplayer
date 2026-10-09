"""Read-only population discovery receipt joiner. Never grants creation authority."""
import argparse
import json
import re
from pathlib import Path

PREFIX = '[population-authority] '
FIELDS = ('controller', 'header', 'record', 'location', 'point', 'definition0', 'definition1', 'definition2', 'definition3', 'definition4')
LENGTHS = (128, 88, 128, 20, 32, 128, 128, 128, 128, 128)


def analyze(text):
    events, chunks, issues, drains = {}, {}, [], []
    for line in text.splitlines():
        if PREFIX not in line:
            continue
        row = line.split(PREFIX, 1)[1]
        facts = dict(re.findall(r'(\w+)=([^\s]+)', row))
        if row.startswith('event '):
            try:
                sequence = int(facts['seq'])
                if sequence <= 0 or sequence in events:
                    issues.append('duplicate/invalid event sequence')
                else:
                    events[sequence] = facts
                if facts.get('authority') != 'UNKNOWN':
                    issues.append('unexpected authority claim')
            except (KeyError, ValueError):
                issues.append('invalid event row')
        elif row.startswith('bytes '):
            try:
                key = (int(facts['seq']), facts['field'])
                if key in chunks:
                    issues.append('duplicate byte chunk')
                chunks[key] = facts['value']
            except (KeyError, ValueError):
                issues.append('invalid byte chunk')
        elif row.startswith('drain '):
            drains.append(facts)
            if facts.get('creationAuthority') != '0':
                issues.append('unexpected creation authority claim')
    invocations = {}
    observed_kinds = set()
    for sequence, event in sorted(events.items()):
        try:
            invocation = int(event['invocation'])
            kind = int(event['kind'])
            exiting = int(event['exit'])
            parent = int(event['parent'])
            context = int(event['context'])
            if invocation <= 0 or context <= 0 or exiting not in (0, 1) or event['identityValid'] != '1':
                raise ValueError('invalid invocation identity')
            pair = invocations.setdefault(invocation, {})
            if exiting in pair:
                issues.append('duplicate invocation phase')
            pair[exiting] = (sequence, event)
            observed_kinds.add(kind)
            if parent and parent >= invocation:
                issues.append('invalid parent ancestry')
            for name, length in zip(FIELDS, LENGTHS):
                value = chunks.get((sequence, name))
                if value is None or len(value) != length or re.fullmatch('[0-9a-f]+', value) is None:
                    issues.append('missing/invalid byte chunk')
        except (KeyError, ValueError):
            issues.append('invalid event identity')
    for invocation, pair in invocations.items():
        if set(pair) != {0, 1}:
            issues.append('missing invocation terminal')
            continue
        before_seq, before = pair[0]
        after_seq, after = pair[1]
        try:
            if before_seq != invocation or after_seq <= before_seq or int(after['ms']) < int(before['ms']):
                issues.append('invocation ordering differs')
            for field in ('kind', 'context', 'parent', 'depth', 'thread', 'fiber', 'isFiber', 'stackHigh'):
                if before[field] != after[field]:
                    issues.append('return identity differs')
            if after['returned'] != '1' or after['unwind'] != '0':
                issues.append('native call did not return normally')
            parent = int(before['parent'])
            if parent:
                owner = invocations.get(parent, {})
                if set(owner) != {0, 1} or not owner[0][0] < before_seq < after_seq < owner[1][0]:
                    issues.append('missing/non-enclosing parent')
        except (KeyError, ValueError):
            issues.append('invalid invocation terminal')
    if any(sequence not in events or name not in FIELDS for sequence, name in chunks):
        issues.append('orphan byte chunk')
    if not drains or not events:
        issues.append('missing events or terminal drain')
    else:
        terminal = drains[-1]
        if terminal.get('mask') != '4095' or terminal.get('openObserved') != '0':
            issues.append('incomplete hook mask or open invocations')
        if any(d.get('loss') != '0' for d in drains):
            issues.append('irreversible recorder loss/refusal')
    return dict(verdict='INCONCLUSIVE', creationAuthority=False,
                authority={key: 'UNKNOWN' for key in ('owner', 'fiber', 'creator', 'globalPending', 'enrollment')},
                receiptGraphComplete=bool(events) and not issues,
                events=len(events), invocations=len(invocations), observedKinds=sorted(observed_kinds),
                issues=sorted(set(issues)),
                reason='Observational diagnostic only; native creator closure/pending barrier and lifecycle census witnesses remain unqualified')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('log', type=Path)
    args = parser.parse_args()
    print(json.dumps(analyze(args.log.read_text(encoding='utf-8', errors='replace')), indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
