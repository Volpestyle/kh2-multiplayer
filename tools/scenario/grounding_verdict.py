"""Read-only Borough ground/pose check at equal authoritative host frames.

Requires existing ENEMY_MIRROR_TRACE and LATENCY_TRACE logs on both local peers.
QPC latency decomposition is valid only for logs from the same Windows PC.
No game control, process access, or memory writes. Grounding and latency remain
separate results; extra delay is reported even when matched poses agree.
"""
from __future__ import annotations
import argparse
import bisect
import json
import math
import re
import statistics
from pathlib import Path

NUM = r"-?\d+(?:\.\d+)?"
HOST = re.compile(r"\[enemy-mirror\] trace-host netId=(\d+) hostFrame=(\d+) pos=("+NUM+r"),("+NUM+r"),("+NUM+r") motion=(\d+) time=("+NUM+r")")
CLIENT = re.compile(r"\[enemy-mirror\] trace-client netId=(\d+) hostFrame=(\d+) frame=(\d+) driven=(\d+) blend=(\d+) pos=("+NUM+r"),("+NUM+r"),("+NUM+r") motion=(\d+)")
LAT = re.compile(r"\[latency-enemy\] role=(host|client) frame=(\d+) qpc=(\d+) epoch=(\d+) newest=(\d+) cursor=("+NUM+r")")
SCOPE = re.compile(r"\[statehash\] role=\w+ epoch=(\d+) frame=\d+ room=04/09 door=2 map=1 btl=1 evt=0\b")
NATIVE = re.compile(r"\[statehash\] native epoch=(\d+) frame=\d+ netId=(\d+) objectId=302 hp=(\d+) actor=")

def parse(text: str):
    result = {"host": {}, "client": [], "latency": [], "shadowKeys": set()}
    epoch = None
    scope = False
    for line in text.splitlines():
        if "[warp] load complete " in line:
            scope = "room=04/09 door=2 map=1 btl=1 evt=0" in line
            epoch = None
        if m := SCOPE.search(line):
            scope = True
            epoch = int(m[1])
        if m := NATIVE.search(line):
            if scope and int(m[1]) == epoch and int(m[3]) > 0:
                result["shadowKeys"].add((epoch, int(m[2])))
        if not scope or epoch is None:
            continue
        if m := HOST.search(line):
            n, frame = int(m[1]), int(m[2])
            result["host"].setdefault((epoch, n), {})[frame] = {
                "frame": frame, "position": [float(m[i]) for i in (3,4,5)], "motion": int(m[6])}
        if m := CLIENT.search(line):
            result["client"].append({"epoch": epoch, "netId": int(m[1]), "hostFrame": int(m[2]),
                "frame": int(m[3]), "driven": int(m[4]), "blend": int(m[5]),
                "position": [float(m[i]) for i in (6,7,8)], "motion": int(m[9])})
        if m := LAT.search(line):
            result["latency"].append({"scopeEpoch": epoch,"role":m[1],"frame":int(m[2]),
                "qpc":int(m[3]),"streamEpoch":int(m[4]),"newest":int(m[5]),"cursor":float(m[6])})
    return result

def summarize(values):
    return {"count": len(values), "min": min(values), "median": statistics.median(values), "max": max(values)} if values else {"count":0}

