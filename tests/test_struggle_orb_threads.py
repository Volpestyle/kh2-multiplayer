"""VUH-2881: execute the committed orb/thread acceptance predicates with no game.

Historical 192703 excerpts remain negative evidence. Synthetic positives test the
new receipt contract; they are not a live affinity or clone-pickup claim.
"""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("orb_thread_runner", ROOT / "tools/scenario/run.py")
RUN = importlib.util.module_from_spec(spec)
spec.loader.exec_module(RUN)
SCENARIO = json.loads((ROOT / "tools/scenario/scenarios/net_prologue_struggle_orbs.json").read_text())
PREDICATES = [s for s in SCENARIO["steps"] if s.get("message", "").startswith(
    ("orb hook and entity-thread", "worker receipts must", "host pickups must"))]
HOST = """[struggle-orbs] thread site=NoteGameThread tid=10 gameTid=10
[struggle-orbs] thread site=create tid=10 gameTid=10
[struggle-orbs] thread site=init tid=10 gameTid=10
[struggle-orbs] thread site=fly tid=20 gameTid=10
[struggle-orbs] thread site=pick-a tid=20 gameTid=10
[struggle-orbs] stats births=242 collects=3 unresolved=0 dropsSuppressed=0 picksSuppressed=0 offThread=41208 queued=245 drained=245 stale=0 overflow=0
[struggle-orbs] sent epoch=2 seq=243 collect orb=2 type=0 point=(0,0,0) collector=2 netId=0
"""
FRIEND = """[struggle-orbs] thread site=NoteGameThread tid=30 gameTid=30
[struggle-orbs] thread site=pick-b tid=40 gameTid=30
[struggle-orbs] stats births=0 collects=0 unresolved=0 dropsSuppressed=38 picksSuppressed=44591 offThread=44591 queued=0 drained=0 stale=0 overflow=0
[struggle-orbs] received epoch=2 seq=243 collect orb=2
[struggle-orbs] applied epoch=2 collect orb=2 collector=2 netId=0 ok=1
[struggle-orbs] client shown=239 created=242 vanished=3 skipped=0 createFailed=0 vanishFailed=0 queued=0
"""


class ThreadPredicatesTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.logs = Path(temp.name)
        old_logs = RUN.LOGS
        RUN.LOGS = self.logs
        self.addCleanup(setattr, RUN, "LOGS", old_logs)
        self.ctx = RUN.Context(self.logs)
        self.addCleanup(self.ctx._stop.set)
        for i, pid in enumerate((910001, 910002)):
            inst = RUN.Instance(i, pid)
            inst.expect_exit = True
            self.ctx.instances.append(inst)

    def write(self, host=HOST, friend=FRIEND):
        for pid, text in ((910001, host), (910002, friend)):
            (self.logs / f"kh2coop_inject_{pid}.log").write_text(text)

    def test_worker_entries_are_allowed_only_with_booked_and_mirrored_collects(self):
        self.write()
        self.assertEqual(len(PREDICATES), 3)
        for step in PREDICATES:
            RUN.STEPS["assert"](self.ctx, step)

    def test_historical_failure_still_fails_all_three_new_predicates(self):
        fixture = ROOT / "tests/fixtures/struggle_orb_threads"
        self.write((fixture / "host.log").read_text(), (fixture / "friend.log").read_text())
        for step in PREDICATES:
            with self.subTest(predicate=step["message"]), self.assertRaises(RUN.StepFailed):
                RUN.STEPS["assert"](self.ctx, step)

    def test_missing_thread_logging_fails(self):
        self.write(host=HOST.replace("thread site=pick-a", "not-a-receipt site=pick-a"))
        with self.assertRaises(RUN.StepFailed):
            RUN.STEPS["assert"](self.ctx, PREDICATES[0])

    def test_overflow_fails_on_either_seat(self):
        for seat in (0, 1):
            with self.subTest(seat=seat):
                self.write(host=HOST.replace("overflow=0", "overflow=1") if seat == 0 else HOST,
                           friend=FRIEND.replace("overflow=0", "overflow=1") if seat == 1 else FRIEND)
                with self.assertRaises(RUN.StepFailed):
                    RUN.STEPS["assert"](self.ctx, PREDICATES[1])

    def test_no_drain_no_collect_no_receive_or_no_vanish_fails(self):
        cases = [(HOST.replace("drained=245", "drained=0"), FRIEND, 1),
                 (HOST.replace("collects=3", "collects=0"), FRIEND, 2),
                 (HOST.replace("collect orb=", "birth orb="), FRIEND, 2),
                 (HOST, FRIEND.replace("collect orb=", "birth orb="), 2),
                 (HOST, FRIEND.replace("vanished=3", "vanished=0"), 2)]
        for host, friend, predicate in cases:
            with self.subTest(host=host, friend=friend):
                self.write(host, friend)
                with self.assertRaises(RUN.StepFailed):
                    RUN.STEPS["assert"](self.ctx, PREDICATES[predicate])

    def test_zero_friend_suppression_fails_even_with_correlated_collects(self):
        for old, new in (("dropsSuppressed=38", "dropsSuppressed=0"),
                         ("picksSuppressed=44591", "picksSuppressed=0")):
            with self.subTest(counter=old):
                self.write(friend=FRIEND.replace(old, new))
                with self.assertRaises(RUN.StepFailed):
                    RUN.STEPS["assert"](self.ctx, PREDICATES[2])

    def test_mismatched_collect_identity_fails(self):
        for old, new in (("collect orb=2", "collect orb=999"), ("epoch=2", "epoch=3"), ("seq=243", "seq=244")):
            with self.subTest(identity=old):
                self.write(friend=FRIEND.replace(old, new))
                with self.assertRaises(RUN.StepFailed):
                    RUN.STEPS["assert"](self.ctx, PREDICATES[2])

    def test_a_shared_collect_cannot_hide_an_additional_unmatched_collect(self):
        self.write(friend=FRIEND + '[struggle-orbs] received epoch=2 seq=244 collect orb=999\n')
        with self.assertRaises(RUN.StepFailed):
            RUN.STEPS["assert"](self.ctx, PREDICATES[2])

    def test_clone_collect_and_matching_successful_application_are_required(self):
        cases = [(HOST.replace('collector=2', 'collector=1'), FRIEND),
                 (HOST, FRIEND.replace('applied epoch=', 'not-applied epoch=')),
                 (HOST, FRIEND.replace('applied epoch=2 collect orb=2', 'applied epoch=2 collect orb=999')),
                 (HOST, FRIEND.replace('ok=1', 'ok=0'))]
        for host, friend in cases:
            with self.subTest(host=host, friend=friend):
                self.write(host, friend)
                with self.assertRaises(RUN.StepFailed):
                    RUN.STEPS["assert"](self.ctx, PREDICATES[2])

    def test_missing_initializer_or_flight_thread_evidence_fails(self):
        for site in ('init', 'fly'):
            with self.subTest(site=site):
                self.write(host=HOST.replace('thread site='+site, 'not-a-receipt site='+site))
                with self.assertRaises(RUN.StepFailed):
                    RUN.STEPS["assert"](self.ctx, PREDICATES[0])


if __name__ == "__main__":
    unittest.main()
