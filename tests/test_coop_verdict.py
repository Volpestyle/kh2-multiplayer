"""Co-op route verdict (VUH-2344): log lines in, pass/fail and problems out."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location("coop_verdict", ROOT / "tools" / "scenario" / "coop_verdict.py")
coop_verdict = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(coop_verdict)

HOST = """\
[progresssync] host full version=1 spans=4 bytes=8108 hash=991449DB
[enemysync] host arrived epoch=1 room=05/01 door=99 map=0 btl=0 evt=20
[travel] native request target=0F/00 door=65284 from=05/01
[event-hold] published active=1 epoch=1 evt=20 frame=3846 tid=1 generation=2 delivery=1 source=401
[travel] landed room=08/00 door=0
[progresssync] host delta version=2 spans=5 bytes=6 hash=337247B1
[enemysync] host arrived epoch=2 room=08/00 door=50 map=0 btl=0 evt=19
[event-hold] published active=0 epoch=2 evt=19 frame=14529 tid=1 generation=2 delivery=1 source=405
"""
FRIEND = """\
[progresssync] apply version=1 spans=0 bytes=0 hash=991449DB personal_before=1 personal_after=1 personal_unchanged=1
[enemysync] client arrived epoch=1 room=05/01 door=99 map=0 btl=0 evt=20
[progresssync] apply version=2 spans=5 bytes=6 hash=337247B1 personal_before=1 personal_after=1 personal_unchanged=1
[enemysync] client arrived epoch=2 room=08/00 door=50 map=0 btl=0 evt=19
[event-control] input phase=8 menu=255 neutral=1 start=0 failed=0 released=1 ordinal=4 tid=1 abort=0
"""


class CoopVerdictTest(unittest.TestCase):
    def test_followed_route_passes(self):
        v = coop_verdict.verdict(HOST, {"friend": FRIEND})
        self.assertTrue(v["pass"], v["problems"])
        friend = v["friends"]["friend"]
        self.assertEqual([r["result"] for r in friend["rooms"]], ["followed", "followed"])
        self.assertEqual(friend["holdsReleased"], 1)
        self.assertEqual(len(v["host"]["travel"]), 2)

    def test_wrong_door_fails(self):
        friend = FRIEND.replace("client arrived epoch=2 room=08/00 door=50", "client arrived epoch=2 room=08/00 door=0")
        v = coop_verdict.verdict(HOST, {"friend": friend})
        self.assertFalse(v["pass"])
        self.assertIn("epoch 2: friend arrived", v["problems"][0])

    def test_missing_last_room_fails_but_superseded_room_warns(self):
        friend = "\n".join(line for line in FRIEND.splitlines() if "client arrived" not in line)
        v = coop_verdict.verdict(HOST, {"friend": friend})
        self.assertFalse(v["pass"])
        report = v["friends"]["friend"]
        self.assertEqual([r["result"] for r in report["rooms"]], ["superseded", "missing"])
        self.assertEqual(len(report["warnings"]), 1)

    def test_progress_hash_mismatch_and_personal_change_fail(self):
        friend = FRIEND.replace("hash=337247B1 personal_before=1 personal_after=1 personal_unchanged=1",
                                "hash=00000000 personal_before=1 personal_after=2 personal_unchanged=0")
        v = coop_verdict.verdict(HOST, {"friend": friend})
        self.assertIn("friend: progress v2: hash 00000000 != host 337247B1", v["problems"])

    def test_skipped_intermediate_progress_passes_and_unapplied_latest_warns(self):
        host = HOST + "[progresssync] host delta version=3 spans=1 bytes=1 hash=AAAA0003\n"
        friend = "\n".join(line for line in FRIEND.splitlines() if "apply version=1" not in line)
        v = coop_verdict.verdict(host, {"friend": friend})
        self.assertTrue(v["pass"], v["problems"])
        self.assertIn("progress v3: not applied yet (next room boundary)", v["friends"]["friend"]["warnings"])

    def test_relay_desync_and_exceptions_fail(self):
        v = coop_verdict.verdict(HOST + "EXCEPTION in HookedInputCollector post-call\n", {"friend": FRIEND},
                                 "[SessionHost] Desync: peer1 fields=2 epoch=2\n")
        self.assertFalse(v["pass"])
        self.assertIn("host: 1 EXCEPTION lines", v["problems"])
        self.assertIn("relay desync: peer1 fields=2 epoch=2", v["problems"])

    def test_converged_progress_desync_warns(self):
        friend = FRIEND.replace("[enemysync] client arrived epoch=1", "[warp] client issued epoch=1\n[enemysync] client arrived epoch=1")
        friend = friend.replace("[enemysync] client arrived epoch=2", "[warp] client issued epoch=2\n[enemysync] client arrived epoch=2")
        v = coop_verdict.verdict(HOST, {"friend": friend}, "[SessionHost] Desync: peer1 fields=4 epoch=1\n")
        self.assertTrue(v["pass"], v["problems"])
        self.assertIn("converged at the epoch 2 boundary (v2 hash 337247B1)", v["warnings"][-1])

    def test_unconverged_progress_desync_and_enemy_desync_fail(self):
        friend = FRIEND.replace("[enemysync] client arrived epoch=2", "[warp] client issued epoch=2\n[enemysync] client arrived epoch=2")
        late = coop_verdict.verdict(HOST, {"friend": friend}, "[SessionHost] Desync: peer1 fields=4 epoch=2\n")
        self.assertIn("relay desync: peer1 fields=4 epoch=2; progress never converged", late["problems"])
        enemies = coop_verdict.verdict(HOST, {"friend": friend}, "[SessionHost] Desync: peer1 fields=2 epoch=1\n")
        self.assertIn("relay desync: peer1 fields=2 epoch=1", enemies["problems"])

    def test_no_host_room_fails(self):
        self.assertFalse(coop_verdict.verdict("", {"friend": ""})["pass"])

    def test_room_rows_time_each_arrival_from_the_shared_tick(self):
        # VUH-2898: line formats from run 20261026-193245 (Axel I's room, friend 5.75 s behind).
        host = HOST + "[statehash] role=host epoch=2 frame=8309 room=08/00 door=50 map=0 btl=0 evt=19 count=1 tickMs=794568187\n"
        friend = FRIEND.replace(
            "[enemysync] client arrived epoch=2",
            "[statehash] role=client epoch=2 frame=4100 room=08/00 count=0 tickMs=794571000\n"  # before its arrival: not it
            "[enemysync] client arrived epoch=2") + \
            "[statehash] role=client epoch=2 frame=4200 room=08/00 door=50 map=0 btl=0 evt=19 count=1 tickMs=794573937\n"
        v = coop_verdict.verdict(host, {"friend": friend})
        self.assertTrue(v["pass"], v["problems"])
        last = v["friends"]["friend"]["rooms"][-1]
        self.assertEqual((last["hostTickMs"], last["friendTickMs"], last["lagMs"]), (794568187, 794573937, 5750))
        self.assertEqual(v["host"]["rooms"][-1]["tickMs"], 794568187)
        first = v["friends"]["friend"]["rooms"][0]
        self.assertEqual((first["hostTickMs"], first["friendTickMs"], first["lagMs"]), (None, None, None))


# VUH-2897: line formats from run 20261027-043745 (Axel I, friend ACK withheld, deadline release
# with the pause menu open). The frozen host lags the friend's own room-visited progress byte.
def axel_sample(role, frame, progress, tick):
    return (f"[statehash] role={role} epoch=4 frame={frame} room=02/14 door=0 map=137 btl=137 evt=137 "
            f"enemies=210A0FB3 progress={progress} count=1 unmatched=0 observed=1 tickMs={tick}\n")


AXEL_HOST = (
    "[progresssync] host delta version=4 spans=2 bytes=2 hash=30BEFB6E\n"
    "[enemysync] host arrived epoch=4 room=02/14 door=0 map=137 btl=137 evt=137\n"
    "[combat-admission] released failed-barrier native-fallback incarnation=1 outcome=deadline failed=1 "
    "elapsedMs=15000 epoch=4 load=4 transition=3 deferred=3 owned=18 released=1B missingSeats=2 seats=1/0 "
    "mask=0000001B event=0 menu=8 challenge=931A122524878104\n"
    + axel_sample("host", 9517, "30BEFB6E", 827297531)
    + "[progresssync] host delta version=5 spans=1 bytes=1 hash=7F55CDFE\n"
    + axel_sample("host", 9577, "7F55CDFE", 827298546)
    + axel_sample("host", 9637, "7F55CDFE", 827299546))
AXEL_FRIEND = (
    "[progresssync] apply version=4 spans=0 bytes=0 hash=30BEFB6E personal_before=1 personal_after=1 "
    "personal_unchanged=1\n"
    "[enemysync] client arrived epoch=4 room=02/14 door=0 map=137 btl=137 evt=137\n"
    + axel_sample("client", 6355, "7F55CDFE", 827297140)
    + axel_sample("client", 6413, "7F55CDFE", 827298140)
    + axel_sample("client", 6471, "7F55CDFE", 827299140))
AXEL_RELAY = "[SessionHost] Desync: peer1 fields=4 epoch=4\n"


class AdmissionHoldDesyncTest(unittest.TestCase):
    def verdict(self, host=AXEL_HOST, friend=AXEL_FRIEND, host_hash=0x30BEFB6E, friend_hash=0x7F55CDFE):
        with tempfile.TemporaryDirectory() as root:
            report = Path(root) / "session" / "1"
            report.mkdir(parents=True)
            (report / "manifest.json").write_text(json.dumps({"trigger": {
                "epoch": 4, "fields": 4, "hostHash": {"worldId": 2, "roomId": 20, "progressHash": host_hash},
                "clientHash": {"worldId": 2, "roomId": 20, "progressHash": friend_hash}}}), encoding="utf-8")
            return coop_verdict.verdict(host, {"friend": friend}, AXEL_RELAY, Path(root))

    def test_hold_lag_converging_inside_the_bound_warns(self):
        v = self.verdict()
        self.assertTrue(v["pass"], v["problems"])
        self.assertTrue(any("combat-admission hold, converged 1015 ms into host play" in w for w in v["warnings"]),
                        v["warnings"])

    def test_hold_lag_converging_past_the_bound_fails(self):
        late = AXEL_HOST.replace(axel_sample("host", 9577, "7F55CDFE", 827298546),
                                 axel_sample("host", 9577, "30BEFB6E", 827298546))
        v = self.verdict(host=late)
        self.assertFalse(v["pass"])
        self.assertTrue(any("converged 2015 ms" in p and "over the 2000 ms bound" in p for p in v["problems"]),
                        v["problems"])

    def test_without_an_admission_release_it_never_converged(self):
        host = "".join(l + "\n" for l in AXEL_HOST.splitlines() if "[combat-admission]" not in l)
        v = self.verdict(host=host)
        self.assertFalse(v["pass"])
        self.assertTrue(any(p.endswith("progress never converged") for p in v["problems"]), v["problems"])

    def test_a_mismatch_that_is_not_the_held_host_lag_still_fails(self):
        # The relay's host hash was never a held host sample, so this is unrelated drift.
        v = self.verdict(host_hash=0x12345678)
        self.assertFalse(v["pass"])
        # A friend that ends the epoch on another hash has not converged either.
        drift = AXEL_FRIEND + axel_sample("client", 6531, "0BADF00D", 827300140)
        self.assertFalse(self.verdict(friend=drift)["pass"])


if __name__ == "__main__":
    unittest.main()
