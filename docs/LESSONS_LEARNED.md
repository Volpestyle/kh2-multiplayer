# Lessons Learned — KH2 Multiplayer RE & Hook Development

Hard-won insights from reverse engineering and modifying a closed-source game at runtime. These aren't theoretical — every lesson below cost at least one session of debugging.

## 1. The game always has more writers than you think

**Lesson:** When you identify a memory field and hook one writer, assume there are others you haven't found yet. The field you care about is almost certainly written by multiple code paths, and the one you didn't hook is the one that will silently undo your work.

**Example:** `motCtrl+0x44` (animation current time) had THREE writers: the motCtrl tick advancing it, the vanilla AI resetting it via `FUN_1403c86a0`, and a hidden per-frame "motion playback system" resetting it via `FUN_1403c88c0` → `FUN_1403c8a40`. We found the first two in Session 4 but didn't discover the third until Session 5, when we used a CE data breakpoint to catch all writers.

**Rule:** Before building any hook strategy around a memory field, set a **data write breakpoint** on it first and let it run for a few seconds. Count the distinct RIP addresses. If there are more than you expected, you don't understand the field yet.

## 2. "NOP for friends" doesn't mean "irrelevant for friends"

**Lesson:** A function that returns early for friend entities (e.g., gated by a bit flag) might still be part of a chain that matters. The function itself does nothing, but the code that CALLS it may have side effects, or the call site's fallthrough path may be where the real work happens.

**Example:** `FUN_1403d5e50` (movement dispatch) is gated by `actor+0x9B8` bit 2 — both the decel handler and accumulator return immediately for friends. Hooking it and replacing the speed delta had zero effect. The actual animation path for friends goes through the behavior timer → `FUN_1403c86a0`, a completely separate code path that shares no call ancestry with the movement dispatch.

**Rule:** When a function is "NOP for entity type X," trace the CALLER to find what path type X actually takes. Don't assume the function you found is the only path.

## 3. Per-frame calls destroy override strategies

**Lesson:** If the game calls a function every frame that resets the state you're trying to override, calling your override "just once" or "only on change" will fail — the game's per-frame call will undo it on the very next frame. You must either (a) suppress the game's per-frame call, or (b) also call your override every frame without side effects.

**Example:** The vanilla friend AI calls `FUN_1403c86a0` every frame (through the behavior timer). This creates a new motion object each time, resetting animation to frame 0. Even after we stopped calling the AI, a SECOND per-frame caller (`FUN_1403c88c0` from the motion playback system) continued resetting the time via the blend path in `FUN_1403c8a40`. We had to block BOTH callers.

**Rule:** If your override works for one frame then reverts, the game has a per-frame writer you haven't found. Use a data breakpoint to find ALL writers before designing the hook.

## 4. Guard flags beat parameter sniffing

**Lesson:** When you need your hook to distinguish "our call" from "the game's call" to the same function, use an explicit boolean guard flag (`g_inOurAnimSet = true`) rather than trying to infer the caller from parameter values, return addresses, or call stack inspection.

**Example:** We needed `HookedMotionChainSetAnim` to pass through our own calls to `FUN_1403c88c0` (via `FUN_1403c86a0`) while blocking the game's per-frame calls. The game's calls had non-zero blend params while ours had zero, but relying on parameter values is fragile. The guard flag is unambiguous and costs nothing.

**Rule:** An explicit guard distinguishes an intended call scope. Restore it on
every normal and unwind exit. Thread-local state can be inherited by another
fiber on the same thread, and nested native callbacks can reenter it; the guard
alone grants neither frame ownership nor creator exclusion. Qualify the actual
native activation separately when those properties matter. The
[executed construction controls](../build/rig/native-resource-serialization-20261004-01/receipt.md)
retain the two-fiber false-positive case.

## 5. Ghidra decompilation lies about argument counts

**Lesson:** Ghidra's decompiler frequently elides function arguments, showing `FUN_xxx()` with no args when the function actually takes 2-4 parameters via x64 calling convention (RCX, RDX, R8, R9, XMM0-3). Always cross-reference with the disassembly to see what's actually loaded into registers before a CALL.

**Example:** `FUN_1403c7c50()` appeared to take no arguments in the decompilation, but actually takes `(motCtrl, animId)` — it's the animation resolver that maps an animation ID to an internal motion index. Missing this made the call chain through `FUN_1403c88c0` harder to understand.

The saved552430 factory caller is another consequential example: decompilation
showed a constant-zero object ID, but no instruction from function entry to the
factory call defines ECX. It forwards the caller's arbitrary ID and then
dereferences the returned pointer without a null check. Classifying it as a
non-Shadow constant caller would permit an unsafe creation fence. The
[byte-checked correction](../build/rig/native-resync-creator-admission-feasibility-20261004-01/decision.md)
supersedes that entry in the earlier frozen caller matrix.

