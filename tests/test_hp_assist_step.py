"""VUH-2912 assisted reruns (lead #105): the test-only hp_assist step and its two assisted Axel splits. No game.

    python -B tests/test_hp_assist_step.py
"""
import importlib.util
import json
import tempfile
import threading
import unittest
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("hp_assist_runner", ROOT / "tools/scenario/run.py")
RUN = importlib.util.module_from_spec(spec)
spec.loader.exec_module(RUN)
SCENARIOS = ROOT / "tools/scenario/scenarios"

BASE, STATUS, CANON = 0x7FF6A0000000, 0x7FF6C2B40000, "0x7FF7014EE050"


class FakeGame:
    """The canonical player's status i32s at absolute addresses, read by RVA keys and written by --addr like kh2ctl.
    `moves` scripts status HP per status read (a hit landing mid-check); `stick` ignores pokes."""

    def __init__(self, hp=10, max_hp=22, moves=None, stick=False):
        self.memory = {STATUS: hp, STATUS + 4: max_hp}
        self.moves, self.stick, self.pokes, self.reads = list(moves or []), stick, [], 0

    def __call__(self, *args, pid=None, **_):
        if args[0] == "peek":
            fields = [field.split(":")[0] for field in args[2].split(",")]
            if f"0x{STATUS - BASE:X}" in fields and self.moves and not self.pokes:
                self.memory[STATUS] = self.moves[min(self.reads, len(self.moves) - 1)]
                self.reads += 1
            return {"moduleBase": hex(BASE), "samples": [{k: self.memory.get(BASE + int(k, 16), 0) for k in fields}]}
        if args[0] == "poke":
            address, value = int(args[2], 16), int(args[6])
            self.pokes.append((pid, address, args[4], value))
            if not self.stick:
                self.memory[address] = value
            return {"ok": True}
        raise AssertionError(args)


def context(game, downs=0, revives=0, canonical=CANON, test_only=("player_hp_refill",)):
    inst = SimpleNamespace(pid=4242)

    def actors(index):
        return [{"address": canonical, "name": "P_EX110_BTLF", "status": f"0x{STATUS:X}",
                 "hp": game.memory[STATUS], "maxHp": game.memory[STATUS + 4]},
                {"address": "0x7FF701508DA0", "name": "B_EX110", "status": "0x1", "hp": 449, "maxHp": 449}]
    counts = {RUN.DOWNED_DOWN: downs, RUN.DOWNED_REVIVED: revives}
    namespace = {"log_count": lambda text, index=0: counts.get(text, 0), "peek": lambda rva, kind="u32", index=0: CANON}
    return SimpleNamespace(test_only=set(test_only), inst=lambda index: inst, actors=actors, saved={},
                           namespace=lambda: namespace)


