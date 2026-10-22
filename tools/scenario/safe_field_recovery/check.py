"""Bounded offline controls only; never calls native process APIs."""
import ast
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace

import run
import pause
import evidence

checks = []
def check(name, value):
    checks.append({"name": name, "pass": bool(value)})
    if not value:
        raise AssertionError(name)


class Proc(subprocess.Popen):
    def __init__(self):
        self.pid = 111
        self._handle = object()
        self.returncode = None
        self.args = [sys.executable, "--network", "--pid", "222", "--role", "friend1"]
        self.killed = False
    def __del__(self):
        pass
    def poll(self):
        return self.returncode
    def terminate(self):
        self.killed = True
        self.returncode = 1
    def wait(self, timeout=None):
        return self.returncode


class Handle:
    def __int__(self):
        return 9


class Timer:
    latest = None
    def __init__(self, delay, callback):
        self.delay, self.callback, self.daemon = delay, callback, False
        self.live = False
        Timer.latest = self
    def start(self):
        self.live = True
    def cancel(self):
        self.live = False
    def join(self, timeout):
        pass
    def is_alive(self):
        return self.live


class API:
    def __init__(self, proc):
        self.proc = proc
        self.calls = []
        self.birth, self.pid, self.image = 12345, proc.pid, sys.executable
        self.suspend_result, self.resume_result = 0, 0
        self.tick_error_after_resume = False
        self.now = 0.0
    def process_id(self, handle):
        check("metadata uses same retained handle", handle is self.proc._handle)
        return self.pid
    def executable(self, handle):
        return self.image
    def creation(self, handle):
        return self.birth
    def tick(self):
        if self.tick_error_after_resume and "resume" in self.calls:
            raise OSError("metadata unavailable")
        return 10000 + int(self.now * 1000)
    def suspend(self, handle):
        check("suspend retained handle", handle is self.proc._handle)
        self.calls.append("suspend")
        return self.suspend_result
    def resume(self, handle):
        check("resume retained handle", handle is self.proc._handle)
        self.calls.append("resume")
        return self.resume_result


def setup():
    proc = Proc()
    proc._handle = Handle()
    api = API(proc)
    owned = [("runtime_1", proc, None)]
    games = [SimpleNamespace(index=0, pid=221), SimpleNamespace(index=1, pid=222)]
    return proc, api, owned, games


def execute(edit=None, failure=None, overrun=False, late=None):
    proc, api, owned, games = setup()
    if edit:
        edit(proc, api, owned, games)
    count = 0
    def alive():
        nonlocal count
        count += 1
        if failure and count == failure:
            raise KeyboardInterrupt("cancel")
    def sleep(seconds):
        api.now += seconds
        if overrun and api.now > 0.1:
            api.now = overrun if overrun is not True else pause.WATCHDOG_MS / 1000
            Timer.latest.callback()
        if late and api.now >= pause.PAUSE_MS / 1000:
            api.now = late  # the scheduler resumed this loop late; the watchdog did not fire
    try:
        result = pause.pause(run.shared._runtime_lifecycle, owned, games, proc, sys.executable, alive,
                             api=api, clock=lambda: api.now, sleep=sleep, timer_factory=Timer)
        return result, None, proc, api
    except BaseException as error:
        return getattr(error, "receipt", {}), error, proc, api


result, error, proc, api = execute()
check("fixed4200 successful", error is None and result["completed"] and result["durationMs"] == pause.PAUSE_MS == 4200)
check("watchdog armed at the relay budget", Timer.latest.delay == pause.WATCHDOG_MS / 1000 == 4.5 and result["watchdogMs"] == 4500)
b = result["budget"]
check("stated budget: past the lease, under the relay with pre-age, overrun and delivery",
      b["pauseMs"] >= b["leaseMs"] + b["expiryMarginMs"] and not b["preAgeMeasured"] and
      b["preAgeBudgetMs"] + b["pauseMs"] + b["overrunBudgetMs"] + b["deliveryBudgetMs"] < b["relayTimeoutMs"] and
      b["pauseMs"] + b["overrunBudgetMs"] <= b["watchdogMs"] == b["relayTimeoutMs"] - b["preAgeBudgetMs"] - b["deliveryBudgetMs"])
