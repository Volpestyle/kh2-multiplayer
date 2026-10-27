"""VUH-2869: prove that a forced downing came before a wave's first birth, on both seats.

Pure functions over the two seats' inject logs (no game, no files). The scenario runner exposes
them as `downed_wave_order(downed)` (tools/scenario/run.py), reading each instance's current log.

Ordering is by line position in each seat's own log, after `marker` (the room's control-release
line), which is the only order a seat's own log can establish:

- the downing is the scenario's own Kill: the one `[downed] kill actor=` line after the marker
  on the downed seat, and the first `[downed] gate intercept ... episode=E` after it names its
  fresh episode E;
- the downed seat must log that intercept, and the other seat must first name episode E, before
  each logs its first line about the wave's object (`objectId=309` / `objectId 309`: a host
  manifest or difficulty scale line, a client local spawn or binding line). Any such line first
  means the wave had already started on that seat, and the downing proves nothing about it;
- births lag the wave's native trigger: the Basement Hall spawns its two 309s at wave 1's THIRD
  host death, not its fourth (fixtures 194057/194738; VUH-2901's run 225539 logged death < Kill
  < first 309). So the host's own log (seat 0, where every `[enemysync] host death epoch` line
  is) must also log episode E before its `trigger_deaths`-th death after the marker. E exists
  only after the Kill, so this proves the Kill came before the trigger. When the friend is the
  downed seat the host learns E over the wire, a little after the Kill, so a trigger that
  raced the Kill fails closed;
- the episode's end is a later downed-seat line naming E, or a re-mint of E: its logical hold
  ending, a revive, or a return. While still downed the owner re-mints the episode after 30 s
  without an accepted request (docs/DOWNED_REVIVE.md, "Episodes"); the same downing then ends
  under the re-minted number (live runs 20261026-194057 and -194738). A later intercept is a
  new downing, so re-mints are collected only up to it.
"""
from __future__ import annotations

import re

KILL = re.compile(r"\[downed\] kill actor=")
INTERCEPT = re.compile(r"\[downed\] gate intercept mode=\d+ .*?episode=([0-9A-Fa-f]+)\b")
REMINT = re.compile(r"\[downed\] episode re-minted reason=\S+ episode=([0-9A-Fa-f]+)\b")
DEATH = re.compile(r"\[enemysync\] host death epoch")
END = r"\[downed\] (?:logical hold ended reason=\d+ episode={e}\b|revive episode={e}\b|return (?!owed ).*episode={e}\b|owed return kind=\d+ episode={e}\b)"


def _after(lines: list[str], marker: str) -> int | None:
    for i, line in enumerate(lines):
        if marker in line:
            return i
    return None


def _first(lines: list[str], start: int, pattern: re.Pattern | str) -> int | None:
    rx = re.compile(pattern) if isinstance(pattern, str) else pattern
    for i in range(start + 1, len(lines)):
        if rx.search(lines[i]):
            return i
    return None


def _episodes(lines: list[str], intercept: int, episode: str) -> list[str]:
    """The downing's episode and its re-mints, up to the next intercept (a new downing)."""
    chain = [episode]
    for i in range(intercept + 1, len(lines)):
        if INTERCEPT.search(lines[i]):
            break
        m = REMINT.search(lines[i])
        if m:
            chain.append(m.group(1).upper())
    return chain


def wave_order(texts: list[str], downed: int, marker: str, wave_object: int = 309,
               trigger_deaths: int = 3) -> dict:
    """texts[i] is seat i's log, seat 0 the host. Returns the evidence and ok/why; never raises on a
    bad trace. `triggeredFirst` is whether the host logged its `trigger_deaths`-th death after the
    marker before it named the episode (or at all, while no episode is known yet)."""
    out = {"ok": False, "why": "", "episode": None, "episodes": [], "downed": downed, "seats": [], "ended": False,
           "episodeSeen": [False] * len(texts), "waveStarted": [False] * len(texts), "triggeredFirst": False}
    if downed not in range(len(texts)) or len(texts) != 2:
        out["why"] = "two seats and a downed seat index are required"
        return out
    lines = [t.splitlines() for t in texts]
    starts = [_after(l, marker) for l in lines]
    wave = re.compile(rf"objectId[= ]{wave_object}\b")
    for seat, start in enumerate(starts):
        first_wave = None if start is None else _first(lines[seat], start, wave)
        out["seats"].append({"marker": start, "firstWave": first_wave})
        out["waveStarted"][seat] = first_wave is not None
    if any(s is None for s in starts):
        out["why"] = f"marker missing on seat {starts.index(None)}"
        return out
    deaths = [i for i in range(starts[0] + 1, len(lines[0])) if DEATH.search(lines[0][i])]
    trigger = deaths[trigger_deaths - 1] if len(deaths) >= trigger_deaths else None
    out["seats"][0]["trigger"] = trigger
    out["triggeredFirst"] = trigger is not None
    d = lines[downed]
    kills = [i for i in range(starts[downed] + 1, len(d)) if KILL.search(d[i])]
    if len(kills) != 1:
        out["why"] = f"want exactly one forced kill after the marker on seat {downed}, saw {len(kills)}"
        return out
    intercept = None
    for i in range(kills[0] + 1, len(d)):
        m = INTERCEPT.search(d[i])
        if m:
            intercept = (i, m.group(1).upper())
            break
    if intercept is None:
        out["why"] = f"no downed episode after the forced kill on seat {downed}"
        return out
    episode = intercept[1]
    out["episode"] = episode
    out["seats"][downed].update(kill=kills[0], episodeLine=intercept[0])
    other = 1 - downed
    seen = _first(lines[other], starts[other], rf"episode={episode}\b")
    out["seats"][other]["episodeLine"] = seen
    for seat in (downed, other):
        line = out["seats"][seat]["episodeLine"]
        first_wave = out["seats"][seat]["firstWave"]
        out["episodeSeen"][seat] = line is not None and (first_wave is None or line < first_wave)
    host_episode = out["seats"][0]["episodeLine"]
    out["triggeredFirst"] = trigger is not None and (host_episode is None or trigger < host_episode)
    chain = _episodes(d, intercept[0], episode)
    out["episodes"] = chain
    ended = _first(d, intercept[0], END.format(e="(?:" + "|".join(chain) + ")"))
    out["seats"][downed]["episodeEnd"] = ended
    out["ended"] = ended is not None
    for seat in (downed, other):
        s = out["seats"][seat]
        if s["episodeLine"] is None:
            out["why"] = f"seat {seat} never names episode {episode}"
            return out
        if s["firstWave"] is None:
            out["why"] = f"seat {seat} logged no object {wave_object} after the marker: the wave never started there"
            return out
        if s["firstWave"] < s["episodeLine"]:
            out["why"] = (f"seat {seat}'s wave started (line {s['firstWave']}) before it knew episode {episode} "
                          f"(line {s['episodeLine']})")
            return out
    if out["triggeredFirst"]:
        out["why"] = (f"the host's death {trigger_deaths} (line {trigger}), which triggers the wave, came before "
                      f"it knew episode {episode} (line {host_episode})")
        return out
    if out["seats"][downed]["firstWave"] < kills[0]:
        out["why"] = f"seat {downed}'s wave started before the forced kill"
        return out
    if not out["ended"]:
        out["why"] = f"episode {episode} (or a re-mint of it) has no hold end, revive or return on seat {downed}"
        return out
    out["ok"] = True
    out["why"] = "the downing preceded the wave on both seats and its episode ended"
    return out