class HpAssistTest(unittest.TestCase):
    def setUp(self):
        self.original = RUN.kh2ctl

    def tearDown(self):
        RUN.kh2ctl = self.original

    def tick(self, game, ctx, floor=12):
        RUN.kh2ctl = game
        return RUN.HpAssist(ctx, [0], floor).tick(0)

    def test_refills_the_canonical_player_to_max_at_or_below_the_floor(self):
        for hp in (12, 10, 1):
            with self.subTest(hp):
                game = FakeGame(hp=hp)
                event = self.tick(game, context(game))
                self.assertEqual(game.pokes, [(4242, STATUS, "i32", 22)])
                self.assertEqual((event["before"], event["readback"], event["actor"]), ([hp, 22], [22, 22], CANON))
                self.assertNotIn("readbackMismatch", event)

    def test_leaves_hp_above_the_floor_at_max_or_at_zero_alone(self):
        for hp, max_hp in ((13, 22), (22, 22), (0, 22), (5, 5)):
            with self.subTest((hp, max_hp)):
                game = FakeGame(hp=hp, max_hp=max_hp)
                self.assertIsNone(self.tick(game, context(game)))
                self.assertEqual(game.pokes, [])

    def test_never_refills_a_downed_seat(self):
        game = FakeGame(hp=1)
        self.assertIsNone(self.tick(game, context(game, downs=2, revives=1)))
        self.assertEqual(game.pokes, [])
        self.assertIsNotNone(self.tick(game, context(game, downs=2, revives=2)))  # revived: assisted again

    def test_only_the_seats_own_canonical_player(self):
        game = FakeGame(hp=4)
        self.assertIsNone(self.tick(game, context(game, canonical="0x7FF7014D3300")))  # the clone, not canonical
        self.assertEqual(game.pokes, [])

    def test_a_hit_during_the_check_defers_the_write(self):
        game = FakeGame(hp=10, moves=[10, 6, 6])  # status 10, the row then reads 6, status 6: not agreement
        self.assertIsNone(self.tick(game, context(game)))
        self.assertEqual(game.pokes, [])

    def test_a_failed_readback_is_recorded(self):
        game = FakeGame(hp=8, stick=True)
        event = self.tick(game, context(game))
        self.assertTrue(event["readbackMismatch"])
        self.assertEqual(event["readback"], [8, 22])

    def test_step_needs_the_opt_in_and_saves_its_events(self):
        game = FakeGame(hp=9)
        RUN.kh2ctl = game
        with self.assertRaisesRegex(RUN.StepFailed, "player_hp_refill"):
            RUN.step_hp_assist(context(game, test_only=()), {"do": "hp_assist"})
        ctx = context(game)
        self.assertEqual(RUN.step_hp_assist(ctx, {"do": "hp_assist", "seats": [0], "floor": 12})["floor"], 12)
        with self.assertRaisesRegex(RUN.StepFailed, "already on"):
            RUN.step_hp_assist(ctx, {"do": "hp_assist"})
        ctx.hp_assist.thread.join(timeout=0.5)  # one round runs before the first wait
        result = RUN.step_hp_assist(ctx, {"do": "hp_assist", "on": False, "as": "assist"})
        self.assertIs(ctx.saved["assist"], result)
        self.assertEqual((result["refills"], result["error"], game.pokes[0][3]), (1, "", 22))
        self.assertIsNone(ctx.hp_assist)

    def test_validator_requires_the_opt_in_and_sane_arguments(self):
        steps = [{"do": "boot"}, {"do": "hp_assist", "seats": [0, 1], "floor": 12}, {"do": "hp_assist", "on": False}]
        with self.assertRaisesRegex(ValueError, "player_hp_refill"):
            RUN.validate_scenario({"steps": steps})
        RUN.validate_scenario({"testOnly": ["player_hp_refill"], "steps": steps})
        for bad in ({"floor": 0}, {"floor": "12"}, {"seats": [0, "1"]}, {"seats": 0}, {"on": "no"}):
            with self.subTest(bad), self.assertRaisesRegex(ValueError, "hp_assist needs"):
                RUN.validate_scenario({"testOnly": ["player_hp_refill"],
                                       "steps": [steps[0], {"do": "hp_assist", **bad}]})


class LateGame(FakeGame):
    """FakeGame whose n-th status read (1-based) first runs `hooks[n]`, and whose status reads can block on `gate`
    (a worker stuck in an RPC) after signalling `entered`."""

    def __init__(self, hooks=None, gate=None, **kw):
        super().__init__(**kw)
        self.hooks, self.gate, self.status_reads = dict(hooks or {}), gate, 0
        self.entered = threading.Event()

    def __call__(self, *args, pid=None, **kw):
        if args[0] == "peek" and f"0x{STATUS - BASE:X}:i32" in args[2]:
            self.status_reads += 1
            self.hooks.get(self.status_reads, lambda: None)()
            if self.gate is not None:
                self.entered.set()
                self.gate.wait()
        return super().__call__(*args, pid=pid, **kw)


