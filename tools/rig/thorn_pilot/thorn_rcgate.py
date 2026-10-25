"""Lead #249 read-only RC gate sampler: which native condition clears REACT_CMD (C+0xB42) on the friend.

Ghidra (kh2_full): the per-frame update 0x3B2340(C=0x2A10620) refreshes the reaction cache 0x408520(C+0xB10), whose
+0x32 is REACT_CMD 0x2A11162. Refresh returns early (REACT stale) on system 0x9AB580 bit23 without bit28 or C+0xB3C
bit0 clear; else selection 0x4081C0 writes the winner or 0. Selection is skipped when the player is blocked
(FUN_3BA4C0: +0x9B8 bit2 / bit10 clear, +0x120 bit6, +0x18C bit27, ...) or 0x2A11400 bit14; the script slot is
C+0x9D4 (command u16 at +2, target handle at +0xC), turned down by 3B6A10 (player +0xDE0) or a blocked target.
0x3B20B0 clears the seven command slots C+0x9B0.. at the end of every update.

While the seat is in an event in Thorn's room, a daemon thread reads those fields about every 2 ms with
ReadProcessMemory and queues a row whenever any of them changes; step() writes the queued rows as `rc_gate`
(capped at 4000). Lead #250: each armed session also ends with one `rc_gate_jitter` row (samples, loop interval
mean/p99/max, intervals over 10 ms, read time mean/max) so the sampler's own cost is visible. It never writes game memory or sends input. The target actor (Thorn) is the slot's or the
cache's target handle decoded like head_observation.Reader.resolve, accepted only when the actor's own +4 handle
matches it.
"""
import collections
import struct
import threading
import time

CONTROLLER, SYSTEM, SELECT_BLOCK, CANONICAL, REGIONS = 0x2A10620, 0x9AB580, 0x2A11400, 0x2A105D0, 0x2B0D720
BLOCK_AT, BLOCK_LEN = 0x9B0, 0x298           # C+0x9B0 .. C+0xC48: command slots, player/pad, cache, countdown
ROW_CAP, TAIL_SEC, PERIOD = 4000, 1.0, 0.002


def decode(block, head, system, select, player_fields, thorn_fields):
    """One sample as a flat dict; `block` is C+0x9B0..+0xC48 and `head` is C+0x20..+0x28."""
    u16 = lambda off: struct.unpack_from('<H', block, off - BLOCK_AT)[0]
    u32 = lambda off: struct.unpack_from('<I', block, off - BLOCK_AT)[0]
    u8 = lambda off: block[off - BLOCK_AT]
    row = dict(ctl24=struct.unpack_from('<I', head, 4)[0], slotCmd=u16(0x9D6), slotHead=u16(0x9D4),
               slotTarget=u32(0x9E0), b3c=u32(0xB3C), b40=u16(0xB40), react=u16(0xB42), b44=u8(0xB44),
               status=u8(0xB45), reactTarget=u32(0xB28), countdown=u8(0xC40), system=system, select=select)
    if player_fields:
        row.update(p9b8=player_fields[0], p120=player_fields[1], p18c=player_fields[2], p6c8=player_fields[3],
                   pde0=player_fields[4])
    if thorn_fields:
        row.update(t9b8=thorn_fields[0], t120=thorn_fields[1], t18c=thorn_fields[2])
    return row