import re as _re
lease_src = (run.ROOT/"common/include/kh2coop/EventHoldControl.hpp").read_text(encoding="utf-8")
relay_main = (run.ROOT/"server/src/ServerMain.cpp").read_text(encoding="utf-8")
relay_cfg = (run.ROOT/"server/include/kh2coop/SessionHost.hpp").read_text(encoding="utf-8")
check("budget constants match the sources (lease, relay default and relay main)",
      _re.search(r"HeartbeatTimeoutMs = (\d+);", lease_src).group(1) == str(pause.LEASE_MS) and
      _re.search(r"config\.heartbeatTimeoutMs = (\d+);", relay_main).group(1) == str(pause.RELAY_TIMEOUT_MS) and
      _re.search(r"heartbeatTimeoutMs\{(\d+)\}", relay_cfg).group(1) == str(pause.RELAY_TIMEOUT_MS))
check("one matching resume", api.calls == ["suspend", "resume"] and not proc.killed)
check("creation/game identity retained", result["identity"]["creationFiletime"] == 12345 and result["identity"]["gamePid"] == 222)
for name, edit in [
    ("wrong registry", lambda p,a,o,g: o.append(("runtime_1",p,None))),
    ("wrong argv game", lambda p,a,o,g: p.args.__setitem__(3,"999")),
    ("wrong role", lambda p,a,o,g: p.args.__setitem__(5,"player")),
    ("wrong handle PID", lambda p,a,o,g: setattr(a,"pid",999)),
    ("missing creation", lambda p,a,o,g: setattr(a,"birth",0)),
    ("exited runtime", lambda p,a,o,g: setattr(p,"returncode",1)),
    ("missing handle", lambda p,a,o,g: setattr(p,"_handle",None)),
]:
    result, error, proc, api = execute(edit)
    check(name + " refuses before suspend", error is not None and not api.calls)
result, error, proc, api = execute(failure=2)
check("cancel finally resumes", isinstance(error,KeyboardInterrupt) and api.calls == ["suspend","resume"] and result["resumed"])
result, error, proc, api = execute(overrun=True)
check("watchdog resumes independently and refuses pass", error is not None and result["watchdogFired"] and result["resumed"])
result, error, proc, api = execute(overrun=4.8)
check("late watchdog (fired past its 4.5 s) still refuses pass", error is not None and result["watchdogFired"] and
      result["durationMs"] > pause.PAUSE_MS + pause.OVERRUN_BUDGET_MS)
result, error, proc, api = execute(late=4.45)
check("over-budget resume without the watchdog is refused (jitter past the overrun budget)",
      error is not None and "budget" in str(error) and not result["watchdogFired"] and result["resumed"] and
      result["durationMs"] == 4450)
result, error, proc, api = execute(late=4.4)
check("resume exactly at the overrun budget still qualifies", error is None and result["completed"] and result["durationMs"] == 4400)
result, error, proc, api = execute(lambda p,a,o,g: setattr(a,"suspend_result",-1))
check("uncertain failed suspend still resumes", error is not None and api.calls == ["suspend","resume"])
result, error, proc, api = execute(lambda p,a,o,g: setattr(a,"resume_result",-1))
check("resume failure terminal only owned cleanup", error is not None and result["terminal"] and proc.killed and api.calls.count("resume")==3)
result, error, proc, api = execute(lambda p,a,o,g: setattr(a,"tick_error_after_resume",True))
check("successful resume not repeated on metadata error", error is not None and result["resumed"] and api.calls.count("resume")==1)

