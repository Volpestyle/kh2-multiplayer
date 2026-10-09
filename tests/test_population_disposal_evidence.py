"""Socketless repair-06 receipt controls. Does not launch native tools or games."""
import copy
import json
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools" / "scenario"))
from population_disposal_evidence import PREFIX, check_outcomes


def receipt(action, ticket=1, reason="ok", **changes):
    row = dict(schema=1, seq=1, ticket=ticket, action=action, reason=reason,
               expected=1, observed=1, available=True, frame=100, netId=1,
               cut=100, deathSequence=90, load=2, transition=1,
               roots=[100, 200, 300, 400, 500], priorFirst=0, priorLast=0, priorRepeats=0)
    row.update(changes)
    return row


def stream(rows, *, seal=True):
    rows = copy.deepcopy(rows)
    for index, row in enumerate(rows, 1):
        row["seq"] = index
    text = "\n".join(PREFIX + json.dumps(row) for row in rows)
    if seal:
        calls = {(*r["roots"], r["load"], r["transition"]) for r in rows if r["action"] in ("call-returned", "call-fault")}
        text += f"\n[enemy-pop] disposal-outcome-seal seq={len(rows)} tickets={len({r['ticket'] for r in rows})} loss=0 attempts={len(calls)}"
    return text


def positive():
    return [receipt("produced"), receipt("attempted"), receipt("call-returned"), receipt("disposed", frame=101)]


class DisposalControls(unittest.TestCase):
    def test_positive(self):
        self.assertEqual(check_outcomes(stream(positive()))["status"], "PASS")

    def test_fault_and_reauthorization_preserve_poison(self):
        for action in ("call-returned", "call-fault"):
            rows = [receipt("produced"), receipt("attempted"), receipt(action), receipt("cancelled", reason="new-cut")]
            rows += [receipt("produced", 2, cut=101), receipt("waiting-native-removal", 2, "one-shot-poisoned", cut=101),
                     receipt("disposed", 2, cut=101, frame=101)]
            self.assertEqual(check_outcomes(stream(rows))["status"], "PASS")

    def test_reauthorization_cannot_repeat_native_call(self):
        rows = positive() + [receipt("produced", 2, cut=101, frame=102), receipt("attempted", 2, cut=101, frame=102)]
        self.assertEqual(check_outcomes(stream(rows))["status"], "FAIL")

    def test_final_read_refusal_is_a_complete_outcome(self):
        rows = [receipt("produced"), receipt("attempted"), receipt("refused", reason="dispatch-status", observed=301)]
        self.assertEqual(check_outcomes(stream(rows))["status"], "PASS")

    def test_hp_zero_and_native_return_do_not_qualify_disposal(self):
        for rows in ([receipt("produced"), receipt("waiting-native-removal", reason="native-hp-nonpositive"), receipt("disposed")],
                     positive()[:-1]):
            self.assertEqual(check_outcomes(stream(rows))["status"], "INCONCLUSIVE")

    def test_coverage_and_identity_adversaries(self):
        base = stream(positive())
        mutants = [PREFIX + "[]", PREFIX + "{invalid", base.replace('"seq": 2', '"seq": 3'), base + "\n[enemy-pop] receipt-loss disposal-outcome-cap loss=1",
                   base.replace("loss=0", "loss=1"), base.replace("attempts=1", "attempts=0"),
                   base.replace("tickets=1", "tickets=2"), stream(positive(), seal=False)]
        for field, value in (("roots", [100, 200, 301, 400, 500]), ("cut", 101), ("deathSequence", 91),
                             ("available", 1), ("expected", None), ("reason", ""), ("ticket", True), ("schema", True), ("frame", 100)):
            rows = positive(); rows[-1][field] = value; mutants.append(stream(rows))
        for text in mutants:
            with self.subTest(text=text):
                self.assertEqual(check_outcomes(text)["status"], "INCONCLUSIVE")

    def test_unsealed_live_prefix_may_be_checked_without_accepting_completion(self):
        text = stream(positive()[:-1], seal=False)
        self.assertEqual(check_outcomes(text, require_seal=False)["status"], "PASS")
        self.assertEqual(check_outcomes(text)["status"], "INCONCLUSIVE")


if __name__ == "__main__":
    unittest.main()
