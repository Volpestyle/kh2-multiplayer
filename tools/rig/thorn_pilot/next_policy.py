"""Pure proposal rules. No game imports or side effects."""
import math


def reach(s, head, blade_reach=220.0):
    if not head.get('known') or not s.get('playerPos'):
        return None
    # A planning estimate, not a read of native weapon/attack reach.
    return max(0.0, math.dist(head.get('playerPos',s['playerPos']), head['headPos'])-head['headRadius']) <= blade_reach


def head_action(s, head, *, own_down=False, peer_down=False, logs_known=True, low=0.4):
    if not logs_known or own_down or peer_down:
        return 'yield'
    if not s.get('free') or s.get('react') or not s.get('hp') or not s.get('maxHp'):
        return 'yield'
    if s['hp'] < low*s['maxHp']:
        return 'yield'  # c1 ordinary heal/evasion keeps its priority
    if not head.get('known') or not head.get('collisionEnabled') or not head.get('nativeVictimEligible'):
        return 'yield'
    if head.get('motion') not in (202,209,210,211):
        return 'yield'  # explicitly excludes the post-298 capture/fade
    if not reach(s, head):
        return 'approach'
    if not head.get('lockedHead'):
        return 'lock'
    return 'attack'


def preheal_reason(s, items, *, own_down, peer_down, full_party, logs_known,
                   episode, expected, used, limit=3):
    if not logs_known or own_down or full_party:
        return 'logical-state'
    if not peer_down or episode != expected:
        return 'peer-episode'
    if not s.get('free') or not s.get('inField') or s.get('menu') != 255 or s.get('event'):
        return 'not-ordinary-start'
    if s.get('react'):
        return 'reaction'
    if not s.get('hp') or not s.get('maxHp') or s['hp'] >= 0.4*s['maxHp']:
        return 'hp-or-threshold'
    if used >= limit:
        return 'fight-heal-limit'
    if not items.get('known') or not items.get('slot0Potion'):
        return 'slot0-not-proven-potion'
    return None


def swing_fraction(events):
    """Input denominator only. Never relabel an input or CanHit check as a hit."""
    sent = [r for r in events if r.get('kind') == 'thorn_swing' and r.get('sent')]
    good = sum(r.get('lockedHead') is True and r.get('inReachEstimate') is True for r in sent)
    unknown = sum(r.get('lockedHead') is None or r.get('inReachEstimate') is None for r in sent)
    return dict(sent=len(sent),headLockedAndInEstimatedReach=good,unknown=unknown,
                fraction=good/len(sent) if sent else None,physicalHeadHits=None)