def live_context(game, counts, canonical):
    """context() whose down counts and canonical pointer are read live, so a test can change them mid-tick."""
    ctx = context(game)
    namespace = {"log_count": lambda text, index=0: counts.get(text, 0), "peek": lambda rva, kind="u32", index=0: canonical[0]}
    ctx.namespace = lambda: namespace
    return ctx


class HpAssistRound2Test(unittest.TestCase):
    """Ione r1 CHANGES (VUH-2912 04a25cff): quiescent stop, commit-time recheck, latched label, journalled write."""

    def setUp(self):
        self.original, self.join = RUN.kh2ctl, RUN.HpAssist.JOIN_S

    def tearDown(self):
        RUN.kh2ctl, RUN.HpAssist.JOIN_S = self.original, self.join

    def test_a_seat_that_downs_during_the_reads_is_not_refilled(self):
        counts = {RUN.DOWNED_DOWN: 0, RUN.DOWNED_REVIVED: 0}
        game = LateGame(hp=1, hooks={2: lambda: counts.update({RUN.DOWNED_DOWN: 1})})  # downed, held at HP 1
        RUN.kh2ctl = game
        self.assertIsNone(RUN.HpAssist(live_context(game, counts, [CANON]), [0], 12).tick(0))
        self.assertEqual(game.pokes, [])

    def test_a_canonical_change_during_the_reads_withholds_the_write(self):
        canonical = [CANON]
        game = LateGame(hp=5, hooks={2: lambda: canonical.__setitem__(0, "0x7FF7014D3300")})  # old actor still listed
        RUN.kh2ctl = game
        self.assertIsNone(RUN.HpAssist(live_context(game, {}, canonical), [0], 12).tick(0))
        self.assertEqual(game.pokes, [])

    def test_off_waits_for_a_worker_inside_an_rpc_and_no_write_follows_it(self):
        gate = threading.Event()
        game = LateGame(hp=6, gate=gate)
        RUN.kh2ctl = game
        ctx = context(game)
        RUN.step_hp_assist(ctx, {"do": "hp_assist", "seats": [0], "floor": 12})
        self.assertTrue(game.entered.wait(2))  # the worker is now blocked in its first status read
        done = []
        off = threading.Thread(target=lambda: done.append(RUN.step_hp_assist(ctx, {"do": "hp_assist", "on": False})))
        off.start()
        off.join(0.3)
        self.assertTrue(off.is_alive())  # off has not returned while the worker can still write
        gate.set()
        off.join(5)
        self.assertEqual((done[0]["workerExited"], done[0]["refills"], game.pokes, ctx.hp_assist), (True, 0, [], None))

    def test_off_refuses_to_proceed_past_a_worker_that_will_not_exit(self):
        gate = threading.Event()
        game = LateGame(hp=6, gate=gate)
        RUN.kh2ctl, RUN.HpAssist.JOIN_S = game, 0.2
        ctx = context(game)
        RUN.step_hp_assist(ctx, {"do": "hp_assist", "seats": [0], "floor": 12})
        self.assertTrue(game.entered.wait(2))
        with self.assertRaisesRegex(RUN.StepFailed, "still running"):
            RUN.step_hp_assist(ctx, {"do": "hp_assist", "on": False})
        self.assertIsNotNone(ctx.hp_assist)  # not forgotten: close tries again
        self.assertFalse(ctx.saved["hp_assist"]["workerExited"])
        gate.set()
        RUN.HpAssist.JOIN_S = 5
        self.assertTrue(RUN.step_hp_assist(ctx, {"do": "hp_assist", "on": False})["workerExited"])
        self.assertEqual(game.pokes, [])

    def test_context_close_retires_a_running_assist(self):
        game = FakeGame(hp=22)  # full HP: the worker only polls
        RUN.kh2ctl = game
        with tempfile.TemporaryDirectory() as tmp:
            ctx = FakeContext(Path(tmp), game)
            ctx.test_only = {"player_hp_refill"}
            RUN.step_hp_assist(ctx, {"do": "hp_assist", "seats": [0], "floor": 12})
            worker = ctx.hp_assist.thread
            ctx.close()  # a later step failed with the assist still on
            self.assertFalse(worker.is_alive())
            self.assertIsNone(ctx.hp_assist)
            self.assertEqual(ctx.saved["hp_assist"]["stoppedBy"], "close")
            self.assertTrue((Path(tmp) / "ASSISTED.json").exists())

    def test_a_write_is_journalled_before_it_is_made(self):
        game = FakeGame(hp=7)
        RUN.kh2ctl = lambda *a, **k: (_ for _ in ()).throw(RUN.StepFailed("readback lost")) \
            if a[0] == "peek" and game.pokes else game(*a, **k)
        assist = RUN.HpAssist(context(game), [0], 12)
        with self.assertRaisesRegex(RUN.StepFailed, "readback lost"):
            assist.tick(0)
        self.assertEqual(len(assist.events), 1)
        self.assertEqual((assist.events[0]["poked"], assist.events[0]["readbackError"]), (True, "readback lost"))

    def test_a_generically_named_assisted_run_is_labelled_everywhere(self):
        """The run is named like a native milestone; a refill really happens; the report, its Markdown, the
        evidence and the suite line must all say ASSISTED, after off."""
        game = FakeGame(hp=9)
        scenario = {"name": "native_milestone_probe", "testOnly": ["player_hp_refill"],
                    "steps": [{"do": "hp_assist", "seats": [0], "floor": 12}, {"do": "wait", "ms": 600},
                              {"do": "hp_assist", "on": False}]}
        original_context, original_runs = RUN.Context, RUN.RUNS
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "native_milestone_probe.json"
            path.write_text(json.dumps(scenario), encoding="utf-8")
            RUN.kh2ctl, RUN.RUNS = game, Path(tmp) / "runs"
            RUN.Context = lambda run_dir: FakeContext(run_dir, game)
            try:
                report = RUN.run_scenario(path, 1)
            finally:
                RUN.Context, RUN.RUNS = original_context, original_runs
            run_dir = Path(report["dir"])
            self.assertEqual(report["status"], "pass")
            self.assertGreaterEqual(game.pokes and report["assisted"]["refills"], 1)
            self.assertFalse(report["assisted"]["nativeMilestone"])
            self.assertIn("(ASSISTED, not a native milestone)", (run_dir / "report.md").read_text().splitlines()[0])
            self.assertTrue(json.loads((run_dir / "report.json").read_text())["assisted"])
            self.assertTrue(json.loads((run_dir / "hp_assist.json").read_text())["assisted"])
            self.assertIn("ASSISTED.json", report["artifacts"])

    def test_verdicts_written_after_the_assist_carry_the_label(self):
        ctx = SimpleNamespace(assisted=None)
        self.assertNotIn("assisted", RUN.label_assisted(ctx, {"pass": True}))
        RUN.mark_assisted(ctx, [0, 1], 12)
        self.assertEqual(RUN.label_assisted(ctx, {"pass": True})["assisted"]["label"], RUN.ASSISTED_LABEL)