def evaluate(host, friend):
    keys = host["shadowKeys"] & friend["shadowKeys"]
    missing_keys = sorted(host['shadowKeys'] ^ friend['shadowKeys'])
    ordered = {key: sorted(rows) for key, rows in host["host"].items()}
    pairs = []; unavailable = []; refused = {"undriven":0,"blend":0}; latency = []
    clocks = sorted((r["qpc"], r["frame"]) for r in host["latency"] if r["role"] == "host")
    qpcs = [q for q,_ in clocks]
    fl = {(r["scopeEpoch"],r["frame"]):r for r in friend["latency"] if r["role"] == "client"}
    for row in friend["client"]:
        key = row["epoch"], row["netId"]
        if key not in keys: continue
        if not row["driven"]:refused["undriven"]+=1;continue
        if row["blend"]:refused["blend"]+=1;continue
        frames = ordered.get(key,[]); f = row["hostFrame"]; at = bisect.bisect_left(frames,f)
        a = frames[at] if at<len(frames) and frames[at]==f else frames[at-1] if at else None
        b = frames[at] if at<len(frames) else None
        if a is None or b is None or b-a>3:
            unavailable.append({"key":key,"frame":f,"reason":"no adjacent published bracket"});continue
        h0,h1 = host["host"][key][a],host["host"][key][b]
        u = (f-a)/(b-a) if b>a else 0
        expected = [x+(y-x)*u for x,y in zip(h0["position"],h1["position"])]
        dy = row["position"][1]-expected[1]
        if not math.isfinite(dy):
            unavailable.append({"key":key,"frame":f,"reason":"nonfinite"});continue
        # A strict independent rest window: 12 host frames, contiguous publications,
        # constant motion, and <=0.5u host vertical range. Never use airborne samples.
        window = [host["host"][key][v] for v in frames if f-6<=v<=f+6]
        rest = (len(window)>=5 and window[-1]["frame"]-window[0]["frame"]>=12
            and all(y["frame"]-x["frame"]<=3 for x,y in zip(window,window[1:]))
            and len({r["motion"] for r in window})==1 and row["motion"]==window[0]["motion"]
            and max(r["position"][1] for r in window)-min(r["position"][1] for r in window)<=0.5)
        pairs.append({"epoch":key[0],"netId":key[1],"hostFrame":f,"clientFrame":row["frame"],
            "expected":expected,"observed":row["position"],"dy":dy,"rest":rest})
        clock = fl.get((key[0],row["frame"]))
        # TickMirror logs BEFORE advancing its cursor; TraceObserved labels the
        # previous update's pose. The same-frame cursor must match explicitly.
        if not clock or clock["streamEpoch"]!=key[0] or clock["cursor"]!=f:continue
        j = bisect.bisect_left(qpcs,clock["qpc"])
        if not 0<j<len(clocks):continue
        (q0,hf0),(q1,hf1) = clocks[j-1],clocks[j]
        if not 0<q1-q0 or hf1-hf0>2:continue
        now = hf0+(hf1-hf0)*(clock["qpc"]-q0)/(q1-q0)
        latency.append({"netId":key[1],"clientFrame":row["frame"],"hostFrameNow":now,
            "freshnessFrames":now-clock["newest"],"bufferFrames":clock["newest"]-f,"totalFrames":now-f})
    by_id = {}
    for key in sorted(keys):
        rows = [r for r in pairs if (r["epoch"],r["netId"])==key]
        ground = [r for r in rows if r["rest"]]
        by_id[f"{key[0]}/{key[1]}"]={"matched":len(rows),"rest":len(ground),
            "maxAbsY":max((abs(r["dy"]) for r in rows),default=None),
            "maxRestAbsY":max((abs(r["dy"]) for r in ground),default=None)}
    pose_pass = bool(by_id) and not missing_keys and all(r["matched"]>=30 and r["maxAbsY"]<=5 for r in by_id.values())
    rest_pass = bool(by_id) and not missing_keys and all(r["rest"]>=20 and r["maxRestAbsY"]<=2 for r in by_id.values())
    ls = {k:summarize([r[k] for r in latency]) for k in ("freshnessFrames","bufferFrames","totalFrames")}
    extra = ls['totalFrames'].get('median',6)-6 if latency else None
    latency_status = 'UNAVAILABLE' if extra is None else 'OPEN_EXTRA_DELAY' if extra>1 else 'NO_EXTRA_DELAY'
    return {"posePass":pose_pass,"groundRestPass":rest_pass,"byNetId":by_id,"missingKeys":missing_keys,"unavailable":unavailable,
        "refused":refused,"pairs":pairs,"latency":{"configuredDelay":6,"components":ls,"extraMedianFrames":extra,"status":latency_status,
            "extraDelayFinding":"OPEN: quantify observed total minus configured6 separately; it never changes grounding tolerances",
            "samples":latency,"scope":"same-PC QPC only; freshness includes publish/pump/transport; buffer includes cursor hysteresis"},
        "thresholds":{"maxPublishedBracketFrames":3,"poseY":5,"groundRestY":2,"groundRestSpan":12,
            "hostRestRange":0.5,"minPairsPerId":30,"minRestPerId":20},
        "limits":"Unavailable/undriven/blended rows grant no evidence. Grounding still requires reviewed pixels; report latency separately even when both height gates pass."}

