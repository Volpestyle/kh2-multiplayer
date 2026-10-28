"""Boss HP ledger (VUH-2910): host native hits and friend claims tile one host HP pool."""
import importlib.util
import os
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location(
    "boss_hp_ledger", os.environ.get("BOSS_HP_LEDGER", ROOT / "tools" / "scenario" / "boss_hp_ledger.py"))
boss_hp_ledger = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(boss_hp_ledger)

BOSS, HOST_ME, FRIEND_ME, PUPPET, OTHER = "7FF6274329C0", "7FF62741A1D0", "7FF62741A1D0", "7FF6274019E0", "7FF627433000"
SOURCE = HOST_ME  # the attacking canonical player


def hit(seq, victim, victim_object, source_object, before, after, victim_class, source_class, action, reason,
        zero_attempted=0, claim_queued=0, zero_result=0, sync_drop=0, damage=None, damage_after=None, attack_id=1,
        epoch=2, frame=4000, source=SOURCE, after_actor=None, after_epoch=None, hit_mask_after=255,
        victim_mask=511, event=None, seat="host"):
    """One ApplyHitDamage scope in the order and shape run 20261027-101224 logged: the hittrace event, both
    phases' context/actors/hit record with complete reads (context 127, actors 511, hit record 255) and stable
    identity, then the damagepolicy event and its authority rows (the phase-0 capture). Context and policy role,
    slot and connection are the seat's (host role 1 slot 0, friend role 2 slot 1). `damage`/`damage_after` are
    the hit record's amount before and after the call; a sync-dropped claim zeroes it before DamagePolicy,
    which then reports Changed. The remaining keywords break one piece of that proof."""
    damage = before - after if damage is None else damage
    damage_after = damage if damage_after is None else damage_after
    role, slot, connection = (1, 0, 1) if seat == "host" else (2, 1, 2)
    row = lambda tag, kind, f: f"[{tag}] {kind} schema=1 seq={seq} " + " ".join(f"{k}={v}" for k, v in f.items())
    flags = {"parent": 0, "depth": 1, "coverageSerial": 1, "coverageMask": 7, "lossSerial": 0, "callerRva": "410EFF",
             "callerAvailable": 1, "rawResult": 96, "ownerThread": 1, "returned": 1, "unwound": 0, "nested": 0,
             "overflow": 0, "coverageStable": 1, "contextStable": 1, "metadataStable": 1, "lossStable": 1, "witness": 0,
             "takeCalls": 0, "statCalls": 1, "childCount": 1, **(event or {})}
    lines, captured = [row("hittrace", "event", flags)], {}
    for phase, hp, amount in ((0, before, damage), (1, after, damage_after)):
        context = {"phase": phase, "readMask": 127, "available": 1, "frame": frame, "generation": 2,
                   "epoch": after_epoch if phase and after_epoch else epoch, "transitionSerial": 2, "loadSerial": 3,
                   "connectionId": connection, "hostConnectionId": 1, "role": role, "slot": slot,
                   "location": "000200050000005700570057"}
        target = {"phase": phase, "subject": 0, "actor": after_actor if phase and after_actor else victim,
                  "objentry": "7FF6089BA978", "status": "7FF6089930A8", "objectId": victim_object,
                  "readMask": victim_mask, "type": 3, "team": 2, "hp": hp, "maxHp": 150, "namePrefix": "5F42"}
        attacker = {"phase": phase, "subject": 1, "actor": source, "objentry": "7FF608996AF8", "status": "7FF608993320",
                    "objectId": source_object, "readMask": 511, "type": 0, "team": 1, "hp": 22, "maxHp": 22,
                    "namePrefix": "5F50"}
        record = {"phase": phase, "hit": "7FF6089949F0", "attack": "7FF6085E07A0", "owner": source,
                  "canonicalPlayer": "7FF60847A1D0", "head": "7FF60847A1D0", "tracked": "7FF60847A1D0",
                  "flags": 2 * phase, "attackHandle": 2220754848, "atkpHandle": 2224983944, "ownerHandle": 2219286992,
                  "attackId": attack_id, "readMask": hit_mask_after if phase else 255, "damage": amount, "stat": 0,
                  "kind": 0, "syncDrop": sync_drop, "manualFilterOn": 0, "manualDrop": 0}
        lines += [row("hittrace", "context", context), row("hittrace", "actor", target),
                  row("hittrace", "actor", attacker), row("hittrace", "input", record)]
        if phase == 0:
            captured = {"context": context, "victim": target, "source": attacker, "input": {**record, "syncDrop": 0}}
    lines.append(row("damagepolicy", "event", {
        "recorded": 1, "role": role, "ownerThread": 1, "contextAvailable": 1, "victimClass": victim_class,
        "sourceClass": source_class, "flags": 0, "readMask": 15, "amount": damage, "stat": 0, "kind": 0,
        "action": action, "reason": reason, "supported": 1, "roster0": 1, "roster1": 2, "roster2": 0,
        "revalidationAttempted": 1, "revalidationPassed": 1, "zeroAttempted": zero_attempted,
        "zeroResult": zero_result, "claimAttempted": claim_queued, "claimQueued": claim_queued}))
    lines += [row("damagepolicy", "context", captured["context"]), row("damagepolicy", "input", captured["input"]),
              row("damagepolicy", "actor", captured["victim"]), row("damagepolicy", "actor", captured["source"])]
    return lines


