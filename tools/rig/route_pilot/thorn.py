"""VUH-2617: Thorn pilots sharing the original runner, state reader and pad writer.

Import the shared pilot without changing its bytes; override behavior only in
this child process. Revive completion follows native progress and release, not
a fixed wall-clock hold. Gameplay input uses the existing pad mailbox.
"""
from __future__ import annotations

import math
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pilot as base

OriginalGame, OriginalPilot, OriginalPad = base.Game, base.Pilot, base.PadInput
OriginalPrompt = base.PromptReactionReader
re = base.re
k32, psapi = base.k32, base.psapi  # existing runner's read-only Game import contract


class FightRoomExit(Exception):
    def __init__(self, state):
        self.state = state


class PromptReader(OriginalPrompt):
    def interpret(self, state, raw):
        command, info = super().interpret(state, raw)
        # Same existing double-read mapping and all original identity/freshness
        # guards. These already-published fields are diagnostic, not authority.
        if info.get("source") == "downed-prompt-v7":
            kind, slot, progress, hide, triangle, fires = struct.unpack_from("<6I", raw, 328)
            if progress <= 1000 and (kind == 0 or slot < 3):
                info.update(reviveSlot=slot, reviveProgress=progress,
                            triangleFrames=triangle, promptFires=fires)
        return command, info


class Game(OriginalGame):
    def state(self):
        state = super().state()
        if (state["world"], state["room"]) == (2, 34) and state["playerPos"]:
            # An entity camera may follow somebody else. Combat/recovery always
            # navigates from the canonical living player's already-checked feet.
            state["cameraActorPos"] = state["pos"]
            state["pos"] = state["playerPos"]
        return state


class Pad(OriginalPad):
    thorn_combat = False
    # VUH-2617 r4: the one cadence owner of every Reversal press, the base reaction branch's and the Thorn
    # policy's. The pilot's latest state read names the command on screen; a Reversal tap within `reversal_gap`
    # of the last Reversal press is dropped. Other reactions (the owned event RCs) and held Triangle (revive,
    # the cooperative RC) are never gated.
    reversals = frozenset()
    reversal_gap = 0.0
    react_command = 0
    reversal_at = float("-inf")
    # VUH-2617 r6: the press edge of the reflect Reversal (432 on Thorn's projectile), for the reflect stance.
    reflect_command = None
    reflect_at = float("-inf")

    def reversal_ready(self, now):
        return now - self.reversal_at >= self.reversal_gap - 1e-6  # a slept-out gap is not short by rounding

    def tap(self, action, ms=90):
        # Recording can reduce native update rate. Give a real RC press time to
        # cross input frames; native admission still decides whether it is usable.
        if self.thorn_combat and action == "react":
            ms = max(ms, 180)
            if self.react_command in self.reversals:
                now = time.monotonic()
                if not self.reversal_ready(now):
                    return False
                self.reversal_at = now
                if self.react_command == self.reflect_command:
                    self.reflect_at = now
        return super().tap(action, ms)


class DownedLog(base.DownedLog):
    # The base reader has no per-line hook. Keep its bounded incremental read
    # and old event order, adding only v32's transition outcome observations.
    def update(self) -> bool:
        try:
            with self.path.open("rb") as log:
                if log.seek(0, 2) < self.offset:
                    self.known = False  # unexpected truncation is not proof of recovery
                    return False
                log.seek(self.offset)
                data = log.read(1024 * 1024)
                self.offset += len(data)
        except OSError:
            self.known = False
            return False
        lines = (self.pending + data).split(b"\n")
        self.pending = lines.pop()
        for raw in lines:
            line = raw.decode("utf-8", "replace")
            if "[downed] gate intercept " in line:
                actor = re.search(r" actor=([0-9A-Fa-f]+) ", line)
                self.actor = int(actor[1], 16) if actor else 0
                self.downed, self.full_party = True, False
                self.downs += 1
            elif "[downed] revive returned " in line:
                state = re.search(r" state=(\d+)", line)
                fault = re.search(r" fault=(\d+)", line)
                hp = re.search(r" hp=(\d+)/", line)
                if state and state[1] == "1" and hp and int(hp[1]) > 0 and (not fault or fault[1] == "0"):
                    self.downed, self.full_party, self.actor = False, False, 0
                    self.revives += 1
            elif "[downed] logical hold ended " in line:
                # A v32 transition retires the old actor; it has not proved the
                # new room's resolve or sit-out yet. Stay neutral until its log.
                self.downed, self.full_party, self.actor = True, False, 0
            elif "[downed] sitting out in the new room " in line:
                held = re.search(r" episode=([0-9A-Fa-f]+) actor=([0-9A-Fa-f]+) hp=", line)
                valid = held and int(held[1], 16) and int(held[2], 16)
                self.actor = int(held[2], 16) if valid else 0
                self.downed, self.full_party = True, False
                if valid:
                    self.downs += 1
            elif "[downed] logical resolve after transition " in line:
                resolved = re.search(r" actor=([0-9A-Fa-f]+) hp=-?\d+ -> (-?\d+) target=(\d+)", line)
                if resolved and int(resolved[1], 16) and int(resolved[2]) >= int(resolved[3]) > 0:
                    self.downed, self.full_party, self.actor = False, False, 0
                else:
                    self.downed, self.actor = True, 0  # unreadable/failed resolve: unknown
            elif "[downed] full party down " in line or "[downed] gate pass mode=" in line and "full party down:" in line:
                self.full_party = True
            elif "[downed] hold lost " in line or "[downed] revive FAULT" in line:
                self.downed, self.actor = True, 0  # refused/unknown: never infer living
        self.known = len(data) < 1024 * 1024 and (not self.downed or bool(self.actor))
        return self.known