def final_peek_context(game, counts, status, hooks):
    """context() whose n-th canonical peek first runs hooks[n] (1: the early read, 2: the final read before the
    commit gate, 3: the post-write audit), with live down counts and a live status pointer for the canonical row."""
    ctx = context(game)
    calls = [0]

    def peek(rva, kind="u32", index=0):
        calls[0] += 1
        hooks.get(calls[0], lambda: None)()
        return CANON
    ctx.namespace = lambda: {"log_count": lambda text, index=0: counts.get(text, 0), "peek": peek}
    ctx.actors = lambda index: [{"address": CANON, "name": "P_EX110_BTLF", "status": f"0x{status[0]:X}",
                                 "hp": game.memory[STATUS], "maxHp": game.memory[STATUS + 4]}]
    return ctx


class PokeGame(FakeGame):
    """FakeGame that runs `on_poke` as its poke lands (a down receipt arriving with the write)."""

    def __init__(self, on_poke, **kw):
        super().__init__(**kw)
        self.on_poke = on_poke

    def __call__(self, *args, pid=None, **kw):
        if args[0] == "poke":
            self.on_poke()
        return super().__call__(*args, pid=pid, **kw)


class FakeProc:
    """An owned helper that records whether teardown touched it."""

    def __init__(self):
        self.terminated = False

    def poll(self):
        return 0 if self.terminated else None

    def terminate(self):
        self.terminated = True

    def wait(self, timeout=None):
        return 0

    def kill(self):
        self.terminated = True