class RcGateSampler:
    def __init__(self, game):
        self.game, self.rows, self.lock = game, collections.deque(), threading.Lock()
        self.active_until, self.handle, self.thorn = 0.0, 0, 0
        self.last, self.queued, self.errors = None, 0, 0
        self.session = None   # (started, previous loop time, intervals, read times, errors at start)
        self.thread = threading.Thread(target=self.run, name='rc-gate', daemon=True)
        self.thread.start()

    def arm(self, seconds=TAIL_SEC):
        self.active_until = max(self.active_until, time.monotonic() + seconds)

    def target(self, handle):
        """head_observation.Reader.resolve, kept only when the actor names the same handle at +4."""
        if handle in (0, 0xFFFFFFFF):
            return 0
        if handle != self.handle:
            g = self.game
            region = struct.unpack('<Q', g.rva(REGIONS + 8 * ((handle & 0x7fffffff) >> 25), 8))[0]
            actor = region | (handle & 0x1ffffff) if region else 0
            self.handle, self.thorn = handle, actor
        if self.thorn and struct.unpack('<I', self.game.read(self.thorn + 4, 4))[0] == self.handle:
            return self.thorn
        return 0

    def sample(self):
        g = self.game
        block = g.rva(CONTROLLER + BLOCK_AT, BLOCK_LEN)
        head = g.rva(CONTROLLER + 0x20, 8)
        system = struct.unpack('<Q', g.rva(SYSTEM, 8))[0]
        select = struct.unpack('<I', g.rva(SELECT_BLOCK, 4))[0]
        player = struct.unpack('<Q', g.rva(CANONICAL, 8))[0]
        pf = None
        if player:
            pf = (struct.unpack('<I', g.read(player + 0x9B8, 4))[0], struct.unpack('<I', g.read(player + 0x120, 4))[0],
                  struct.unpack('<I', g.read(player + 0x18C, 4))[0], struct.unpack('<I', g.read(player + 0x6C8, 4))[0],
                  struct.unpack('<i', g.read(player + 0xDE0, 4))[0])
        handle = struct.unpack_from('<I', block, 0x9E0 - BLOCK_AT)[0] or struct.unpack_from('<I', block, 0xB28 - BLOCK_AT)[0]
        tf, thorn = None, self.target(handle or self.handle)
        if thorn:
            tf = (struct.unpack('<I', g.read(thorn + 0x9B8, 4))[0], struct.unpack('<I', g.read(thorn + 0x120, 4))[0],
                  struct.unpack('<I', g.read(thorn + 0x18C, 4))[0])
        return decode(block, head, system, select, pf, tf)

    def close_session(self, now):
        started, _, gaps, reads, errors = self.session
        self.session = None
        gaps, reads = sorted(gaps), sorted(reads)
        ms = lambda v: round(v * 1000, 3)
        summary = dict(seconds=round(now - started, 3), samples=len(reads), errors=self.errors - errors,
                       intervalMeanMs=ms(sum(gaps) / len(gaps)) if gaps else None,
                       intervalP99Ms=ms(gaps[int(len(gaps) * .99)]) if gaps else None,
                       intervalMaxMs=ms(gaps[-1]) if gaps else None, over10ms=sum(1 for v in gaps if v > .010),
                       readMeanMs=ms(sum(reads) / len(reads)) if reads else None, readMaxMs=ms(reads[-1]) if reads else None)
        with self.lock:
            self.rows.append(('rc_gate_jitter', summary))

    def run(self):
        while True:
            now = time.monotonic()
            if now >= self.active_until or self.queued >= ROW_CAP:
                if self.session:
                    self.close_session(now)
                self.last = None
                time.sleep(0.02)
                continue
            if self.session is None:
                self.session = (now, None, [], [], self.errors)
            started, previous, gaps, reads, errors = self.session
            if previous is not None and len(gaps) < 200000:
                gaps.append(now - previous)
            self.session = (started, now, gaps, reads, errors)
            try:
                row = self.sample()
            except (OSError, struct.error):
                self.errors += 1
                time.sleep(0.02)
                continue
            if len(reads) < 200000:
                reads.append(time.monotonic() - now)
            if row != self.last:
                self.last = row
                with self.lock:
                    self.rows.append(('rc_gate', dict(row, at=round(now, 4))))
                self.queued += 1
            time.sleep(PERIOD)

    def drain(self):
        with self.lock:
            rows, self.rows = list(self.rows), collections.deque()
        return rows


class RcGateMixin:
    def step(self, pilot, own, event_reaction=False):
        sampler = getattr(self, 'rc_gate', None)
        if sampler is None:
            sampler = self.rc_gate = RcGateSampler(pilot.game)
        if tuple(own.get('roomScope') or ())[:2] == (2, 34) and own.get('event'):
            sampler.arm()
        for kind, row in sampler.drain():
            pilot.write(kind, **row)
        return super().step(pilot, own, event_reaction)