session = "a" * 32
initial = f"[eventhold-control] armed generation=1 host=10 self=11 hostDelivery=20 targetDelivery=21\n[runtime-identity] peerHex=7065657231 identityCurrent=1 pinPresent=1 stringsComplete=1 slot=1 selfConnection=11 hostConnection=10 pinSession={session} session={session} pinHostConnection=10 pinSlot=1"
old = evidence.baseline(initial)
friend = f"[eventhold-control] aborted reason=4 observationMs=14201\n[eventhold-recovery] expired connection=11 pinnedRejoin=1 cached baseline quarantined=1\n[runtime-identity] identityCurrent=1 pinPresent=1 stringsComplete=1 selfConnection=12 hostConnection=10 pinSession={session} session={session} pinHostConnection=10 pinSlot=1\n[eventhold-recovery] fresh baseline request=7 generation=2 self=12 epoch=3 resetAckRequired=1\n[eventhold-control] armed generation=2 host=10 self=12 hostDelivery=20 targetDelivery=22\n[eventhold-recovery] control restored generation=2 self=12 nativeResetAck=1"
host = "[returning-seat-resync] action=submitted reason=host-generated-request slot=1 connection=12 epoch=3 request=7"
native = f"[resync] ack session={session} host=10 request=7 targetSlot=1 connection=12 delivery=22 phase=0 cut=8 fingerprint=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa status=2 epoch=3 world=2 room=32 door=0 map=0 battle=0 event=0 loadBefore=1 loadAfter=2 frame1=101 frame2=102 serialized=1\n[event-control] recovered lifetime generation=2 self=12 delivery=22 epoch=3 resetAckRequired=1\n[event-control] resync result request=7 reason=0 recoveredLifetime=1\n[event-control] owner ack ordinal=1 source=0 epoch=3 flags=3 generation=2 delivery=22"
relay = f"[resync-result] component=relay session={session} host=10 request=7 reason=0 targetCount=1 target0=1,12,22,2,8,aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa,-"
pause_receipt = {"completed":True,"resumed":True,"terminal":False,"suspendTickMs":10000,"resumeTickMs":14200}
def correlation(f=friend,h=host,n=native,r=relay,p=pause_receipt):
    return evidence.correlate(old,p,f,h,n,r)
check("exact log chain correlates", correlation()["ready"])
check("the friend's own retirement and a pending handshake timeout do not void the proof",
      correlation(r=relay+"\n[SessionHost] Peer disconnected: peer1\n[SessionHost] Peer timed out: 127.0.0.1:61000")["ready"])
check("relay_check alone: clean tail ok, friend timeout refused, pending timeout and own disconnect ignored",
      evidence.relay_check(old, relay)["ok"] and not evidence.relay_check(old, relay+"\n[SessionHost] Peer timed out: peer1")["ok"] and
      evidence.relay_check(old, relay+"\n[SessionHost] Peer timed out: 127.0.0.1:61000\n[SessionHost] Peer disconnected: peer1")["ok"])
check("friend peer identity is required for the relay check",
      not evidence.correlate({**old, "identity": {k: v for k, v in old["identity"].items() if k != "peerHex"}},
                             pause_receipt, friend, host, native, relay)["ready"])
checkpoint = native.splitlines()[0].replace("phase=0", "phase=1").replace("cut=8", "cut=9").replace("frame1=101 frame2=102", "frame1=103 frame2=104").replace("loadBefore=1", "loadBefore=2")
check("actual terminal checkpoint retains Bootstrap proof", correlation(n=native.replace("\n[event-control] recovered", "\n"+checkpoint+"\n[event-control] recovered"),r=relay.replace(",2,8,",",2,9,"))["ready"])
for name, args in [
    ("wrong reason", {"f":friend.replace("reason=4","reason=8")}),
    ("late observed expiry", {"f":friend.replace("14201","17201")}),
    ("relay timed out the friend (pre-pause age or late watchdog)", {"r":relay+"\n[SessionHost] Peer timed out: peer1"}),
    ("missing pinned rejoin", {"f":friend.replace("pinnedRejoin=1","pinnedRejoin=0")}),
    ("same connection", {"f":friend.replace("self=12","self=11")}),
    ("changed pin", {"f":friend.replace("pinSession="+session,"pinSession="+"b"*32)}),
    ("wrong request", {"h":host.replace("request=7","request=9")}),
    ("same load", {"n":native.replace("loadAfter=2","loadAfter=1")}),
    ("one native frame", {"n":native.replace("frame2=102","frame2=101")}),
    ("unserialized", {"n":native.replace("serialized=1","serialized=0")}),
    ("wrong cut", {"r":relay.replace(",2,8,",",2,9,")}),
    ("wrong fingerprint", {"r":relay.replace(",aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa,",",bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb,")}),
    ("missing resetACK", {"n":native.replace("ordinal=1","ordinal=2")}),
    ("result not consumed", {"n":native.replace("recoveredLifetime=1","recoveredLifetime=0")}),
    ("unsafe ACK", {"n":native.replace("flags=3","flags=1")}),
    ("late new abort", {"f":friend+"\n[eventhold-control] aborted reason=4 observationMs=15000"}),
]:
    check(name+" refuses", not correlation(**args)["ready"])

