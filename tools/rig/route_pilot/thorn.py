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

    def tap(self, action, ms=90):
        # Recording can reduce native update rate. Give a real RC press time to
        # cross input frames; native admission still decides whether it is usable.
        if self.thorn_combat and action == "react":
            ms = max(ms, 180)
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


class CooperativeFight(base.CooperativeFight):
    START_3D, KEEP_3D = 110.0, 140.0  # native range remains150; stay inside it

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
        direction = base.steer({**own, "pos": point}, (target[0], target[2])) if horizontal > 80 else set()
        if now - self.observed_at >= 0.5:
            pilot.write("coop_approach", peerPid=self.peer.pid, own=list(point), target=list(target),
                        distance3D=round(distance, 2), horizontal=round(horizontal, 2),
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
        limit = self.KEEP_3D if self.hold_started is not None else self.START_3D
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

    def state(self):
        state = super().state()
        now = time.monotonic()
        if (state["world"], state["room"]) == (2, 34) and now-self.observation_at >= 0.5:
            self.write("thorn_observe", state=state, held=sorted(self.kb.held))
            self.observation_at = now
        room = getattr(self, "fighting_room", None)
        if room is not None and state["world"] != 255 and (state["world"], state["room"]) != room:
            # Existing fight completion on a native room exit must precede the
            # HP-unavailable wait: post-boss events may free the player/clone.
            # Scenario victory/reward assertions still judge that exit.
            raise FightRoomExit(state)
        return state

    def fight(self, step, deadline):
        thorn = bool(step.get("eventReactions"))
        self.kb.thorn_combat = thorn
        self.fighting_room = (2, 34) if thorn else None
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
            self.kb.thorn_combat = False
            self.fighting_room = None


def main():
    base.PromptReactionReader = PromptReader
    base.Game, base.PadInput, base.Pilot = Game, Pad, Pilot
    base.DownedLog = DownedLog
    base.CooperativeFight = CooperativeFight
    return base.main()


if __name__ == "__main__":
    sys.exit(main())
