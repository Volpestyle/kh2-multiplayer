"""Local synthetic/archived log controls; never launches or reads KH2."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("trace_audit", ROOT / "tools/scenario/trace_audit.py")
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


def line(family, record_kind, **fields):
    return f"[{family}] {record_kind} " + " ".join(f"{k}={v}" for k, v in fields.items()) + "\n"


def states(family, seq, phases):
    text = ""
    for phase in phases:
        text += line(family, "state", seq=seq, phase=phase, controllerAvailable=1,
                     header="140100000", spawnArray="14010002C", key=9, headerId=115,
                     nativeType=2, records=6, flags=0, stage=0, currentCountU32=4,
                     initialCountU32=4, cooldown=0, activation=1, cacheAvailable=1,
                     cacheBucket="142AE5E60", cacheRoom=11, cacheAge=1)
        for chunk in range(8):
            text += line(family, "cache", seq=seq, phase=phase, chunk=f"{chunk}/8", idsU16Hex="0000" * 32)
    return text


def spawn_event(seq=1, **overrides):
    fields = dict(seq=seq, tick=1, wrapper="fixed", wrapperOutcome="observed", wrapperComplete=1,
                  callerRva="3FE83F", callerRvaAvailable=1, outcome="observed", reason="native observed actor",
                  captureRole=1, roleAvailable=1, drainRole=1, drainGeneration=1, transition=2, load=3,
                  postTransition=2, postLoad=3, stampAvailable=1, postStampAvailable=1,
                  enclosingTick=1, tickComplete=1, lifecycleStable=1, controller="140200000",
                  record="14010002C", recordIndex=0, recordIndexAvailable=1, nativeRecordId=42,
                  objectId=309, objectIdMatchesRecord=1, actor="140300000", objentry="140400000",
                  status="140500000", actorController="140200000", actorRecord="14010002C",
                  actorType=3, actorObjectId=309, hp=160, maxHp=160, actorAvailable=1,
                  censusComplete=1, censusConfirmed=1, bindingEpoch=1, netId=5,
                  enclosingDispatcher=1, dispatcherSeq=1, dispatcherCallerRva="3FF431",
                  dispatcherCallerRvaAvailable=1, dispatcherRegion="0", enclosingScript42DC10=0,
                  scriptSeq=0, scriptCallerRva="0", scriptCallerRvaAvailable=0,
                  generatedPointAvailable=0, generatedPoint="(0,0,0,0)")
    fields.update(overrides)
    raw = bytearray(64)
    raw[:4] = fields["objectId"].to_bytes(4, "little")
    raw[30:32] = fields["nativeRecordId"].to_bytes(2, "little")
    return (line("spawntrace", "event", **fields) +
            line("spawntrace", "record", seq=seq, available=1, beforeNow="120B0000000001000000",
                 afterNow="120B0000000001000000", bytes=raw.hex()) +
            states("spawntrace", seq, ("tick-before", "tick-after", "wrapper-before", "wrapper-after")))


def spawn_summary(count=1, **overrides):
    fields = dict(available=1, fixedAvailable=1, generatedAvailable=1, dispatcherAvailable=1,
                  scriptAvailable=1, started=count, published=count, drained=count, dropped=0,
                  unsupportedCaller=0, unavailable=0, nativeFaults=0, lastException="00000000")
    fields.update(overrides)
    return line("spawntrace", "summary", **fields)


def spawn_log(event=None, summary=None):
    return (line("spawntrace", "ready", fixed=1, generated=1, dispatcher=1, script42DC10=1,
                 callers="all", queueCap=128, tickCap=64, **{"diagnostic-only": 1}) +
            (spawn_event() if event is None else event) + (spawn_summary() if summary is None else summary))


def life_event(seq=1, **overrides):
    fields = dict(seq=seq, parent=0, depth=0, kindId=0, kind="removal-bookkeeping",
                  callerRva="3FCAA0", callerInImage=1, role=1, roleAvailable=1, actorArgument="140300000",
                  controllerArgument="140200000", originalReturned=1, unwound=0, lifecycleStable=1,
                  postActorComparable=1, controllerComparable=1, controllerFromActor=1,
                  actorControllerMismatch=0, beforeStampAvailable=1, afterStampAvailable=1,
                  unavailable=0, outOfScope=0, exceptionCode="00000000", censusComplete=1,
                  censusCurrentPresent=1, bindingEpoch=1, netId=5, drainGeneration=1)
    fields.update(overrides)
    count_only = fields["kindId"] == 4
    if count_only:
        fields.update(kind="count-decrement", actorArgument="0", postActorComparable=0,
                      controllerFromActor=0, censusCurrentPresent=0, bindingEpoch=0, netId=0)
    text = line("lifecycletrace", "event", **fields)
    for phase in ("before", "after"):
        actor_line = line("lifecycletrace", "actor", seq=seq, phase=phase, available=1,
                     classificationAvailable=1, isCombat=1, actor="140300000", objentry="140400000",
                     status="140500000", controller="140200000", record="14010002C", objectId=309,
                     actorType=3, hp=160, maxHp=160, flags120=0, flags9B8=0, flags6C8=0,
                     recordAvailable=1, recordId=42, recordMode=0, recordStage=0,
                     fadeA08=1, slopeA0C=0, fadeAAC=1, slopeAB0=0)
        if count_only:
            parsed = AUDIT.parse_lines(actor_line, AUDIT.Audit())[0]["fields"]
            actor_line = line("lifecycletrace", "actor", **{k: v if k in ("seq", "phase") else 0 for k, v in parsed.items()})
        text += actor_line
        text += line("lifecycletrace", "stamp", seq=seq, phase=phase, transition=2, load=3,
                     nowHex="120B0000000001000000")
    return text + states("lifecycletrace", seq, ("before", "after"))


def life_log(event="", count=0, **summary_overrides):
    text = ""
    for kind, (_, rva) in AUDIT.LIFECYCLE_HOOKS.items():
        text += line("lifecycletrace", "hook", kind=kind, rva=f"{rva:X}", verified=1, installed=1, status=0)
    text += line("lifecycletrace", "ready", requested=1, verifiedMask=31, installedMask=31,
                 failedMask=0, phase="afterInstall")
    summary = dict(requested=1, verifiedMask=31, installedMask=31, failedMask=0, started=count,
                   published=count, drained=count, dropped=0, unavailable=0, outOfScope=0,
                   nativeFaults=0, unwound=0, depthOverflow=0, lastException="00000000")
    summary.update(summary_overrides)
    return text + event + line("lifecycletrace", "summary", **summary)


def append_fields(text, family, kind, **fields):
    suffix = " " + " ".join(f"{key}={value}" for key, value in fields.items())
    return "".join(row.rstrip("\n") + suffix + "\n" if f"[{family}] {kind} " in row else row
                   for row in text.splitlines(True))


def factory_record(outcome=3, **overrides):
    fields = dict(seq=1, coverageSerial=1, coverageMask=3, depth=0, eligible=1, complete=1,
                  unwound=0, countOverflow=0, operandMask=15, weightBits="3F800000",
                  limitBeforeBits="41200000", usedBeforeBits="40000000",
                  limitAfterBits="41200000", usedAfterBits="40000000",
                  admissionCalls=1, admissionReturned=1, admissionResult=1, admissionFault=0,
                  allocationCalls=1, allocationReturned=1, allocationSize=0xD50,
                  allocationResult="140300000", allocationFault=0, outcome=outcome)
    if outcome in (1, 2):
        fields["allocationResult"] = "0"
    if outcome == 1:
        fields.update(admissionResult=0, allocationCalls=0, allocationReturned=0, allocationSize=0)
    fields.update(overrides)
    return line("spawntrace", "factory", **fields)


def factory_log(outcome=3, supplement=None, event=None, **summary_overrides):
    if event is None:
        event = spawn_event() if outcome == 3 else spawn_event(
            outcome="null-return", wrapperOutcome="null-return", actor="0", actorAvailable=0,
            censusConfirmed=0, netId=0, bindingEpoch=0)
    text = spawn_log(event=event + (factory_record(outcome) if supplement is None else supplement))
    masks = dict(factoryVerifiedMask=3, factoryInstalledMask=3, factoryFailedMask=0)
    text = append_fields(text, "spawntrace", "ready", **masks)
    stats = dict(**masks, factoryRequestedMask=3, factoryForeignScopes=0, factoryUnwoundScopes=0, factoryAmbiguousScopes=0)
    stats.update(summary_overrides)
    return append_fields(text, "spawntrace", "summary", **stats)


def predicate_record(branch=1, **overrides):
    auxiliary = branch in (3, 4)
    fields = dict(seq=1, coverageMask=224, coverageGeneration=1, coverageStable=1,
                  originalReturned=1, resultAvailable=1, parentResult=int(branch in (4, 5)),
                  scriptCalls=1, scriptReturned=1, scriptResult=int(branch != 1),
                  auxiliaryCalls=int(auxiliary), auxiliaryReturned=int(auxiliary),
                  auxiliaryResult=int(branch == 3), faultMask=0, unwindMask=0,
                  countOverflow=0, nestedAmbiguous=0, branch=branch,
                  auxiliaryArgument="140600000" if auxiliary else "0",
                  auxiliaryBefore=1 if branch == 3 else 0, auxiliaryAfter=1 if branch == 3 else 0,
                  auxiliaryAvailableMask=3 if auxiliary else 0)
    fields.update(overrides)
    text = line("lifecycletrace", "predicate", **fields)
    for phase in ("before", "afterScript", "after"):
        text += line("lifecycletrace", "operands", seq=fields["seq"], phase=phase, availableMask=31,
                     scriptState="0", scriptTest=0, field80="0", field98="0", auxiliaryHandle=7)
    return text


def predicate_log(branch=1, supplement=None, event=None, empty=False, **summary_overrides):
    if event is None:
        event = life_event(kindId=5, kind="removal-predicate", callerRva="3BFD6F")
    text = life_log("" if empty else event + (predicate_record(branch) if supplement is None else supplement),
                    0 if empty else 1)
    text = text.replace("verifiedMask=31", "verifiedMask=255").replace("installedMask=31", "installedMask=255")
    hooks = "".join(line("lifecycletrace", "hook", kind=kind, rva=f"{rva:X}", verified=1, installed=1, status=0)
                    for kind, (_, rva) in AUDIT.REMOVAL_HOOKS.items())
    stats = dict(predicateStarted=0 if empty else 1, predicatePublished=0 if empty else 1,
                 predicateDropped=0, predicateForeign=0, predicateUnmatched=0, predicateUnwound=0,
                 predicateDepthOverflow=0, predicateCountOverflow=0)
    stats.update(summary_overrides)
    return hooks + append_fields(text, "lifecycletrace", "summary", **stats)


class TraceAuditTest(unittest.TestCase):
    def result(self, text=None):
        return AUDIT.audit_text(spawn_log() + life_log() if text is None else text)

    def codes(self, result):
        return {issue["code"] for issue in result["issues"]}

    def test_complete_recorded_envelope(self):
        result = self.result()
        self.assertTrue(result["provenanceComplete"], result["issues"])
        self.assertEqual(result["spawnTrace"]["returnedObjectHints"][0]["actorObjectId"], 309)
        self.assertFalse(any("acceptance passed" in text.lower() for text in result["limits"]))

    def test_untraced_text(self):
        result = self.result("[enemysync] host arrived epoch=1\n")
        self.assertTrue(result["structuralComplete"])
        self.assertFalse(result["provenanceComplete"])
        self.assertIn("spawn_trace_not_recorded", self.codes(result))

    def test_archived_untraced_log(self):
        folder = ROOT / "build/scenarios/20261002-210512_net_enemy_sync_waves_1"
        paths = sorted(folder.glob("kh2coop_inject_*.log"))
        if not paths:
            self.skipTest("local archived log is not present in this checkout")
        report = AUDIT.audit_paths(paths[:1])
        self.assertFalse(report["provenanceComplete"])
        self.assertIn("spawn_trace_not_recorded", self.codes(report["logs"][0]))

    def test_missing_and_duplicate_chunks(self):
        text = spawn_log() + life_log()
        chunk = next(s for s in text.splitlines(True) if "cache " in s)
        for mutated, code in ((text.replace(chunk, "", 1), "incomplete_cache_chunks"),
                              (text.replace(chunk, chunk * 2, 1), "duplicate_cache_chunk")):
            with self.subTest(code=code):
                result = self.result(mutated)
                self.assertFalse(result["structuralComplete"])
                self.assertIn(code, self.codes(result))

    def test_missing_event_record_state(self):
        for token, code in ((" event ", "event_header_count_mismatch"),
                            (" record ", "record_count_mismatch"), (" state ", "state_count_mismatch")):
            text = spawn_log() + life_log()
            victim = next(s for s in text.splitlines(True) if token in s)
            self.assertIn(code, self.codes(self.result(text.replace(victim, "", 1))))

    def test_record_mismatch(self):
        text = (spawn_log() + life_log()).replace("bytes=35010000", "bytes=37010000")
        self.assertIn("record_object_mismatch", self.codes(self.result(text)))

    def test_loss_and_unsupported(self):
        for key in ("dropped", "unavailable", "nativeFaults", "unsupportedCaller"):
            result = self.result(spawn_log(summary=spawn_summary(**{key: 1})) + life_log())
            self.assertIn("nonzero_coverage_counter", self.codes(result))
            self.assertFalse(result["provenanceComplete"])

    def test_summary_counts_and_pending_tail(self):
        result = self.result(spawn_log(summary=spawn_summary(2)) + life_log())
        self.assertIn("drained_event_count_mismatch", self.codes(result))
        self.assertIn("published_events_missing_at_eof", self.codes(result))

    def test_summary_before_drain_tail_is_not_malformed(self):
        text = spawn_log(event="", summary=spawn_summary(0)) + spawn_event() + life_log()
        result = self.result(text)
        self.assertTrue(result["structuralComplete"], result["issues"])
        self.assertFalse(result["provenanceComplete"])
        self.assertIn("events_after_latest_summary", self.codes(result))

    def test_wrapper_outside_tick_and_dynamic_alias_are_valid(self):
        event = spawn_event(wrapper="generated", tick=0, enclosingTick=0, tickComplete=0,
                            actorObjectId=311, objectIdMatchesRecord=0, generatedPointAvailable=1,
                            generatedPoint="(1,2,3,1)", enclosingDispatcher=0, dispatcherSeq=0,
                            dispatcherCallerRvaAvailable=0)
        result = self.result(spawn_log(event=event) + life_log())
        self.assertTrue(result["provenanceComplete"], result["issues"])
        self.assertEqual(result["spawnTrace"]["returnedObjectHints"][0]["actorObjectId"], 311)

    def test_script_ancestry_not_inferred_from_address(self):
        result = self.result(spawn_log(event=spawn_event(dispatcherCallerRva="42DC10")) + life_log())
        self.assertTrue(result["provenanceComplete"])
        self.assertEqual(result["spawnTrace"]["hooks"]["script42DC10"]["observedEventCount"], 0)

    def test_explicit_script_ancestry_survives_unknown_dispatcher_return(self):
        event = spawn_event(enclosingScript42DC10=1, scriptSeq=1, scriptCallerRva="42DD24",
                            scriptCallerRvaAvailable=1, dispatcherCallerRva="0", dispatcherCallerRvaAvailable=0)
        result = self.result(spawn_log(event=event) + life_log())
        self.assertTrue(result["structuralComplete"], result["issues"])
        self.assertEqual(result["spawnTrace"]["hooks"]["script42DC10"]["observedEventCount"], 1)
        self.assertIn("unknown_caller_scope", self.codes(result))

    def test_unknown_scope(self):
        result = self.result(spawn_log(event=spawn_event(wrapper="unknown")) + life_log())
        self.assertIn("unknown_wrapper_scope", self.codes(result))
        self.assertFalse(result["provenanceComplete"])

    def test_malformed_log(self):
        for suffix in ("[spawntrace broken\n", "[spawntrace] event seq=1 seq=2\n",
                       "[spawntrace] invented whatever=1\n", "[spawntrace] event seq=1"):
            with self.subTest(suffix=suffix):
                self.assertFalse(self.result(spawn_log() + life_log() + suffix)["provenanceComplete"])

    def test_large_sequence_does_not_allocate_sequence_range(self):
        result = self.result(spawn_log(event=spawn_event(seq=10**15)) + life_log())
        self.assertIn("event_sequence_gaps", self.codes(result))

    def test_lifecycle_complete_native_call(self):
        result = self.result(spawn_log() + life_log(life_event(), 1))
        self.assertTrue(result["provenanceComplete"], result["issues"])

    def test_lifecycle_nested_return_order(self):
        text = life_event(seq=2, parent=1, depth=1) + life_event()
        result = self.result(spawn_log() + life_log(text, 2))
        self.assertTrue(result["provenanceComplete"], result["issues"])

    def test_lifecycle_partial_hook_coverage(self):
        result = self.result(spawn_log() + life_log(installedMask=15, failedMask=16))
        self.assertIn("hook_not_ready", self.codes(result))
        self.assertFalse(result["provenanceComplete"])

    def test_lifecycle_exception_and_comparison_limits(self):
        text = life_event(originalReturned=0, unwound=1, postActorComparable=0, exceptionCode="C0000005")
        result = self.result(spawn_log() + life_log(text, 1, nativeFaults=1, unwound=1))
        self.assertIn("interrupted_native_call", self.codes(result))
        self.assertIn("post_actor_not_comparable", self.codes(result))

    def test_lifecycle_missing_parent(self):
        result = self.result(spawn_log() + life_log(life_event(parent=8, depth=1), 1))
        self.assertIn("missing_parent_event", self.codes(result))

    def test_count_decrement_has_no_actor_identity(self):
        text = spawn_log() + life_log(life_event(kindId=4), 1)
        result = self.result(text)
        self.assertTrue(result["provenanceComplete"], result["issues"])
        self.assertFalse({"lifecycle_record_unavailable", "lifecycle_classification_unavailable",
                          "lifecycle_actor_out_of_scope"} & self.codes(result))
        actor_line = next(s for s in text.splitlines(True) if "[lifecycletrace] actor " in s)
        bad = text.replace(actor_line, actor_line.replace("objectId=0", "objectId=309"), 1)
        self.assertIn("count_event_nonzero_actor_placeholder", self.codes(self.result(bad)))

    def test_lifecycle_null_record_preserves_actor_facts_but_not_complete_provenance(self):
        original = spawn_log() + life_log(life_event(), 1)
        self.assertTrue(self.result(original)["provenanceComplete"])
        mutated = ""
        for row in original.splitlines(True):
            if "[lifecycletrace] actor " in row:
                row = row.replace("record=14010002C", "record=0").replace("recordAvailable=1", "recordAvailable=0").replace("recordId=42", "recordId=0")
            mutated += row
        result = self.result(mutated)
        self.assertTrue(result["structuralComplete"], result["issues"])
        self.assertFalse(result["provenanceComplete"])
        self.assertIn("lifecycle_record_unavailable", self.codes(result))
        actor = result["lifecycleTrace"]["events"][0]["actors"]["before"]
        self.assertEqual(actor["available"], "1")
        self.assertEqual(actor["record"], "0")

    def test_unknown_or_noncombat_lifecycle_classification_is_a_scope_limit(self):
        original = spawn_log() + life_log(life_event(), 1)
        self.assertTrue(self.result(original)["provenanceComplete"])
        for field, code in (("classificationAvailable", "lifecycle_classification_unavailable"),
                            ("isCombat", "lifecycle_actor_out_of_scope")):
            with self.subTest(field=field):
                mutated = "".join(row.replace(field + "=1", field + "=0")
                                  if "[lifecycletrace] actor " in row else row for row in original.splitlines(True))
                result = self.result(mutated)
                self.assertTrue(result["structuralComplete"], result["issues"])
                self.assertFalse(result["provenanceComplete"])
                self.assertIn(code, self.codes(result))

    def test_generated_point_unavailable_is_not_fixed_wrapper_na(self):
        fixed = self.result(spawn_log(event=spawn_event(generatedPointAvailable=0)) + life_log())
        self.assertTrue(fixed["provenanceComplete"], fixed["issues"])
        good = self.result(spawn_log(event=spawn_event(wrapper="generated", generatedPointAvailable=1,
                                                     generatedPoint="(1,2,3,1)")) + life_log())
        self.assertTrue(good["provenanceComplete"], good["issues"])
        bad = self.result(spawn_log(event=spawn_event(wrapper="generated", generatedPointAvailable=0)) + life_log())
        self.assertTrue(bad["structuralComplete"], bad["issues"])
        self.assertFalse(bad["provenanceComplete"])
        self.assertIn("generated_point_unavailable", self.codes(bad))

    def test_unknown_ordinary_record_index_retains_raw_record(self):
        good = self.result(spawn_log(event=spawn_event(recordIndexAvailable=1, recordIndex=0)) + life_log())
        self.assertTrue(good["provenanceComplete"], good["issues"])
        bad = self.result(spawn_log(event=spawn_event(recordIndexAvailable=0, recordIndex=0)) + life_log())
        self.assertTrue(bad["structuralComplete"], bad["issues"])
        self.assertFalse(bad["provenanceComplete"])
        self.assertIn("ordinary_record_index_unavailable", self.codes(bad))
        self.assertEqual(bad["spawnTrace"]["events"][0]["record"], good["spawnTrace"]["events"][0]["record"])

    def test_actor_metadata_claims_are_checked(self):
        result = self.result(spawn_log(event=spawn_event(actor="0")) + life_log())
        self.assertIn("available_actor_has_null_identity", self.codes(result))
        result = self.result(spawn_log(event=spawn_event(actorController="123")) + life_log())
        self.assertIn("observed_actor_provenance_mismatch", self.codes(result))
        text = spawn_log() + life_log(life_event(), 1)
        actor_line = next(s for s in text.splitlines(True) if "actor seq=1 phase=after" in s)
        text = text.replace(actor_line, actor_line.replace("objectId=309", "objectId=311"))
        self.assertIn("comparable_actor_metadata_mismatch", self.codes(self.result(text)))

    def test_legacy_wrapper_scope_is_not_broader_coverage(self):
        legacy = spawn_log().replace(
            "ready fixed=1 generated=1 dispatcher=1 script42DC10=1 callers=all",
            "ready wrapper=3FE590 return=3FE83F status=0")
        result = self.result(legacy + life_log())
        self.assertIn("unobserved_producer_hooks", self.codes(result))
        self.assertFalse(result["provenanceComplete"])

    def test_missing_summary_or_unavailable_component(self):
        self.assertIn("missing_summary", self.codes(self.result(spawn_log(summary="") + life_log())))
        text = spawn_log() + line("spawntrace", "component-unavailable", rva="3FE700", reason="byte-gate") + life_log()
        self.assertIn("trace_hook_unavailable", self.codes(self.result(text)))

    def test_invalid_chunk_bytes_and_counter_regression(self):
        text = spawn_log() + life_log()
        self.assertIn("invalid_hex_bytes", self.codes(self.result(text.replace("idsU16Hex=" + "0000" * 32, "idsU16Hex=BAD", 1))))
        self.assertIn("counter_regressed", self.codes(self.result(text + spawn_summary(0))))

    def test_io_error_exit_code(self):
        with tempfile.TemporaryDirectory() as folder, contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(AUDIT.main([str(Path(folder) / "missing.log")]), 2)

    def test_foreign_thread_role_zero_is_unknown_for_both_families(self):
        spawn = spawn_event(captureRole=0, roleAvailable=0, netId=0, bindingEpoch=0,
                            censusConfirmed=0, stampAvailable=0, postStampAvailable=0,
                            transition=0, postTransition=0, load=0, postLoad=0, lifecycleStable=0)
        life = life_event(role=0, roleAvailable=0, netId=0, bindingEpoch=0, censusCurrentPresent=0,
                          beforeStampAvailable=0, afterStampAvailable=0, lifecycleStable=0)
        result = self.result(spawn_log(event=spawn) + life_log(life, 1))
        self.assertFalse(result["provenanceComplete"])
        self.assertIn("capture_role_unavailable", self.codes(result))
        for family in ("spawnTrace", "lifecycleTrace"):
            self.assertEqual(result[family]["events"][0]["captureRole"]["meaning"], "unknown")
        # Readable raw NOW is retained without upgrading stamp availability.
        self.assertEqual(result["spawnTrace"]["events"][0]["record"]["beforeNow"], "120B0000000001000000")

    def test_legacy_absent_role_bit_is_unknown(self):
        text = (spawn_log() + life_log(life_event(), 1)).replace(" roleAvailable=1", "")
        result = self.result(text)
        self.assertIn("role_availability_not_recorded", self.codes(result))
        self.assertFalse(result["provenanceComplete"])
        self.assertTrue(result["structuralComplete"], result["issues"])
        self.assertTrue(all(result[family]["events"][0]["captureRole"]["meaning"] == "unknown"
                            for family in ("spawnTrace", "lifecycleTrace")))

    def test_available_role_zero_is_known_off(self):
        result = self.result(spawn_log(event=spawn_event(captureRole=0, netId=0, bindingEpoch=0, censusConfirmed=0)) +
                             life_log(life_event(role=0, netId=0, bindingEpoch=0, censusCurrentPresent=0), 1))
        self.assertTrue(result["provenanceComplete"], result["issues"])
        self.assertTrue(all(result[family]["events"][0]["captureRole"]["meaning"] == "off"
                            for family in ("spawnTrace", "lifecycleTrace")))

    def test_unknown_capture_role_cannot_claim_binding(self):
        result = self.result(spawn_log(event=spawn_event(roleAvailable=0)) + life_log())
        self.assertIn("binding_without_capture_role", self.codes(result))

    def test_binding_is_only_current_correlation(self):
        result = self.result(spawn_log(event=spawn_event(censusConfirmed=0, netId=0, bindingEpoch=0)) + life_log())
        self.assertTrue(result["provenanceComplete"], result["issues"])
        invalid = self.result(spawn_log(event=spawn_event(censusConfirmed=0)) + life_log())
        self.assertIn("binding_without_confirmation", self.codes(invalid))

    def test_multiple_logs_and_cli_artifact(self):
        with tempfile.TemporaryDirectory() as folder:
            first, second, output = [Path(folder) / name for name in ("first.log", "second.log", "audit.json")]
            first.write_text(spawn_log() + life_log(), encoding="utf-8")
            second.write_text("[enemysync] no tracing\n", encoding="utf-8")
            with contextlib.redirect_stdout(io.StringIO()):
                status = AUDIT.main([str(first), str(second), "--output", str(output)])
            report = json.loads(output.read_text())
            self.assertEqual(status, 1)
            self.assertEqual(len(report["logs"]), 2)
            self.assertTrue(report["logs"][0]["provenanceComplete"])
            self.assertFalse(report["logs"][1]["provenanceComplete"])
            self.assertEqual(len(report["logs"][0]["sha256"]), 64)

    def test_invalid_encoding_and_output_guard(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "log.txt"
            path.write_bytes(b"\xff\n")
            self.assertFalse(AUDIT.audit_paths([path])["structuralComplete"])
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
                AUDIT.main([str(path), "--output", str(path)])
            self.assertEqual(error.exception.code, 2)


class PredicateAuditTest(unittest.TestCase):
    def audit(self, spawn=None, life=None):
        return AUDIT.audit_text((factory_log() if spawn is None else spawn) +
                                (predicate_log() if life is None else life))

    def test_legacy_envelope_does_not_claim_new_predicates(self):
        result = AUDIT.audit_text(spawn_log() + life_log())
        self.assertTrue(result["provenanceComplete"])
        self.assertFalse(result["predicateTrace"]["schemaRecorded"])
        self.assertFalse(result["predicateTrace"]["complete"])
        self.assertEqual(result["predicateTrace"]["factory"]["events"], [])

    def test_three_executed_factory_outcomes_saved_logs(self):
        expected = ("weight-admission-rejected", "type4-allocation-returned-null", "allocation-and-wrapper-returned-nonnull")
        with tempfile.TemporaryDirectory() as folder:
            for number, name in enumerate(expected, 1):
                path = Path(folder) / f"factory{number}.log"
                path.write_text(factory_log(number) + predicate_log(), encoding="utf-8")
                result = AUDIT.audit_paths([path])["logs"][0]
                self.assertTrue(result["provenanceComplete"], result["issues"])
                self.assertTrue(result["predicateTrace"]["complete"])
                self.assertEqual(result["predicateTrace"]["factory"]["events"][0]["branch"], name)

    def test_five_executed_removal_branches_saved_logs(self):
        expected = ("post-service-script-predicate-blocked", "both-pointer-tests-blocked", "auxiliary-predicate-blocked",
                    "auxiliary-predicate-permitted", "post-service-resolution-null-permitted")
        with tempfile.TemporaryDirectory() as folder:
            for branch, name in enumerate(expected, 1):
                path = Path(folder) / f"removal{branch}.log"
                path.write_text(factory_log() + predicate_log(branch), encoding="utf-8")
                result = AUDIT.audit_paths([path])["logs"][0]
                self.assertTrue(result["provenanceComplete"], result["issues"])
                self.assertEqual(result["predicateTrace"]["predicate"]["events"][0]["branch"], name)

    def test_operand_snapshots_do_not_recompute_admission(self):
        # Even contradictory/NaN samples cannot replace the genuine returned AL.
        supplement = factory_record(1, operandMask=0, weightBits="7FC00001", limitBeforeBits="7F800000",
                                    usedBeforeBits="FF800000")
        result = self.audit(factory_log(1, supplement=supplement))
        event = result["predicateTrace"]["factory"]["events"][0]
        self.assertTrue(event["branchSupported"])
        self.assertFalse(event["operandSamplesComplete"])
        self.assertEqual(event["floatSamples"]["weightBits"]["classification"], "nan")
        self.assertEqual(event["floatSamples"]["limitBeforeBits"]["classification"], "infinity")
        json.dumps(result, allow_nan=False)

    def test_missing_child_observation_never_uses_budget_samples(self):
        supplement = factory_record(0, admissionCalls=0, admissionReturned=0,
                                    allocationCalls=0, allocationReturned=0, allocationResult="0")
        result = self.audit(factory_log(1, supplement=supplement))
        self.assertFalse(result["predicateTrace"]["factory"]["events"][0]["branchSupported"])

    def test_unknown_factory_eligibility_is_conservative_not_malformed(self):
        for event, supplement in ((spawn_event(objectId=0x236), factory_record(0)),
                                  (spawn_event(), factory_record(0)),
                                  (spawn_event(), factory_record(4, admissionCalls=2))):
            result = self.audit(factory_log(event=event, supplement=supplement))
            self.assertTrue(result["structuralComplete"], result["issues"])
            self.assertFalse(result["predicateTrace"]["factory"]["events"][0]["branchSupported"])
        for event in (spawn_event(objectId=0x236), spawn_event().replace("record seq=1 available=1", "record seq=1 available=0")):
            result = self.audit(factory_log(event=event))
            self.assertFalse(result["predicateTrace"]["factory"]["events"][0]["branchSupported"])

    def test_factory_partial_fault_overflow_and_ambiguous_scopes(self):
        for change in ({"coverageMask": 1}, {"complete": 0}, {"eligible": 0}, {"coverageSerial": 0},
                       {"unwound": 1}, {"countOverflow": 1}, {"admissionCalls": 2},
                       {"allocationCalls": 2}, {"admissionFault": 1}, {"allocationFault": 1},
                       {"allocationSize": 1}, {"allocationReturned": 0}, {"operandMask": 16}):
            with self.subTest(change=change):
                result = self.audit(factory_log(supplement=factory_record(**change)))
                self.assertFalse(result["predicateTrace"]["factory"]["events"][0]["branchSupported"])
                self.assertFalse(result["provenanceComplete"])

    def test_allocation_nonnull_wrapper_null_is_not_allocator_failure(self):
        result = self.audit(factory_log(2, supplement=factory_record(2, allocationResult="140300000")))
        self.assertEqual(result["predicateTrace"]["factory"]["events"][0]["branch"], "unknown")

    def test_constructor_return_must_match_allocation(self):
        result = self.audit(factory_log(supplement=factory_record(allocationResult="140300010")))
        self.assertFalse(result["predicateTrace"]["factory"]["events"][0]["branchSupported"])

    def test_requested_and_unknown_hook_masks_are_explicit(self):
        for change in ({"factoryRequestedMask": 1}, {"factoryVerifiedMask": 7},
                       {"factoryFailedMask": 1}, {"factoryInstalledMask": 7}):
            result = self.audit(factory_log(**change))
            self.assertFalse(result["predicateTrace"]["complete"])
            self.assertFalse(result["provenanceComplete"])

    def test_loss_does_not_erase_local_return_but_prevents_complete_coverage(self):
        text = factory_log().replace("dropped=0", "dropped=1")
        result = self.audit(text)
        self.assertTrue(result["predicateTrace"]["factory"]["events"][0]["branchSupported"])
        self.assertFalse(result["predicateTrace"]["factory"]["complete"])

    def test_removal_result_survives_unavailable_hp_metadata(self):
        event = life_event(kindId=5, kind="removal-predicate", callerRva="3BFD6F", postActorComparable=0)
        event = "".join(row.replace("available=1", "available=0").replace("status=140500000", "status=0")
                        if " actor " in row else row for row in event.splitlines(True))
        result = self.audit(life=predicate_log(event=event))
        self.assertFalse(result["provenanceComplete"])
        self.assertTrue(result["predicateTrace"]["predicate"]["events"][0]["branchSupported"])

    def test_successful_factory_predicate_survives_unavailable_actor_metadata(self):
        event = spawn_event(actorAvailable=0, status="0", outcome="actor-read-unavailable",
                            wrapperOutcome="actor-read-unavailable", censusConfirmed=0, netId=0, bindingEpoch=0)
        result = self.audit(factory_log(event=event))
        self.assertFalse(result["provenanceComplete"])
        self.assertEqual(result["predicateTrace"]["factory"]["events"][0]["branch"], "allocation-and-wrapper-returned-nonnull")

    def test_removal_zero_child_branch_needs_full_installed_scope(self):
        for branch in (1, 2, 5):
            for change in ({"coverageMask": 96}, {"coverageStable": 0}, {"coverageGeneration": 0},
                           {"scriptCalls": 0}, {"scriptCalls": 2}, {"scriptReturned": 0},
                           {"countOverflow": 1}, {"nestedAmbiguous": 1}, {"faultMask": 64},
                           {"unwindMask": 64}, {"originalReturned": 0}, {"resultAvailable": 0}):
                with self.subTest(branch=branch, change=change):
                    result = self.audit(life=predicate_log(branch, supplement=predicate_record(branch, **change)))
                    self.assertFalse(result["predicateTrace"]["predicate"]["events"][0]["branchSupported"])

    def test_auxiliary_results_and_parent_contradictions(self):
        for change in ({"auxiliaryCalls": 2}, {"auxiliaryReturned": 0}, {"auxiliaryArgument": "0"},
                       {"parentResult": 1}, {"scriptResult": 0}):
            result = self.audit(life=predicate_log(3, supplement=predicate_record(3, **change)))
            self.assertFalse(result["predicateTrace"]["predicate"]["events"][0]["branchSupported"])

    def test_unavailable_operand_samples_do_not_erase_genuine_removal_returns(self):
        supplement = predicate_record(4, auxiliaryAvailableMask=0).replace("availableMask=31", "availableMask=0")
        result = self.audit(life=predicate_log(4, supplement=supplement))
        self.assertEqual(result["predicateTrace"]["predicate"]["events"][0]["branch"], "auxiliary-predicate-permitted")

    def test_missing_duplicate_and_orphan_supplements_are_incomplete(self):
        for text in (factory_log(supplement=""), factory_log(supplement=factory_record() * 2),
                     factory_log(supplement=factory_record(seq=99))):
            self.assertFalse(self.audit(text)["provenanceComplete"])
        for text in (predicate_log(supplement=""), predicate_log(supplement=predicate_record() * 2),
                     predicate_log(supplement=predicate_record(seq=99))):
            self.assertFalse(self.audit(life=text)["provenanceComplete"])

    def test_missing_malformed_operands_are_not_complete(self):
        text = predicate_log()
        row = next(r for r in text.splitlines(True) if " operands " in r)
        for changed in (text.replace(row, "", 1), text.replace("availableMask=31", "availableMask=broken", 1),
                        text.replace("auxiliaryCalls=0", "auxiliaryCalls=4294967296"),
                        text.replace("phase=afterScript", "phase=unknown")):
            self.assertFalse(self.audit(life=changed)["provenanceComplete"])

    def test_effective_coverage_is_not_retroactive(self):
        text = factory_log().replace("ready fixed=1", "ready fixed=1", 1)
        ready = next(r for r in text.splitlines(True) if " ready " in r)
        text = text.replace(ready, ready.replace("factoryInstalledMask=3", "factoryInstalledMask=1"))
        result = self.audit(text)
        self.assertFalse(result["predicateTrace"]["factory"]["events"][0]["branchSupported"])

    def test_new_loss_counters_and_missing_parent_totals(self):
        for key in ("factoryForeignScopes", "factoryUnwoundScopes", "factoryAmbiguousScopes"):
            result = self.audit(factory_log(**{key: 1}))
            self.assertFalse(result["predicateTrace"]["complete"])
        for key in ("predicateDropped", "predicateForeign", "predicateUnmatched", "predicateUnwound",
                    "predicateDepthOverflow", "predicateCountOverflow", "predicatePublished", "predicateStarted"):
            result = self.audit(life=predicate_log(**{key: 2}))
            self.assertFalse(result["predicateTrace"]["complete"])

    def test_foreign_or_unknown_parent_never_supports_branch(self):
        result = self.audit(factory_log(event=spawn_event(roleAvailable=0)))
        self.assertFalse(result["predicateTrace"]["factory"]["events"][0]["branchSupported"])
        event = life_event(kindId=5, kind="removal-predicate", callerRva="3BFD87")
        result = self.audit(life=predicate_log(event=event))
        self.assertFalse(result["predicateTrace"]["predicate"]["events"][0]["branchSupported"])

    def test_absence_does_not_infer_a_removal_branch_or_later_parent(self):
        result = self.audit(life=predicate_log(empty=True))
        self.assertEqual(result["predicateTrace"]["predicate"]["events"], [])
        self.assertTrue(any("precedes later removal" in limit for limit in result["limits"]))


def hit_log():
    """Schema-1 Drain serialization, with one genuine-shaped Take/Stat chain.

    Synthetic facts, not a live attack. Full-width values deliberately exceed
    old narrowing boundaries. The Stat's DLL return address remains unavailable.
    """
    family = "hittrace"
    text = line(family, "ready", schema=1, requested=1, verifiedMask=7, installedMask=7, coverageSerial=1)
    text += line(family, "event", schema=1, seq=1, parent=0, depth=1, coverageSerial=1,
                 coverageMask=7, lossSerial=0, callerRva="3D613C", callerAvailable=1,
                 rawResult="0", ownerThread=1, returned=1, unwound=0, nested=0,
                 overflow=0, coverageStable=1, contextStable=1, metadataStable=1,
                 lossStable=1, witness=1, takeCalls=1, statCalls=1, childCount=2)
    def actor(phase, subject):
        source = subject == 1
        return line(family, "actor", schema=1, seq=1, phase=phase, subject=subject,
                    actor="140200000" if source else "140100000", objentry="140400000" if source else "140300000",
                    status="140600000" if source else "140500000", objectId=302 if source else 84,
                    readMask=511, type=4 if source else 0, team=2 if source else 1,
                    hp=20 if source else (24 if phase == 0 else 16), maxHp=20 if source else 40,
                    namePrefix="5F4D" if source else "5F50")
    for phase in range(2):
        text += line(family, "context", schema=1, seq=1, phase=phase, readMask=127,
                     available=1, frame=100, generation=9, epoch=12, transitionSerial=3,
                     loadSerial=4, connectionId=4294967298, hostConnectionId=8589934594,
                     role=2, slot=1, location="000500060000123402010300")
        text += actor(phase, 0) + actor(phase, 1)
        text += line(family, "input", schema=1, seq=1, phase=phase, hit="140700000",
                     attack="140800000", owner="140200000", canonicalPlayer="140100000",
                     head="140100000", tracked="140100000", flags=0 if phase == 0 else 2,
                     attackHandle=11, atkpHandle=12, ownerHandle=13, attackId=10,
                     readMask=255, damage=8, stat=0, kind=1, syncDrop=0, manualFilterOn=0, manualDrop=0)
    for index in range(2):
        text += line(family, "child", schema=1, seq=1, index=index, childSeq=2 + index,
                     takeSeq=0 if index == 0 else 2, kind=index + 1, actor="140100000",
                     callerRva="3D4030" if index == 0 else "0", callerAvailable=1 if index == 0 else 0,
                     delta=-8, stat=0, react=1 if index == 0 else 257, result=0 if index == 0 else 16,
                     matching=1, returned=1, unwound=0)
        text += actor(0, index + 2) + actor(1, index + 2)
    text += line(family, "summary", schema=1, requested=1, verifiedMask=7, installedMask=7,
                 coverageSerial=1, started=1, published=1, drained=1, dropped=0, foreign=0,
                 unwound=0, nested=0, overflow=0, unmatched=0)
    return text


def hit_change(text, record_kind, match=None, **changes):
    """Mutate exact saved records, then exercise the production text parser."""
    result = []
    for record in text.splitlines(True):
        if record.startswith(f"[hittrace] {record_kind} "):
            parsed = dict(part.split("=", 1) for part in record.strip().split()[2:])
            if all(parsed.get(k) == str(v) for k, v in (match or {}).items()):
                parsed.update({k: str(v) for k, v in changes.items()})
                record = line("hittrace", record_kind, **parsed)
        result.append(record)
    return "".join(result)


class NativeHitAuditTest(unittest.TestCase):
    def audit(self, text=None):
        return AUDIT.audit_text(hit_log() if text is None else text)["nativeHitTrace"]

    def rejected(self, text):
        result = self.audit(text)
        self.assertFalse(result["complete"], result)
        self.assertEqual(result["witnessCount"], 0, result)
        self.assertTrue(all(not e["ordinaryIncomingClientLocalWitness"] for e in result["events"]))
        return result

    def test_saved_schema_one_positive_and_scope(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "native-hit.log"
            path.write_text(hit_log(), encoding="utf-8")
            result = AUDIT.audit_paths([path])["logs"][0]["nativeHitTrace"]
        self.assertTrue(result["complete"], result)
        self.assertEqual(result["witnessCount"], 1)
        self.assertIn("not full rejection matrix or step 2", result["scope"])

    def test_native_cpp_positive_facts_match_serialized_contract(self):
        # Facts/After/Emit from NativeHitTraceTest, encoded exactly as Drain.
        # This remains a synthetic compatibility control, never live evidence.
        text = hit_change(hit_log(), "context", frame=71, generation=4, epoch=9, slot=2,
                          loadSerial=2, hostConnectionId=8589934595,
                          location="000200080003123423453456")
        text = hit_change(text, "event", rawResult="123456789ABCDEF0")
        for subject in (0, 2, 3):
            text = hit_change(text, "actor", {"subject": subject}, actor="10100", objentry="10200",
                              status="10300", maxHp=120, team=0)
            text = hit_change(text, "actor", {"subject": subject, "phase": 0}, hp=100)
            text = hit_change(text, "actor", {"subject": subject, "phase": 1}, hp=93)
        text = hit_change(text, "actor", {"subject": 1}, actor="20100", objentry="20200", status="20300",
                          objectId=309, hp=160, maxHp=160, team=0x12340001)
        text = hit_change(text, "input", hit="30100", attack="30200", owner="20100", canonicalPlayer="10100",
                          head="10100", tracked="10100", attackHandle=19, atkpHandle=21, ownerHandle=20,
                          attackId=33, damage=12)
        text = hit_change(text, "child", actor="10100", delta=-7)
        text = hit_change(text, "child", {"kind": 1}, callerRva="3D3CD5")
        text = hit_change(text, "child", {"kind": 2}, result=93)
        result = self.audit(text)
        self.assertEqual(result["witnessCount"], 1, result)

    def test_historical_schema_absence_preserves_old_envelope(self):
        result = AUDIT.audit_text(spawn_log() + life_log())
        self.assertTrue(result["provenanceComplete"], result["issues"])
        self.assertFalse(result["nativeHitTrace"]["schemaRecorded"])
        self.assertFalse(result["nativeHitTrace"]["complete"])
        self.assertEqual(result["nativeHitTrace"]["status"], "unavailable")

    def test_untraced_archive_has_no_native_hit_proof(self):
        result = self.audit("[hit] frame=1 attacker=M_EX020 -> victim=P_EX100 damage=8\n[hp] delta=-8 ->16\n")
        self.assertFalse(result["schemaRecorded"])
        self.assertEqual(result["witnessCount"], 0)

    def test_full_width_connection_program_and_team_facts(self):
        text = hit_change(hit_log(), "actor", {"subject": 1}, team=4294967295)
        result = self.audit(text)
        self.assertTrue(result["complete"], result)
        context = result["events"][0]["contexts"][0]
        self.assertEqual(context["connectionId"], 4294967298)
        self.assertEqual(context["hostConnectionId"], 8589934594)
        self.assertEqual(context["location"][3:], [0x1234, 0x201, 0x300])
        self.rejected(hit_change(text, "context", {"phase": 1}, connectionId=2))
        self.rejected(hit_change(text, "context", {"phase": 1}, location="000500060000003400010000"))
        self.rejected(hit_change(text, "context", hostConnectionId=4294967298))
        self.rejected(hit_change(text, "context", connectionId=2, hostConnectionId=2))

    def test_raw_apply_return_is_not_success(self):
        for raw in ("0", "FFFFFFFFFFFFFFFF", "DEADBEEF"):
            result = self.audit(hit_change(hit_log(), "event", rawResult=raw))
            self.assertEqual(result["witnessCount"], 1, result)
        self.rejected(hit_change(hit_log(), "event", returned=0, rawResult="1"))

    def test_adjusted_delta_not_build_hit_amount_and_lethal_clamp(self):
        # Native damage adjustment is not required to equal BuildHit's amount.
        self.assertEqual(self.audit(hit_change(hit_log(), "input", damage=11))["witnessCount"], 1)
        text = hit_change(hit_log(), "child", delta=-100)
        text = hit_change(text, "child", {"kind": 2}, result=0)
        for subject in (0, 2, 3):
            text = hit_change(text, "actor", {"phase": 1, "subject": subject}, hp=0)
        self.assertEqual(self.audit(text)["witnessCount"], 1)
        self.rejected(hit_change(text, "child", {"kind": 2}, delta=-3))

    def test_direct_parent_caller_thread_and_depth(self):
        for fields in ({"callerRva": "3D613D"}, {"callerAvailable": 0}, {"ownerThread": 0},
                       {"parent": 99}, {"depth": 2}, {"nested": 1}):
            with self.subTest(fields=fields):
                self.rejected(hit_change(hit_log(), "event", **fields))

    def test_child_dll_caller_is_not_fabricated_native_ancestry(self):
        result = self.audit()
        self.assertEqual(result["witnessCount"], 1)
        self.assertEqual(result["events"][0]["children"][1]["callerAvailable"], 0)
        self.rejected(hit_change(hit_log(), "child", {"kind": 2}, takeSeq=0))
        self.rejected(hit_change(hit_log(), "child", {"kind": 1}, takeSeq=2))

    def test_partial_changed_and_missing_effective_hooks(self):
        for kind, fields in (("ready", {"installedMask": 3}), ("summary", {"verifiedMask": 3}),
                             ("event", {"coverageMask": 3}), ("event", {"coverageStable": 0}),
                             ("event", {"coverageSerial": 2}), ("summary", {"coverageSerial": 2})):
            self.rejected(hit_change(hit_log(), kind, **fields))
        self.rejected("".join(r for r in hit_log().splitlines(True) if " ready " not in r))

    def test_full_context_required_and_immutable(self):
        for fields in ({"readMask": 63}, {"available": 0}, {"role": 1}, {"slot": 0},
                       {"generation": 0}, {"epoch": 0}, {"loadSerial": 0}, {"connectionId": 0}, {"hostConnectionId": 0}):
            self.rejected(hit_change(hit_log(), "context", **fields))
        for key in ("frame", "generation", "epoch", "transitionSerial", "loadSerial", "connectionId", "hostConnectionId"):
            self.rejected(hit_change(hit_log(), "context", {"phase": 1}, **{key: 999}))

    def test_canonical_victim_and_stable_actor_join(self):
        for key in ("canonicalPlayer", "head", "tracked"):
            self.rejected(hit_change(hit_log(), "input", {"phase": 0}, **{key: "140900000"}))
        for key, value in (("actor", "140900000"), ("objentry", "0"), ("status", "140900000"),
                           ("objectId", 99), ("type", 4), ("team", 5), ("maxHp", 99), ("readMask", 127)):
            self.rejected(hit_change(hit_log(), "actor", {"phase": 1, "subject": 0}, **{key: value}))

    def test_direct_living_enemy_owner_and_stable_metadata(self):
        for fields in ({"type": 0}, {"type": 3}, {"objectId": 0}, {"hp": 0}, {"maxHp": 0},
                       {"readMask": 255}, {"namePrefix": "5F46"}):
            self.rejected(hit_change(hit_log(), "actor", {"subject": 1}, **fields))
        for key in ("attack", "owner", "attackHandle", "atkpHandle", "ownerHandle"):
            self.rejected(hit_change(hit_log(), "input", **{key: 0}))
        self.rejected(hit_change(hit_log(), "input", owner="140900000"))
        self.rejected(hit_change(hit_log(), "actor", {"phase": 1, "subject": 1}, objectId=303))

    def test_healing_nonhp_dead_or_nonpositive_damage_are_unknown(self):
        for fields in ({"kind": 5}, {"kind": 6}, {"stat": 1}, {"damage": 0}, {"damage": -5}, {"readMask": 127}):
            self.rejected(hit_change(hit_log(), "input", **fields))
        self.rejected(hit_change(hit_log(), "actor", {"phase": 0, "subject": 0}, hp=0))

    def test_applied_bit_two_and_filter_facts(self):
        self.rejected(hit_change(hit_log(), "input", {"phase": 0}, flags=2))
        self.rejected(hit_change(hit_log(), "input", {"phase": 1}, flags=1))
        for key in ("syncDrop", "manualFilterOn", "manualDrop"):
            self.rejected(hit_change(hit_log(), "input", {"phase": 1}, **{key: 1}))

    def test_child_return_matching_actor_and_hp_delta(self):
        for fields in ({"matching": 0}, {"returned": 0}, {"unwound": 1}, {"actor": "140900000"},
                       {"delta": -7}, {"stat": 1}, {"result": 17}):
            self.rejected(hit_change(hit_log(), "child", {"kind": 2}, **fields))
        self.rejected(hit_change(hit_log(), "actor", {"phase": 1, "subject": 3}, hp=15))
        self.rejected(hit_change(hit_log(), "actor", {"phase": 0, "subject": 2}, status="140900000"))

    def test_repeated_saturated_and_overflow_child_counts(self):
        for fields in ({"takeCalls": 2}, {"statCalls": 2}, {"takeCalls": 4294967295},
                       {"statCalls": 4294967295}, {"overflow": 1}, {"childCount": 4294967295}):
            self.rejected(hit_change(hit_log(), "event", **fields))

    def test_missing_duplicate_and_orphan_records(self):
        for kind in ("context", "actor", "input", "child"):
            rows = hit_log().splitlines(True)
            chosen = next(row for row in rows if f"] {kind} " in row)
            self.rejected("".join(rows).replace(chosen, "", 1))
            self.rejected("".join(rows).replace(chosen, chosen * 2, 1))
        self.rejected(hit_change(hit_log(), "child", seq=77))
        self.rejected(hit_change(hit_log(), "child", {"index": 1}, index=0))
        self.rejected(hit_change(hit_log(), "actor", {"subject": 3}, subject=4))

    def test_malformed_truncated_unknown_schema_and_numbers(self):
        self.rejected(hit_log().rstrip())
        self.rejected(hit_log() + "[hittrace broken\n")
        self.rejected(hit_log().replace("schema=1", "schema=2", 1))
        self.rejected(hit_change(hit_log(), "event", rawResult="0x1"))
        self.rejected(hit_change(hit_log(), "event", returned=2))
        self.rejected(hit_change(hit_log(), "context", location="0005060000"))
        self.rejected(hit_change(hit_log(), "context", connectionId=18446744073709551616))
        self.rejected(hit_change(hit_log(), "child", delta=-2147483649))
        self.rejected(hit_change(hit_log(), "actor", team=4294967296))

    def test_loss_foreign_fault_and_summary_totals(self):
        for key in ("dropped", "foreign", "unwound", "nested", "overflow", "unmatched"):
            self.rejected(hit_change(hit_log(), "summary", **{key: 1}))
        self.rejected(hit_change(hit_log(), "event", lossSerial=1))
        self.rejected(hit_change(hit_log(), "event", lossStable=0))
        self.rejected(hit_change(hit_log(), "event", unwound=1))
        for fields in ({"started": 2}, {"published": 2}, {"drained": 0}):
            self.rejected(hit_change(hit_log(), "summary", **fields))

    def test_pending_tail_missing_summary_and_sequence_integrity(self):
        rows = hit_log().splitlines(True)
        summary = rows.pop()
        self.rejected("".join(rows))
        self.rejected(rows[0] + summary + "".join(rows[1:]))
        self.rejected(hit_change(hit_log(), "child", {"kind": 2}, childSeq=99))
        self.rejected(hit_change(hit_log(), "child", {"kind": 2}, childSeq=2))

    def test_reported_witness_alone_is_not_proof(self):
        self.rejected(hit_change(hit_log(), "context", readMask=0))
        self.rejected(hit_change(hit_log(), "event", witness=0))
        result = self.rejected(hit_change(hit_log(), "summary", dropped=1))
        self.assertTrue(result["events"][0]["localChecksSatisfied"])

    def test_ready_empty_does_not_infer_absence_of_attacks(self):
        rows = hit_log().splitlines(True)
        text = rows[0] + hit_change(rows[-1], "summary", started=0, published=0, drained=0)
        result = self.audit(text)
        self.assertTrue(result["complete"], result)
        self.assertEqual(result["witnessCount"], 0)
        self.assertIn("later activity unknown", result["pendingTail"])

    def test_all_required_fields_fail_closed_when_missing_or_malformed(self):
        # Exercise actual parser joins, including partial logs, not just numeric helpers.
        original = hit_log().splitlines(True)
        for index, record in enumerate(original):
            for field in record.strip().split()[2:]:
                key = field.split("=", 1)[0]
                for replacement in ("", f"{key}=invalid"):
                    with self.subTest(line=index, field=key, replacement=replacement):
                        rows = original.copy()
                        rows[index] = record.replace(field, replacement, 1)
                        self.rejected("".join(rows))

    def test_late_supplement_or_configuration_is_not_at_summary_cutoff(self):
        rows = hit_log().splitlines(True)
        late = rows.pop(-2)
        self.rejected("".join(rows) + late)
        self.rejected(hit_log() + hit_change(rows[0], "ready", coverageSerial=2))

    def test_invalid_encoding_cannot_preserve_native_witness(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "native-hit.log"
            path.write_bytes(hit_log().encode() + b"\xff\n")
            result = AUDIT.audit_paths([path])["logs"][0]["nativeHitTrace"]
        self.assertFalse(result["complete"])
        self.assertEqual(result["witnessCount"], 0)
        self.assertFalse(result["events"][0]["ordinaryIncomingClientLocalWitness"])


def policy_change(text, record_kind, match=None, **changes):
    """Mutate only the optional policy family, retaining historical hit rows."""
    result = []
    for record in text.splitlines(True):
        if record.startswith(f"[damagepolicy] {record_kind} "):
            parsed = dict(part.split("=", 1) for part in record.strip().split()[2:])
            if all(parsed.get(k) == str(v) for k, v in (match or {}).items()):
                parsed.update({k: str(v) for k, v in changes.items()})
                record = line("damagepolicy", record_kind, **parsed)
        result.append(record)
    return "".join(result)


def policy_log(remote_victim=False, host=False, children=False):
    """Frozen serializer-shaped synthetic veto, not production membership proof."""
    text = hit_log()
    text = hit_change(text, "event", witness=0, takeCalls=int(children), statCalls=int(children), childCount=2 if children else 0)
    if not children:
        text = "".join(row for row in text.splitlines(True) if not row.startswith("[hittrace] child ") and
                       not (row.startswith("[hittrace] actor ") and any(f"subject={n} " in row for n in (2, 3))))
    text = hit_change(text, "actor", {"phase": 1, "subject": 0}, hp=24)
    text = hit_change(text, "actor", {"subject": 1}, type=0, objectId=84, namePrefix="5F50")
    text = hit_change(text, "input", {"phase": 1}, damage=0)
    if children:
        text = hit_change(text, "child", delta=0)
        text = hit_change(text, "child", {"kind": 2}, result=24)
        for subject in (2, 3):
            text = hit_change(text, "actor", {"subject": subject}, hp=24)
    if remote_victim:
        text = hit_change(text, "actor", {"subject": 1}, type=4, objectId=302, namePrefix="5F4D")
        text = hit_change(text, "input", canonicalPlayer="140900000", head="140900000", tracked="140900000")
    if host:
        text = hit_change(text, "context", role=1, slot=0, connectionId=8589934594)
    rows = text.splitlines(True)
    event = line("damagepolicy", "event", schema=1, seq=1, recorded=1, role=1 if host else 2,
                 ownerThread=1, contextAvailable=1, victimClass=3 if remote_victim else 1,
                 sourceClass=5 if remote_victim else 3, flags=0, readMask=15, amount=8,
                 stat=0, kind=1, action=1, reason=7 if remote_victim else 8, supported=1,
                 roster0=8589934594, roster1=4294967298, roster2=12884901891,
                 revalidationAttempted=1, revalidationPassed=1, zeroAttempted=1, zeroResult=0,
                 claimAttempted=0, claimQueued=0)
    captures = "".join(row.replace("[hittrace]", "[damagepolicy]") for row in rows
                       if " phase=0 " in row and (row.startswith("[hittrace] context ") or
                       row.startswith("[hittrace] input ") or
                       (row.startswith("[hittrace] actor ") and any(f"subject={n} " in row for n in (0, 1)))))
    return "".join(rows[:-1]) + event + captures + rows[-1]


class DamagePolicyAuditTest(unittest.TestCase):
    def audit(self, text=None):
        return AUDIT.audit_text(policy_log() if text is None else text)["damagePolicyTrace"]

    def rejected(self, text):
        result = self.audit(text)
        self.assertEqual(result["zeroVetoCount"], 0, result)
        self.assertTrue(all(not e["knownOwnerZeroVetoEvidence"] for e in result["events"]))
        return result

    def test_direct_type0_source_and_victim_for_both_roles(self):
        for victim in (False, True):
            for host in (False, True):
                with self.subTest(victim=victim, host=host):
                    result = self.audit(policy_log(victim, host))
                    self.assertTrue(result["complete"], result)
                    self.assertEqual(result["zeroVetoCount"], 1, result)
                    self.assertEqual(result["decisionCount"], 1)
                    self.assertIn("not authenticated", result["scope"])

    def test_old_witness_behavior_and_expected_nonwitness_are_separate(self):
        old = AUDIT.audit_text(hit_log())
        self.assertEqual(old["nativeHitTrace"]["witnessCount"], 1)
        self.assertFalse(old["damagePolicyTrace"]["schemaRecorded"])
        veto = AUDIT.audit_text(policy_log())
        self.assertFalse(veto["nativeHitTrace"]["complete"])
        self.assertEqual(veto["nativeHitTrace"]["witnessCount"], 0)
        self.assertEqual(veto["damagePolicyTrace"]["zeroVetoCount"], 1)

    def test_numeric_enum_and_reason_mapping_not_trusted(self):
        for key, value in (("action", 0), ("reason", 7), ("supported", 0), ("sourceClass", 6),
                           ("sourceClass", 7), ("zeroResult", 5), ("role", 255)):
            with self.subTest(key=key, value=value):
                self.rejected(policy_change(policy_log(), "event", **{key: value}))

    def test_matrix_order_and_special_scopes(self):
        raw = {"ownerThread": 1, "contextAvailable": 1, "role": 2, "readMask": 15,
               "flags": 0, "stat": 0, "amount": 8, "kind": 1, "victimClass": 1, "sourceClass": 3}
        controls = [({}, (1, 8, 1)), ({"victimClass": 3, "sourceClass": 0}, (1, 7, 1)),
                    ({"ownerThread": 0}, (0, 1, 0)), ({"contextAvailable": 0}, (0, 2, 0)),
                    ({"role": 0}, (0, 0, 0)), ({"readMask": 6}, (0, 3, 0)),
                    ({"flags": 2}, (0, 4, 0)), ({"stat": 1}, (0, 5, 0)),
                    ({"amount": 0}, (0, 6, 0)), ({"sourceClass": 0}, (1, 9, 1)),
                    ({"sourceClass": 5}, (0, 10, 1)),
                    ({"role": 1, "victimClass": 5, "sourceClass": 1}, (0, 11, 1)),
                    ({"role": 1, "victimClass": 5, "sourceClass": 4}, (0, 12, 1)),
                    ({"victimClass": 5, "sourceClass": 1}, (2, 13, 1)),
                    ({"victimClass": 5, "sourceClass": 1, "kind": 5}, (1, 14, 1)),
                    ({"victimClass": 5, "sourceClass": 1, "kind": 6}, (1, 14, 1))]
        for changes, expected in controls:
            self.assertEqual(AUDIT.policy_expected({**raw, **changes}), expected)

    def test_full_frozen_actor_matrix(self):
        # Independent specification table: actor enum order is 0..6, rows
        # victim, columns source. Compare actions for both supported roles.
        host = ((1,1,1,1,1,1,1), (1,0,1,1,0,0,0), (1,1,1,1,1,1,1),
                (1,1,1,1,1,1,1), (1,1,1,1,1,1,1), (1,0,1,1,0,1,1), (1,1,1,1,1,1,1))
        client = ((1,1,1,1,1,1,1), (1,0,1,1,0,0,0), (1,1,1,1,1,1,1),
                  (1,1,1,1,1,1,1), (1,1,1,1,1,1,1), (1,2,1,1,1,1,1), (1,1,1,1,1,1,1))
        for role, table in ((1, host), (2, client)):
            for victim in range(7):
                for source in range(7):
                    facts = dict(ownerThread=1, contextAvailable=1, role=role, readMask=15,
                                 flags=0, stat=0, amount=8, kind=1, victimClass=victim, sourceClass=source)
                    action, _, supported = AUDIT.policy_expected(facts)
                    self.assertEqual((action, supported), (table[victim][source], 1))

    def test_unknown_driver_or_ai_membership_never_certifies(self):
        for kind in (0, 2, 4, 6):
            text = policy_change(policy_log(), "event", sourceClass=kind)
            # Give the producer a matrix-consistent decision, still not raw proof.
            reason = 9 if kind == 0 else 8 if kind == 2 else 10
            text = policy_change(text, "event", reason=reason, action=1 if kind in (0, 2) else 0)
            self.rejected(text)

    def test_raw_remote_identity_must_be_type0_and_noncanonical(self):
        for change in ({"type": 1}, {"type": 4}, {"actor": "140100000"}, {"readMask": 255}):
            text = hit_change(policy_log(), "actor", {"subject": 1}, **change)
            text = policy_change(text, "actor", {"subject": 1}, **change)
            self.rejected(text)

    def test_other_reported_class_must_fit_raw_metadata(self):
        # Remote source still forces ZeroHp in the pure matrix, so these cannot
        # be rejected merely by re-evaluating the reported action/reason.
        for victim_class in (3, 4, 5, 6):
            text = policy_change(policy_log(), "event", victimClass=victim_class,
                                 reason=7 if victim_class == 3 else 8)
            self.rejected(text)

    def test_raw_authority_actor_and_context_cannot_disagree(self):
        for kind, match, values in (("actor", {"subject": 0}, {"status": "140500001"}),
                                   ("actor", {"subject": 1}, {"objectId": 99}),
                                   ("context", None, {"generation": 10}),
                                   ("input", None, {"ownerHandle": 99})):
            self.rejected(policy_change(policy_log(), kind, match, **values))

    def test_raw_post_metadata_and_hp_must_match(self):
        for subject in (0, 1):
            for field, value in (("hp", 1), ("status", "140600099"), ("type", 1),
                                 ("team", 3), ("namePrefix", "5F46"), ("readMask", 255)):
                self.rejected(hit_change(policy_log(), "actor", {"phase": 1, "subject": subject}, **{field: value}))

    def test_full_active_context_and_roster_are_required(self):
        for change in ({"roster0": 7}, {"roster1": 0}, {"roster2": 8589934594},
                       {"roster2": 1 << 64}, {"contextAvailable": 0}, {"ownerThread": 0}):
            self.rejected(policy_change(policy_log(), "event", **change))
        for change in ({"available": 0}, {"readMask": 63}, {"epoch": 0}, {"loadSerial": 0},
                       {"role": 0}, {"slot": 0}, {"location": "000500060000123402010301"}):
            self.rejected(policy_change(policy_log(), "context", **change))

    def test_actual_revalidation_and_write_required(self):
        for key in ("revalidationAttempted", "revalidationPassed", "zeroAttempted"):
            self.rejected(policy_change(policy_log(), "event", **{key: 0}))
        for result in (1, 2, 3, 4):
            self.rejected(policy_change(policy_log(), "event", zeroResult=result))

    def test_claim_is_not_veto_evidence(self):
        for attempted, queued in ((1, 0), (1, 1), (0, 1)):
            self.rejected(policy_change(policy_log(), "event", claimAttempted=attempted, claimQueued=queued))

    def test_manual_sync_filters_and_unchanged_hp_alone_are_insufficient(self):
        for key in ("syncDrop", "manualFilterOn", "manualDrop"):
            self.rejected(hit_change(policy_log(), "input", {"phase": 0}, **{key: 1}))
        self.rejected(policy_change(policy_log(), "event", zeroAttempted=0, zeroResult=1))
        self.rejected(hit_change(policy_log(), "input", {"phase": 1}, damage=8))

    def test_applied_bit_and_preinput_projection_are_checked(self):
        for flags in (0, 1, 3, 4):
            self.rejected(hit_change(policy_log(), "input", {"phase": 1}, flags=flags))
        for key, value in (("flags", 2), ("amount", 7), ("stat", 1), ("kind", 5), ("readMask", 7)):
            self.rejected(policy_change(policy_log(), "event", **{key: value}))

    def test_zero_children_can_be_checked_but_nonzero_hp_child_cannot(self):
        result = self.audit(policy_log(children=True))
        self.assertEqual(result["zeroVetoCount"], 1, result)
        for change in ({"delta": -1}, {"delta": 1}, {"matching": 0}, {"returned": 0},
                       {"unwound": 1}, {"actor": "140200000"}):
            self.rejected(hit_change(policy_log(children=True), "child", {"index": 0}, **change))
        no_write = hit_change(policy_log(children=True), "child", {"index": 1}, result=0)
        self.assertEqual(self.audit(no_write)["zeroVetoCount"], 1)
        self.rejected(hit_change(no_write, "child", {"index": 1}, delta=-1))
        self.rejected(hit_change(no_write, "child", {"index": 1}, takeSeq=0))
        self.rejected(hit_change(policy_log(children=True), "child", {"index": 1}, result=23))
        self.rejected(hit_change(policy_log(children=True), "child", {"index": 1}, takeSeq=0))
        self.rejected(hit_change(policy_log(children=True), "actor", {"subject": 2, "phase": 1}, hp=23))

    def test_event_completion_and_global_loss_disable_proof(self):
        for key, value in (("coverageStable", 0), ("lossStable", 0), ("metadataStable", 0),
                           ("contextStable", 0), ("ownerThread", 0), ("returned", 0), ("unwound", 1),
                           ("nested", 1), ("overflow", 1), ("parent", 99), ("depth", 2)):
            self.rejected(hit_change(policy_log(), "event", **{key: value}))
        for key in ("dropped", "foreign", "unwound", "nested", "overflow", "unmatched"):
            self.rejected(hit_change(policy_log(), "summary", **{key: 1}))
        self.rejected(hit_change(policy_log(), "ready", installedMask=3))
        self.rejected(hit_change(policy_log(), "summary", coverageSerial=2))
        self.rejected(hit_change(policy_log(), "event", coverageSerial=2))

    def test_missing_malformed_duplicate_and_unknown_records(self):
        text = policy_log()
        for record in [r for r in text.splitlines(True) if r.startswith("[damagepolicy]")]:
            self.rejected(text.replace(record, "", 1))
            self.rejected(text.replace(record, record + record, 1))
        for suffix in ("[damagepolicy broken\n", "[damagepolicy] alien schema=1 seq=1\n"):
            self.rejected(text + suffix)
        self.rejected(policy_change(text, "event", schema=2))
        self.rejected(policy_change(text, "event", mystery=1))
        self.rejected(policy_change(text, "event", zeroResult="bogus"))
        self.rejected(text.replace(" revalidationPassed=1", "", 1))
        self.rejected(text.replace(" zeroResult=0", " zeroResult=0 zeroResult=0", 1))
        self.rejected(policy_change(text, "input", damage=1 << 31))
        self.rejected(policy_change(text, "actor", {"subject": 1}, namePrefix="10000"))

    def test_orphan_and_late_envelope_do_not_certify(self):
        self.rejected(policy_change(policy_log(), "event", seq=99))
        text = policy_log().splitlines(True)
        final = text.pop()
        self.rejected(final + "".join(text))
        self.rejected("".join(text))
        self.rejected(policy_log().rstrip("\n"))

    def test_loss_anywhere_in_mixed_log_and_encoding_prevent_certification(self):
        self.rejected(policy_log() + "[hittrace broken\n")
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "policy.log"
            path.write_bytes(policy_log().encode() + b"\xff\n")
            result = AUDIT.audit_paths([path])["logs"][0]["damagePolicyTrace"]
        self.assertEqual(result["zeroVetoCount"], 0)
        self.assertFalse(result["complete"])

    def test_unenveloped_scope_still_contributes_global_coverage(self):
        first = policy_log().splitlines(True)
        second = [row.replace(" seq=1 ", " seq=2 ") for row in policy_log(True).splitlines(True)
                  if not row.startswith("[damagepolicy]") and not row.startswith("[hittrace] ready ")]
        second[-1] = hit_change(second[-1], "summary", started=2, published=2, drained=2)
        mixed = "".join(first[:-1] + second)
        self.assertEqual(self.audit(mixed)["zeroVetoCount"], 1)
        for key, value in (("nested", 1), ("coverageMask", 3), ("returned", 0), ("lossSerial", 1)):
            self.rejected(hit_change(mixed, "event", {"seq": 2}, **{key: value}))

    def test_signed_and_healing_remote_veto_still_has_bounded_proof(self):
        for amount, kind in ((-8, 1), (8, 5), (8, 6)):
            text = hit_change(policy_log(), "input", {"phase": 0}, damage=amount, kind=kind)
            text = hit_change(text, "input", {"phase": 1}, kind=kind)
            text = policy_change(text, "input", damage=amount, kind=kind)
            text = policy_change(text, "event", amount=amount, kind=kind)
            self.assertEqual(self.audit(text)["zeroVetoCount"], 1)


if __name__ == "__main__":
    unittest.main()