CLAIM = {"connection": 11, "seq": 1, "epoch": 2, "netId": 1, "objectId": 2229, "attackId": 1, "damage": 7}


def claim(role, before=None, after=None, strip=(), **override):
    keys = {k: v for k, v in {**CLAIM, **override}.items() if k not in strip}
    line = f"[enemysync] {role} hit claim " + " ".join(f"{k}={v}" for k, v in keys.items())
    return line + (f" beforeHp={before} afterHp={after} nativeOk=1 afterHpAvailable=1" if role == "host" else "")


def tick(t):
    return f"[statehash] role=host epoch=2 frame=1 room=02/05 door=0 map=87 btl=87 evt=87 tickMs={t}"


def native(hp, actor=BOSS, net_id=1, epoch=2):
    return f"[statehash] native epoch={epoch} frame=1 netId={net_id} objectId=2229 hp={hp} actor={actor}"


def mirror(what="actor", updates=60, skips=60, actor=BOSS, net_id=1):
    return (f"[enemy-mirror] {what} frame=900 netId={net_id} actor={actor} running=1 runStart=841 updates={updates} "
            f"skips={skips} passBound=0 sets=0 blocked=0 runs=1 gaps=0 restarts=0 settle=60 noBrain=0 "
            f"maxNoBrainRun=0 missMotion=-1 motion=2 cursor=1 objectId=2229")


SCALE_LINE = f"[coop-difficulty] scale frame=3564 objectId=2229 actor={BOSS} players=2 mode=1 hp=115->150 max=115->150"
BINDING_LINE = (f"[enemy-binding] epoch=2 frame=781 spawn=0 actor={BOSS} objentry=7FF6089BA978 status=7FF6089930A8 "
                "controller=7FF608A58130 record=7FF60892CEF4 objectId=2229 prior=0 floor=0 candidate=1 selected=1 "
                "reason=newest-live-point conflict=0 sampledIdentityOnly=1")
FRIEND_CTZ = {"action": 2, "claim_queued": 1, "zero_attempted": 1, "zero_result": 2, "sync_drop": 1, "damage_after": 0}


