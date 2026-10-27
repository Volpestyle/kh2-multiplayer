# Mansion terminal ordering candidate (VUH-2884)

Code: `9e6b2e6`, based on `origin/main 9d36a74`, branch
`vuh2884-mansion-death-race`. Historical source runs used `bc8d9ad`.
This is an offline candidate, not a live acceptance result. Juno released the PC; a direct serial,
below-normal-priority CMake build is authorized. No game, desk, Steam, install
or save action was taken.

## Observed failures

Source root (read-only):
`C:/Users/volpe/repos/kh2-multiplayer/.local/wt/vuh2466/build/scenarios/`.
Line numbers below are one-based in the original inject logs.

| Run | Source | Decisive sequence |
| --- | --- | --- |
| `20261026-193245_net_prologue_mansion_hall_1` | host `157120` | `2343`: spawn5/netId6 disappears at HP44; `2410`: sent as `left=1`; `2424`: native mission-complete, frame6617; census closes; `2456`: spawn4/netId5 disappears at its retained HP16. No native funnel receipt for either wave-two body. |
| same | friend `188396` | `2665`: receives netId6 as leave; `2694`, `2707`: end defers with two living bodies. No netId5 death. |
| `20261026-194057_net_prologue_mansion_hall_host_down_1` | host `238900`, friend `162672` | Host records and publishes all six native deaths before its end. Friend applies all six, then enters its own native end event. At `2870` it releases following; its census is empty. No replay log; the old observer only reports Bailey's native-local end. |
| `20261026-194738_net_prologue_mansion_hall_friend_down_1` | host `99924` | `2619`: final netId5 funnel kill; `2620`: mission-complete closes gameplay; `2673`: death finally published after the event. |
| same | friend `236068` | End defers while netId5 is HP7, then **does replay**, `3167`, `ok=1 path=frame-cap`. Death arrives `3180`, applies `3208`, empty census `3209`. The 64 deferrals are not evidence of an empty roster blocking. |
| `20261026-180147_net_seat_activation_calling_1` | host `233804` | `2326`: group-one Dusk also sent as `despawned, not refilled, left=1`. |
| `20261026-200433_net_prologue_mansion_hall_host_down_1` | host `100968`, friend `179272` | `2340`: netId5 disappears at HP32; `2416`: leave=1. NetId6 remains at HP31 until the full-party game over at frame7155 (`2605`/`2606`), then disappears (`2659`). **No host mission-complete.** This disproves attributing every wave-two failure to the mission-ending kill. |
| `20261026-201309_net_prologue_mansion_hall_friend_down_1` | host `156668`, friend `89568` | NetId5 disappears at HP43 (`2138`), publishes leave=1 (`2200`); mission-complete at `2202`; netId6 disappears after the hold at HP32 (`2256`), with no death. Friend replays through the cap (`2619`). |

The base log proves absence won over death accounting; it does not identify
the missing native lethal caller. Dusks also have real alive removals. Neither
absence, a positive last HP, nor mission-complete alone licenses a synthetic kill.

## Candidate

- Reuse the byte-verified `3D4A40` lethal-notification detour as a default-on
  observer. Diagnostic hooks/storage remain opt-in. On normal return, require
  a newly set native dead flag and unchanged actor, objentry, status, controller,
  record, object ID, announced occurrence and complete session/load/room scope.
  Foreign calls, unavailable reads, native unwind and changed identities refuse.
- Retain a copied terminal receipt separately from sampled HP. The HP funnel
  remains a second source. Pending real deaths drain before mission snapshots,
  including gameplay holds, without dereferencing cached actors. Failed enqueue
  retains the death and holds back that mission publication.
- Reconcile client deaths before mission replay. The readiness check takes a
  fresh census and accepts empty or all-dead rosters; unavailable or living
  rosters defer. Record native-local hall completion once as well as replayed
  completion, bound to the retained admitted host mission-complete occurrence
  even after a later inactive snapshot. The three Mansion scenarios now require
  exactly one replayed or host-correlated native-local friend completion;
  a missing friend end no longer passes `<= 1`. The existing fallback cap remains.

## Offline controls

`kh2coop_mission_mirror_test` covers changed scope/roots, duplicate or unavailable
death edges, pending publication and empty/settled/living/unavailable rosters.
It replays exact selected host-log lines in
`tests/fixtures/mansion_terminal/`; `sources.json` pins each original log.
Run `194738` exercises drain of an actually observed final funnel kill during
the hold before mission publication. The base trace deliberately expects only
four observed native receipts: it never manufactures the two missing kills.

`kh2coop_nativehit_test` exercises the actual `EnemySync` adapter over owned
memory: positive-HP native terminal capture, changed record/load refusal,
enqueue refusal and retry, the held owner frame publishing a death before
mission state, no duplicate publication, client death before clean-census end,
and a native-local end bound to a retained admitted host completion. An
independent local end, wrong load and end admitted during travel earn no receipt.

`kh2coop_lifecycletrace_test` exercises the real detour wrapper with synthetic
native functions: exactly one native invocation, diagnostics off, LastError
preservation, duplicate notification, changed scope, unregistered caller and
propagated native exception without completion. It installs no hooks and opens
no game, bridge or process.

## Result

- Full serial Release build passed on `9e6b2e6` / `9d36a74`.
- Serial CTest: **277/277 passed**, 264.87 seconds, including the actual adapter,
  detour, log-excerpt controls and the updated wave-order scenario tests.
- All three Mansion scenarios passed offline structure/expression validation.
- [Verification receipt](https://uploads.linear.app/75f1d1f0-542b-4095-9967-fd7b27093472/8f4cd219-2a56-449f-8cb9-59eec52dc353/d6dff93f-6b9d-4fb6-b3fb-2335899b3c7f)
  is attached to VUH-2884. It pins the code, fixture provenance, build/test logs
  and staged DLL. Local logs: `.local/build-rebased-vuh2884.log`,
  `.local/ctest-rebased-vuh2884.log`, `.local/scenario-validation-vuh2884.log`.

Independent review and the lead's live reruns remain: base, host-down and
friend-down must each prove 6/6 actual deaths applied and one mission end settled
before room exit. Native-notification coverage of the missing historical
wave-two bodies remains unverified. No live run or Thorn victory testing occurred.
