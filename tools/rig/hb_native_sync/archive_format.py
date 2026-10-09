"""Derive a rescue starting archive from an explicitly supplied COPY only.

PC container/checksum layout: KingdomSaveEditor 37a7a9d (GPL-3.0-or-later).
Never opens a destination outside a directory named save_sandbox_*.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

COUNT, ENTRY, STRIDE, HEADER, SIZE = 100, 0x158, 0x10FC0, 0x70, 0x6BED08
DATA_BASE = HEADER + COUNT * ENTRY
TT_FLAGS = 0x1CD0


def checksum(raw):
    table = []
    for x in range(256):
        r = x << 24
        for _ in range(255):
            r = ((r << 1) ^ (0x04C11DB7 if r & 0x80000000 else 0)) & 0xFFFFFFFF
        table.append(r)
    crc = 0xFFFFFFFF
    for b in raw[:8] + raw[12:]:
        crc = (table[(crc >> 24) ^ b] ^ (crc << 8)) & 0xFFFFFFFF
    return crc ^ 0xFFFFFFFF


def entries(data):
    if len(data) != SIZE or data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('unsupported native PC archive')
    table = bytearray(data[HEADER:DATA_BASE])
    key = bytes(table[0xE0:0xF0])
    for i in range(0xF0):
        table[i] ^= key[i & 15]
    result = []
    for index in range(COUNT):
        row = table[index * ENTRY:(index + 1) * ENTRY]
        name = row[:64].split(b'\0')[0].decode('ascii')
        length, flag = struct.unpack_from('<II', row, 0x50)
        if length > STRIDE or bool(name) != bool(length) or flag or any(row[0x58:]):
            raise ValueError('unknown entry format')
        start = DATA_BASE + index * STRIDE
        raw = data[start:start + length]
        if raw.startswith(b'KH2J'):
            if len(raw) != STRIDE or struct.unpack_from('<I', raw, 4)[0] != 58:
                raise ValueError('unsupported gameplay save')
            if checksum(raw) != struct.unpack_from('<I', raw, 8)[0]:
                raise ValueError('source checksum invalid')
        result.append((index, name, start, raw))
    return result


def derive(data, slot=4):
    rows = entries(data)
    original = rows[slot][3]
    if not original.startswith(b'KH2J') or original[12:14] != bytes([5, 1]):
        raise ValueError('expected pinned early-Sora Beast Castle slot4')
    payload = bytearray(original)
    # Safe common join room; host then enters rescue via exact explicit tuple.
    payload[12:16] = bytes([2, 2, 0, 0])
    for room, programs in ((2, (4, 0, 0)), (8, (108, 108, 108)), (9, (4, 0, 0))):
        struct.pack_into('<3H', payload, 0x10 + 6 * (2 * 64 + room), *programs)
    for flag in range(0x99, 0xAE):
        offset, mask = TT_FLAGS + flag // 8, 1 << (flag % 8)
        payload[offset] = (payload[offset] | mask) if flag in (0x99, 0x9A) else (payload[offset] & ~mask)
    payload[TT_FLAGS + 0xD0 // 8] &= ~(1 << (0xD0 % 8))
    struct.pack_into('<I', payload, 8, checksum(payload))
    output = bytearray(data)
    replaced = []
    for index, name, start, raw in rows:
        if raw.startswith(b'KH2J'):
            output[start:start + STRIDE] = payload
            replaced.append(index)
    entries(output)  # all payload checksums revalidated
    changes = [i for i, (a, b) in enumerate(zip(original, payload)) if a != b]
    allowed = set(range(8, 16)) | set(range(0x31C, 0x322)) | set(range(0x340, 0x34C)) | set(range(TT_FLAGS + 0x99 // 8, TT_FLAGS + 0xAD // 8 + 1)) | {TT_FLAGS + 0xD0 // 8}
    assert set(changes) <= allowed
    receipt = dict(status='STATICALLY_QUALIFIED_PENDING_BOOT', sourceSlot=slot,
                   sourceSha256=hashlib.sha256(data).hexdigest(),
                   outputSha256=hashlib.sha256(output).hexdigest(),
                   payloadSha256=hashlib.sha256(payload).hexdigest(),
                   replacedSlots=replaced, changedPayloadOffsets=changes,
                   kitAndInventoryPreserved=payload[0x24F0:] == original[0x24F0:],
                   start=[2, 2, 0, 4, 0, 0], rescue=[2, 8, 0, 108, 108, 108],
                   successor=[2, 9, 0, 118, 118, 118],
                   scope='derived copy; no natural-save or successful boot claim')
    return bytes(output), receipt


def sandbox_write(destination, data):
    destination = Path(destination).resolve()
    protected = (Path.home() / 'OneDrive/Documents/My Games/KINGDOM HEARTS HD 1.5+2.5 ReMIX').resolve()
    if destination.is_relative_to(protected):
        raise ValueError('protected real-save tree is never a sandbox destination')
    if not any(p.name.startswith('save_sandbox_') for p in destination.parents):
        raise ValueError('destination must be within an explicit save sandbox')
    if destination.suffix.lower() != '.png':
        raise ValueError('archive destination must be PNG')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open('xb') as stream:
        stream.write(data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--copy', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = args.copy.resolve()
    if '.local' not in source.parts and not any(p.name.startswith('save_sandbox_') for p in source.parents):
        raise ValueError('input must be an existing local copy or sandbox archive')
    data, receipt = derive(source.read_bytes())
    sandbox_write(args.output, data)
    args.output.with_suffix('.receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt, indent=2))
