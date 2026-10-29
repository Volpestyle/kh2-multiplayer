"""VUH-2921 (lead #116): the post-Thorn proof fixture never films or captures FMV. No game.

    python -B tests/test_thorn_after_chain_fixture.py
"""
import json
import unittest
from pathlib import Path

FIXTURE = Path(__file__).resolve().parents[1] / "tools/scenario/scenarios/net_twilight_thorn_after_chain.json"
TT03 = "location(0)==[2,3,50,2,0,2]"
BOTH_TT03 = "all(location(i)==[2,3,50,2,0,2]"


class AfterChainFixtureTest(unittest.TestCase):
    def setUp(self):
        self.steps = json.loads(FIXTURE.read_text(encoding="utf-8"))["steps"]

    def index(self, predicate):
        return next(i for i, s in enumerate(self.steps) if predicate(s))

    def test_no_clip_step(self):
        """chain-02 died on a clip of the me4 movie: FMV doesn't present through the hooked Present."""
        self.assertEqual([s.get("name") for s in self.steps if s["do"] in ("clip", "clip_wait")], [])

    def test_no_capture_while_a_seat_can_be_in_me4(self):
        """From the host's me4 decode until both seats are in the tt03 Back Alley dialog, a seat may be in me4 (the
        friend plays its own, later). Only the host may be captured, and only once it is in tt03."""
        decode = self.index(lambda s: s["do"] == "wait_until" and "me4\\.mp4" in s["expr"])
        host_tt03 = self.index(lambda s: s["do"] == "wait_until" and s["expr"] == TT03)
        both_tt03 = self.index(lambda s: s["do"] == "wait_until" and s["expr"].startswith(BOTH_TT03))
        self.assertLess(decode, host_tt03)
        self.assertLess(host_tt03, both_tt03)
        for i in range(decode, both_tt03):
            step = self.steps[i]
            if step["do"] == "capture":
                self.assertEqual(step.get("instance", 0), 0, f"step {i} captures the friend during me4: {step}")
                self.assertGreater(i, host_tt03, f"step {i} captures the host before tt03: {step}")

    def test_me4_progress_stays_proven_by_the_decode_receipts(self):
        exprs = [s.get("expr", "") for s in self.steps]
        self.assertTrue(any("me4\\.mp4 -> ',0))>=1" in e for e in exprs))
        self.assertTrue(any("me4\\.mp4 -> ',0))>=2" in e for e in exprs))


if __name__ == "__main__":
    unittest.main()
