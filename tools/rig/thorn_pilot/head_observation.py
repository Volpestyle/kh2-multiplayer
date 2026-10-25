"""OFFLINE CANDIDATE: external RPM only; no native calls, writes or game handles.

Caller supplies the existing owned pilot.Game. See PROPOSAL.md for byte/source
proof and the limits: collision-enabled and geometric reach are not CanHit.
"""
import math
import struct
import time

NOW, CANONICAL, HEAD, REGIONS = 0x717008, 0x2A105D0, 0x2A171C8, 0x2B0D720
PLAYER_CONTROLLER = 0x2AE80A0  # native 3FD550 getter / 3FD830 bind, not the character root
IN_FIELD, MENU, EVENT, CUTSCENE, FROZEN = 0x9BA8D0, 0x7435D0, 0x2A11478, 0xB65210, 0x2A171E8
SAVE, ITEM_SLOTS, POTION_STOCK = 0x9A98B0, 0x2524, 0x3581
ROW = struct.Struct('<BBhBBhhhhhhh')
TARGET_ROW = (0, 0, 0, 3, 3, 67, 79, 35, 0, 0, 100, 100)
HEAD_ROW = (0, 0, 0, 2, 3, 68, 20, 4, 0, 0, 90, 90)


class Refused(ValueError):
    pass


def scope(raw):
    return tuple(raw[:3]) + struct.unpack_from('<HHH', raw, 4)


def group_enabled(mask, group, default):
    """3BA550: script enable/disable overrides, motion overrides, table default."""
    a0, a4, a8, ac = mask
    bit = 1 << group
    if a8 & bit:
        return True
    if ac & bit:
        return False
    if a0 & bit:
        return True
    if a4 & bit:
        return False
    return bool(default & bit)


def world_point(bone, world, local):
    # 1A7AF0 flips model Y/Z; 3B50F0 composes bone * actor+808.
    b = list(bone)
    for i in range(4):
        b[4*i+1] *= -1
        b[4*i+2] *= -1
    mat = [sum(b[4*r+k] * world[4*k+c] for k in range(4))
           for r in range(4) for c in range(4)]
    v = (*local, 1.0)
    point = tuple(sum(v[k]*mat[4*k+c] for k in range(4)) for c in range(3))
    if not all(math.isfinite(x) and abs(x) < 100000 for x in (*mat, *point)):
        raise Refused('nonfinite-or-implausible-matrix')
    return point