scenario = json.loads((run.ROOT/"tools/scenario/scenarios/net_safe_field_recovery.json").read_text(encoding="utf-8"))
run.shared.validate_scenario(scenario)
names = set(run.shared.Context(run.HERE).namespace())
for number, step in enumerate(scenario["steps"]):
    for key in ("expr","argsExpr","until"):
        if key not in step:
            continue
        tree=ast.parse(step[key],mode="eval")
        local={node.id for node in ast.walk(tree) if isinstance(node,ast.Name) and isinstance(node.ctx,ast.Store)}
        used={node.id for node in ast.walk(tree) if isinstance(node,ast.Name) and isinstance(node.ctx,ast.Load)}
        check(f"expression namespace step{number}", used <= names | local)
check("no live drivers/warps/control writes", not any(s["do"] in {"pilot","warp","input","press","kh2ctl","protect","freeze","avatar_stall","runtime_pause"} for s in scenario["steps"]))
sources = {
    "runtime": (run.ROOT/"runtime/src/RuntimeMain.cpp").read_text(encoding="utf-8"),
    "native": (run.ROOT/"inject/src/EventHoldNative.cpp").read_text(encoding="utf-8"),
    "enemy": (run.ROOT/"inject/src/EnemySync.cpp").read_text(encoding="utf-8"),
    "result": (run.ROOT/"common/include/kh2coop/ResyncEvidence.hpp").read_text(encoding="utf-8"),
}
for kind, snippets in {
    "runtime": ["[eventhold-control] armed generation=", "[eventhold-control] aborted reason=", " observationMs=", "[eventhold-recovery] expired connection=", " pinnedRejoin=", " cached baseline quarantined=1", "[eventhold-recovery] fresh baseline request=", " resetAckRequired=1", "[eventhold-recovery] control restored generation=", " nativeResetAck=1", "[returning-seat-resync] action=", "[runtime-identity] schema=1", " pinPresent=", " pinSession=", "std::unitbuf"],
    "native": ["[event-control] recovered lifetime generation=%u self=%llu delivery=%llu epoch=%u resetAckRequired=1", "[event-control] owner ack ordinal=%llu source=%llu epoch=%u flags=%u tid=%lu generation=%u delivery=%llu"],
    "enemy": ["[resync] ack session=", "loadBefore=%u loadAfter=%u frame1=%llu frame2=%llu", "[event-control] resync result request=%llu reason=%u recoveredLifetime=%u"],
    "result": ["[resync-result] component=", " targetCount=", " target", "fingerprint"],
}.items():
    check("current source marker contract "+kind, all(s in sources[kind] for s in snippets))
for file in ("pause.py", "run.py", "evidence.py"):
    tree = ast.parse((run.HERE/file).read_text(encoding="utf-8"))
    check("no process opener "+file, not any(isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute) and n.func.attr in {"OpenProcess", "DuplicateHandle", "NtSuspendProcess", "NtResumeProcess"} for n in ast.walk(tree)))
check("exact scenario name", scenario["name"] == "vuh2628_safe_field_recovery04")
# VUH-2640 Leg A live check: the same thin entry; the pause's relay refusal is wired after the expiry.
import make_thorn_legA
legA_built = make_thorn_legA.build(json.loads(make_thorn_legA.SOURCE.read_text(encoding="utf-8")))
legA = json.loads(make_thorn_legA.TARGET.read_text(encoding="utf-8"))
check("Leg A scenario is current with its generator", legA == legA_built)
run.validate(legA)
for number, step in enumerate(legA["steps"]):
    for key in ("expr","argsExpr","until"):
        if key in step:
            tree=ast.parse(step[key],mode="eval")
            local={node.id for node in ast.walk(tree) if isinstance(node,ast.Name) and isinstance(node.ctx,ast.Store)}
            used={node.id for node in ast.walk(tree) if isinstance(node,ast.Name) and isinstance(node.ctx,ast.Load)}
            check(f"Leg A expression namespace step{number}", used <= names | local)
pause_at = next(i for i, s in enumerate(legA["steps"]) if s["do"] == "friend_heartbeat_pause")
relay_at = [i for i, s in enumerate(legA["steps"]) if s.get("expr") == "pause_relay()['ok']" and s["do"] == "assert"]
check("Leg A asserts the pause's relay-timeout refusal after the rejoin and as its final step",
      len(relay_at) == 2 and relay_at[0] > pause_at + 1 and relay_at[1] == len(legA["steps"]) - 1)
