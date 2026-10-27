"""VUH-2911: the generated Axel II fixtures' own assertions, evaluated against synthetic run data, with negative
controls (Ione 61ea3f02): a clone-hit change to any SAVE character-record or form-row byte (not only the first
eight), an expired admission hold, and a seat whose selected body is not back after Continue. No game.

    python -B -m unittest discover -s tests -p test_axel_form_fixture.py -v
"""
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCENARIOS = ROOT / "tools/scenario/scenarios"


def load(name):
    return json.loads((SCENARIOS / f"{name}.json").read_text(encoding="utf-8"))["steps"]


def assertion(steps, fragment):
    found = [s["expr"] for s in steps if s["do"] == "assert" and fragment in s.get("message", "")]
    assert len(found) == 1, (fragment, len(found))
    return found[0]


def held(saved, logs=None):
    counts = logs or {}
    return lambda expr: eval(expr, {"saved": saved, "log_count": lambda text, i: counts.get((text, i), 0)})


def isolation(diff=None, local_after=20):
    diffs = {"characters": [], "forms": []}
    if diff:
        diffs[diff[0]] = [{"saveOffset": diff[1], "before": 0, "after": 1}]
    return {"probe_before": [{"local": 20}, {"local": 20}], "probe_after": [{"local": local_after}, {"local": 20}],
            "hit_after": {"instances": {"0": {"diffs": diffs}, "1": {"diffs": {"characters": [], "forms": []}}}}}


class AxelFormFixtureTest(unittest.TestCase):
    def test_clone_hit_isolation_covers_every_record_byte(self):
        expr = assertion(load("net_prologue_mansion_axel_form"), "a hit on a clone changed")
        self.assertTrue(held(isolation())(expr))
        # Byte 0x10 of the first character record, the last byte of the character range, and form 10's row.
        for diff in (("characters", 0x24F0 + 0x10), ("characters", 0x24F0 + 0xE03), ("forms", 0x34EC + 2), ("forms", 0x32F4)):
            self.assertFalse(held(isolation(diff))(expr), diff)
        self.assertFalse(held(isolation(local_after=19))(expr))

    def test_isolation_snapshots_read_the_full_rows_around_the_hits(self):
        steps = load("net_prologue_mansion_axel_form")
        names = [s.get("as") for s in steps]
        before, after = steps[names.index("hit_before")], steps[names.index("hit_after")]
        for snap in (before, after):
            self.assertEqual((snap["only"], snap["extraRanges"]), (["characters", "forms"], [["forms", 0x32F4, 0x230]]))
        self.assertEqual(after["compare"], "hit_before")
        self.assertLess(names.index("hit_before"), names.index("hit0"))
        self.assertLess(names.index("hit1"), names.index("hit_after"))

    def test_an_expired_hold_is_a_setup_failure(self):
        expr = assertion(load("net_prologue_mansion_axel_form"), "SETUP FAILURE")
        self.assertTrue(held({"window_open": [1, 0], "window_close": 0})(expr))
        self.assertFalse(held({"window_open": [1, 1], "window_close": 1})(expr))  # already released at the start
        self.assertFalse(held({"window_open": [1, 0], "window_close": 1})(expr))  # released inside the window
        self.assertFalse(held({"window_open": [0, 0], "window_close": 0})(expr))  # never held

    def test_owner_downs_follow_the_release_and_tolerate_a_natural_down(self):
        for name in ("net_prologue_mansion_axel_form", "net_prologue_mansion_axel_form_sora_host",
                     "net_prologue_mansion_axel_form_sora_friend"):
            steps = load(name)
            dos = [s["do"] for s in steps]
            release = next(i for i, s in enumerate(steps) if "released admitted" in s.get("expr", "") and s["do"] == "wait_until")
            downs = dos.index("owner_downs")
            self.assertEqual(downs, release + 1, name)  # right after the admission release (lead decision)
            self.assertLess(downs, next(i for i, s in enumerate(steps) if s.get("args", [None])[0] == "player-input"), name)
            self.assertEqual(steps[downs]["owners"], [1, 0])
            self.assertNotIn("revive_guard", dos)
            kills = [s for s in steps if s["do"] == "downed_kill"]
            self.assertEqual([(s["instance"], s["require"]) for s in kills], [(1, False), (0, False)], name)
            expr = assertion(steps, "each owner must be downed once")
            ok = {"owner_downs": {"owners": {"0": {"kind": "natural"}, "1": {"kind": "forced"}}},
                  "downs_after": [{"btlf": 2 if name.endswith("form") else 1}] * 2}
            self.assertTrue(held(ok)(expr), name)
            self.assertFalse(held({**ok, "owner_downs": {"owners": {"0": None, "1": {"kind": "forced"}}}})(expr), name)

    def census(self, member0, roxas=0, sora=0, btlf=0, w=0):
        return {"btlf": btlf, "roxas": roxas, "sora": sora, "w10": w, "w20": w, "member0": member0}

    def test_each_selection_is_back_after_continue_in_both_seat_orders(self):
        exit_line = "[prologue] encounter exit at=02/15"
        for name, sora_seat in (("net_prologue_mansion_axel_form_sora_host", 0), ("net_prologue_mansion_axel_form_sora_friend", 1)):
            expr = assertion(load(name), "after Continue each seat")
            good = [self.census(0x54, sora=1) if i == sora_seat else self.census(0x5A, roxas=1) for i in (0, 1)]
            logs = {(exit_line, sora_seat): 1}
            self.assertTrue(held({"retired": good}, logs)(expr), name)
            native = [self.census(0x5A, roxas=1), self.census(0x5A, roxas=1)]
            self.assertFalse(held({"retired": native}, logs)(expr), name)            # Sora not restored
            self.assertFalse(held({"retired": good}, {})(expr), name)                # no exit receipt
            pair = [dict(c, w10=1) for c in good]
            self.assertFalse(held({"retired": pair}, logs)(expr), name)              # an orphan keyblade
        expr = assertion(load("net_prologue_mansion_axel_form"), "after Continue each seat")
        both = [self.census(0x5A, roxas=1), self.census(0x5A, roxas=1)]
        self.assertTrue(held({"retired": both})(expr))
        self.assertFalse(held({"retired": both}, {(exit_line, 0): 1})(expr))         # a Roxas seat never swaps

    def test_mixed_constructions_and_census(self):
        host = load("net_prologue_mansion_axel_form_sora_host")
        born = assertion(host, "each seat must build one bound tt20-137 pair")
        # The Sora host's clone is the friend's dual-wield Roxas; the Roxas friend's clone is Sora.
        self.assertIn(r"cloneObject=803 cloneKey=14 local=\S+ localKey=1 ', 0)", born)
        self.assertIn(r"cloneObject=84 cloneKey=1 local=\S+ localKey=14 ', 1)", born)
        census = assertion(host, "each screen must hold one body per seat")
        mixed = [self.census(0x54, sora=1, btlf=1, w=1), self.census(0x323, sora=1, btlf=1, w=1)]
        self.assertTrue(held({"census_axel": mixed})(census))
        self.assertFalse(held({"census_axel": [self.census(0x323, btlf=2, w=2), mixed[1]]})(census))
        envs = [s.get("env", {}).get("KH2COOP_PROFILE") for s in host if s["do"] == "boot"]
        self.assertEqual(envs, ["0x54", None])


if __name__ == "__main__":
    unittest.main()