The later [entry-binding review](../build/rig/native-creator-552430-owner-binding-20261004-01/decision.md)
found no saved direct/literal caller or export. Preserve both conclusions:
unsafe behavior if entered does not establish that a live ingress exists.

**Rule:** Trace each argument register to its actual defining instruction or
function entry; a short window before CALL may miss an inherited argument.
Check the caller's post-return use before assuming a failure value is safe.

## 6. QWORD writes can affect two DWORD fields

**Lesson:** A single 8-byte write can atomically modify two adjacent 4-byte fields. The decompiler may show this as `*(undefined8*)(ptr + 0x50) = 0` which looks like it's writing one field, but it's actually zeroing both `+0x50` and `+0x54`.

**Example:** `FUN_1403c86a0` writes `*(undefined8*)(motCtrl + 0x50) = 0`, which clears both the queue size (+0x50) and queue index (+0x54). Missing this would have led us to believe the queue index wasn't reset, potentially causing the tick to take the CHAIN path with stale queue entries.

**Rule:** When the decompiler shows a QWORD (8-byte) write to a struct, check whether the next 4-byte field in the struct is also affected.

## 7. "Skip AI" only works if you handle ALL of AI's side effects

**Lesson:** The friend AI does more than just decide animation — it maintains timers, state machines, behavior flags, and follow-steering parameters. Skipping it entirely works for fixing one problem (animation) but creates a "debt" of unhandled side effects that will surface later.

**Example:** We skip the vanilla friend AI for controlled friends, which fixes animation. But this also disables combat reactions, ability triggers, and battle targeting. The behavior timer at `actor+0xDC0` stops being serviced. These are acceptable tradeoffs NOW but will need selective re-enablement for combat.

**Rule:** When skipping a major game system, document exactly what else it was responsible for. "Skip AI" is never just about AI — it's about everything the AI function touched.

## 8. The entity update call order is law

**Lesson:** The order of calls inside `PerEntityUpdate` determines what can override what. Any hook that runs BEFORE the motCtrl tick will have its writes overwritten by the tick. Any hook that runs AFTER physics will have the final word on position/velocity.

```
1. vtable+0x10 → AI dispatch        (animation decisions made here)
2. FUN_1403c6740 → motCtrl tick      (animation TIME advanced here)
3. vtable+0x18 → pre-physics
4. vtable+0x78 → ???
5. EntityPositionPhysics              (position/velocity integrated here)
```

**Example:** Session 4 tried calling `FUN_1403c86a0` from the AI hook (step 1), but the tick (step 2) processed the animation. When something in steps 3-5 called `FUN_1403c88c0` and reset the time, it happened AFTER our tick had advanced it. Understanding this order was essential to placing our block correctly.

**Rule:** Map the full call order first. Then design hooks to run at the right point in the chain.

## 9. CE data breakpoints are the fastest path to truth

**Lesson:** When stuck on "why does this value keep changing," stop theorizing and set a hardware data breakpoint. In 500ms you'll have every writer's RIP address, their frequency, and the values they write. This is faster than any amount of Ghidra analysis.

**Example:** We spent Session 4 trying increasingly clever animation override strategies. In Session 5, a single data breakpoint on `motCtrl+0x44` immediately revealed two writers alternating at 60Hz — the tick advancing time and `FUN_1403c8a40` resetting it. The fix was obvious once we saw the data.

**Rule:** If a field has unexpected behavior, breakpoint it BEFORE reading code. 30 seconds of runtime data is worth 30 minutes of static analysis.

## 10. Test at every distance from Sora

**Lesson:** Friend entity behavior in KH2 is distance-dependent in non-obvious ways. The follow-distance thresholds (idle < ~30 units, walk < ~100, run > 100) affect not just the vanilla AI's animation choice but also internal state flags, motion table modes, and behavior timer thresholds. Always test controlled-friend behavior at multiple distances.

**Example:** Our initial animation fix appeared to work because we tested while standing at a distance where our target animation happened to match what the vanilla system would have chosen. Moving closer/farther revealed the "stuck at frame 0" bug that only manifested when our choice DIFFERED from the vanilla distance-based selection.

**Rule:** For any friend-entity change, test at: on top of Sora (0 distance), walk range (~50 units), run range (~150 units), and edge of follow range (~300 units).

## 11. Relocated prologues need tested unwind behavior

