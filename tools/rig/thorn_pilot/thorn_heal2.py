"""VUH-2617 lead #156/#160/#161: heal v2 for the Thorn pilot, as mixins for Quill's composed child (thorn_next.py).

Changes from thorn_heal.py (c1, unchanged), from c1 run 2's evidence (11 of 13 sequences aborted on `reaction`,
and the aborts left the command cursor off Attack for up to ~18 s):
- Every refusal condition is checked once, at press 1: ordinary free field play in the fight's own room and scope,
  menu FF, no event, no reaction, own HP > 0, readable co-op logs, nobody logically down. A downed peer is allowed
  only through the child's heal-before-revive exception (`heal_peer_exception`), and only for the exact episode it
  names: peer actor, the peer log's real down count, own actor and scope.
- Presses 2..n and the reset keys stop only on hard hazards: own HP <= 0 or logically down, Game Over, room or
  scope exit, event start, unreadable logs, full party down, a peer newly down, or (under the exception) any
  change of that episode, including the peer's recovery.
- A stop restores navigation without Cross. Until the command-cursor address is calibrated (`heal_cursor` unset)
  the restore is counted from emitted pulses (one Circle per emitted confirm, at most 2, then one UP per net
  emitted DOWN), logged as method counted-unverified with verified false: an emitted pulse is not proof the menu
  moved. With `heal_cursor` set, it steers by readback instead: Circle out of a submenu, then UP until the cursor
  reads Attack, at most CURSOR_TRIES pulses, logging attempted pulses and observed rows separately. Every restore
  pulse is preceded by a Game Over/room check; nothing is restored in Game Over or after a room exit.
- Key 1 of every heal, ordinary or pre-revive, requires heal_available(): a fresh observe_items proof that slot 0
  holds a Potion, plus the shared caps (Potions used < max, full-sequence menu misses < missMax). A supplied
  heal_peer_exception that does not match a still-downed peer refuses at key 1 instead of becoming an ordinary heal.
Heal-before-revive itself lives in the child (Quill's gating, lead #160). pilot.py and thorn.py are unchanged.
A dropped sequence still reads as a miss to pilot.py; up to REFUNDS such misses are handed back per fight.
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import thorn  # noqa: E402

base = thorn.base
Game, k32, psapi = thorn.Game, thorn.k32, thorn.psapi  # the runner's read-only Game import contract

KEY, RESET = "heal:", "healreset:"
REFUNDS = 4
CURSOR_TRIES = 6
SUBMENU_TRIES = 2
# Command-menu cursor row (u8, 0 Attack .. 3), calibrated 2026-10-24 on this build: run
# 20261024-011810_net_cursor_calibration_1 read 0,1,2,1,0,1,1,0 here across Down Down Up Up Down Circle Up, matching
# every capture, and no other byte in 0x2A0E000-0x2A13000 / 0x743000-0x744000 did. No submenu-open byte is known.
CURSOR_RVA = 0x2A106BC


def episode(coop, s):
    """The identity a peer-down exception is bound to (R3)."""
    return {"peerActor": coop.peer_log.actor, "peerDowns": coop.peer_log.downs, "ownActor": s["player"],
            "scope": list(s["roomScope"])}


class HealPadMixin:
    heal_guard = None  # set by HealPilotMixin.fight: (first, reset) -> abort reason or None
    heal_hazard = None  # () -> True when no restore pulse may be sent (Game Over, room exit)
    heal_cursor = None  # () -> (row, submenu_open) from a calibrated readback, or None while uncalibrated
    heal_log = None
    heal_baseline = None
    heal_aborted = False
    heal_emitted = None

    def tap(self, action, ms=90):
        if not action.startswith((KEY, RESET)) or self.heal_guard is None:
            return super().tap(action, ms)
        reset = action.startswith(RESET)
        if not reset and action.startswith(KEY + "0:"):
            self.heal_aborted, self.heal_baseline = False, None  # a new heal sequence
            self.heal_emitted = {"DOWN": 0, "UP": 0, "confirm": 0}
        if self.heal_aborted:
            return False
        key = action.split(":", 2)[-1]
        reason = self.heal_guard(self.heal_baseline is None and not reset, reset)
        if reason:
            self.heal_aborted = True
            self.hold(set())
            self.heal_restore(reason)
            return False
        sent = super().tap(key, ms)
        if self.heal_emitted is not None and key in self.heal_emitted:
            self.heal_emitted[key] += 1
        return sent

    def heal_pulse(self, key, attempted):
        if self.heal_hazard is not None and self.heal_hazard():
            return False
        super().tap(key)
        attempted.append(key)
        time.sleep(0.25)
        return True

    def heal_restore(self, reason):
        emitted = self.heal_emitted or {"DOWN": 0, "UP": 0, "confirm": 0}
        attempted, observed = [], []
        readback = self.heal_cursor is not None and self.heal_cursor()[0] is not None
        if reason not in ("game-over", "room"):
            if not readback:
                for key in ["SPACE"] * min(emitted["confirm"], 2) + ["UP"] * max(emitted["DOWN"] - emitted["UP"], 0):
                    if not self.heal_pulse(key, attempted):
                        break
            else:
                row, submenu = self.heal_cursor()
                observed.append([row, submenu])
                if submenu is None:
                    # No submenu readback: one Circle per emitted confirm (at most 2) backs out of Items/target;
                    # at the top level Circle is a jump and leaves the cursor where it is (calibration cal6).
                    for _ in range(min(emitted["confirm"], SUBMENU_TRIES)):
                        if not self.heal_pulse("SPACE", attempted):
                            break
                else:
                    for _ in range(SUBMENU_TRIES):
                        if not submenu or not self.heal_pulse("SPACE", attempted):
                            break
                        row, submenu = self.heal_cursor()
                        observed.append([row, submenu])
                for _ in range(CURSOR_TRIES):
                    row, submenu = self.heal_cursor()
                    observed.append([row, submenu])
                    if row == 0 or row is None or submenu or not self.heal_pulse("UP", attempted):
                        break
                else:
                    observed.append(list(self.heal_cursor()))
        self.heal_emitted = None
        if self.heal_log is not None:
            self.heal_log("heal_restore", reason=reason, emitted=emitted, attempted=attempted, observed=observed,
                          method="readback" if readback else "counted-unverified", verified=readback,
                          atAttack=(observed[-1][0] == 0 and not observed[-1][1]) if observed else None)


class HealPilotMixin:
    heal_peer_exception = None  # set by the child's heal-before-revive: episode(...) it grants, or None

    def fight(self, step, deadline):
        heal = step.get("heal")
        if not heal or not isinstance(self.kb, HealPadMixin):
            return super().fight(step, deadline)
        self.heal_cfg = dict(heal)
        self.heal_used = self.heal_missed = 0
        heal = {**heal, "keys": [f"{KEY}{i}:{k}" for i, k in enumerate(heal["keys"])],
                "reset": [f"{RESET}{k}" for k in heal.get("reset", ["SPACE", "SPACE"])]}
        refunds = [0]
        start = self.state()
        fight_room, fight_scope = (start["world"], start["room"]), start["roomScope"]

        def guard(first, reset):
            s = self.state()  # thorn.Pilot.state raises FightRoomExit on a room exit, as before
            coop = self.cooperative
            logs_ok = coop is None or (coop.own_log.update() and coop.peer_log.update())
            if first:
                grant = self.heal_peer_exception
                bound = coop is not None and logs_ok and coop.peer_log.downed and grant is not None \
                    and grant == episode(coop, s)
                self.kb.heal_baseline = {"room": fight_room, "scope": fight_scope,
                                         "episode": episode(coop, s) if bound else None}
            b = self.kb.heal_baseline
            if b["episode"] is not None:
                changed = not logs_ok or not coop.peer_log.downed or episode(coop, s) != b["episode"]
                peer = "episode-changed" if changed else None
            else:
                peer = "peer-down" if coop is not None and coop.peer_log.downed else None
            reason = "own-hp" if s["hp"] is None or s["hp"] <= 0 else \
                "game-over" if s["menu"] == base.GAME_OVER_MENU else \
                "room" if (s["world"], s["room"]) != b["room"] or s["roomScope"] != b["scope"] else \
                "event" if s["event"] else \
                "unknown-log" if not logs_ok else \
                "logical-down" if coop is not None and (coop.own_log.downed or coop.own_log.full_party
                                                        or coop.peer_log.full_party) else peer
            if reason is None and first:
                # Quill (lead #163/#164): a supplied exception never downgrades to an ordinary heal, and every
                # heal, ordinary or pre-revive, needs fresh slot-0 Potion proof and the shared caps at key 1.
                reason = "not-ordinary-start" if not (s["free"] and s["inField"] and s["menu"] == 0xFF) else \
                    "reaction" if s["react"] else \
                    "exception-mismatch" if self.heal_peer_exception is not None and b["episode"] is None else \
                    "unavailable" if not self.heal_available(s) else None
            if reason:
                self.write("heal_guard", reason=reason, first=first, reset=reset, hp=s["hp"], menu=s["menu"],
                           event=s["event"], react=s["react"], baseline={**b, "room": list(b["room"])})
                if not reset and refunds[0] < REFUNDS:
                    refunds[0] += 1
                    heal["missMax"] = heal.get("missMax", 4) + 1  # this abort's miss is not a menu miss
            return reason

        def hazard():
            s = self.state()
            return s["menu"] == base.GAME_OVER_MENU or (s["world"], s["room"]) != fight_room \
                or s["roomScope"] != fight_scope

        def cursor():
            """(row, submenu) from this seat's own game: row 0-3 or None on any read problem; submenu unknown."""
            try:
                row = self.game.rva(CURSOR_RVA, 1)[0]
            except (OSError, ValueError, AttributeError, IndexError, TypeError):
                return None, None
            return (row if 0 <= row <= 3 else None), None

        self.kb.heal_guard, self.kb.heal_hazard, self.kb.heal_log = guard, hazard, self.write
        supplied = self.kb.heal_cursor  # a child (or control) may supply its own reader; otherwise the calibrated byte
        if supplied is None:
            self.kb.heal_cursor = cursor
        try:
            return super().fight({**step, "heal": heal}, deadline)
        finally:
            self.kb.heal_guard = self.kb.heal_hazard = self.kb.heal_log = None
            self.kb.heal_cursor = supplied
            self.kb.heal_baseline, self.kb.heal_aborted, self.kb.heal_emitted = None, False, None
            self.heal_cfg = None
            if refunds[0]:
                self.write("heal_guard_summary", refunds=refunds[0])

    def write(self, kind, **fields):
        if kind == "heal" and getattr(self, "heal_cfg", None) is not None:
            if fields.get("after") is not None and fields["after"] > fields["hp"]:
                self.heal_used += 1
            elif not self.kb.heal_aborted:
                self.heal_missed += 1  # a sequence that ran in full and used no Potion
        return super().write(kind, **fields)

    def heal_available(self, state):
        """Caps from this pilot's own heal rows, plus the child's fresh slot-0 Potion proof (lead #160)."""
        cfg = getattr(self, "heal_cfg", None)
        if not cfg or self.heal_used >= cfg.get("max", 3) or self.heal_missed >= cfg.get("missMax", 4):
            return False
        observe = getattr(self, "observe_items", None)
        items = observe(state) if observe is not None else None
        return bool(items and items.get("known") and items.get("slot0Potion"))


class Pad(HealPadMixin, thorn.Pad):
    pass


class Pilot(HealPilotMixin, thorn.Pilot):
    pass


def main():
    base.PromptReactionReader = thorn.PromptReader
    base.Game, base.PadInput, base.Pilot = thorn.Game, Pad, Pilot
    base.CooperativeFight = thorn.CooperativeFight
    return base.main()


if __name__ == "__main__":
    sys.exit(main())