# R2: the actual pilot argv this entry builds: the reviewed Thorn pilot for the Calling, the awakening and the
# host fight; bots05's Calling keeps revivePeer, the host fight has none (the friend's outage is no revive).
launched = []
class _Proc:
    def poll(self): return 0
def _start(ctx, name, cmd):
    launched.append((name, cmd)); return _Proc()
saved_start, saved_finish = run.shared.start_process, run.shared.finish_pilot
run.shared.start_process = _start
run.shared.finish_pilot = lambda *a, **k: {"exit": 0}
try:
    games = [SimpleNamespace(index=0, pid=4100), SimpleNamespace(index=1, pid=4101)]
    pilot_ctx = SimpleNamespace(inst=lambda i: games[i], protect=set(), piloted=set(), run_dir=Path("."), saved={})
    for step in legA["steps"]:
        if step["do"] == "pilot":
            run.shared.STEPS["pilot"](pilot_ctx, step)
finally:
    run.shared.start_process, run.shared.finish_pilot = saved_start, saved_finish
thorn_pilot = str(run.ROOT / "tools/rig/route_pilot/thorn.py")
by_name = {name: cmd for name, cmd in launched}
check("R2: every Leg A pilot runs the reviewed Thorn pilot (route_pilot/thorn.py, as run_thorn.py selects)",
      launched and all(cmd[2] == thorn_pilot for _, cmd in launched) and Path(thorn_pilot).exists())
check("R2: the Calling pilots keep bots05's revivePeer; the host fight has none; no friend pilot after the pause",
      "--revive-peer-pid" in by_name["host_calling_path"] and "--revive-peer-pid" in by_name["friend_calling_path"] and
      "--revive-peer-pid" not in by_name["host_thorn"] and by_name["host_thorn"][4] == "4100" and
      [name for name, _ in launched] == ["host_to_calling", "host_calling_path", "friend_calling_path",
                                          "host_menu_tutorial", "friend_menu_tutorial", "host_to_awakening", "host_thorn"])
# R1: the scenario's actual predicate fails on a matching verified-peer timeout.
relay_expr = legA["steps"][relay_at[0]]["expr"]
check("Leg A has no friend pilot after the pause",
      not any(s["do"] == "pilot" and s.get("instance") == 1 for s in legA["steps"][pause_at:]))
# pause_relay() through the thin entry: only the relay tail after the recorded boundary counts.
import tempfile
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    relay_log = tmp / "relay.log"
    before = "[SessionHost] Peer timed out: peer1\n"            # before the pause: not this pause's
    relay_log.write_text(before + relay + "\n", encoding="utf-8")
    fake = SimpleNamespace(run_dir=tmp, artifacts=[], saved={"heartbeat_pause": {
        "completed": True, "baseline": old,
        "boundaries": {"relay": {"path": str(relay_log), "bytes": len(before.encode("utf-8"))}}}})
    clean = run.pause_relay(fake)
    with relay_log.open("a", encoding="utf-8") as tail:
        tail.write("[SessionHost] Peer timed out: peer1\n")
    timed_out = run.pause_relay(fake)
    incomplete = run.pause_relay(SimpleNamespace(run_dir=tmp, artifacts=[], saved={}))
    check("pause_relay: a timeout before the boundary is ignored, one after it refuses, an incomplete pause refuses",
          clean["ok"] and not timed_out["ok"] and timed_out["timeouts"] and not incomplete["ok"] and
          (tmp / "heartbeat_relay.json").exists())
    relay_log.write_text(before + relay + "\n[SessionHost] Peer disconnected: peer1\n[SessionHost] Peer timed out: peer2\n"
                         "[SessionHost] Peer timed out: 127.0.0.1:61000\n", encoding="utf-8")
    other = eval(relay_expr, {"pause_relay": lambda: run.pause_relay(fake)})
    with relay_log.open("a", encoding="utf-8") as tail:
        tail.write("[SessionHost] Peer timed out: peer1\n")
    matching = eval(relay_expr, {"pause_relay": lambda: run.pause_relay(fake)})
    check("R1: the scenario's own predicate passes for the friend's disconnect and other peers' timeouts, fails for its own",
          other is True and matching is False)
# R3: the post-victory field catalog: stale Calling/Thorn rows and other loads never match; the current one does.
def _load(serial, room, door, prog):
    return f"[warp] load complete serial={serial} transition={serial-1} room=02/{room:02X} door={door} map={prog} btl={prog} evt={prog}"