def build(host_hit_after=142, puppet_hp_after=None, friend_local_after=None, brain_skips=60, host_contributes=True,
          friend_contributes=True, extra_client_claim=False, strip=(), claim_override=None, friend_actor=BOSS,
          friend_net_id=1, friend_policy=None, claim_before_window=False, phases=None, friend_evidence_actor=None, unrecorded=0, host_taken=True, friend_taken=True, friend_hit=None, host_hit=None,
          player_hit=None, puppet_hit=None, clone_hit=None):
    """Host-only then friend-only, as the positive case. The friend's local hit is a sync-dropped claim
    (zeroResult Changed with the record zeroed), as in run 20261027-101224. `clone_hit` (VUH-2912) adds the host
    boss's hit on the host's copy of the friend in the friend phase: a checked RemoteVictim zero, then the changes."""
    claim_override = claim_override or {}
    bound, friend_actor = friend_actor, friend_evidence_actor or friend_actor
    # The real order: the previous room's one-player scale and arrival, then this room's scale before arrival.
    host = ["[coop-difficulty] room role=host mode=1 players=1 mob=1000 boss=1000",
            "[enemysync] host arrived epoch=1 room=02/02 door=99 map=1 btl=0 evt=0",
            "[coop-difficulty] room role=host mode=1 players=2 mob=1600 boss=1300",
            "[warp] load complete serial=2 transition=1 room=02/05 door=0 map=87 btl=87 evt=87",
            "[prologue] hold at=02/05 evt=87 site=tt05-87 localBound=1", SCALE_LINE,
            "[enemysync] host arrived epoch=2 room=02/05 door=0 map=87 btl=87 evt=87",
            "[prologue] control released at=02/05 evt=87 site=tt05-87 localBound=1", native(150), tick(1000)]
    friend = [BINDING_LINE.replace(BOSS, bound),
              mirror("take-over", 0, 0, friend_actor, friend_net_id),
              "[prologue] control released at=02/05 evt=87 site=tt05-87 localBound=1", tick(1100)]
    sent = claim("client", strip=strip, **claim_override)
    if claim_before_window:
        friend.append(sent)
    marks = {"start": [len(host), len(friend)]}
    hp = 150
    # Host only: a native hit, a guard (no HP change), and the boss hits the host.
    if host_contributes:
        host += hit(1, BOSS, 2229, 0, hp, host_hit_after, 5, 1, 0, 11, **(host_hit or {}))
        hp = host_hit_after
    host += hit(2, BOSS, 2229, 0, hp, hp, 5, 1, 0, 11)
    if host_taken:
        host += hit(3, HOST_ME, 0, 2229, 22, 16, 1, 5, 0, 0, **{"source": BOSS, **(player_hit or {})})
    host += [native(hp), tick(2000)]
    hp -= unrecorded  # a writer with no hit record
    marks["m1"] = [len(host), len(friend)]
    # Friend only: its local hit claims and zeroes; the host applies the claim; its puppet's swing on the host zeroes.
    if friend_contributes:
        policy = {**FRIEND_CTZ, **(friend_policy or {})}
        after = hp if friend_local_after is None else friend_local_after
        friend += hit(1, friend_actor, 2229, 0, hp, after, 5, 1, policy.pop("action"), 13, damage=7, seat="friend",
                      **policy, **(friend_hit or {}))
        if not claim_before_window:
            friend.append(sent)
        host += [claim("host", hp, hp - 7, strip=strip, **claim_override)]
        hp -= 7
    if extra_client_claim:
        ctz = {k: v for k, v in FRIEND_CTZ.items() if k != "action"}
        friend += hit(5, friend_actor, 2229, 0, hp, hp, 5, 1, 2, 13, damage=7, attack_id=2, seat="friend", **ctz)
        friend.append(claim("client", seq=2, attackId=2))
    friend.append(mirror(skips=brain_skips, actor=friend_actor, net_id=friend_net_id))
    if friend_taken:
        friend += hit(2, FRIEND_ME, 0, 2229, 22, 16, 1, 5, 0, 0, seat="friend",
                      **{"source": friend_actor, **(player_hit or {})})
    if clone_hit is not None:
        zero = {"before": 22, "after": 22, "action": 1, "zero_attempted": 1, "zero_result": 0, **clone_hit}
        before, after, action = zero.pop("before"), zero.pop("after"), zero.pop("action")
        host += hit(6, PUPPET, 0, 2229, before, after, 2, 5, action, 7, damage=6, **{"source": BOSS, **zero})
    host += hit(4, BOSS, 2229, 0, hp, hp if puppet_hp_after is None else puppet_hp_after, 5, 2, 1, 8, 1, damage=7,
                damage_after=0, source=PUPPET, **(puppet_hit or {}))
    host += [native(hp), tick(3000)]
    friend += [native(hp, friend_actor, friend_net_id), tick(3100)]
    marks["end"] = [len(host), len(friend)]
    host += [native(hp), tick(4000)]
    friend += [native(hp, friend_actor, friend_net_id), tick(4100)]
    marks = {"marks": marks, "phases": phases if phases is not None else
             [{"name": "host_only", "from": "start", "to": "m1", "host": True, "friend": False},
              {"name": "friend_only", "from": "m1", "to": "end", "host": False, "friend": True}]}
    return "\n".join(host), "\n".join(friend), marks


def run(**changes):
    host, friend, marks = build(**changes)
    return boss_hp_ledger.ledger(host, friend, 2229, marks)


INTERLEAVED = [{"name": "interleaved", "from": "start", "to": "end", "host": True, "friend": True}]