The finite `107240` package callback starts with four pushes in five bytes.
MinHook v1.3.3 copies these into a generated trampoline without registering
unwind metadata. Registering the matching push codes alone still failed an
actual Windows `RtlVirtualUnwind` control at the trailing `FF25` jump: Windows
treated that encoding as an epilogue and returned saved RDI as caller RIP.
Increasing `SizeOfProlog` to19 also failed. A validated `mov R11,imm64; jmp R11`
tail plus the matching prefix metadata passed the owned-buffer controls.
This choice is specific to the inspected body, which has no incoming R11
parameter; it is not a generic trampoline rewrite.

Check actual generated instruction boundaries and Windows unwind behavior
before enabling a stack-changing entry hook. Keep the function table, metadata
and code alive together until execution is quiescent. Retaining metadata while
`MH_Uninitialize` frees the trampoline is not safe teardown. The
[executed design packet](../build/rig/native-resource-dispatch-design-20261004-01/design.md)
retains the failing controls and the corrected finite candidate; production
installation remains a separate gate.

For chained runtime entries, check alignment relative to their common RVA base,
not only absolute metadata alignment. An unaligned base produces unaligned
UnwindData even when the metadata address is aligned. The
[shared-generator review](../build/rig/native-owned-emitter-code-independent-review-20261004-01/review.md)
retains that corrected guard and its executed negative control.

Set the retained-resource latch before registering an OS function table, and
keep code, metadata and module lifetime together even if registration actually
succeeds but its caller sees failure. A failed operation cannot prove that no
observer retained its pointer. The [preparation review](../build/rig/native-owned-emitter-install-independent-review-20261004-01/review.md)
executes that ambiguous failure against Windows and verifies permanent retention.
Default-off preparation must return false when its Boolean promises a prepared
view; disabled success would contradict that API even with creation still held.

## 12. Preserve AL semantics in resource callback evidence

The native `107240` package callback uses AL as its logical result. In the
203.8-second natural-resource run, every logged AL0 return still had nonzero
upper RAX bits. Treating the entire opaque register as a Boolean would report
failures as successes. Forward the full register unchanged, derive AL separately,
and qualify either as returned evidence only after normal return.

The selected Shadow records actually supplied `.a.us` names and reached AL1
after unsuccessful earlier package callbacks. An older all-AL0 filename probe
therefore cannot establish complete native routing or resource absence. Preserve
the actual arguments, caller, parent bytes and coverage limits. Both friends
also exhausted the512-receipt logger budget: complete parent records plus zero
logged children did not mean no callbacks. See the
[actual callback audit](../build/rig/native-resource-native-20261004-01/callback-review.md).

## 13. One dispatcher call can contain several irreversible attempts

This lesson applies to the **parked added-dispatch/A-B-C branch route**. Current
all-alive recovery changes the recorded activation input to the game's normal
scheduled original update; it adds no dispatcher call and does not inherit B1
creator admission or loader-installation requirements.

The ordinary emitter's null wrapper return at3FE98C advances to the next record
without assigning cooldown; its stage helper can also rescan. Recording failure
only after the dispatcher returns cannot stop subsequent creation attempts.
Nonnull assigns delay8, so one successful emission is not one attempted record.

Place a terminal stop or admission check inside the owned invocation before its
next irreversible entry, and preserve natural calls and native cleanup. Treat
dispatcher AL1, factory RAX, and independently ready record membership as distinct
results. The [saved-byte execution design](../build/rig/native-surviving-pack-execution-design-20261004-01/design.md)
records the actual null, mode/stage skip and outer-loop branches. The private
[A/B/C machine component](../build/rig/native-owned-emitter-abc-independent-review-20261004-01/review.md)
has independent executed controls for pre-entry, null-return and post-cooldown
stops, including stage rescans. Its
[versioned generator](../build/rig/native-owned-emitter-code-independent-review-20261004-01/review.md)
preserves those controls; that added-call production seam and creator boundary
remain unimplemented and unqualified.

For the direct assembly gateway, compare the forwarded register to the actual
dispatcher return-PC RAX. The native post-wrapper cooldown path can change its
upper bits before the dispatcher sets AL1; wrapper RAX and dispatcher RAX are
different evidence. The [gateway controls](../build/rig/native-owned-emitter-gateway-20261004-01/receipt.md)
retain the initially incorrect equality assertion and the corrected boundary
check. Compile the C++ and MASM files to distinct object basenames, and verify
the linked DLL's actual body and handler metadata rather than assuming assembly
compilation alone retained them.

## 14. Count completed native work and preserve input provenance

The current historical-input policy must record the exact host float4 from the
qualified native update that first returns a nonnull selected wrapper and then
completes normally. Periodic rounded capture logs, dispatcher AL1 and native
counts5/5 do not prove that event. Pending actor metadata can be unavailable at
nonnull return; join the exact record and normal-return cache attachment, then
wait for independently ready full-set membership before HP reconciliation.