def _block(frame, load, world, room, door, prog):
    return [f"[resync-catalog] schema=1 kind=summary frame={frame} phase=field fresh=0 status=1 issues=32",
            f"[resync-catalog] schema=1 kind=scope frame={frame} phase=field layout=aa beforeNOW=00 beforeLoad={load} beforeTransition=1 afterNOW=00 afterLoad={load} afterTransition=1 censusState=1 censusLoad={load} censusTransition=1 world={world} room={room} door={door} map={prog} battle={prog} event={prog}",
            f"[resync-catalog] schema=1 kind=entry frame={frame} phase=field index=0 issues=32"]
calling = [_load(3, 33, 0, 1)] + _block(100, 3, 2, 33, 0, 1)
thorn = [_load(4, 34, 0, 157)] + _block(200, 4, 2, 34, 0, 157)
field = [2, 1, 0, 54, 54, 54]
stale = "\n".join(calling + thorn + [_load(11, 1, 0, 54)])                         # the field's load, no block yet
current = stale + "\n" + "\n".join(_block(900, 11, 2, 1, 0, 54))
check("R3: only stale Calling/Thorn field rows: no match", evidence.field_catalog(stale, field, 4) is None)
matched = evidence.field_catalog(current, field, 4)
check("R3: the current post-victory load's own block matches, with its rows only",
      matched and matched["load"] == 11 and matched["location"] == field and matched["frame"] == 900 and len(matched["rows"]) == 3)
check("R3: an older load, another tuple, or no newer load than the victory: no match",
      evidence.field_catalog(current, field, 11) is None and evidence.field_catalog(current, [2, 1, 0, 54, 54, 55], 4) is None and
      evidence.field_catalog("\n".join(calling + thorn), [2, 34, 0, 157, 157, 157], 4) is None)
check("R3: a block of the current load but another tuple, or an older load's block, does not count",
      evidence.field_catalog(stale + "\n" + "\n".join(_block(901, 11, 2, 2, 0, 54)), field, 4) is None and
      evidence.field_catalog(stale + "\n" + "\n".join(_block(902, 10, 2, 1, 0, 54)), field, 4) is None)
check("last_load: the last completed native load", evidence.last_load(current) == 11 and evidence.last_load("") == 0)
# Quill r2 R2: anchored on the arena load before combat and the host's actual victory row, not a late baseline.
victory = "[mission-mirror] host end trap=mission_dead_boss file=TT34_MS304.bar"
chain = [_load(n, 0x10 + n, 0, 50 + n) for n in range(5, 11)]
fast = "\n".join(calling + thorn + [victory] + chain + [_load(11, 1, 0, 54)] + _block(900, 11, 2, 1, 0, 54))
marker = "[mission-mirror] host end trap=mission_dead_boss"
check("R2: a fast kill and chain (field reached before the late save) still match on the arena anchor",
      evidence.field_catalog(fast, field, 4, marker) and evidence.field_catalog(fast, field, 4, marker)["load"] == 11)
check("R2: the old late baseline (the field's own load) would have rejected it; the arena anchor does not",
      evidence.field_catalog(fast, field, evidence.last_load(fast), marker) is None)
pre_victory = "\n".join(calling + thorn + [_load(11, 1, 0, 54)] + _block(900, 11, 2, 1, 0, 54) + [victory])
check("R2: a field load completed before the victory row, or no victory row at all, never matches",
      evidence.field_catalog(pre_victory, field, 4, marker) is None and
      evidence.field_catalog("\n".join(calling + thorn + chain + [_load(11, 1, 0, 54)] + _block(900, 11, 2, 1, 0, 54)), field, 4, marker) is None)
# Route (b), lead #32: no kill and no post-Thorn field in Leg A (the field_catalog helper above stays checked for
# the two-seat Leg B run). The host must still be fighting while the friend lands and sits out.
check("Route (b): no post-Thorn field wait and no kill assert in Leg A",
      not any(s["do"] == "wait_until" and "field_catalog(" in s["expr"] for s in legA["steps"]) and
      not any(s["do"] == "assert" and "host_end']==1" in s["expr"] for s in legA["steps"]))
host_fight = next(s for s in legA["steps"] if s.get("name") == "host_thorn")
check("Route (b): the host fight pilot may end in a full party down (allowStuck), without revivePeer",
      host_fight.get("allowStuck") is True and "revivePeer" not in host_fight)