def edited(*edits, **changes):
    """The interleaved build() with line edits. Each (seat, needles, replace) replaces the first line containing
    every needle with replace(line): [] deletes it, several lines insert. Marks after it shift, so each judged
    window keeps its lines."""
    host, friend, marks = build(**{"phases": INTERLEAVED, **changes})
    seats = {"host": host.split("\n"), "friend": friend.split("\n")}
    for seat, needles, replace in edits:
        lines, col = seats[seat], ("host", "friend").index(seat)
        i = next(i for i, line in enumerate(lines) if all(n in line for n in needles))
        new = replace(lines[i])
        lines[i:i + 1] = new
        for mark in marks["marks"].values():
            mark[col] += len(new) - 1 if mark[col] > i else 0
    return boss_hp_ledger.ledger("\n".join(seats["host"]), "\n".join(seats["friend"]), 2229, marks)


def drop(*names):
    """A line edit removing `name=value` fields."""
    return lambda line: [" ".join(w for w in line.split(" ") if "=" not in w or w.split("=")[0] not in names)]


def sub(**values):
    """A line edit setting `name=value` fields."""
    def edit(line):
        words = line.split(" ")
        return [" ".join(f"{w.split('=')[0]}={values[w.split('=')[0]]}" if "=" in w and w.split("=")[0] in values
                         else w for w in words)]
    return edit