class FinalGateTest(unittest.TestCase):
    """Lead #115 after Ione's final round (VUH-2912 e735c0d2): her four remaining counterexamples, plus the audit."""

    def setUp(self):
        self.original, self.join = RUN.kh2ctl, RUN.HpAssist.JOIN_S

    def tearDown(self):
        RUN.kh2ctl, RUN.HpAssist.JOIN_S = self.original, self.join

    def tick(self, hooks, counts=None, status=None, assist_hook=None, hp=1):
        game = FakeGame(hp=hp)
        RUN.kh2ctl = game
        counts = {} if counts is None else counts
        status = [STATUS] if status is None else status
        assist = RUN.HpAssist(final_peek_context(game, counts, status, hooks), [0], 12)
        if assist_hook:
            hooks.update(assist_hook(assist))
        return assist, assist.tick(0), game

    def test_a_down_during_the_final_canonical_read_withholds_the_write(self):
        counts = {}
        _, event, game = self.tick({2: lambda: counts.update({RUN.DOWNED_DOWN: 1})}, counts=counts)
        self.assertEqual((event, game.pokes), (None, []))

    def test_a_stop_during_the_final_canonical_read_withholds_the_write(self):
        _, event, game = self.tick({}, assist_hook=lambda assist: {2: assist.stop.set})
        self.assertEqual((event, game.pokes), (None, []))

    def test_a_status_move_during_the_final_canonical_read_withholds_the_write(self):
        status = [STATUS]
        _, event, game = self.tick({2: lambda: status.__setitem__(0, STATUS + 0x1000)}, status=status)
        self.assertEqual((event, game.pokes), (None, []))  # never aimed at the old status

    def test_the_final_gate_still_writes_when_nothing_changed(self):
        _, event, game = self.tick({})
        self.assertEqual(game.pokes, [(4242, STATUS, "i32", 22)])
        self.assertNotIn("invalid", event)

    def test_a_down_landing_with_the_write_invalidates_the_refill(self):
        counts = {}
        game = PokeGame(lambda: counts.update({RUN.DOWNED_DOWN: 1}), hp=4)
        RUN.kh2ctl = game
        assist = RUN.HpAssist(final_peek_context(game, counts, [STATUS], {}), [0], 12)
        event = assist.tick(0)
        self.assertEqual((event["poked"], event["invalid"]), (True, "the seat downed around the write"))
        ctx = SimpleNamespace(hp_assist=assist, assisted=None, saved={}, artifacts=[])
        RUN.mark_assisted(ctx, [0], 12)
        assist.stop.set()
        assist.thread.start()  # exits at once: stop is already set
        result = RUN.stop_hp_assist(ctx, "off")
        self.assertEqual(len(result["invalid"]), 1)
        self.assertEqual(ctx.assisted["invalid"], result["invalid"])

    def test_a_canonical_change_landing_with_the_write_invalidates_the_refill(self):
        game = FakeGame(hp=4)
        RUN.kh2ctl = game
        ctx = final_peek_context(game, {}, [STATUS], {})
        peeks = iter([CANON, CANON, "0x7FF7014D3300"])  # early, final, audit
        ctx.namespace = lambda: {"log_count": lambda text, index=0: 0, "peek": lambda *a, **k: next(peeks)}
        event = RUN.HpAssist(ctx, [0], 12).tick(0)
        self.assertEqual(event["invalid"], "the canonical player changed around the write")

    def test_close_fails_closed_when_the_worker_will_not_halt(self):
        gate = threading.Event()
        game = LateGame(hp=6, gate=gate)
        RUN.kh2ctl, RUN.HpAssist.JOIN_S = game, 0.2
        with tempfile.TemporaryDirectory() as tmp:
            ctx = FakeContext(Path(tmp), game)
            ctx.test_only = {"player_hp_refill"}
            helper = FakeProc()
            ctx.processes.append(("runtime", helper, SimpleNamespace(close=lambda: None)))
            RUN.step_hp_assist(ctx, {"do": "hp_assist", "seats": [0], "floor": 12})
            self.assertTrue(game.entered.wait(2))
            with self.assertRaises(RUN.CloseFailed):
                ctx.close()
            self.assertFalse(helper.terminated)  # nothing torn down as if closure succeeded
            self.assertIsNotNone(ctx.hp_assist)  # handle and evidence retained
            self.assertIn("still running", ctx.assisted["closeError"])
            self.assertFalse(json.loads((Path(tmp) / "hp_assist.json").read_text())["workerExited"])
            gate.set()
            RUN.HpAssist.JOIN_S = 5
            ctx.close()
            self.assertTrue(helper.terminated)
            self.assertEqual(game.pokes, [])

    def test_an_invalid_refill_or_a_failed_closure_fails_the_run(self):
        counts = {}
        game = PokeGame(lambda: counts.update({RUN.DOWNED_DOWN: 1}), hp=9)
        scenario = {"name": "native_milestone_probe", "testOnly": ["player_hp_refill"],
                    "steps": [{"do": "hp_assist", "seats": [0], "floor": 12}, {"do": "wait", "ms": 600},
                              {"do": "hp_assist", "on": False}]}
        original_context, original_runs = RUN.Context, RUN.RUNS
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "native_milestone_probe.json"
            path.write_text(json.dumps(scenario), encoding="utf-8")
            RUN.kh2ctl, RUN.RUNS = game, Path(tmp) / "runs"
            RUN.Context = lambda run_dir: FakeContext(run_dir, game, counts)
            try:
                report = RUN.run_scenario(path, 1)
            finally:
                RUN.Context, RUN.RUNS = original_context, original_runs
            self.assertEqual(report["status"], "fail")
            self.assertIn("seat downed around the write", report["invalid"][0])
            self.assertIn("**INVALID:**", (Path(report["dir"]) / "report.md").read_text(encoding="utf-8"))