Native initialization can finish before the load-complete hook publishes its
new serial. Bind the witnessed initialization once at its first qualified host
update, allowing only the same load or exactly+1 with unchanged transition/NOW,
pointers and definition. Never restamp a later update or revive a refused
first-emission record. Header+E0/1 is an explicit comparison exception because
the selected native type-2 body joins both values before emission; retain both
raw values and never rewrite the marker to manufacture equality.

Two lease copies in one update are not two completed updates. Record successful
original returns per controller; a single last-return slot can be overwritten
by another controller. The bounded64-slot table never evicts and refuses on
overflow. Count distinct completed selected-controller identities, not gaps in
the global sequence. Historical512 and live-input120 budgets retain the same30s
transaction deadline, including pending/deferred progress and partial outcomes.
Hold selected claims until the whole exact set reconciles, and verify survival
after live input resumes. A fresh Bootstrap transaction reset must not erase
the parked owned gateway's sticky tombstones.

Finally, label where a fixture failed. The132438/133512 activation-replay runs
stopped at the pre-reconnect observer, not recovery. An outdated WorldBridge10
validator against current11 is an observer compatibility defect, not native
population evidence. Its correction and a later successful observation still
cannot retroactively pass either run. See the
[current scenario record](SCENARIOS.md#historical-activation-replay-single-cycle-2026-10-04).

Check the actual transaction route before choosing a live fixture. Automatic
ClientHello cached-world bootstrap and an explicit ResyncPlan are separate paths;
adding metadata to the latter does not make it reachable from the former. Run134727
passed ordinary rejoin at5/5/5 but never invoked the historical-input receiver.
An enabled feature and matching enemies are not proof it ran. Establish the
missing-population negative control, require production attempt/verification
receipts, and label forced-resync scope before claiming automatic recovery.

## D3D12 queue identity and capture failures (2026-10-05)

A DIRECT queue on the same device can still differ from the swapchain's
presentation queue. The overlay dump established distinct controlling COM
identities; the corrected live path matched its selected queue to all three
actual presentation slots. Check association separately from type/device.
Keep version-specific DXGI structure reads in ignored diagnostics, with exact
DLL/code/layout guards; they are not production offsets. Recheck fence
completion after an event wakes, including the removed-device sentinel, before
mapping or reusing resources. Never release a queue in a TLS destructor under
loader teardown.

KH2 can change Present threads during startup while keeping the same swapchain
and DIRECT queue. Guide attempts `014643` and `015527` recorded this both before
and after initial capture bound the queue. Permanent CPU-thread affinity blocked
later captures. Serialize injected CPU state across callers while retaining the
canonical queue and swapchain for the single GPU fence timeline; reject another
swapchain before touching capture jobs or the ring. A CPU-thread change alone
is not a GPU-queue migration.

Shared GDI state needs that same serialization and a flush before handoff.
Create the memory DC from a temporarily acquired screen DC, release the screen
DC on the acquiring thread, and publish the memory DC only after its DIB is
usable. A NULL-source memory DC becomes invalid when its creating thread exits.
See Microsoft's [DC lifetime contract](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createcompatibledc)
and [flush requirement](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-gdiflush),
and the [native source review](../build/rig/vuh1493-join-guide-dry-run-20261005-01/same-queue-handoff-review.md).
An owned native control reproduced the permanent-thread rejection; the candidate
completed capture and overlay refresh after that first thread exited. See the
[bounded handoff result](../build/rig/overlay-gpu-probe-20261005-01/thread-handoff-full-20261005-01/root02-result.md).

A title-menu timeout can be a capture failure: the current detector treats a
failed capture as no highlighted menu row. Retain the actual capture reply and
owner/freshness receipts before blaming loading or tracing. A puppet release
can also precede reactivation without retiring the network session; check role
and runtime identity separately, and scope puppet damage to actual ownership.
See the [reviewed overlay run](../build/rig/vuh1493-combat-progress-mac-relay-20261005-01/combined6-acceptance-review.md).

## Close the pre-injection save window (2026-10-07)

Installing the save guard after a game window appears leaves boot code free to
create, delete or rewrite save containers. An existing, nonempty container is
not a safety gate: invalid headers or a zero decoded payload can still trigger
destructive paths. Launch suspended, finish guard installation and initialization,
require its acknowledgement, then resume; fail closed if any step fails.

Verify explicit receipts: the DLL acknowledgement QPC must precede the launcher's
resume receipt, and guard installation must precede the first hooked Present.
Save hashes remain a backstop, not permission to save. PC1 run `20261007-172006`
loaded an existing save, passed both ordering checks and preserved every save
hash; see the [live result](../build/rig/vuh-saveguard-preinject-20261007-01/live-pc1/output/result.json).
This evidence does not include OS-wide transient-write tracing.