class BossHpLedgerTest(unittest.TestCase):
    def fails(self, text, **changes):
        v = run(**changes)
        self.assertFalse(v["pass"], v)
        self.assertTrue(any(text in p for p in v["problems"]), v["problems"])
        return v

    def test_both_seats_damage_one_pool_once(self):
        v = run()
        self.assertTrue(v["pass"], v["problems"])
        self.assertEqual(v["hp"], {"start": 150, "settled": 135, "friendSettled": 135, "decrease": 15, "sum": 15})
        c = v["counts"]
        self.assertEqual((c["hostNativeHits"], c["friendClaimsApplied"], c["hostRefusals"], c["puppetZeroed"]), (1, 1, 1, 1))
        self.assertEqual((c["bossHitsOnHost"], c["bossHitsOnFriend"], c["friendLocalClaimZeros"]), (1, 1, 1))
        self.assertEqual([(p["host"], p["friend"]) for p in v["phases"]], [(1, 0), (0, 1)])
        self.assertEqual(v["scale"]["max1"], "150")
        self.assertEqual(v["timing"]["host"]["release"]["tickMs"], 1000)
        self.assertEqual(v["boss"], {"epoch": 2, "netId": 1, "hostActor": BOSS, "friendActor": BOSS})

    def test_interleaved_round_needs_both_contributors(self):
        self.assertTrue(run(phases=INTERLEAVED)["pass"])
        self.fails("phase interleaved: 0 successful host hits", phases=INTERLEAVED, host_contributes=False)
        self.fails("phase interleaved: 0 successful friend hits", phases=INTERLEAVED, friend_contributes=False)

    def test_interleaved_phase_requires_a_boss_hit_on_each_player(self):
        v = run(phases=INTERLEAVED)
        self.assertEqual((v["phases"][0]["bossHitsOnHost"], v["phases"][0]["bossHitsOnFriend"]), (1, 1))
        self.fails("phase interleaved: native boss hits on the canonical players: host 1, friend 0", phases=INTERLEAVED, friend_taken=False)
        self.fails("phase interleaved: native boss hits on the canonical players: host 0, friend 1", phases=INTERLEAVED, host_taken=False)

    def test_solo_phases_report_player_hits_without_requiring_them(self):
        v = run(friend_taken=False)
        self.assertTrue(v["pass"], v["problems"])
        self.assertEqual([(p["bossHitsOnHost"], p["bossHitsOnFriend"]) for p in v["phases"]], [(1, 0), (0, 0)])
        self.fails("native boss hits on the canonical players: host 1, friend 0", phases=[], friend_taken=False)

    def test_unrecorded_writer_breaks_the_chain(self):
        self.fails("chain gap", unrecorded=2)

    def test_puppet_replayed_hit_fails(self):
        self.fails("puppet", puppet_hp_after=128)

    def test_friend_local_hp_write_fails(self):
        self.fails("friend copy HP changed locally", friend_local_after=135)

    def test_friend_brain_running_fails(self):
        self.fails("brain not skipped", brain_skips=59)

    # Rhea's false passes (VUH-2910 review bebc234f): each passed before the stricter join.
    def test_unapplied_client_claim_fails(self):
        self.fails("never applied", extra_client_claim=True)

    def test_claim_without_identity_fields_fails(self):
        self.fails("lacks", strip=("epoch", "netId", "attackId"))

    def test_claims_relabelled_to_another_lifetime_fail(self):
        self.fails("not the boss lifetime", claim_override={"epoch": 999, "netId": 999})

    def test_friend_evidence_from_another_incarnation_fails(self):
        self.fails("friend", friend_actor=OTHER, friend_net_id=999)

    def test_friend_evidence_split_across_incarnations_fails(self):
        self.fails("friend", friend_evidence_actor=OTHER)

    def test_friend_native_unclaimed_hit_fails(self):
        self.fails("claim-then-zero", friend_policy={"action": 0, "claim_queued": 0, "zero_attempted": 0, "zero_result": 1,
                                                     "sync_drop": 0, "damage_after": 7})

    def test_friend_claim_with_failed_zero_fails(self):
        self.fails("claim-then-zero", friend_policy={"zero_result": 4})
        self.fails("claim-then-zero", friend_policy={"sync_drop": 0})

    # Rhea's delta review (b2b6aac6): each passed at 92345c6.
    def test_unread_zero_is_not_a_sync_drop(self):
        self.fails("hit record phase 1 unread", friend_hit={"hit_mask_after": 251})

    def test_friend_scope_switching_lifetime_mid_call_fails(self):
        self.fails("incomplete proof", friend_hit={"after_actor": OTHER, "after_epoch": 999})

    def test_player_hits_from_another_lifetime_do_not_count(self):
        self.fails("phase interleaved: native boss hits on the canonical players: host 0, friend 0",
                   phases=INTERLEAVED, player_hit={"epoch": 999})

    # The same read-validity and lifetime pattern for the other trusted fields.
    def test_host_hit_with_unread_victim_hp_fails(self):
        self.fails("actor phase 0 subject 0 unread", host_hit={"victim_mask": 511 - 32})

    def test_puppet_zero_on_an_unstable_scope_fails(self):
        self.fails("incomplete hit proof: event flags ['contextStable']", puppet_hit={"event": {"contextStable": 0}})

    def test_player_hit_from_another_actor_does_not_count(self):
        self.fails("phase interleaved: native boss hits on the canonical players: host 0, friend 0",
                   phases=INTERLEAVED, player_hit={"source": OTHER})

    # Rhea's final sweep (191d3b62): each control passed at 1720c54.
    def assertFails(self, v, text):
        self.assertFalse(v["pass"], v)
        self.assertTrue(any(text in p for p in v["problems"]), v["problems"])

    def test_interleaved_edit_baseline_passes(self):
        v = edited()
        self.assertTrue(v["pass"], v["problems"])

    def test_scope_missing_its_policy_or_victim_snapshot_fails(self):
        self.assertFails(edited(("host", ("[damagepolicy] event", "seq=4 "), lambda line: [])), "missing components")
        self.assertFails(edited(("host", ("[hittrace] actor", "seq=4 ", "phase=1 subject=0"), lambda line: [])),
                         "missing components")

    def test_conflicting_duplicate_policy_fails(self):
        conflict = lambda line: [line, sub(sourceClass=1, action=0)(line)[0]]
        self.assertFails(edited(("host", ("[damagepolicy] event", "seq=4 "), conflict)), "duplicate components")

    def test_fields_missing_from_both_phases_fail(self):
        self.assertFails(edited(("friend", ("[hittrace] input", "seq=1 ", "phase=0"), drop("hit", "attack", "owner")),
                                ("friend", ("[hittrace] input", "seq=1 ", "phase=1"), drop("hit", "attack", "owner"))),
                         "hit record phase 0 unread")
        self.assertFails(edited(("host", ("[hittrace] context", "seq=1 ", "phase=0"), drop("location")),
                                ("host", ("[hittrace] context", "seq=1 ", "phase=1"), drop("location"))),
                         "context changed across the call ['location']")

    def test_unstable_hit_handles_fail(self):
        self.assertFails(edited(("friend", ("[hittrace] input", "seq=1 ", "phase=1"),
                                 sub(attackHandle=999, atkpHandle=999, ownerHandle=999, kind=999, stat=999))),
                         "hit record identity changed across the call")

    def test_policy_stat_not_the_capture_fails(self):
        self.assertFails(edited(("host", ("[damagepolicy] event", "seq=1 "), sub(stat=1))),
                         "policy decision input is not the capture ['stat']")

    def test_contradictory_policy_authority_fails(self):
        self.assertFails(edited(("host", ("[damagepolicy] context", "seq=1 "), sub(epoch=999)),
                                ("host", ("[damagepolicy] actor", "seq=1 ", "subject=0"), sub(actor=OTHER, objectId=999))),
                         "policy context is not the capture ['epoch']")
        added = lambda line: [line, sub(epoch=999)(line)[0]]
        self.assertFails(edited(("host", ("[damagepolicy] context", "seq=1 "), added)), "duplicate components")

    def test_unverified_hook_coverage_fails(self):
        self.assertFails(edited(("host", ("[hittrace] event", "seq=1 "), sub(coverageMask=0))),
                         "event flags ['coverageMask']")

    def test_binding_lost_inside_the_window_fails(self):
        lost = lambda line: [sub(selected=0, conflict=1)(BINDING_LINE)[0], line]
        self.assertFails(edited(("friend", ("[hittrace] event", "seq=1 "), lost)), "friend binding lost or changed")

    def test_binding_after_the_window_does_not_certify_it(self):
        self.assertFails(edited(("friend", ("[enemy-binding]",), lambda line: []),
                                ("friend", ("tickMs=4100",), lambda line: [line, BINDING_LINE])),
                         "friend copy not bound to netId 1")

    def test_unrelated_or_one_player_scale_fails(self):
        v = edited(("host", ("[coop-difficulty] scale",), sub(actor=OTHER, players=1)))
        self.assertFails(v, "no two-player scale")
        self.assertIsNone(v["scale"])

    def test_claim_sent_before_the_window_fails(self):
        self.fails("outside the friend window", claim_before_window=True)

    # VUH-2912: the host boss's hit on the host's copy of the friend is a checked zero; its owner takes it natively.
    def test_boss_hit_on_the_remote_copy_zeroes(self):
        v = run(clone_hit={})
        self.assertTrue(v["pass"], v["problems"])
        self.assertEqual(v["counts"]["bossHitsOnClone"], 1)
        self.assertEqual([p["bossHitsOnClone"] for p in v["phases"]], [0, 1])

    def test_boss_hit_damaging_the_remote_copy_fails(self):
        self.fails("was not a checked zero", clone_hit={"after": 16, "action": 0, "zero_attempted": 0})

    def test_boss_hit_on_the_remote_copy_with_a_failed_zero_fails(self):
        self.fails("was not a checked zero", clone_hit={"zero_result": 1})

    def test_incomplete_remote_copy_scope_fails(self):
        # Ione's probe (24860051): a logged 22->16 loss behind an incomplete victim read passed as "unproven".
        self.fails("has incomplete proof", clone_hit={"after": 16, "action": 0, "zero_attempted": 0, "victim_mask": 7})
        self.fails("has incomplete proof", clone_hit={"victim_mask": 7})

    def test_clone_zero_phase_requires_a_complete_checked_zero(self):
        zero = [{"name": "host_only", "from": "start", "to": "m1", "host": True, "friend": False},
                {"name": "friend_only", "from": "m1", "to": "end", "host": False, "friend": True, "cloneZero": True}]
        self.assertTrue(run(phases=zero, clone_hit={})["pass"])
        self.fails("phase friend_only: no complete checked zero", phases=zero)
        self.fails("has incomplete proof", phases=zero, clone_hit={"victim_mask": 7})

    def test_targeting_phase_requires_the_chosen_seat_to_be_hit(self):
        target = [{"name": "host_only", "from": "start", "to": "m1", "host": True, "friend": False, "hitOnHost": True},
                  {"name": "friend_only", "from": "m1", "to": "end", "host": False, "friend": True, "hitOnFriend": True}]
        self.assertTrue(run(phases=target)["pass"])
        self.fails("phase friend_only: no native boss hit on the friend's canonical player", phases=target, friend_taken=False)
        self.fails("phase host_only: no native boss hit on the host's canonical player", phases=target, host_taken=False)


if __name__ == "__main__":
    unittest.main()