class IoneRound2ProbeTest(unittest.TestCase):
    """Ione's four final-round counterexamples (.local/ione-review/vuh2912-hp-assist-r2/probe.py, sent by her), with
    their bug assertions inverted to the fixed behaviour."""

    def setUp(self):
        self.original = RUN.kh2ctl

    def tearDown(self):
        RUN.kh2ctl = self.original

    def final_read_change(self, kind):
        game = FakeGame(hp=1)
        counts, canonical = {}, [CANON]
        ctx = live_context(game, counts, canonical)
        helper, calls = ctx.namespace(), [0]
        assist = RUN.HpAssist(ctx, [0], 12)
        original_actors, status = ctx.actors, [STATUS]

        def actors(index):
            rows = original_actors(index)
            rows[0]["status"] = hex(status[0])
            return rows
        ctx.actors = actors

        def peek(*args):
            calls[0] += 1
            if calls[0] == 2:  # the final canonical read, after the old HP reads and down-count check
                if kind == "down":
                    counts[RUN.DOWNED_DOWN] = 1
                elif kind == "stop":
                    assist.stop.set()
                else:
                    status[0] = STATUS + 0x1000
            return CANON
        helper["peek"] = peek
        RUN.kh2ctl = game
        return assist.tick(0), game

    def test_probe_down_during_the_final_canonical_rpc(self):
        event, game = self.final_read_change("down")
        self.assertEqual((event, game.pokes), (None, []))

    def test_probe_stop_during_the_final_canonical_rpc(self):
        event, game = self.final_read_change("stop")
        self.assertEqual((event, game.pokes), (None, []))

    def test_probe_status_move_during_the_final_canonical_rpc(self):
        event, game = self.final_read_change("status")
        self.assertEqual((event, game.pokes), (None, []))

    def test_probe_close_with_a_nonquiescent_assist(self):
        class JoinStub:
            def join(self, *args, **kwargs):
                pass

        class NeverExits:
            JOIN_S = 0
            events, error = [], ""

            def halt(self):
                return False
        teardown = []

        class Proc:
            def poll(self):
                return None

            def terminate(self):
                teardown.append("process terminated")

            def wait(self, *args, **kwargs):
                pass

        class Log:
            def close(self):
                pass
        ctx = object.__new__(RUN.Context)
        ctx.hp_assist, ctx.assisted, ctx.saved = NeverExits(), {"refills": 0}, {}
        ctx._stop, ctx._protector = threading.Event(), JoinStub()
        ctx.processes = [("sentinel", Proc(), Log())]
        with self.assertRaises(RUN.CloseFailed):
            RUN.Context.close(ctx)
        self.assertIsNotNone(ctx.hp_assist)
        self.assertEqual(teardown, [])
        self.assertIn("closeError", ctx.assisted)


