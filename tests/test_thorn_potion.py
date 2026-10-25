"""Offline controls for tools/rig/thorn_pilot/thorn_potion.py (VUH-2617 lead #343, keep the last Potion for a rescue).

The production Pilot class runs its real heal_available chain (PotionReservePilotMixin over thorn_heal2's caps and
slot-0 proof), the real opening/grace heal entry (thorn_survival254b.opening_heal) and the real pre-revive start check
(thorn_next.PreReviveCooperativeMixin). Only the item read, the downed logs, the clock, the pilot's write and the
heal sequences themselves are fakes.
Run: python -B tests/test_thorn_potion.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "rig" / "thorn_pilot"))
import thorn_heal2  # noqa: E402
import thorn_next  # noqa: E402
import thorn_potion as tp  # noqa: E402
import thorn_survival254b  # noqa: E402

failures = 0
PLAYER, PARTNER = 0x1000, 0x2000
SCOPE = [2, 34, 0, 157, 157, 157]


def check(ok, label):
    global failures
    print(("PASS" if ok else "FAIL") + ": " + label)
    failures += 0 if ok else 1


class Clock:
    def __init__(self):
        self.now = 1000.0

    def __call__(self):
        return self.now


clock = Clock()
for module in (tp, thorn_next, thorn_survival254b):
    module.time.monotonic = clock


class Log:
    def __init__(self, downed=False, full=False, actor=0):
        self.downed, self.full_party, self.actor = downed, full, actor
        self.downs = 1 if downed else 0
        self.revives = 0

    def update(self):
        return True


class Coop:
    def __init__(self, peer_down=False, own_down=False, full=False):
        self.own_log = Log(own_down, full)
        self.peer_log = Log(peer_down, full, actor=PARTNER if peer_down else 0)


def items(potions, known=True):
    if not known:
        return dict(known=False, reason="x", slots=None, slot0Potion=None)
    slots = [1] * potions + [0] * (8 - potions)
    return dict(known=True, slots=slots, potionSlots=potions, slot0Potion=potions > 0, stockByte=0, record=0)


class Kb:
    def __init__(self):
        self.repairs = 0

    def heal_repair_hook(self):
        self.repairs += 1
        return False  # stop the real opening heal right after its availability check


def pilot(potions=1, reserve=True, peer_down=False, own_down=False, full=False, known=True, used=0, missed=0):
    p = object.__new__(tp.Pilot)
    p.rows = []
    p.write = lambda kind, **f: p.rows.append((kind, f))
    p.inventory = items(potions, known)
    p.observe_items = lambda state, stage="proof": p.inventory
    p.heal_cfg, p.heal_used, p.heal_missed = {"max": 3, "missMax": 4}, used, missed
    p.potion_reserve = reserve
    p.potion_reserve_stats = dict(refusals=0, rescueAllowed=0)
    p.potion_reserved_at = None
    p.cooperative = Coop(peer_down, own_down, full)
    p.kb = Kb()
    return p


def own(hp=5, **kw):
    s = dict(world=2, room=34, free=True, inField=True, hp=hp, maxHp=20, roomScope=SCOPE, player=PLAYER, react=0,
             event=0, menu=255, reactObservation={})
    s.update(kw)
    return s


def kinds(p):
    return [k for k, _ in p.rows]


# ---- heal_available: the one refusal --------------------------------------------------------------------------
check(not pilot(1).heal_available(own()), "last Potion, partner up: unavailable to a heal")
check(pilot(2).heal_available(own()), "two Potions, partner up: available as before")
check(pilot(1, peer_down=True).heal_available(own()), "last Potion, partner logically downed: available (a rescue)")
check(pilot(1, reserve=False).heal_available(own()), "switch off: the last Potion is available as before")
check(not pilot(0).heal_available(own()), "no Potion: unavailable as before")
check(not pilot(1, known=False).heal_available(own()), "unknown items: the base proof refuses as before")
check(not pilot(1, used=3).heal_available(own()) and not pilot(1, peer_down=True, used=3).heal_available(own()),
      "the fight's Potion cap still refuses, rescue or not")
check(not pilot(2, missed=4).heal_available(own()), "the menu-miss cap still refuses")
check(not pilot(1, peer_down=True, own_down=True).heal_available(own()),
      "both seats logically down: not a rescue, the last Potion stays reserved")
check(not pilot(1, peer_down=True, full=True).heal_available(own()), "full party down: not a rescue")
p = pilot(2)
reads = iter([items(2), items(1, known=False)])
p.observe_items = lambda state, stage="proof": next(reads)
check(p.heal_available(own()) and "potion_reserved" not in kinds(p),
      "the reserve's own item read comes back unknown: the base decision stands (no reservation on an unknown read)")

p = pilot(1)
p.heal_available(own(hp=6))
row = [f for k, f in p.rows if k == "potion_reserved"]
check(len(row) == 1 and row[0]["potionSlots"] == 1 and row[0]["hp"] == 6 and p.potion_reserve_stats["refusals"] == 1,
      "a refusal is logged with the HP and the Potion count")
clock.now += 1.0
p.heal_available(own())
clock.now += 4.5
p.heal_available(own())
check(kinds(p).count("potion_reserved") == 2 and p.potion_reserve_stats["refusals"] == 3,
      "repeated refusals are counted; the row repeats at most every 5 s")
p = pilot(1, peer_down=True)
p.heal_available(own())
check(p.potion_reserve_stats["rescueAllowed"] == 1 and "potion_reserved" not in kinds(p), "a rescue use is counted, not logged as reserved")


# ---- the real opening / post-revive-grace heal entry ------------------------------------------------------------
def opening(potions, window, reserve=True):
    p = pilot(potions, reserve)
    p.heal_window = lambda state, head: window
    p.opening_heal_ready = 0.0
    started = thorn_survival254b.SurvivalPilotMixin.opening_heal(p, (own(hp=5), None, dict(known=True, motion=0)))
    return p, started


for window in ("post-revive-grace", "dazed"):
    p, started = opening(1, window)
    check(started is False and p.kb.repairs == 0 and "potion_reserved" in kinds(p),
          f"{window} heal with the last Potion: skipped at its availability check")
    p, started = opening(2, window)
    check(p.kb.repairs == 1, f"{window} heal with two Potions: passes its availability check as before")
    p, started = opening(1, window, reserve=False)
    check(p.kb.repairs == 1, f"{window} heal with the last Potion and the switch off: passes as before")


# ---- the real pre-revive start check ----------------------------------------------------------------------------
class Core:
    TIMEOUT = 60.0

    def step(self, pilot, own, event_reaction=False):
        return "core"


class PreRevive(thorn_next.PreReviveCooperativeMixin, Core):
    def __init__(self):
        self.own_log, self.peer_log = Log(), Log(True, actor=PARTNER)
        self.hold_started, self.waiting_release, self.since = None, False, None

    def stop(self, pilot, reason, own):
        pilot.rows.append(("stop", dict(reason=reason)))
        return True


def prerevive(potions, reserve=True):
    p = pilot(potions, reserve)
    c = PreRevive()
    p.cooperative = c
    p.head_active, p.next_options = True, {"healBeforeRevive": True}
    p.preheal_attempted, p.preheal_logged, p.preheal_aborts = set(), set(), 0
    p.potions = []
    p.pre_revive_heal = lambda s: p.potions.append(s["hp"])
    res = c.step(p, own(hp=5))
    return p, res


p, res = prerevive(1)
check(res is True and p.potions == [5] and "potion_reserved" not in kinds(p) and p.potion_reserve_stats["rescueAllowed"] >= 1,
      "pre-revive heal with the last Potion: it still starts (the partner is down)")
p.cooperative.peer_log.downed = True
check(p.heal_available(own()), "the heal's key-1 recheck during the pre-revive sequence (partner still down): available")
p, res = prerevive(1, reserve=False)
check(res is True and p.potions == [5], "control, switch off: the pre-revive heal starts as before")
p, res = prerevive(0)
check(res == "core" and p.potions == [] and any(f.get("reason") == "common-heal-cap-or-unavailable" for k, f in p.rows),
      "control, no Potion: the pre-revive heal is skipped as before")


# ---- the pilot.py / HealPad key-1 guard reads the same check ----------------------------------------------------
import inspect  # noqa: E402
src = inspect.getsource(thorn_heal2.HealPilotMixin.fight)
check('"unavailable" if not self.heal_available(s)' in src,
      "every heal sequence's key-1 guard (ordinary, opening, pre-revive) asks heal_available, so the refusal reaches it")


# ---- switch semantics, summary, MRO ------------------------------------------------------------------------------
class Lower:
    def fight(self, step, deadline):
        self.seen = self.potion_reserve
        return "fought"


class P(tp.PotionReservePilotMixin, Lower):
    def __init__(self):
        self.rows = []

    def write(self, kind, **f):
        self.rows.append((kind, f))


pp = P()
check(pp.fight({"eventReactions": True, "rescuePotionReserve": True}, 0) == "fought" and pp.seen is True,
      "true on the Thorn fight step turns it on")
check(bool(pp.rows) and pp.rows[-1][0] == "rescue_potion_reserve_summary" and pp.potion_reserve is False, "one summary row; off after the fight")
pp = P()
pp.fight({"eventReactions": True}, 0)
check(pp.seen is False and not pp.rows, "absent key: off, no summary")
pp = P()
pp.fight({"rescuePotionReserve": True}, 0)
check(pp.seen is False, "the key outside the Thorn fight step is ignored")
mro = tp.Pilot.__mro__
check(mro[1] is tp.PotionReservePilotMixin and mro[2] is tp.child.Pilot
      and [c for c in mro if "heal_available" in c.__dict__] == [tp.PotionReservePilotMixin, thorn_heal2.HealPilotMixin],
      "Pilot: the reserve is outermost over thorn_protect, the only other heal_available is thorn_heal2's")

print(f"failures={failures}")
sys.exit(1 if failures else 0)