class Reader:
    def __init__(self, game):
        self.game = game

    def raw(self, address, size):
        b = self.game.read(address, size)
        if len(b) != size:
            raise Refused('partial-read')
        return b

    def u32(self, a):
        return struct.unpack('<I', self.raw(a, 4))[0]

    def u64(self, a):
        return struct.unpack('<Q', self.raw(a, 8))[0]

    def resolve(self, handle):
        if not handle:
            return 0
        return self.u64(self.game.base+REGIONS+8*((handle & 0x7fffffff) >> 25)) | (handle & 0x1ffffff)

    def members(self):
        root = self.u64(self.game.base+HEAD)
        result, seen = {}, set()
        for _ in range(256):
            if not root:
                return result
            if root < 0x10000 or root in seen:
                raise Refused('bad-list')
            seen.add(root)
            h = self.u32(root+4)
            if not h or self.resolve(h) != root:
                raise Refused('list-handle')
            result[root] = h
            root = self.resolve(self.u32(root+0xA90))
        raise Refused('list-cap')

    def gate(self, s, ordinary=True):
        g = self.game.base
        if tuple(s.get('roomScope', ()))[:2] != (2, 34) or not s.get('player'):
            raise Refused('not-thorn-player')
        raw = self.raw(g+NOW, 10)
        if scope(raw) != tuple(s['roomScope']) or self.u64(g+CANONICAL) != s['player']:
            raise Refused('scope-or-player')
        if ordinary and (not s.get('free') or not self.raw(g+IN_FIELD, 1)[0]
                         or self.raw(g+MENU, 1)[0] != 255 or self.u64(g+EVENT)
                         or self.u32(g+CUTSCENE) or self.u32(g+FROZEN)):
            raise Refused('not-ordinary')
        return raw

    def items(self, s):
        """Character-record-0 slots, as in the existing TT34 scenario samples."""
        try:
            start = self.gate(s, ordinary=False)
            g = self.game.base+SAVE
            a = self.raw(g+ITEM_SLOTS, 16)
            stock = self.raw(g+POTION_STOCK, 1)[0]
            b = self.raw(g+ITEM_SLOTS, 16)
            if a != b or self.raw(g+POTION_STOCK,1)[0] != stock or start != self.gate(s, ordinary=False):
                raise Refused('items-changed')
            slots = list(struct.unpack('<8H', a))
            return dict(known=True, slots=slots, potionSlots=sum(x == 1 for x in slots),
                        slot0Potion=slots[0] == 1, stockByte=stock, record=0)
        except (OSError, ValueError, struct.error) as e:
            return dict(known=False, reason=str(e), slots=None, slot0Potion=None)

    def lock_snapshot(self, player):
        # 3FD830 binds the character to a separate controller container.
        # 3DD5B0 stores owner at (container+BE8)+38; 3FD830 also sets C38.
        container = self.u64(self.game.base+PLAYER_CONTROLLER)
        if container < 0x10000:
            raise Refused('no-lock-controller')
        if self.u64(container+0xC20) != player:
            raise Refused('lock-controller-owner')
        if self.u64(container+0xC38) != player:
            raise Refused('lock-controller-character')
        raw = self.raw(container+0xBE8, 24)
        if struct.unpack_from('<I', raw)[0] not in (0, 1, 2):
            raise Refused('lock-controller-mode')
        return container, raw

    def head(self, s):
        try:
            started = time.monotonic()
            gate = self.gate(s)
            members = self.members()
            player = s['player']
            if player not in members:
                raise Refused('player-not-listed')
            player_pos_raw = self.raw(player+0x670,12)
            player_pos = struct.unpack('<3f',player_pos_raw)
            if not all(math.isfinite(x) and abs(x)<100000 for x in player_pos):
                raise Refused('player-position')
            bosses = []
            for actor, handle in members.items():
                entry = self.u64(actor+0x918)
                if entry and self.u32(entry) == 307 and self.raw(entry+4, 1)[0] == 12:
                    bosses.append((actor, handle, entry))
            if len(bosses) != 1:
                raise Refused('not-one-thorn')
            actor, handle, entry = bosses[0]
            coct, model = self.u64(actor+0xAB8), self.u64(actor+0x7B0)
            if not coct or not model:
                raise Refused('no-model-or-collision')
            header = self.raw(coct, 8)
            if struct.unpack('<II', header) != (68, 1):
                raise Refused('collision-header')
            table = self.raw(coct+64,68*20)
            target = table[20*21:20*22]
            hurt = table[20*23:20*24]
            if sum(ROW.unpack_from(table,i*20)[3] == 3 for i in range(68)) != 1:
                raise Refused('not-one-head-target')
            if ROW.unpack(target) != TARGET_ROW or ROW.unpack(hurt) != HEAD_ROW:
                raise Refused('collision-row-drift')
            controller, lock = self.lock_snapshot(player)  # mode, then native 20-byte TARGET
            mode, target_handle, group, alternate, alternate_index, tail = struct.unpack('<6I', lock)
            bones = self.u64(model+0x50)
            if not bones:
                raise Refused('no-bones')
            status = self.u64(actor+0x5C0)
            hp = struct.unpack('<i', self.raw(status,4))[0] if status else 0
            if hp <= 0:
                raise Refused('dead-thorn')
            mask = struct.unpack('<4I', self.raw(actor+0x5A0, 16))
            pattern = self.raw(actor+0xD10,12)
            flags = self.u32(actor+0x9B8)
            flags120 = self.u32(actor+0x120)
            collision_ptr = self.u64(actor+0x640)
            collision_count = struct.unpack('<i', self.raw(collision_ptr,4))[0] if collision_ptr else 0
            motion = self.u32(actor+0x180)
            b67 = self.raw(bones+64*67, 64)
            b68 = self.raw(bones+64*68, 64)
            world = self.raw(actor+0x808, 64)
            target_pos = world_point(struct.unpack('<16f', b67), struct.unpack('<16f', world), (79,35,0))
            head_pos = world_point(struct.unpack('<16f', b68), struct.unpack('<16f', world), (20,4,0))
            # Fresh membership before any final root dereferences, then identity/pose bookends.
            after = self.members()
            if after.get(actor) != handle or after.get(player) != members[player]:
                raise Refused('roots-replaced')
            if (self.u64(actor+0x918), self.u64(actor+0xAB8), self.u64(actor+0x7B0),
                self.u64(model+0x50), self.u64(actor+0x5C0)) != (entry,coct,model,bones,status):
                raise Refused('identity-drift')
            if (self.lock_snapshot(player) != (controller, lock) or self.raw(coct,8) != header
                or self.raw(player+0x670,12) != player_pos_raw
                or self.raw(coct+64,68*20) != table
                or self.raw(actor+0x5A0,16) != struct.pack('<4I',*mask)
                or self.raw(actor+0xD10,12) != pattern
                or self.u32(actor+0x9B8) != flags or self.u32(actor+0x120) != flags120
                or self.u64(actor+0x640) != collision_ptr or self.u32(actor+0x180) != motion
                or struct.unpack('<i',self.raw(collision_ptr,4))[0] != collision_count
                or struct.unpack('<i',self.raw(status,4))[0] != hp
                or self.raw(bones+64*67,64) != b67 or self.raw(bones+64*68,64) != b68
                or self.raw(actor+0x808,64) != world or self.gate(s) != gate):
                raise Refused('snapshot-drift')
            elapsed = time.monotonic()-started
            if elapsed > 0.05:
                raise Refused('snapshot-over-50ms')
            locked = mode == 2 and target_handle == handle and group == 0 and alternate == 0
            enabled = group_enabled(mask, 0, 1) and bool(flags & 0x400)
            native_victim = not flags & 4 and not flags120 & 8 and collision_count > 0
            d10,d14,d18 = struct.unpack('<3I',pattern)
            return dict(known=True, actor=actor, handle=handle, motion=motion,
                        playerPos=player_pos,
                        lockController=controller, lockOwner=player,
                        lockMode=mode, lockHandle=target_handle, lockGroup=group, lockAlternate=alternate,
                        lockedHead=locked, targetPart='head-target-bone67' if locked else None,
                        targetPos=target_pos, headPos=head_pos, headRadius=90,
                        rootWorldPos=tuple(struct.unpack('<16f',world)[12:15]),
                        collisionEnabled=enabled, nativeVictimEligible=native_victim,
                        pattern=[d10,d14,d18], effectivePattern=(d10|d14)&~d18,
                        nativeCanHit=None, snapshotMs=round(elapsed*1000,3))
        except (OSError, ValueError, struct.error) as e:
            return dict(known=False, reason=str(e), lockedHead=None, nativeCanHit=None)