alive = [i for i, s in enumerate(legA["steps"]) if s["do"] == "assert" and s["expr"] == make_thorn_legA.HOST_ALIVE]
landed = next(i for i, s in enumerate(legA["steps"]) if s["do"] == "wait_until" and "[resync-sit-out] landed" in s["expr"])
sat_out = next(i for i, s in enumerate(legA["steps"]) if s["do"] == "wait_until" and "outage sit-out" in s["expr"])
check("Route (b): bounded host survival is asserted before the landing waits and after the arrival asserts",
      len(alive) == 2 and alive[0] < landed < sat_out < alive[1] and
      legA["steps"][alive[1] - 1] == {"do": "capture", "instance": 1, "name": "legA_arrival_friend"})
class _P:
    def __init__(self, code): self.code = code
    def poll(self): return self.code
def _alive(pilots):
    return eval(make_thorn_legA.HOST_ALIVE, {"any": any, "saved": {"_background_pilots": pilots}})
check("Route (b): HOST_ALIVE is true only while the host_thorn process runs",
      _alive([(_P(None), 0, None, {}, "host_thorn", None)]) is True and
      _alive([(_P(1), 0, None, {}, "host_thorn", None)]) is False and
      _alive([(_P(None), 0, None, {}, "friend_thorn", None)]) is False and _alive([]) is False)
wait_at = next(i for i, s in enumerate(legA["steps"]) if s["do"] == "pilot_wait" and i > alive[1])
check("Route (b): after the host pilot ends, the friend's sit-out invariants are asserted, then the relay at closure",
      "never replays 4/16 or 315" in legA["steps"][wait_at + 2]["message"] and
      legA["steps"][wait_at + 1].get("as") == "legA_fight")
inventory = next(i for i, s in enumerate(legA["steps"]) if s["do"] == "progress_snapshot")
check("Lead #32 (d): one read-only SAVE snapshot of both seats in the Calling, before the host leaves for the arena",
      legA["steps"][inventory] == {"do": "progress_snapshot", "instances": [0, 1], "as": "legA_inventory"} and
      legA["steps"][inventory - 1].get("as") == "legA_calling" and legA["steps"][inventory + 1].get("name") == "host_to_awakening" and
      ("inventory", 0x3580, 0x140) in run.shared.PERSONAL_RANGES and ("characters", 0x24F0, 0xE04) in run.shared.PERSONAL_RANGES)
check("R1: the arena anchor is saved before the host's fight pilot starts",
      next(i for i, s in enumerate(legA["steps"]) if s.get("as") == "legA_arena_load") <
      next(i for i, s in enumerate(legA["steps"]) if s.get("name") == "host_thorn"))
check("Leg A boots both games with the catalog trace",
      all(legA["steps"][i]["env"].get("KH2COOP_RESYNC_CATALOG_TRACE") == "1" for i in (0, 1)))
pause_tree = ast.parse((run.HERE/"pause.py").read_text(encoding="utf-8"))
protected = [node for node in ast.walk(pause_tree) if isinstance(node,ast.With)
             and any(isinstance(item.context_expr,ast.Name) and item.context_expr.id=="lock" for item in node.items)
             and any(isinstance(n,ast.Call) and isinstance(n.func,ast.Attribute)
                     and isinstance(n.func.value,ast.Name) and n.func.value.id=="timer"
                     and n.func.attr=="start" for n in ast.walk(node))]
check("watchdog start and suspend attempt share guard", len(protected)==1 and
      any(isinstance(n,ast.Assign) and isinstance(n.targets[0],ast.Subscript)
          and isinstance(n.targets[0].slice,ast.Constant) and n.targets[0].slice.value=="suspendAttempted"
          for n in ast.walk(protected[0])) and
      any(isinstance(n,ast.Call) and isinstance(n.func,ast.Attribute) and n.func.attr=="suspend" for n in ast.walk(protected[0])))
output={"ok":all(r["pass"] for r in checks),"checks":len(checks),"scope":"offline fake retained-handle API and log/expression controls only; no live native calls", "results":checks}
receipt = run.ROOT/".local/thorn/safe-field-recovery-checks.json"
receipt.parent.mkdir(parents=True, exist_ok=True)
receipt.write_text(json.dumps(output,indent=2)+"\n",encoding="utf-8")
print(json.dumps({"ok":output["ok"],"checks":len(checks)}))
