"""Offline retained-evidence/projection audit; native controls require steam."""
import gzip
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = "2725ff71b6c70701c266bc6c7c2442d7c4487258"
checks = []

def check(value, label):
    assert value, label
    checks.append(label)

def source(path):
    return (ROOT / path).read_text(encoding="utf-8")

def original(path):
    return subprocess.check_output(["git", "show", BASE + ":" + path], cwd=ROOT).decode()

def body(text, name):
    start = text.index(name)
    brace = text.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]

sync = source("inject/src/EnemySync.cpp")
old = original("inject/src/EnemySync.cpp")
impl = source("inject/src/EnemyOrdinaryLifecycle.inl")
check(body(sync, "bool ReleaseClientClaims(") == body(old, "bool ReleaseClientClaims("), "full claim release gate byte-exact")
begin, end = "bool lifecycleUnknown=false;", "g_hostHpSequence = m.sequence;"
check(sync[sync.index(begin):sync.index(end)] == old[old.index(begin):old.index(end)], "reason21 in-flight-birth refusal byte-exact")
for path in ("inject/src/OrdinaryEnemyBinding.hpp", "inject/src/EnemyMirror.inl", "CMakeLists.txt"):
    check(source(path) == original(path), path + " unchanged")
trace=source("inject/src/CombatCausalTrace.hpp")
oldtrace=original("inject/src/CombatCausalTrace.hpp")
for method in ("void Hit(","void Claim(","void HostApply(","void Consumer(","void Packet(","void Applied(","bool PacketWire(","bool ClaimWire(","bool LifecycleProof(","bool BindBirth("):
    check(body(trace,method)==body(oldtrace,method),method+" exact; no causal or HP guard relaxation")
leaf = body(sync, "bool RequestOrdinaryNativeLeave(")
check("g_applyStatDelta" not in leaf and "g_takeDamage" not in leaf and "*flags|=0x10" in leaf, "native leave has no damage/death call")
check("memcmp" in leaf and "wrapperBytes" in leaf and "fadeBytes" in leaf and "hp<=0" in leaf, "native ABI/version and positive HP gated")
consumer = body(impl, "bool ConsumeOrdinaryRetirements(")
check(consumer.index("ticket.attempted=true") < consumer.index("g_ordinaryLeave("), "one-shot poison precedes native operation")
check("ticket.failed)return false" in consumer and "frame-ticket.frame>120" in consumer, "unknown/timeout holds without retry")
check("complete-native-absence" in consumer and "bookend.watchedActorPresent" in consumer, "completion requires independent native absence")
check("pending.completed" in impl and "pending.original.record==member.record[0]" in impl, "replacement cannot bypass unresolved exact old binding")
check(sync.index("ConsumeOrdinaryRetirements(frame,census)") < sync.index("if (!ClientFrame(census))"), "native mutation stops frame before client stores and release")
fixture = ROOT / "tests/fixtures/ordinary_retirement"
raw = gzip.decompress((fixture / "b-client.log.gz").read_bytes())
manifest = json.loads((fixture / "manifest.json").read_text())
check(hashlib.sha256(raw).hexdigest() == manifest["sourceSha256"], "retained B log exact hash")
text = raw.decode(errors="replace")
cuts = [int(v) for v in re.findall(r"action=client-admitted cut=(\d+)", text)]
check(all(cut in cuts for cut in range(6, 16)), "actual B cuts6..15 retained")
check("authenticated-membership-retired" in text and "7FF6468B7F10" in text and manifest["oldBody"]["hp"] == 20, "actual original net4 living-body evidence retained")
check("frame=691" in text and "complete-stable-readback" in text and not manifest["nativeCompletionRetained"], "original release retained; no retroactive completion")
native = source("tests/NativeHitClaimTest.cpp")
check("B frame691 complete original manifest release" in native and "B cuts7..15 new IDs6..10" in native, "production-handler controls use retained B sequence")
check("foreign/reused/unbound/dead retirement refuses" in native and "replacement complete current native readback releases" in native, "native positive/negative controls added, not executed")
print(json.dumps({"status": "PASS", "checks": checks, "nativeExecuted": False, "acceptance": False}, indent=2))
