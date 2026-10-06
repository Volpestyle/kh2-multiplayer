# Native Sora visual profile (VUH-1489)

This development-only profile is **off by default**. It uses the existing injected Sora-clone driver and a real two-peer loopback session. It is not enabled in the friend package.

Run only from the canonical owned desktop rig, with reviewed pins and a fresh packet directory:

```powershell
python -B tools/rig/native_sora/run_profile.py --check --packet <packet>
python -B tools/rig/native_sora/run_profile.py --native-sora-visual --packet <packet> --review <lead-review.json> --foreign-inventory <absolute-files.json>
```

The packet pins the six exact products (game, DLL, runtime, relay, avatarctl and full rig kh2ctl), source helpers, canonical runner and selected native source. The review binds its pins hash. A packet can be spent once. Use the scenario runner's ownership/rig lock, never launch another game beside it. The loopback relay is explicitly bound to127.0.0.1:27794; a foreign listener refuses.

The only new memory operation is the previously reviewed native party selector leaf: in owned friend1, exact GoA row00/01/02/12 becomes00/00/02/12 by one u8 write at module+9ACDF5. One explicit native reload creates the clone; the unchanged networking path may also perform its initial native bootstrap reload. Current model/type/list/local-head/status and runtime owner provenance must be freshly qualified after arrival. Pre-arrival transitional bindings are retained and do not count as the observation. Every driver binding and sampled identity in the60-second interval must be the qualified native Sora; any later room load refuses.

Host actions are paired short horizontal inputs, jump and basic attack, with no magic, save menu, combat fixture, health/protection writer or reconnect. Native position tracks, raw stream/driver logs, read-only AvatarBridge observations and both clips are retained. The fixture explicitly requires native run2, jump3 and basic attack151; join those motions to the host recording and inspect the clips before reporting visual acceptance.

Both runtimes must stay alive throughout the interval, then exit naturally0 with Shutdown and a native puppet-release receipt. The tick budget is a loop-iteration limit, not a promised wall duration; a separate150s shutdown wait refuses. Cleanup stops only registered helpers, restores the target byte only when still00 or already original01 with owned PID/creation/module checks, requires exact full-row restoration, then immediately kills the owned games through kh2ctl. An unknown target value is never overwritten. All four save files, sandbox attempts and explicit foreign inventory are checked.

Known unsupported gates: cloned Sora may share native HP/status with local Sora; no separate combat/spell/status authority is claimed. Multiple remote player-class actors, cross-room lifetime, reconnect, arbitrary character selection and package usability remain open. Motion9 remains suppressed by the unchanged DLL because of the known clone Fire crash. The selector is save-backed RAM: SaveGuard and unchanged disk hashes do not make an attempted save permissible.

Evidence and the offline Roxas/Riku assessment: `build/rig/vuh1489-native-sora-candidate-20261006-02/`. Original165347 and first network180206 FAILs stay retained. Only the lead publishes acceptance.
