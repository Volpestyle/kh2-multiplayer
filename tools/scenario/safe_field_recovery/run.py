"""Thin test-only entry; delegates all ordinary runner/governor behavior."""
import importlib.util
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools" / "scenario"))
spec = importlib.util.spec_from_file_location("recovery29_shared_runner", ROOT / "tools/scenario/run.py")
shared = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = shared
spec.loader.exec_module(shared)
sys.path.insert(0, str(HERE))
import pause
import evidence

# Quill (Leg A live r1, R2): Thorn scenarios through this entry use the reviewed VUH-2617 pilot,
# selected exactly as tools/scenario/run_thorn.py does. The safe-field scenario has no pilot step.
shared.PILOT = ROOT / "tools/rig/route_pilot/thorn.py"

PAUSE_MS = pause.PAUSE_MS


def original_friend(ctx):
    candidates = [p for name, p, _ in ctx.processes if name == "runtime_1"]
    if len(candidates) != 1:
        raise shared.StepFailed("one original Friend1 runtime required")
    return candidates[0]


def paths(ctx):
    return {"friend": ctx.run_dir / "runtime_1.log", "host": ctx.run_dir / "runtime_0.log",
            "relay": ctx.run_dir / "relay.log",
            "native": ctx.inst(1).inject_log or shared.LOGS / f"kh2coop_inject_{ctx.inst(1).pid}.log"}


def step_pause(ctx, step):
    if step.get("instance") != 1 or step.get("pauseMs") != PAUSE_MS or "heartbeat_pause" in ctx.saved:
        raise shared.StepFailed(f"single fixed {PAUSE_MS}ms original Friend1 pause only")
    logs = paths(ctx)
    original = {key: path.read_bytes() for key, path in logs.items()}
    old = evidence.baseline(original["friend"].decode("utf-8", "replace"))
    receipt = {"baseline": old, "boundaries": {key: {"path": str(logs[key]), "bytes": len(value)}
                                                for key, value in original.items()}}
    try:
        result = pause.pause(shared._runtime_lifecycle, ctx.processes, ctx.instances, original_friend(ctx),
                             shared.RUNTIME, ctx.check_all)
        receipt.update(result)
    except BaseException as error:
        receipt.update(getattr(error, "receipt", {"error": str(error), "completed": False}))
        raise
    finally:
        ctx.saved["heartbeat_pause"] = receipt
        out = ctx.run_dir / "heartbeat_pause.json"
        out.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        ctx.artifacts.append(out.name)
    return receipt


def proof(ctx):
    receipt = ctx.saved.get("heartbeat_pause", {})
    if not receipt.get("completed"):
        return {"ready": False, "missing": "pause incomplete"}
    # Revalidate the original retained object/handle; never select by PID.
    _, identity = pause.validate(shared._runtime_lifecycle, ctx.processes, ctx.instances, original_friend(ctx),
                                 shared.RUNTIME, pause.api_with_metadata(shared._runtime_lifecycle))
    if identity != receipt["identity"]:
        raise shared.StepFailed("original runtime identity/pin holder changed")
    tails = {}
    for key, boundary in receipt["boundaries"].items():
        value = Path(boundary["path"]).read_bytes()
        if len(value) < boundary["bytes"]:
            raise shared.StepFailed("owned evidence log truncated")
        tails[key] = value[boundary["bytes"]:].decode("utf-8", "replace")
    result = evidence.correlate(receipt["baseline"], receipt, **tails)
    out = ctx.run_dir / "heartbeat_recovery.json"
    out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    if out.name not in ctx.artifacts:
        ctx.artifacts.append(out.name)
    return result


def pause_relay(ctx):
    """The qualified pause's relay-timeout refusal on its own, for scenarios that do not
    run the full safe-field recovery proof (the Thorn Leg A check): the relay log after
    the pause's recorded boundary must not show the relay timing out the friend."""
    receipt = ctx.saved.get("heartbeat_pause", {})
    if not receipt.get("completed"):
        return {"ok": False, "missing": "pause incomplete"}
    boundary = receipt["boundaries"]["relay"]
    value = Path(boundary["path"]).read_bytes()
    if len(value) < boundary["bytes"]:
        raise shared.StepFailed("owned relay log truncated")
    result = evidence.relay_check(receipt["baseline"], value[boundary["bytes"]:].decode("utf-8", "replace"))
    out = ctx.run_dir / "heartbeat_relay.json"
    out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    if out.name not in ctx.artifacts:
        ctx.artifacts.append(out.name)
    return result


base_namespace = shared.Context.namespace
def namespace(self):
    base = base_namespace(self)
    def inject(index):
        path = shared.LOGS / f"kh2coop_inject_{self.inst(index).pid}.log"
        return path.read_text(errors="replace") if path.exists() else ""
    def host_ending(pilot="host_thorn", retained="legA_host_pilot", baseline="thorn_baseline"):
        # Leg A route (b), Quill r1 R2: the retained pilot's actual exit code and stuck reason, with the native
        # counts since the fight's baseline, classified; None (an unknown ending) fails the scenario's assert.
        procs = [p[0] for p in self.saved.get(retained, []) if p[4] == pilot]
        exit_code = procs[0].returncode if len(procs) == 1 else None
        jsonl = self.run_dir / pilot / "pilot.jsonl"
        reason = evidence.stuck_reason(jsonl.read_text(encoding="utf-8")) if jsonl.exists() else None
        before = self.saved[baseline]
        host_end = base["log_count"]("[mission-mirror] host end trap=mission_dead_boss", 0) - before["host_end"]
        new_downs = base["log_count"]("[downed] gate intercept ", 0) - before["downs"][0]
        return {"exit": exit_code, "reason": reason, "hostEnd": host_end, "newDowns": new_downs,
                "ending": evidence.route_b_ending(exit_code, reason, host_end, new_downs)}
    return {**base, "recovery_proof": lambda: proof(self), "pause_relay": lambda: pause_relay(self),
            "host_ending": host_ending,
            "last_load": lambda index=0: evidence.last_load(inject(index)),
            "field_catalog": lambda index=0, after_load=0, after_marker=None:
                evidence.field_catalog(inject(index), base["location"](index), after_load, after_marker)}
shared.Context.namespace = namespace
shared.STEPS["friend_heartbeat_pause"] = step_pause
base_validate = shared.validate_scenario
def validate(scenario):
    base_validate(scenario)
    pauses = [step for step in scenario["steps"] if step["do"] == "friend_heartbeat_pause"]
    if len(pauses) != 1 or pauses[0].get("instance") != 1 or pauses[0].get("pauseMs") != PAUSE_MS:
        raise ValueError("exactly one fixed Friend1 heartbeat pause required")
shared.validate_scenario = validate

if __name__ == "__main__":
    sys.exit(shared.main())