class FakeContext(RUN.Context):
    """A real Context (its close, artifacts and run dir) whose seat 0 reads come from a FakeGame."""

    def __init__(self, run_dir, game, counts=None):
        super().__init__(run_dir)
        self.game = game
        self.fake = context(game) if counts is None else live_context(game, counts, [CANON])

    def inst(self, index):
        return self.fake.inst(index)

    def actors(self, index=0):
        return self.fake.actors(index)

    def namespace(self):
        return self.fake.namespace()


class AssistedSplitsTest(unittest.TestCase):
    """Each assisted split is its native split plus exactly two hp_assist steps, opted in and labelled."""

    def load(self, name):
        return json.loads((SCENARIOS / f"{name}.json").read_text(encoding="utf-8"))

    def test_assisted_splits_are_the_native_ones_with_the_assist_around_the_judged_part(self):
        for split, off in (("b1_inter", "m_interleaved1"), ("b2", "owner_downs")):
            with self.subTest(split):
                native = self.load(f"net_prologue_mansion_axel_hp_{split}")
                assisted = self.load(f"net_prologue_mansion_axel_hp_{split}_assisted")
                steps = assisted["steps"]
                on, stop = [i for i, s in enumerate(steps) if s["do"] == "hp_assist"]
                self.assertEqual([s for s in steps if s["do"] != "hp_assist"], native["steps"])
                self.assertEqual(steps[on], {"do": "hp_assist", "seats": [0, 1], "floor": 12})
                self.assertEqual(steps[stop], {"do": "hp_assist", "on": False, "as": "hp_assist"})
                self.assertEqual((steps[on - 1]["do"], steps[on - 1]["role"]), ("runtime", "friend1"))
                before_first_fight = next(i for i, s in enumerate(steps) if s["do"] == "fight")
                self.assertLess(on, before_first_fight)  # the room fight before Axel is covered (hp-13)
                if off == "owner_downs":
                    self.assertEqual(steps[stop + 1]["do"], "owner_downs")  # forced downs are never refilled
                else:
                    self.assertEqual(steps[stop - 1].get("as"), off)
                self.assertEqual(assisted["testOnly"], ["player_hp_refill"])
                self.assertTrue(assisted["description"].startswith("ASSISTED (lead #105)"))
                self.assertEqual(assisted["name"], f"net_prologue_mansion_axel_hp_{split}_assisted")
                RUN.validate_scenario(assisted)
                self.assertNotIn("testOnly", native)


if __name__ == "__main__":
    unittest.main()