def controls():
    start = "[warp] load complete room=04/09 door=2 map=1 btl=1 evt=0\n[statehash] role=host epoch=2 frame=1 room=04/09 door=2 map=1 btl=1 evt=0\n[statehash] native epoch=2 frame=1 netId=2 objectId=302 hp=20 actor=AA\n"
    h = start + '\n'.join(f"[enemy-mirror] trace-host netId=2 hostFrame={f} pos=0,-100,0 motion=0 time=0" for f in range(100,281,3))
    h += '\n'+'\n'.join(f"[latency-enemy] role=host frame={f} qpc={f*1000} epoch=0 newest=0 cursor=0" for f in range(80,310))
    c = start + '\n'.join(f"[enemy-mirror] trace-client netId=2 hostFrame={f} frame={500+f} driven=1 blend=0 pos=0,-100,0 motion=0\n[latency-enemy] role=client frame={500+f} qpc={(f+11)*1000} epoch=2 newest={f+6} cursor={f}" for f in range(100,281,3))
    good=evaluate(parse(h),parse(c));assert good["posePass"] and good["groundRestPass"]
    assert good["latency"]["components"]["freshnessFrames"]["median"]==5
    assert good["latency"]["components"]["bufferFrames"]["median"]==6
    assert good["latency"]["components"]["totalFrames"]["median"]==11
    assert good["latency"]["status"]=='OPEN_EXTRA_DELAY' and good["latency"]["extraMedianFrames"]==5
    assert not evaluate(parse(h),parse(c.replace('pos=0,-100,0','pos=0,-80,0')))["groundRestPass"]
    assert not evaluate(parse(h),parse(c.replace('driven=1','driven=0')))["posePass"]
    assert not evaluate(parse(h),parse(c.replace('blend=0','blend=1')))["posePass"]
    assert not evaluate(parse(h),parse(c.replace('epoch=2','epoch=1')))["posePass"]
    unpaired=parse(h);unpaired['shadowKeys'].add((2,3))
    assert not evaluate(unpaired,parse(c))['posePass']
    assert not evaluate(parse(h),parse(''))["posePass"]
    assert not evaluate(parse(h.replace('hostFrame=1','hostFrame=9')),parse(c))["posePass"]
    # Perfectly matching airborne motion cannot stand in for a rest window.
    moving_h=parse(h);moving_c=parse(c)
    for values in moving_h['host'].values():
        for f,row in values.items():row['position'][1]=-100-f
    for row in moving_c['client']:row['position'][1]=-100-row['hostFrame']
    airborne=evaluate(moving_h,moving_c)
    assert airborne['posePass'] and not airborne['groundRestPass']
    print(json.dumps({"controls":"PASS","cases":["matched-floor","extra5-explicit","offset20","undriven","blended","wrong-epoch","missing-actor","empty","missing-brackets","airborne-not-rest"]}))

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--host',type=Path);ap.add_argument('--friend',type=Path)
    ap.add_argument('--out',type=Path);ap.add_argument('--self-test',action='store_true');a=ap.parse_args()
    if a.self_test:controls();return 0
    if not (a.host and a.friend and a.out):ap.error('--host, --friend and --out are required')
    result=evaluate(parse(a.host.read_text(encoding='utf-8-sig',errors='replace')),parse(a.friend.read_text(encoding='utf-8-sig',errors='replace')))
    a.out.write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k not in ('pairs','latency','unavailable')},indent=2))
    print(json.dumps({'latency':result['latency']['components'],'finding':result['latency']['extraDelayFinding']}))
    return 0 if result['posePass'] and result['groundRestPass'] else 1

if __name__=='__main__':raise SystemExit(main())
