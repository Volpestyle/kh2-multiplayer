"""Offline controls for sealed observations; never attaches to a process."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("native_reconnect_evidence", ROOT / "tools/scenario/native_reconnect_evidence.py")
EVIDENCE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EVIDENCE)
ROOM = "room=05/06 door=0 map=1 btl=1 evt=0"
LOCATION = [5, 6, 0, 1, 1, 0]
SESSION = "ab12cd34"


def identity(slot, roster, generation=5, seq=1, stage="bridge-open-bound", **updates):
    values = dict(schema=1, seq=seq, stage=stage, observationMs=seq * 100,
                  identityCurrent=1, session=SESSION, peerHex=f"peer{slot}".encode().hex(), peerBytes=5,
                  stringsComplete=1, slot=slot, selfConnection=roster[slot], hostConnection=roster[0],
                  attachedPid=100 + slot, generation=generation, generationValid=1, delivery=1,
                  worldSlot=slot, authority=2, bridgeOpen=1, quarantine=0, admitted=1,
                  transportConnected=1, recoveryAttempts=0, pinPresent=1, pinSession=SESSION,
                  pinHostConnection=roster[0], pinSlot=slot, nativeBootstrapReady="unverified", atomicBinding=0, errors=0)
    for index in range(3):
        values[f"roster{index}"] = values[f"worldRoster{index}"] = roster[index]
        values[f"peerFloor{index}"] = 1 if roster[index] else 0
    values.update(updates)
    return ("[runtime-identity] " + " ".join(f"{k}={v}" for k, v in values.items()) + "\n").encode()


def pose(slot, seq):
    return dict(ownerSlot=slot, worldId=5, roomId=6, seq=seq, finite=True, position=[10., 20., 30.])


def observation(slot, roster, generation, seq):
    samples = []
    for index in range(2):
        world = dict(localSlot=slot, generation=generation, deliverySerial=1, authorityMode=2,
                     connectionIds=roster[:], peerDeliverySerials=[1, 1, 1])
        puppets = []
        for puppet_index, owner in enumerate(i for i in range(3) if i != slot):
            provenance = dict(producer=2, localSlot=slot, generation=generation,
                              ownerConnectionId=roster[owner], localConnectionId=roster[slot], hostConnectionId=roster[0])
            puppets.append(dict(index=puppet_index, readAvailable=True,
                                value=dict(active=1, provenanceMatchesWorld=True, provenance=provenance, pose=pose(owner, seq + index))))
        samples.append(dict(index=index, beginMonotonicMs=100 * index, endMonotonicMs=100 * index + 1,
                            complete=True, identityStable=True, worldMappingAvailable=True, avatarMappingAvailable=True,
                            localReadAvailable=True, worldBefore=copy.deepcopy(world), worldAfter=world,
                            local=pose(0, seq + index), puppets=puppets))
    return dict(schema=1, command="observe", processId=100 + slot, ok=True, readOnly=True,
                atomicAcrossBridges=False, avatarBridgeVersion=4, worldBridgeVersion=12, samples=samples)


def initial():
    peers = []
    for slot in range(3):
        native = f"[warp] load complete serial=4 transition=3 {ROOM}\n[enemysync] {'host' if slot == 0 else 'client'} arrived epoch=9 {ROOM}\n".encode()
        if slot:
            native = b"[warp] client issued epoch=9 transition=3\n" + native
        peers.append(dict(slot=slot, gamePid=100 + slot, runtimePid=200 + slot, runtimeAlive=True,
                          nativeLog=native, runtimeLog=identity(slot, [11, 12, 13]), location=LOCATION[:],
                          avatarObservation=observation(slot, [11, 12, 13], 5, 50),
                          nativePuppets=dict(room=[5, 6], pointers=["0x20", "0x30"], localAddress="0x10",
                                             actors=[dict(address=addr, position=dict(x=10., y=20., z=30.)) for addr in ("0x20", "0x30")])) )
    return dict(peers=peers, relayLog=b"relay started\n")


CHAIN = ("[enemysync] session reset: host epoch and pending target cleared\n"
         "[progresssync] client full version=1 spans=4 bytes=8108 complete=1\n"
         "[warp] client queued epoch=9 target=05/06 door=0 map=1 btl=1 evt=0\n"
         "[progresssync] apply version=1 spans=0 bytes=0 hash=1234 personal_before=1234 personal_after=1234 personal_unchanged=1\n"
         "[warp] client issued epoch=9 transition=4\n"
         f"[warp] load complete serial=5 transition=4 {ROOM}\n"
         f"[enemysync] client arrived epoch=9 {ROOM}\n"
         "[spawn-authority] lease epoch=9 source=75 point=(10,20,30,1)\n"
         "[spawnctl] action=apply reason=static combat type2 controller=ABC key=1 headerId=30 records=5 transition=4 load=5 room=05/06 capture=0 apply=1 hold=0 unsupported=0 unavailable=0 pointAvailable=1 point=(10,20,30,1)\n").encode()


def reconnected(before):
    after = copy.deepcopy(before)
    after["relayLog"] += b"Peer timed out: peer1\nPeer verified: peer1 -> slot 1\n"
    for slot, row in enumerate(after["peers"]):
        generation = 7 if slot == 1 else 5
        if slot == 1:
            row["runtimeLog"] += identity(slot, [11, 12, 13], 6, 2, "retire-after", admitted=0)
            row["runtimeLog"] += b"[Runtime] Network: closed code=3 reason=3\n"
            row["runtimeLog"] += identity(slot, [11, 12, 13], 6, 3, "transport-closed", admitted=0)
            row["runtimeLog"] += b"[Runtime] Rejoin attempt=1\n"
            row["runtimeLog"] += identity(slot, [11, 12, 13], 6, 4, "retry-attempted", recoveryAttempts=1)
            row["runtimeLog"] += identity(slot, [11, 12, 13], 6, 5, "transport-connected", admitted=0)
            row["nativeLog"] += CHAIN
        row["runtimeLog"] += identity(slot, [11, 14, 13], generation, 6, "roster-admitted")
        row["avatarObservation"] = observation(slot, [11, 14, 13], generation, 100)
    return after


class ReconnectEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.before = initial()
        self.baseline = EVIDENCE.capture_baseline(self.before)
        self.after = reconnected(self.before)

    def reject(self, sample, fragment=None):
        result = EVIDENCE.validate_reconnect(self.baseline, sample)
        self.assertFalse(result["ready"], result)
        if fragment:
            self.assertIn(fragment, " ".join(result["problems"]))
        return result

    def test_qualified_baseline_and_fresh_chain(self):
        self.assertTrue(self.baseline["ready"], self.baseline)
        result = EVIDENCE.validate_reconnect(self.baseline, self.after)
        self.assertTrue(result["ready"], result)
        json.dumps(self.baseline)
        json.dumps(result)

    def test_bridge_versions_match_production_and_reject_other_layouts(self):
        for name, key, constant in (("AvatarBridge.hpp", "avatarBridgeVersion", "AVATAR_BRIDGE_VERSION"),
                                    ("WorldBridge.hpp", "worldBridgeVersion", "WORLD_BRIDGE_VERSION")):
            header = (ROOT / "common/include/kh2coop" / name).read_text()
            declaration = next(line for line in header.splitlines() if constant + " =" in line)
            current = int(declaration.split("=", 1)[1].split(";", 1)[0].strip())
            self.assertEqual(self.before["peers"][0]["avatarObservation"][key], current)
            self.assertTrue(self.baseline["ready"], self.baseline)
            for incompatible in (0, current - 1, current + 1):
                with self.subTest(key=key, version=incompatible):
                    sample = copy.deepcopy(self.before)
                    sample["peers"][0]["avatarObservation"][key] = incompatible
                    result = EVIDENCE.capture_baseline(sample)
                    self.assertFalse(result["ready"])
                    self.assertIn("invalid " + key, " ".join(result["problems"]))

    def test_incomplete_and_unavailable_inputs_never_throw(self):
        for sample in ({}, None, {"peers": []}, {"peers": [None]}, {"relayLog": "text"}):
            with self.subTest(sample=sample):
                self.assertFalse(EVIDENCE.capture_baseline(sample)["ready"])
                self.assertFalse(EVIDENCE.validate_reconnect(self.baseline, sample)["ready"])

    def test_invalid_baseline_identity_is_not_snapshotted(self):
        for field, value in (("admitted", 0), ("authority", 0), ("attachedPid", 0), ("generationValid", 0),
                             ("pinHostConnection", 999), ("identityCurrent", 0), ("stringsComplete", 0)):
            with self.subTest(field=field):
                sample = copy.deepcopy(self.before)
                sample["peers"][1]["runtimeLog"] = identity(1, [11, 12, 13], **{field: value})
                self.assertFalse(EVIDENCE.capture_baseline(sample)["ready"])

    def test_no_old_arrival_or_room_equality_substitute(self):
        self.after["peers"][1]["nativeLog"] = self.before["peers"][1]["nativeLog"]
        self.reject(self.after, "missing reset")

    def test_each_native_chain_event_is_required(self):
        for line in CHAIN.splitlines(keepends=True):
            with self.subTest(line=line):
                sample = copy.deepcopy(self.after)
                sample["peers"][1]["nativeLog"] = self.before["peers"][1]["nativeLog"] + CHAIN.replace(line, b"")
                self.reject(sample)

    def test_order_serial_transition_and_full_tuple(self):
        for old, new in ((b"serial=5", b"serial=4"), (b"transition=4 room", b"transition=7 room"),
                         (b"epoch=9", b"epoch=10"), (b"door=0", b"door=1"),
                         (b"personal_after=1234", b"personal_after=5678")):
            with self.subTest(new=new):
                sample = copy.deepcopy(self.after)
                sample["peers"][1]["nativeLog"] = self.before["peers"][1]["nativeLog"] + CHAIN.replace(old, new)
                self.reject(sample)

    def test_survivor_load_anywhere_after_offset_is_rejected(self):
        for slot in (0, 2):
            sample = copy.deepcopy(self.after)
            sample["peers"][slot]["nativeLog"] += f"[warp] load complete serial=5 transition=4 {ROOM}\n[enemysync] {'host' if slot == 0 else 'client'} arrived epoch=9 {ROOM}\n".encode()
            self.reject(sample, "surviving native process reloaded")

    def test_exact_prefix_not_just_tail_or_length(self):
        for kind in ("runtimeLog", "nativeLog"):
            sample = copy.deepcopy(self.after)
            raw = sample["peers"][1][kind]
            sample["peers"][1][kind] = b"X" + raw[1:]
            self.reject(sample)
        sample = copy.deepcopy(self.after)
        sample["relayLog"] = b"R" + sample["relayLog"][1:]
        self.reject(sample, "prefix changed")

    def test_partial_logs_rejected(self):
        self.after["peers"][1]["runtimeLog"] += b"[runtime-identity] schema=1"
        self.reject(self.after)

    def test_original_runtime_alive_and_real_retry_required(self):
        for key, value in (("runtimePid", 999), ("runtimeAlive", False), ("gamePid", 999)):
            sample = copy.deepcopy(self.after)
            sample["peers"][1][key] = value
            self.reject(sample)
        self.after["peers"][1]["runtimeLog"] = self.after["peers"][1]["runtimeLog"].replace(b"Rejoin attempt=1", b"not a retry")
        self.reject(self.after, "actual fresh automatic")

    def test_pin_must_be_retained_during_retry(self):
        self.after["peers"][1]["runtimeLog"] = self.after["peers"][1]["runtimeLog"].replace(b"stage=retry-attempted", b"stage=retry-attempted pinPresent=0")
        # Place override last, as duplicate token parsing would otherwise restore it.
        self.after["peers"][1]["runtimeLog"] = self.after["peers"][1]["runtimeLog"].replace(b"recoveryAttempts=1 pinPresent=1", b"recoveryAttempts=1 pinPresent=0")
        self.reject(self.after, "pin not retained")

    def test_stale_provenance_nonfinite_and_frozen_pose(self):
        for mutation in ("provenance", "nan", "frozen"):
            sample = copy.deepcopy(self.after)
            samples = sample["peers"][0]["avatarObservation"]["samples"]
            value = samples[1]["puppets"][0]["value"]
            if mutation == "provenance":
                value["provenance"]["ownerConnectionId"] = 12
            elif mutation == "nan":
                value["pose"]["position"][0] = float("nan")
            else:
                value["pose"]["seq"] = samples[0]["puppets"][0]["value"]["pose"]["seq"]
            self.reject(sample)

    def test_native_active_targets_and_pose_error(self):
        for mutation in ("alias", "missing", "far"):
            sample = copy.deepcopy(self.after)
            native = sample["peers"][0]["nativePuppets"]
            if mutation == "alias":
                native["pointers"][1] = native["pointers"][0]
            elif mutation == "missing":
                native["actors"][0] = None
            else:
                native["actors"][0]["position"]["x"] = 1000
            self.reject(sample)

    def test_retirement_needs_fresh_survivors_and_relay(self):
        logs = {slot: self.before["peers"][slot]["runtimeLog"] + identity(slot, [11, 0, 13], seq=2)
                for slot in (0, 2)}
        relay = self.before["relayLog"] + b"Peer timed out: peer1\n"
        result = EVIDENCE.retirement_proof(self.baseline, relay, logs)
        self.assertTrue(result["retired"], result)
        self.assertTrue(result["identityComplete"])
        self.assertEqual(result["connectionId"], 12)
        self.assertGreaterEqual(result["byteOffset"], self.baseline["relayLog"]["bytes"])
        self.assertFalse(EVIDENCE.retirement_proof(self.baseline, self.before["relayLog"], logs)["retired"])
        logs[0] = self.before["peers"][0]["runtimeLog"]
        self.assertFalse(EVIDENCE.retirement_proof(self.baseline, relay, logs)["retired"])

    def test_saved_pid_zero_smoke_cannot_qualify_native_baseline(self):
        path = ROOT / "build/rig/identity-geometry-runtime-pid0-20261003/runtime.log"
        if not path.exists():
            self.skipTest("saved PID0 smoke not present")
        sample = copy.deepcopy(self.before)
        sample["peers"][1]["runtimeLog"] = path.read_bytes()
        self.assertFalse(EVIDENCE.capture_baseline(sample)["ready"])

    def test_saved_native_forced_resync_cannot_prove_rejoin(self):
        path = ROOT / "build/scenarios/20261003-140717_net_forced_resync_shadows_population_1/kh2coop_inject_365340.log"
        if not path.exists():
            self.skipTest("saved native log not present")
        self.after["peers"][1]["nativeLog"] = path.read_bytes()
        self.reject(self.after)

    def test_saved_native_chain_parser_without_reconnect_claim(self):
        path = ROOT / "build/scenarios/20261003-140717_net_forced_resync_shadows_population_1/kh2coop_inject_365340.log"
        if not path.exists():
            self.skipTest("saved native log not present")
        native = path.read_text(errors="replace")
        tail = native[native.rfind("[enemysync] session reset:"):]
        problems = []
        chain = EVIDENCE._native_chain(tail, dict(loadSerial=3, transition=2), 1, LOCATION, problems)
        self.assertEqual(problems, [])
        self.assertEqual(chain["load"]["serial"], "4")

    def test_reconnected_avatar_sequence_is_connection_scoped(self):
        for row in self.after["peers"]:
            for index, sample in enumerate(row["avatarObservation"]["samples"]):
                for puppet in sample["puppets"]:
                    if puppet["value"]["pose"]["ownerSlot"] == 1:
                        puppet["value"]["pose"]["seq"] = index + 1
        result = EVIDENCE.validate_reconnect(self.baseline, self.after)
        self.assertTrue(result["ready"], result)

    def test_raw_local_dll_sequence_cannot_restart_on_reconnect(self):
        for index, sample in enumerate(self.after["peers"][1]["avatarObservation"]["samples"]):
            sample["local"]["seq"] = index + 1
        self.reject(self.after, "poses not fresh after baseline")

    def test_late_reset_invalidates_old_arrival(self):
        self.after["peers"][1]["nativeLog"] += b"[enemysync] session reset: host epoch and pending target cleared\n"
        self.reject(self.after)

    def test_typed_close_and_native_application_required(self):
        sample = copy.deepcopy(self.after)
        sample["peers"][1]["runtimeLog"] = sample["peers"][1]["runtimeLog"].replace(b"reason=3\n", b"reason=5\n")
        self.reject(sample, "typed close")
        sample = copy.deepcopy(self.after)
        sample["peers"][1]["nativeLog"] = sample["peers"][1]["nativeLog"].replace(b"transition=4 load=5", b"transition=3 load=4")
        self.reject(sample, "activation application")


if __name__ == "__main__":
    unittest.main()
