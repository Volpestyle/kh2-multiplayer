"""VUH-2617 lead #174: deferred cursor repair after the seat itself is downed mid-heal (stacks outside thorn_heal2).

bld next run 1: a heal stopped by the host's own logical down got an immediate readback restore, but a downed seat
ignores menu pulses (6 UP sent, row stayed 2), so the cursor sat on Items until a later repair. Here a restore whose
stop reason is the seat's own down (`own-hp`, `logical-down`) sends nothing and records ONE pending repair, bound to
that downing: own actor, room scope, the own co-op log's down count, plus its revive count and the emitted confirms.
The repair runs once, just before the next `attack` press, and only in ordinary living field play after a proven
revive (own log revives risen): logs known, HP > 0, not logically downed, free, in field, menu FF, no event, no
reaction, no held or releasing revive, same known actor (no fallback)/scope/down count. It sends one Circle per
recorded confirm (at most 2), then reads the cursor row fresh and steers UP to Attack (heal2's bounds). Before every
one of those pulses it requalifies from fresh state (actor, logs, down count, the revive count it started with,
living, in field, no event/reaction/held revive; its own submenu may be open) and stops for good on any failure;
after it, the delayed attack is dropped if a hazard has appeared. It never sends Cross. An unreadable cursor authorizes no pulse. It is cleared, without pulses, on Game Over,
full-party defeat, unknown logs, actor/scope drift, a new downing, or fight end. Every other restore is heal2's,
unchanged. Logged as `heal_repair` rows (stage recorded / waiting / done / cleared).
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import thorn  # noqa: E402
import thorn_heal2 as heal2  # noqa: E402

base = thorn.base
Game, k32, psapi = thorn.Game, thorn.k32, thorn.psapi  # the runner's read-only Game import contract

DOWN_REASONS = ("own-hp", "logical-down")


class DeferredRepairPadMixin:
    heal_defer_hook = None  # (reason, emitted) -> recorded?; set by DeferredRepairPilotMixin.fight
    heal_repair_hook = None  # () -> may the attack go? tries the pending repair before an attack press

    def heal_restore(self, reason):
        if reason in DOWN_REASONS and self.heal_defer_hook is not None:
            emitted = dict(self.heal_emitted or {"DOWN": 0, "UP": 0, "confirm": 0})
            self.heal_emitted = None
            recorded = self.heal_defer_hook(reason, emitted)
            if self.heal_log is not None:
                self.heal_log("heal_restore", reason=reason, emitted=emitted, attempted=[], observed=[],
                              method="deferred" if recorded else "deferred-not-recorded", verified=False, atAttack=None)
            return
        return super().heal_restore(reason)

    def tap(self, action, ms=90):
        if action == "attack" and self.heal_repair_hook is not None and not self.heal_repair_hook():
            self.hold(set())  # a hazard appeared while the repair ran: the delayed attack's stale Cross is dropped
            return False
        return super().tap(action, ms)


class DeferredRepairPilotMixin:
    def fight(self, step, deadline):
        if not step.get("heal") or not isinstance(self.kb, DeferredRepairPadMixin):
            return super().fight(step, deadline)
        pending, waited = [None], [None]

        def log(stage, **fields):
            self.write("heal_repair", stage=stage, **fields)

        def record(reason, emitted):
            coop = self.cooperative
            s = self.state()
            if coop is None or not (coop.own_log.update() and coop.peer_log.update()):
                log("cleared", reason="unknown-log-at-down", stopReason=reason)
                pending[0] = None
                return False
            if not s.get("player"):  # Quill R1: only a known canonical actor can be bound
                log("cleared", reason="unknown-actor-at-down", stopReason=reason)
                pending[0] = None
                return False
            pending[0] = {"actor": s["player"], "scope": list(s["roomScope"]),
                          "downs": coop.own_log.downs, "revives": coop.own_log.revives,
                          "confirms": emitted.get("confirm", 0), "stopReason": reason}
            waited[0] = None
            log("recorded", **pending[0])
            return True

        def clear(why):
            log("cleared", reason=why, pending=pending[0])
            pending[0] = None

        def wait(why):
            if waited[0] != why:  # one row per change of reason
                waited[0] = why
                log("waiting", reason=why)

        def qualify(p, receipt=None, during=False):
            """('clear'|'wait'|None, reason) from fresh state. receipt: the revive count seen when the repair began;
            during: inside this repair, where its own Circle backout may leave a submenu (menu != FF) open."""
            coop, s = self.cooperative, self.state()
            logs_ok = coop is not None and coop.own_log.update() and coop.peer_log.update()
            if s["menu"] == base.GAME_OVER_MENU:
                return "clear", "game-over"
            if not logs_ok:
                return "clear", "unknown-log"
            if coop.own_log.full_party or coop.peer_log.full_party:
                return "clear", "full-party"
            if coop.own_log.downs != p["downs"]:
                return "clear", "new-downing"
            if not s.get("player") or s["player"] != p["actor"] or list(s["roomScope"]) != p["scope"]:
                return "clear", "identity-or-scope"  # Quill R1: no fallback to the recorded actor
            if receipt is not None and coop.own_log.revives != receipt:
                return "clear", "revive-receipt-changed"
            if coop.own_log.downed or s["hp"] is None or s["hp"] <= 0:
                return "wait", "still-down"
            if coop.own_log.revives <= p["revives"]:
                return "wait", "no-proven-revive"
            ordinary = s["inField"] and (during or (s["free"] and s["menu"] == 0xFF))
            if not ordinary or s["event"] or s["react"]:
                return "wait", "not-ordinary"
            if getattr(coop, "hold_started", None) is not None or getattr(coop, "waiting_release", False):
                return "wait", "revive-held"
            return None, None

        def repair():
            """Runs before an attack press. Returns False when the delayed attack must not be sent."""
            p = pending[0]
            if p is None:
                return True
            verdict, why = qualify(p)
            if verdict == "clear":
                clear(why)
                return True
            if verdict == "wait":
                wait(why)
                return True
            pending[0] = None  # consumed once, whatever happens next; never re-recorded or retried
            receipt = self.cooperative.own_log.revives
            kb, attempted, observed = self.kb, [], []

            def pulse(key):
                # Quill R2: requalify before every repair pulse (heal_pulse adds the Game Over/room check).
                verdict, why = qualify(p, receipt, during=True)
                if verdict is not None:
                    log("stopped", reason=why, before=key, attempted=list(attempted), observed=observed)
                    return False
                return kb.heal_pulse(key, attempted)

            row, sub = kb.heal_cursor() if kb.heal_cursor is not None else (None, None)
            observed.append([row, sub])
            if row is None:
                log("done", attempted=attempted, observed=observed, atAttack=None, note="unknown-cursor-no-pulse")
                return qualify(p, receipt)[0] is None
            stopped = False
            for _ in range(min(p["confirms"], heal2.SUBMENU_TRIES)):
                if not pulse("SPACE"):
                    stopped = True
                    break
            if not stopped:
                for _ in range(heal2.CURSOR_TRIES):
                    row, sub = kb.heal_cursor()
                    observed.append([row, sub])
                    if row == 0 or row is None or sub:
                        break
                    if not pulse("UP"):
                        stopped = True
                        break
                else:
                    observed.append(list(kb.heal_cursor()))
            if not stopped:
                log("done", attempted=attempted, observed=observed,
                    atAttack=observed[-1][0] == 0 and not observed[-1][1])
            # Recheck before handing the delayed original attack back: a newly opened event, reaction, down or
            # Game Over must not receive its stale Cross.
            verdict, why = qualify(p, receipt)
            if verdict is not None:
                log("attack-dropped", reason=why)
                return False
            return True

        self.kb.heal_defer_hook, self.kb.heal_repair_hook = record, repair
        try:
            return super().fight(step, deadline)
        finally:
            self.kb.heal_defer_hook = self.kb.heal_repair_hook = None
            if pending[0] is not None:
                clear("fight-end")


class Pad(DeferredRepairPadMixin, heal2.HealPadMixin, thorn.Pad):
    pass


class Pilot(DeferredRepairPilotMixin, heal2.HealPilotMixin, thorn.Pilot):
    pass


def main():
    base.PromptReactionReader = thorn.PromptReader
    base.Game, base.PadInput, base.Pilot = thorn.Game, Pad, Pilot
    base.CooperativeFight = thorn.CooperativeFight
    return base.main()


if __name__ == "__main__":
    sys.exit(main())
