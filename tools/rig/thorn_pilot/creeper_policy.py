"""VUH-2617 Creeper-first (lead #282): pure rules. No game imports or side effects.

Creepers (objectId 317, mob type 4) caused 9 of 22 host downs in zr98-104 and were never fought. While one is near the
host and nothing with priority applies, the host kills it, then returns to Thorn. Revive, logical downs, RCs/Reversal,
events, the existing low-HP heal/evasion and Thorn's dazed opening all keep their priority (`yield`).
"""
import math

CREEPER_OBJECT, CREEPER_TYPE = 317, 4
DAZED = (209, 210, 211)  # Thorn's opening after an RC: the head pilot's window


def creepers(foes):
    """Living Creepers from base.enemies() rows."""
    return [f for f in foes if f.get('objectId') == CREEPER_OBJECT and f.get('objectType') == CREEPER_TYPE
            and f.get('hp', 0) > 0 and f.get('position') and f.get('address')]


def horizontal(point, foe):
    return math.hypot(foe['position']['x'] - point[0], foe['position']['z'] - point[2])


def priority_yield(s, *, own_down, peer_down, logs_known, low=0.4):
    """The priorities decidable from the pilot's own state and the co-op logs alone: checked before any entity or head
    read, so an offered RC or a downed seat never waits on an observer call."""
    if not logs_known or own_down or peer_down:
        return True  # revive and logical-down handling own the inputs
    if not s.get('free') or s.get('react') or s.get('event') or not s.get('hp') or not s.get('maxHp'):
        return True  # RC/Reversal, events and menus first
    return s['hp'] < low * s['maxHp']  # the existing heal/evasion keeps its priority


def creeper_action(s, foes, *, own_down, peer_down, logs_known, head_motion=None, lock=None, handles=None,
                   cooled=(), engage=800.0, reach=170.0, low=0.4):
    """One decision for this tick: ('yield' | 'unlock' | 'approach' | 'attack', target or None).

    `lock` is the player's native lock (mode, handle) or None when unread; `handles` maps a Creeper address to its own
    list handle. A lock on anything but the chosen Creeper (Thorn's head, usually) is released first, because locked
    swings home on the locked target. `cooled` is True while every Creeper is cooling (an engagement timed out), or a
    collection of cooling addresses.
    """
    if priority_yield(s, own_down=own_down, peer_down=peer_down, logs_known=logs_known, low=low):
        return 'yield', None
    if cooled is True:
        return 'yield', None
    if head_motion in DAZED:
        return 'yield', None
    point = s.get('playerPos')
    if not point:
        return 'yield', None
    near = [f for f in creepers(foes) if f['address'] not in cooled and horizontal(point, f) <= engage]
    if not near:
        return 'yield', None
    target = min(near, key=lambda f: horizontal(point, f))
    if lock and lock.get('mode') == 2 and lock.get('handle') != (handles or {}).get(target['address']):
        return 'unlock', target
    if horizontal(point, target) > reach:
        return 'approach', target
    return 'attack', target
