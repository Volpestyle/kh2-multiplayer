"""Authenticate retained 856 roots and preservation; C++ resolver runs in native slot."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
log = Path(sys.argv[1])
raw = log.read_bytes()
lines = raw.decode("utf-8", errors="strict").splitlines()
checks = []
def check(name, condition):
    if not condition:
        raise AssertionError(name)
    checks.append(name)

def fields(line):
    return dict(re.findall(r"(\w+)=([^\s]+)", line))

rows = [fields(line) for line in lines if "[enemy-binding]" in line]
a = next(r for r in rows if r.get("frame") == "984" and r.get("spawn") == "0")
b = next(r for r in rows if r.get("frame") == "986" and r.get("spawn") == "0")
check("retained984 established death", a["selected"] == "1" and a["reason"] == "established-death")
check("retained986 actual refusal preserved", b["selected"] == "0" and b["reason"] == "death-without-continuous-binding")
for key in ("actor", "objentry", "status", "controller", "record", "objectId", "candidate", "epoch"):
    check("retained same " + key, a[key] == b[key])
check("native qualified DeathApply HP2->0", any("serial=524 " in s and "kind=9 " in s and "qualified=1 " in s and "beforeHp=2 " in s and "afterHp=0 " in s for s in lines))
check("985 not represented as retained", not any(r.get("frame") == "985" and r.get("spawn") == "0" for r in rows))
cpp = (root / "tests/EnemyRecordBindingTest.cpp").read_text(encoding="utf-8")
for key in ("actor", "objentry", "status", "controller", "record"):
    check("C++ fixture authenticates " + key, "0x" + a[key].removeprefix("0x").upper() + "ULL" in cpp)
check("constructed intermediate disclosure", "NOT a recovered sample" in cpp)
for path in ("inject/src/EnemySync.cpp", "inject/src/CombatCausalTrace.hpp", "inject/src/EntityHook.cpp", "tools/scenario/causal_summary_acquisition.py"):
    old = subprocess.check_output(["git", "show", "ca007d4:" + path], cwd=root)
    current = (root / path).read_bytes().replace(b"\r\n", b"\n")
    check(path + " unchanged", old == current)
print(json.dumps({"ok": True, "checks": checks, "count": len(checks),
    "retainedLog": str(log), "retainedLogSha256": hashlib.sha256(raw).hexdigest(),
    "cppResolverControls": 31, "cppExecution": "PENDING_STEAM_NATIVE_BUILD",
    "frame985": "CONSTRUCTED_CONTINUOUS_PRECONDITION_NOT_RETAINED",
    "retroactiveAcceptance": False}, indent=2))