class ThornPolicy:
    """VUH-2617 Thorn combat moves on top of the base fight, from live observations only (no memory writes).

    Bots05 attempt05 (r1) showed where the seats lose: Thorn's motion is a phase, not an attack. After the
    opening sequence (204..211, where Thorn loses most of its HP) it sits in motion 202 for minutes, and all
    seat HP was lost there: melee attack 448 around motionTime 160 and projectile 1041 from motionTime ~390
    on, at any distance (1800-2000 units included). Reversal RCs (345/426/432/666/667; 432 is the
    projectile's) were taken in about a quarter of their offers. So, per seat and from its own reads:
      - a Reversal on screen is pressed at once, before any move of this policy;
      - danger is learned per (motion, motionTime band) from this seat's own HP loss: seeded from attempt05
        and learned in the fight (at least `learn` drops in at least `learnRatio` of the band's visits).
        A near band gets one sidestep across Thorn's line on entry, within `radius`. A ranged band (a hit
        came from beyond `radius`) is telemetry only: r3's strafe through it dealt no damage in attempt06
        (Thorn flat at 227 for 100 s) and did not avoid 1041, so r5 fights through it as the base does;
      - Thorn's openings (seeded 209/210/211, plus motions that cost Thorn `openingLearn` HP in one visit)
        get an attack commitment: approach and swing, with no dodge, keep-away or escape;
      - keep-away at `kiteBelow` (25%), only for the lower seat (the partner alive and `kiteMargin` HP
        healthier), as a strafe on the `kiteRadius` ring, never a hold: the other seat keeps chipping, as
        in bot01's kill;
      - telemetry: Thorn's motion and band, band hits and visits, learned bands, openings, Thorn's HP lost
        per motion, counters.
    Pure decisions: every input is a state/entity dict, so they are tested offline."""
    REVERSALS = (345, 426, 432, 666, 667)
    # [motion, band, "near"|"ranged"], 64-frame bands: 202 entry, 448 around 160, 1041 from ~390 (attempt05).
    SEED_BANDS = ((202, 0, "near"), (204, 0, "near"), (226, 0, "near"), (202, 2, "near"),
                  (202, 6, "ranged"), (202, 7, "ranged"))
    OPENINGS = (209, 210, 211)

    def __init__(self, config: dict):
        self.dodge = bool(config.get("dodge", True))
        self.reversals = frozenset(int(c) for c in config.get("reversals", self.REVERSALS))
        self.reversal_gap = float(config.get("reversalGapSec", 0.2))
        self.band_frames = max(int(config.get("bandFrames", 64)), 1)
        self.seed = {(int(m), int(b)): str(kind) for m, b, kind in config.get("seedBands", self.SEED_BANDS)}
        self.openings_seed = frozenset(int(m) for m in config.get("openings", self.OPENINGS))
        self.opening_learn = int(config.get("openingLearn", 30))
        self.radius = float(config.get("radius", 650))
        self.learn = int(config.get("learn", 2))
        self.ratio = float(config.get("learnRatio", 0.34))
        self.cooldown = float(config.get("dodgeCooldownSec", 1.2))
        self.step = float(config.get("dodgeStep", 400))
        self.strafe_step = float(config.get("strafeRad", 0.6))
        self.strafe_flip = float(config.get("strafeFlipSec", 2.0))
        self.kite_below = float(config.get("kiteBelow", 0.25))
        self.kite_radius = float(config.get("kiteRadius", 750))
        self.kite_margin = int(config.get("kiteMargin", 4))
        # r6 reflect stance (off unless configured): in `reflectMotion`'s `reflectBands` (202's projectile stance),
        # the seat Thorn is shooting at (it saw or pressed `reflectCommand` within `reflectTargetSec`) stands still
        # `reflectRange` from Thorn's root and reverses the orb back into Thorn; no stick input while the Reversal
        # is offered or within `reflectStillSec` of a press.
        self.reflect = bool(config.get("reflect", False))
        self.reflect_motion = int(config.get("reflectMotion", 202))
        self.reflect_bands = frozenset(int(b) for b in config.get("reflectBands", (6, 7)))
        self.reflect_command = int(config.get("reflectCommand", 432))
        self.reflect_min, self.reflect_max = (float(v) for v in config.get("reflectRange", (1500, 1800)))
        self.reflect_still = float(config.get("reflectStillSec", 0.5))
        self.reflect_target = float(config.get("reflectTargetSec", 4.0))
        # r6c: false makes this seat the reflect seat outright (the host route): the gate is Thorn's motion and band
        # alone. Thorn shoots the seat farther from it (bot01 58/60 completions), so the stand-off itself draws it.
        self.reflect_targeted = bool(config.get("reflectTargeted", True))
        self.reflect_seen_at = float("-inf")
        self.motion = self.band = None
        self.visit = 0                      # a new (motion, band) is a new visit
        self.band_at = 0.0
        self.previous = None                # the (motion, band) before this visit
        self.seen: dict[int, int] = {}      # motion -> instances
        self.visits: dict[tuple[int, int], int] = {}
        self.hits: dict[tuple[int, int], dict[str, int]] = {}  # (motion, band) -> {"near": n, "ranged": n}
        self.last_hp = None
        self.dodged_visit = None
        self.dodge_at = float("-inf")
        self.side = 1
        self.strafe_sign, self.strafe_at = 1, float("-inf")
        self.kiting = False
        self.thorn = None
        self.thorn_hp = None
        self.dealt: dict[int, int] = {}     # motion -> Thorn HP lost
        self.dealt_visit = 0                # Thorn HP lost in the current motion instance
        self.counters = {"dodges": 0, "kites": 0, "commits": 0, "reversals": 0, "hitsNear": 0, "hitsRanged": 0,
                         "reflectHolds": 0, "reflectMoves": 0}

    @staticmethod
    def flat(entity: dict) -> tuple[float, float]:
        return entity["position"]["x"], entity["position"]["z"]

    def band_of(self, thorn: dict) -> int | None:
        timing = thorn.get("motionTime")
        return None if timing is None else int(max(timing, 0) // self.band_frames)

    def observe(self, thorn: dict | None, own: dict, now: float) -> None:
        if thorn is not None:
            motion, band = thorn.get("motionId"), self.band_of(thorn)
            if motion != self.motion:
                self.seen[motion] = self.seen.get(motion, 0) + 1
                self.dealt_visit = 0
            if (motion, band) != (self.motion, self.band):
                self.previous = (self.motion, self.band)
                self.motion, self.band, self.band_at = motion, band, now
                self.visit += 1
                self.visits[(motion, band)] = self.visits.get((motion, band), 0) + 1
            self.thorn = {"hp": thorn.get("hp"), "motionId": motion, "motionTime": thorn.get("motionTime"),
                          "band": band, "position": list(self.flat(thorn))}
            hp = thorn.get("hp")
            if hp is not None and self.thorn_hp is not None and hp < self.thorn_hp:
                self.dealt[motion] = self.dealt.get(motion, 0) + self.thorn_hp - hp
                self.dealt_visit += self.thorn_hp - hp
            self.thorn_hp = hp
        hp = own.get("hp")
        if hp is not None and self.last_hp is not None and hp < self.last_hp and thorn is not None \
                and own.get("playerPos"):
            kind = "near" if self.distance(own, thorn) <= self.radius else "ranged"
            self.counters["hitsNear" if kind == "near" else "hitsRanged"] += 1
            keys = {(self.motion, self.band)}
            if now - self.band_at <= 0.5 and self.previous and self.previous[0] is not None:
                keys.add(self.previous)  # the hit can land just after Thorn's band changed
            for key in keys:
                if key[0] is not None and key[1] is not None:
                    counts = self.hits.setdefault(key, {"near": 0, "ranged": 0})
                    counts[kind] += 1
        self.last_hp = hp

    def distance(self, own: dict, thorn: dict) -> float:
        p, (tx, tz) = own["playerPos"], self.flat(thorn)
        return math.hypot(tx - p[0], tz - p[2])

    def facing(self, own: dict, thorn: dict) -> float | None:
        """r6c log only: cos between the camera's horizontal forward and this seat's direction to Thorn (bot01's
        chipping reflections mostly had it above 0.9, as lock-on keeps it)."""
        p, (tx, tz), forward = own["playerPos"], self.flat(thorn), own.get("forward")
        if not forward:
            return None
        vx, vz, (fx, fz) = tx - p[0], tz - p[2], forward
        n = math.hypot(vx, vz) * math.hypot(fx, fz)
        return round((vx * fx + vz * fz) / n, 2) if n else None

    def danger(self, motion, band) -> str | None:
        """"near" or "ranged" for a seeded or learned dangerous band, else None."""
        if motion is None or band is None:
            return None
        counts = self.hits.get((motion, band), {})
        hits = counts.get("near", 0) + counts.get("ranged", 0)
        learned = hits >= self.learn and hits / max(self.visits.get((motion, band), 0), 1) >= self.ratio
        if learned:
            return "ranged" if counts.get("ranged", 0) else "near"
        return self.seed.get((motion, band))

    def learned(self) -> list[str]:
        keys = set(self.hits) | set(self.seed)
        return sorted(f"{m}:{b}:{kind}" for m, b in keys if (kind := self.danger(m, b)))

    def reversal(self, own: dict, now: float, last: float) -> bool:
        """A Reversal on screen, and the last Reversal press (`last`, from the pad that owns the cadence) at
        least `reversalGapSec` ago."""
        return own.get("react") in self.reversals and now - last >= self.reversal_gap - 1e-6

    def took_reversal(self) -> None:
        self.counters["reversals"] += 1

    def opening(self, thorn: dict | None) -> bool:
        if thorn is None:
            return False
        motion = thorn.get("motionId")
        return motion in self.openings_seed or (motion == self.motion and self.dealt_visit >= self.opening_learn)

    def openings(self) -> list[int]:
        return sorted(self.openings_seed | {m for m, v in self.dealt.items() if v >= self.opening_learn})

    def threat(self, thorn: dict | None, own: dict, now: float) -> bool:
        """A near band just entered, within `radius`: one sidestep per visit, with a cooldown."""
        if not self.dodge or thorn is None or not own.get("playerPos"):
            return False
        return self.danger(thorn.get("motionId"), self.band_of(thorn)) == "near" \
            and self.dodged_visit != self.visit and now - self.dodge_at >= self.cooldown \
            and self.distance(own, thorn) <= self.radius

    def dodge_target(self, own: dict, thorn: dict) -> tuple[float, float]:
        """Across Thorn's line to this seat, alternating sides, and a little outward."""
        p, (tx, tz) = own["playerPos"], self.flat(thorn)
        vx, vz = p[0] - tx, p[2] - tz
        d = math.hypot(vx, vz) or 1.0
        vx, vz = vx / d, vz / d
        px, pz = -vz * self.side, vx * self.side
        return p[0] + px * self.step + vx * 150, p[2] + pz * self.step + vz * 150

    def took_dodge(self, now: float) -> None:
        self.dodged_visit, self.dodge_at = self.visit, now
        self.side = -self.side
        self.counters["dodges"] += 1

    def strafe_target(self, own: dict, thorn: dict, now: float, radius: float) -> tuple[float, float]:
        """A point `strafeRad` further around Thorn, at `radius`. The direction flips every `strafeFlipSec`."""
        if now - self.strafe_at >= self.strafe_flip:
            self.strafe_sign, self.strafe_at = -self.strafe_sign, now
        p, (tx, tz) = own["playerPos"], self.flat(thorn)
        vx, vz = p[0] - tx, p[2] - tz
        d = math.hypot(vx, vz)
        if d < 1:
            vx, vz, d = 1.0, 0.0, 1.0
        angle = math.atan2(vz, vx) + self.strafe_sign * self.strafe_step
        return tx + math.cos(angle) * radius, tz + math.sin(angle) * radius

    def note_reflect(self, own: dict, now: float) -> None:
        """This seat sees the reflect Reversal on screen: Thorn is shooting at it."""
        if own.get("react") == self.reflect_command:
            self.reflect_seen_at = now

    def reflect_band(self, thorn: dict | None) -> bool:
        return thorn is not None and thorn.get("motionId") == self.reflect_motion and self.band_of(thorn) in self.reflect_bands

    def reflect_stance(self, thorn: dict | None, now: float, last_press: float) -> bool:
        """r6: Thorn in its projectile stance and this seat targeted (it saw or pressed the reflect Reversal within
        `reflectTargetSec`). The other seat never takes the stance. r6c: with `reflectTargeted` false, this seat in the
        band, targeted or not (the host route; the friend route has the stance off)."""
        return self.reflect and self.reflect_band(thorn) and \
            (not self.reflect_targeted or now - max(self.reflect_seen_at, last_press) <= self.reflect_target)

    def reflect_goal(self, own: dict, thorn: dict):
        """None (hold still) inside `reflectRange` of Thorn's root; else a point straight out or in, to its middle."""
        p, (tx, tz) = own["playerPos"], self.flat(thorn)
        vx, vz = p[0] - tx, p[2] - tz
        d = math.hypot(vx, vz)
        if self.reflect_min <= d <= self.reflect_max:
            return None
        if d < 1:
            vx, vz, d = 1.0, 0.0, 1.0
        r = (self.reflect_min + self.reflect_max) / 2
        return tx + vx / d * r, tz + vz / d * r

    def kite(self, own: dict, thorn: dict | None, peer: dict | None, now: float):
        """Keep away while this seat is low and the partner is alive and healthier: a strafe target on the
        `kiteRadius` ring (or further out, if already there), or None to fight on."""
        hp, top = own.get("hp"), max(own.get("maxHp") or 1, 1)
        want = thorn is not None and own.get("playerPos") and hp is not None and hp > 0 \
            and hp <= self.kite_below * top and peer is not None and not peer.get("downed", True) \
            and (peer.get("hp") or 0) >= hp + self.kite_margin
        self.kiting = bool(want)
        if not want:
            return None
        self.counters["kites"] += 1
        return self.strafe_target(own, thorn, now, max(self.distance(own, thorn), self.kite_radius))

    def snapshot(self) -> dict:
        return {"thorn": self.thorn, "visit": self.visit, "learned": self.learned(), "openings": self.openings(),
                "hits": {f"{m}:{b}": v for (m, b), v in sorted(self.hits.items())},
                "visits": {f"{m}:{b}": v for (m, b), v in sorted(self.visits.items(), key=lambda kv: str(kv[0]))},
                "seen": dict(sorted(self.seen.items(), key=lambda kv: str(kv[0]))),
                "dealtByMotion": dict(sorted(self.dealt.items(), key=lambda kv: str(kv[0]))),
                "kiting": self.kiting, **self.counters}


def detour_goal(point, target, side: int, thorn=None, offset: float = 350.0) -> tuple[float, float]:
    """A waypoint beside the straight line from point to target. With Thorn's root known, the side away
    from it; else the given side."""
    dx, dz = target[0] - point[0], target[2] - point[2]
    d = math.hypot(dx, dz) or 1.0
    px, pz = -dz / d, dx / d
    if thorn is not None:
        mx, mz = (point[0] + target[0]) / 2, (point[2] + target[2]) / 2
        side = -1 if (thorn[0] - mx) * px + (thorn[1] - mz) * pz > 0 else 1
    return point[0] + dx * 0.5 + px * offset * side, point[2] + dz * 0.5 + pz * offset * side


class CooperativeFight(base.CooperativeFight):
    START_3D, KEEP_3D = 110.0, 140.0  # native range remains150; stay inside it
    # Revive-approach fallback (VUH-2617): no horizontal progress for STALL_SEC detours beside the line
    # (around Thorn when its root is known) with a moving jump, at most MAX_DETOURS per downed episode.
    STALL_SEC, STALL_GAIN, DETOUR_SEC, MAX_DETOURS = 2.0, 40.0, 1.0, 8

    def __init__(self, game, peer, hold_ms=None):
        super().__init__(game, peer, hold_ms)
        self.hold_started = None
        self.hold_frame = None
        self.hold_identity = None
        self.rc_next = self.rc_until = self.observed_at = 0.0
        self.arm_frame = None
        self.armed_identity = None
        self.waiting_release = False
        self.last_progress = 0
        self.last_hold_hp = None
        self.progress = None       # (time, horizontal) of the current approach window
        self.detour_until = 0.0
        self.detour_point = None
        self.detours = 0
        self.detour_side = 1

    def approach(self, pilot, own, point, target, horizontal, now, detour=True):
        """The keys toward the downed peer: straight, or a bounded detour when the approach stalls. With
        detour False (in revive range, where standing still fills the native bar, or during an RC) no
        detour input is sent and the stall window restarts."""
        if not detour:
            self.progress, self.detour_until, self.detour_point = None, 0.0, None
        if horizontal <= 80:
            self.progress = None
            return set()
        if not detour:
            return base.steer({**own, "pos": point}, (target[0], target[2]))
        if now < self.detour_until and self.detour_point is not None:
            return base.steer({**own, "pos": point}, self.detour_point)
        if self.progress is None:
            self.progress = (now, horizontal)
        elif now - self.progress[0] >= self.STALL_SEC:
            stalled = self.progress[1] - horizontal < self.STALL_GAIN
            self.progress = (now, horizontal)
            if stalled and self.detours < self.MAX_DETOURS:
                policy = getattr(pilot, "thorn_policy", None)
                thorn = tuple(policy.thorn["position"]) if policy is not None and policy.thorn else None
                self.detour_point = detour_goal(point, target, self.detour_side, thorn)
                self.detour_side = -self.detour_side
                self.detour_until = now + self.DETOUR_SEC
                self.detours += 1
                keys = base.steer({**own, "pos": point}, self.detour_point)
                pilot.kb.hold(keys | {"jump"})  # a moving jump over low geometry; tap() would drop the stick
                time.sleep(0.09)
                pilot.kb.hold(keys)
                pilot.write("coop_detour", attempt=self.detours, horizontal=round(horizontal, 1),
                            own=list(point), target=list(target), waypoint=list(self.detour_point),
                            aroundThorn=thorn is not None)
                return keys
        return base.steer({**own, "pos": point}, (target[0], target[2]))

    def end_hold(self, pilot, reason, own):
        if self.hold_started is not None:
            frame = own.get("reactObservation", {}).get("frame")
            pilot.write("coop_hold_end", reason=reason,
                        seconds=round(time.monotonic() - self.hold_started, 3),
                        nativeFrames=frame-self.hold_frame if frame is not None and self.hold_frame is not None else None,
                        observation=own.get("reactObservation"))
        self.hold_started = self.hold_frame = self.hold_identity = None
        self.last_progress = 0
        self.last_hold_hp = None

    def rearm(self, pilot, reason, own, identity):
        self.end_hold(pilot, reason, own)
        info = own.get("reactObservation", {})
        self.arm_frame = info.get("frame") if info.get("source") == "downed-prompt-v7" else None
        self.armed_identity = identity
        self.waiting_release = False
        pilot.kb.hold(set())
        pilot.write("coop_rearm", reason=reason, nativeFrame=self.arm_frame)
        return True

    def stop(self, pilot, reason, own):
        self.end_hold(pilot, reason, own)
        self.arm_frame = self.armed_identity = None
        self.waiting_release = False
        pilot.kb.hold(set())
        return True

    def step(self, pilot, own, event_reaction=False):
        if (own["world"], own["room"]) != (2, 34):
            return super().step(pilot, own, event_reaction)
        if not isinstance(pilot.kb, OriginalPad):
            raise base.Stuck("cooperative revive requires pad input")
        own_ok, peer_ok = self.own_log.update(), self.peer_log.update()
        if not own_ok or not peer_ok or self.own_log.full_party or self.peer_log.full_party or self.own_log.downed:
            self.stop(pilot, "logical-state", own)
            mode = "unknown-log" if not own_ok or not peer_ok else "full-party-down" \
                if self.own_log.full_party or self.peer_log.full_party else "own-logical-downed"
            return self.neutral(pilot, mode)  # original60s failure bound
        if not self.peer_log.downed:
            self.progress, self.detour_until, self.detour_point, self.detours = None, 0.0, None, 0
            self.end_hold(pilot, "peer-recovered", own)
            self.arm_frame = self.armed_identity = None
            self.waiting_release = False
            if self.mode:
                pilot.kb.hold(set())
                pilot.write("coop_revive", state="combat", peerPid=self.peer.pid,
                            ownRevives=self.own_log.revives, peerRevives=self.peer_log.revives)
            self.since, self.mode = None, ""
            return False
        if self.since is None:
            self.since = time.monotonic()
        if time.monotonic() - self.since > self.TIMEOUT:
            self.stop(pilot, "timeout", own)
            raise base.Stuck(f"cooperative revive peer-logical-downed exceeded {self.TIMEOUT:.0f}s")
        if self.mode != "peer-logical-downed":
            pilot.write("coop_revive", state="peer-logical-downed", peerPid=self.peer.pid)
            self.mode = "peer-logical-downed"
        if event_reaction or not own["free"]:
            self.stop(pilot, "event-or-menu", own)
            return False  # original bounded named camera RC/tutorial handling
        try:
            peer = self.peer.state()
        except (OSError, ValueError):
            return self.stop(pilot, "peer-unavailable", own)
        if own["hp"] is None or own["hp"] <= 0 or not peer["free"] \
                or own["roomScope"] != peer["roomScope"] or not own["playerPos"] or not peer["playerPos"] \
                or self.peer_log.actor != peer["player"]:
            return self.stop(pilot, "peer-identity-or-scope", own)
        point, target = own["playerPos"], peer["playerPos"]
        distance = math.dist(point, target)
        horizontal = math.hypot(target[0]-point[0], target[2]-point[2])
        identity = (own["player"], peer["player"], tuple(own["roomScope"]))
        if self.hold_identity is not None and self.hold_identity != identity:
            return self.stop(pilot, "actor-replaced", own)
        now = time.monotonic()
        limit = self.KEEP_3D if self.hold_started is not None else self.START_3D
        direction = self.approach(pilot, own, point, target, horizontal, now,
                                  detour=distance > limit and not own["react"])
        if now - self.observed_at >= 0.5:
            pilot.write("coop_approach", peerPid=self.peer.pid, own=list(point), target=list(target),
                        distance3D=round(distance, 2), horizontal=round(horizontal, 2), detours=self.detours,
                        react=own["react"], forward=own["forward"], direction=sorted(direction),
                        held=sorted(pilot.kb.held), observation=own.get("reactObservation"))
            self.observed_at = now
        if own["react"]:
            self.end_hold(pilot, "native-reaction", own)
            self.arm_frame = self.armed_identity = None
            self.waiting_release = False
            # RCs still win Triangle. Stay on the rescue route during the RC
            # cooldown instead of returning to enemy chasing and attack bursts.
            if now >= self.rc_next:
                self.rc_until, self.rc_next = now + 0.18, now + 0.35
                pilot.write("coop_reaction", command=own["react"], peerPid=self.peer.pid)
            pilot.kb.hold(direction | {"react"} if now < self.rc_until else direction)
            return True
        if distance <= limit:
            info = own.get("reactObservation", {})
            qualified = info.get("source") == "downed-prompt-v7"
            frame = info.get("frame") if qualified else None
            progress = info.get("reviveProgress") if qualified else None
            visible = qualified and progress is not None and info.get("promptKind") == 1 \
                and info.get("promptHide") == 0
            if self.waiting_release:
                pilot.kb.hold(set())
                # A fire shows Reviving/Fired until the peer's native recovery.
                # A later fresh prompt can be a reminted episode; release/rearm
                # normally, never send a request or clear product state here.
                if frame is not None and self.arm_frame is not None and frame-self.arm_frame >= 2 \
                        and visible:
                    return self.rearm(pilot, "fresh-prompt-after-release", own, identity)
                return True
            if not visible:
                # NoTarget/stale/missing publication cannot establish a fresh
                # native press. Release now, then rearm from a visible prompt.
                return self.stop(pilot, "prompt-not-visible", own)
            if self.hold_started is not None:
                hit = self.last_hold_hp is not None and own["hp"] < self.last_hold_hp
                reset = progress == 0 and self.last_progress > 0
                stalled = progress == 0 and self.last_progress == 0 and frame is not None \
                    and self.hold_frame is not None and frame-self.hold_frame >= 6
                if hit or reset or stalled:
                    reason = "hit" if hit else "native-progress-reset" if reset else "native-progress-stalled"
                    return self.rearm(pilot, reason, own, identity)
                if progress is not None and progress >= 1000:
                    self.end_hold(pilot, "native-bar-full-release", own)
                    self.arm_frame, self.armed_identity = frame, identity
                    self.waiting_release = True
                    pilot.kb.hold(set())  # native Step fires on release, not full
                    return True
                self.last_hold_hp = own["hp"]
                if progress is not None:
                    self.last_progress = progress
            elif self.armed_identity != identity:
                return self.rearm(pilot, "initial-fresh-press", own, identity)
            elif self.arm_frame is None:
                # No new native read: wait for the same validated prompt reader.
                if frame is not None:
                    self.arm_frame = frame
                pilot.kb.hold(set())
                return True
            elif frame is None or frame-self.arm_frame < 2:
                pilot.kb.hold(set())
                return True
            if self.hold_started is None:
                self.hold_started = now
                self.hold_frame = frame
                self.hold_identity = identity
                self.last_hold_hp = own["hp"]
                self.last_progress = progress or 0
                pilot.write("coop_hold_begin", peerPid=self.peer.pid, distance3D=round(distance, 2),
                            completion="native progress1000, then release and await recovery")
            # Native60-frame bar owns completion. Hits/progress resets require a
            # fresh press; reaching full requires release to fire the request.
            pilot.kb.hold({"react"})
            return True
        self.end_hold(pilot, "range", own)
        self.arm_frame = self.armed_identity = None
        self.waiting_release = False
        pilot.kb.hold(direction)  # wait for native Y to settle if already near inXZ
        return True


class Pilot(OriginalPilot):
    observation_at = 0.0
    thorn_policy = None
    kite_logged_at = 0.0
    reflect_logged_at = 0.0

    def state(self):
        state = super().state()
        self.kb.react_command = state["react"]  # names the command for the pad's Reversal cadence
        now = time.monotonic()
        if (state["world"], state["room"]) == (2, 34) and now-self.observation_at >= 0.5:
            policy = self.thorn_policy
            self.write("thorn_observe", state=state, held=sorted(self.kb.held),
                       **({"thornCombat": policy.snapshot()} if policy is not None else {}))
            self.observation_at = now
        room = getattr(self, "fighting_room", None)
        if room is not None and state["world"] != 255 and (state["world"], state["room"]) != room:
            # Existing fight completion on a native room exit must precede the
            # HP-unavailable wait: post-boss events may free the player/clone.
            # Scenario victory/reward assertions still judge that exit.
            raise FightRoomExit(state)
        return state

    def peer_vitals(self):
        """The partner's HP and logical-downed state for the keep-away rule; None if unknown."""
        cooperative = self.cooperative
        if cooperative is None:
            return None
        try:
            peer = cooperative.peer.state()
        except (OSError, ValueError):
            return None
        return {"hp": peer.get("hp"), "maxHp": peer.get("maxHp"), "downed": bool(cooperative.peer_log.downed)}

    def combat_turn(self, s, foes, target, gap, reach, stats):
        policy = self.thorn_policy
        if policy is None or (s["world"], s["room"]) != (2, 34) or not s["free"] \
                or s["hp"] is None or s["hp"] <= 0 or not s["playerPos"]:
            return False
        now = time.monotonic()
        # The pad owns the Reversal cadence (fight() and state() configure it; this keeps it configured on every
        # turn too, so the base branch's next press is gated by the same record).
        self.kb.reversals, self.kb.reversal_gap = policy.reversals, policy.reversal_gap
        self.kb.react_command = s["react"]
        if policy.reflect:
            self.kb.reflect_command = policy.reflect_command
            policy.note_reflect(s, now)
        thorn = next((f for f in foes if f.get("objectId") == 307 and f.get("objectType") == 12), None)
        stance = policy.reflect_stance(thorn, now, getattr(self.kb, "reflect_at", float("-inf")))
        if s["react"]:
            # The base already took this reaction if its gap allowed. A Reversal is pressed again at once:
            # missed ones were Thorn's projectile hits (attempt05). Other reactions stay the base's.
            if s["react"] not in policy.reversals:
                return False
            if stance:
                self.kb.hold(set())  # r6: no stick input while the Reversal is offered
            # Hold the turn for the rest of the cadence (at most reversalGapSec) rather than fall into an attack
            # burst, then press: the pad drops a press inside the gap, the base branch's included.
            last = getattr(self.kb, "reversal_at", float("-inf"))
            if not policy.reversal(s, now, last):
                time.sleep(min(max(policy.reversal_gap - (now - last), 0.0), policy.reversal_gap))
            if not self.kb.tap("react"):
                return False
            policy.took_reversal()
            stats["reversals"] = stats.get("reversals", 0) + 1
            self.write("thorn_reversal", command=s["react"], hp=s["hp"])
            return True
        policy.observe(thorn, s, now)
        if thorn is None:
            return False
        point = s["playerPos"]
        if stance:
            # r6 reflect stance: stand still in range (and within reflectStillSec of a press, wherever this seat
            # stands); otherwise move straight out or in to the range, never around. r6b (Quill r6 R1): while the
            # stance holds it comes before the learned opening and the near-band sidestep, so neither can move
            # the seat inside the no-stick window, nor pull it off its stand-off (reflected chip would itself
            # teach 202 as an opening). Revive, event and reaction handling all run earlier.
            still = now - getattr(self.kb, "reflect_at", float("-inf")) < policy.reflect_still
            goal = None if still else policy.reflect_goal(s, thorn)
            keys = set() if goal is None else base.steer({**s, "pos": point}, goal)
            self.kb.hold(keys)
            time.sleep(0.1)
            policy.counters["reflectHolds" if goal is None else "reflectMoves"] += 1
            stats["reflect"] = stats.get("reflect", 0) + 1
            if now - self.reflect_logged_at >= 2.0:
                self.reflect_logged_at = now
                self.write("thorn_reflect", hp=s["hp"], distance=round(policy.distance(s, thorn), 1),
                           band=policy.band_of(thorn), still=still, keys=sorted(keys), facing=policy.facing(s, thorn))
            return True
        if policy.opening(thorn):
            # An opening: approach and swing, with no dodge, keep-away or escape (the base's pinned escape
            # would run). Reactions still break the burst.
            policy.counters["commits"] += 1
            stats["commits"] = stats.get("commits", 0) + 1
            if target is not thorn or gap > reach:
                self.kb.hold(base.steer({**s, "pos": point}, policy.flat(thorn)))
                time.sleep(0.15)
                return True
            self.kb.hold(set())
            for _ in range(3):
                self.kb.tap("attack", 70)
                time.sleep(0.18)
                stats["swings"] = stats.get("swings", 0) + 1
                now_state = self.state()
                if now_state["hp"] is None or now_state["hp"] <= 0 or now_state["react"]:
                    break
            return True
        if policy.threat(thorn, s, now):
            goal = policy.dodge_target(s, thorn)
            keys = base.steer({**s, "pos": point}, goal)
            self.kb.hold(keys)
            time.sleep(0.12)
            self.kb.hold(keys | {"jump"})  # a moving jump; tap() would drop the stick
            time.sleep(0.09)
            self.kb.hold(keys)
            time.sleep(0.25)
            self.kb.hold(set())
            stats["dodges"] = stats.get("dodges", 0) + 1
            self.write("thorn_dodge", motion=thorn.get("motionId"), motionTime=thorn.get("motionTime"),
                       distance=round(policy.distance(s, thorn), 1), side=policy.side, keys=sorted(keys),
                       learned=policy.learned(), hp=s["hp"])
            policy.took_dodge(now)
            return True
        goal = policy.kite(s, thorn, self.peer_vitals(), now)
        if goal is None:
            return False  # the base fight engages, ranged bands included (r5)
        keys = base.steer({**s, "pos": point}, goal)
        self.kb.hold(keys)
        time.sleep(0.2)
        stats["kites"] = stats.get("kites", 0) + 1
        if now - self.kite_logged_at >= 2.0:
            self.kite_logged_at = now
            self.write("thorn_kite", hp=s["hp"], distance=round(policy.distance(s, thorn), 1),
                       motion=thorn.get("motionId"), band=policy.band_of(thorn), goal=[round(v, 1) for v in goal],
                       keys=sorted(keys))
        return True

    def fight(self, step, deadline):
        thorn = bool(step.get("eventReactions"))
        self.kb.thorn_combat = thorn
        self.fighting_room = (2, 34) if thorn else None
        self.thorn_policy = ThornPolicy(step["thornCombat"]) if thorn and step.get("thornCombat") else None
        if self.thorn_policy is not None:
            self.kb.reversals, self.kb.reversal_gap = self.thorn_policy.reversals, self.thorn_policy.reversal_gap
            self.kb.reflect_command = self.thorn_policy.reflect_command if self.thorn_policy.reflect else None
        try:
            try:
                return super().fight({**step, "reactGapSec": 0.35} if thorn else step, deadline)
            except FightRoomExit as exit:
                self.kb.hold(set())
                self.write("fight_room_exit", room=[exit.state["world"], exit.state["room"]],
                           hpAvailable=exit.state["hp"] is not None)
                return {"exitRoom": [exit.state["world"], exit.state["room"]],
                        "nativeRoomExit": True}
        finally:
            if self.thorn_policy is not None:
                self.write("thorn_combat_summary", **self.thorn_policy.snapshot())
            self.kb.thorn_combat = False
            self.kb.reversals, self.kb.reversal_gap = frozenset(), 0.0
            self.kb.reflect_command = None
            self.fighting_room = None
            self.thorn_policy = None


def main():
    base.PromptReactionReader = PromptReader
    base.Game, base.PadInput, base.Pilot = Game, Pad, Pilot
    base.DownedLog = DownedLog
    base.CooperativeFight = CooperativeFight
    return base.main()


if __name__ == "__main__":
    sys.exit(main())
