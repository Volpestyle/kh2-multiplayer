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

**Rule:** `g_inOurCall = true; callGameFunction(); g_inOurCall = false;` — simple, reliable, single-threaded safe.

## 5. Ghidra decompilation lies about argument counts

**Lesson:** Ghidra's decompiler frequently elides function arguments, showing `FUN_xxx()` with no args when the function actually takes 2-4 parameters via x64 calling convention (RCX, RDX, R8, R9, XMM0-3). Always cross-reference with the disassembly to see what's actually loaded into registers before a CALL.

**Example:** `FUN_1403c7c50()` appeared to take no arguments in the decompilation, but actually takes `(motCtrl, animId)` — it's the animation resolver that maps an animation ID to an internal motion index. Missing this made the call chain through `FUN_1403c88c0` harder to understand.

**Rule:** For any function call in decompiled output, check the 4-8 instructions before the CALL in the disassembly listing. The MOV/LEA instructions loading RCX/RDX/R8/R9 reveal the real arguments.

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
