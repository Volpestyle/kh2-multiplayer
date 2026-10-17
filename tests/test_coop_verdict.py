"""Co-op route verdict (VUH-2344): log lines in, pass/fail and problems out."""
import importlib.util
from pathlib import Path
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

    def test_relay_desync_and_exceptions_fail(self):
        v = coop_verdict.verdict(HOST + "EXCEPTION in HookedInputCollector post-call\n", {"friend": FRIEND},
                                 "[SessionHost] Desync: peer1 fields=2 epoch=2\n")
        self.assertFalse(v["pass"])
        self.assertIn("host: 1 EXCEPTION lines", v["problems"])
        self.assertIn("relay desync: peer1 fields=2 epoch=2", v["problems"])

    def test_no_host_room_fails(self):
        self.assertFalse(coop_verdict.verdict("", {"friend": ""})["pass"])


if __name__ == "__main__":
    unittest.main()
