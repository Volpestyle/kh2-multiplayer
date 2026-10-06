# Prior art: online co-op added to single-player 3D action games

Research for `docs/ONLINE_COOP_PLAN.md`, 2026-10-01. Statements are documented
in the linked source unless tagged **[I]** (inference or judgment) or **[S]**
(secondary or press source). Links were checked at research time; Nexus and
Reddit pages were read through Wayback copies where they blocked bots.

## 1. Kingdom Hearts prior art

### KH2 Online Coop (Expert595, Sept 2026)

[Repo](https://github.com/Expert595/kh2-multiplayer). The closest match to this
project's design, and very new.

- **Architecture.** An external client process (Python) hooks the game from
  outside; executable bytes are validated before hooks are installed and
  restored on exit. A TCP relay sends newline-separated JSON and forwards a
  player's state only to peers whose cell `(world, room, map, battle, event)`
  matches exactly ([server](https://github.com/Expert595/kh2-multiplayer/blob/main/KH2OnlineCoop/server/kh2coop_server.py)).
  Send rate defaults to 20 Hz, configurable 5–60 ([client](https://github.com/Expert595/kh2-multiplayer/blob/main/KH2OnlineCoop/client/kh2coop_client.py)).
  Epic Global 2021 and Steam Global use separate memory profiles but join the
  same session.
- **Puppets in the friend slots.** Remote players are projected into the
  Donald/Goofy slots: transform written every tick, velocity and acceleration
  zeroed, a follow timer pinned at 999, HP copied into the party slot. A detour
  on `MotionChainSetAnim` "deliberately blocks vanilla friend animation resets"
  ([animation_hook.py](https://github.com/Expert595/kh2-multiplayer/blob/main/KH2OnlineCoop/client/animation_hook.py)).
  These are the same techniques as this repo's Session 5 work.
- **Showing a remote Sora.** "The visible remote player is now requested
  through KH2's own party spawning logic." The playable-character selector is
  copied into an empty companion slot of the world party table (save
  `+0x3534`; the Epic Global 2021 save body is at RVA `0x09A7070`), and KH2
  spawns a native Sora (`P_EX100`, or `P_EX110` in Roxas areas) on the next room
  load. The README warns: "Do not save while REPLICA SLOT ACTIVE is shown."
- **Two instances on one PC.** "Local two-instance test" binds two clients to
  two KH2 process ids: "If KH2 allows direct double-launching, the tool will try
  to start the missing instances automatically. If Steam/Epic blocks the second
  launch, start the second KH2 process manually." Both copies may share save
  data, so it warns against saving from both.
- **Documented hazards.** An earlier approach that remapped Goofy's resources
  was dropped because it "could crash during room teardown"; the actor-list head
  "can briefly read as NULL while the game updates the actor list".
- **Status, in its own words:** "movement and independent-room
  synchronization, not complete synchronized combat." No license stated.
- **KH1 sibling, v0.9.5:** spawns a second Sora that "does not receive
  position, rotation, animation, or controller updates"
  ([KH1 repo](https://github.com/Expert595/kh1-multiplayer)).

### KH2 Co-op Mix (Snackya, 2021)

[Repo](https://github.com/Snackya/KH2-co-op-mix). Shared-hub co-op for the
randomizer: an external C++ client polls the room every 16 ms and, only when a
player enters the GoA hub, uploads their checks over HTTP and grants everyone
else's ([main.cpp](https://github.com/Snackya/KH2-co-op-mix/blob/master/src/main.cpp)).
Its hard-coded addresses broke on a game patch ("will not work on PC v1.0.0.8").

### ML64-KH2Module (Denoflions, 2021)

[Repo](https://github.com/hylian-modding/ML64-KH2Module). An unfinished
ModLoader64 binding to read and write KH2 PC memory from outside the process.
Its frame tick came from a hidden dummy N64 emulator, not KH2's loop. No online
plugin was built on it.

### Archipelago KH2 (2023 onward)

Not real-time, but the most mature networked KH2 memory client
([Client.py](https://github.com/ArchipelagoMW/Archipelago/blob/main/worlds/kh2/ClientStuff/Client.py)).

- **Reconciliation.** A 0.5 s loop reads save-data bit flags and re-asserts the
  expected state every loop (`verifyChests`, `verifyItems`, `verifyLevel`).
- **Load-zone guard.** Magic is granted only when `FadeStatus == 0` and the
  player gauge is live: it "Can only be given when the player is paused due to
  crashing in loadzones" ([RecieveItems.py](https://github.com/ArchipelagoMW/Archipelago/blob/main/worlds/kh2/ClientStuff/RecieveItems.py)).
- **Per-frame companion.** Death detection moved into a per-frame Lua script
  because "the polling rate for the client can miss a death"; the client and
  script talk through mailbox bytes in memory ([APCompanion](https://github.com/JaredWeakStrike/APCompanion)).
  Setting HP to 0 does not kill Sora on its own.
- **Versions.** Separate Epic and Steam address tables, plus a downloadable
  address file.
- **Game behaviour** ([setup](https://archipelago.gg/tutorial/Kingdom%20Hearts%202/setup/en),
  [game page](https://github.com/ArchipelagoMW/Archipelago/blob/main/worlds/kh2/docs/en_Kingdom%20Hearts%202.md)):
  items arrive "anywhere outside a load or cutscene"; new magic needs a pause
  and a new room before it's usable; vanilla death reverts you "to the last
  non-boss room you entered".

### Other KH findings

- **Main-loop timing.** The original LuaBackend/LuaFrontend "aren't
  syncronized with the game's main loop", causing anything "from crashes to
  warps to incorrect locations"; the LuaBackend Hook fork runs scripts inside the
  loop ([README](https://github.com/Sirius902/LuaBackend)).
- **Engine facts (OpenKH docs,** [areadata.md](https://github.com/OpenKH/OpenKh/blob/master/docs/kh2/file/type/areadata.md)**).**
  Each room loads one `map`, one `btl` and one `evt` program, selected by story
  progress. A room's `Party` opcode can force `NO_FRIEND` (Sora only),
  `W_FRIEND_ONLY` or `DONALD_ONLY`. Room changes are
  `Jump(world, area, entrance, localset, fade)`. `Recov` heals the party and
  reverts forms on entry.
- **Missions and bosses.** Mission files flag boss battles, "No Leave", retry
  and "Mickey spawnable" ([msn.md](https://github.com/OpenKH/OpenKh/blob/master/docs/kh2/file/type/msn.md));
  `BOSS`-type objects need a finisher to die ([00objentry.md](https://github.com/OpenKH/OpenKh/blob/master/docs/kh2/file/type/00objentry.md)).
- **Dead ends.** KH3 "Re:Multiplayer" (2026) has only secondary coverage [S].
  BBS's Mirage Arena multiplayer is absent from the HD releases [S]. No KH2
  emulator netplay (the PCSX2 netplay fork targets fighting games and warns of
  desyncs). A KH2FM PS2 decompilation is in progress ([GovanifY/kh2](https://github.com/GovanifY/kh2)).

## 2. Other projects

| Project | Remote players | Enemies | Rooms / story | Notable |
|---|---|---|---|---|
| [OoT/MM Online (ModLoader64)](https://github.com/hylian-modding/Z64Online) | Custom puppet actor, 134-byte joint table + fields | Never shipped; server-side prototype abandoned ("Don't fight Gohma together") | Independent scenes; sync pauses in cutscenes; cutscene crashes | Save merge: flags OR-merged, counters max; ~1 core dev, 2019→2021 |
| [sm64ex-coop → sm64coopdx](https://github.com/coop-deluxe/sm64coopdx) | Real Mario slots re-simulated from action state + inputs | Nearest-player ownership, judged per client per tick; attacker-side hits; long tail of per-object fixes | Forced warps first; independent areas needed a 2021 rewrite; newcomer gets an area snapshot | Solo dev; first beta ~10 weeks |
| [Teamruns (OpenGOAL Jak 1)](https://github.com/JoKronk/teamruns-client) | Extra real Jak, state machine replicated by name | Kills mirrored by name; bosses by phase; AI not synced | Cutscenes non-blocking; queued events; replay after crash | Solo, 2023–24 |
| [Skyrim Together Reborn](https://github.com/tiltedphoques/TiltedEvolution) | Temporary NPC; hooks block AI tick, game position writes, `PerformAction` | First loader owns; ownership fights ("naked NPCs") 2022→2026 | No loading barrier | HP as deltas (drift); 10 Hz, 300 ms interpolation; all-reliable messages |
| [MTA:SA](https://github.com/multitheftauto/mtasa-blue) | Real peds with pad/camera swap | Server-assigned syncer per ped (nearest, hysteresis) | Story never loaded; interior triggers disabled | Victim-side player HP; first death report wins |
| SA:MP | Spare player slot driven by keys | — | Story co-op dropped (2005) [S] | Shooter-side hits only arrived 2014 |
| [BotW Multiplayer](https://github.com/MilkBarModding/MilkBarLauncher) | 32 custom actors via the game's actor-create | Each world simulates its own; only HP syncs (min) | Main quest sync off | TCP request/response per tick; Cemu pinned |
| Elden Ring Seamless Co-op | Native phantoms + native netcode | Native, still patched | Host world canonical; party gates | Not comparable: reuses native netcode |
| [Sekiro Online](https://www.nexusmods.com/sekiro/mods/577) | P2P | "A degree of client prediction" | — | No pause; death-blow on dead enemy freezes |
| [SMO Online](https://github.com/CraftyBoss/SuperMarioOdysseyOnline) | `PuppetActor` per slot | — | Only moons sync; cutscene-crashing moons excluded | Pinned to game 1.0.0 |
| [SoH Anchor](https://github.com/HarbourMasters/Shipwright) | Real player converted to `DummyPlayer`, 24 joints per frame | None | Flags merged per team | Dummies leaked effect slots (crash) |
| [OOT True Coop](https://github.com/bghill95/OOT-True-Co-op/blob/enemy-sync/soh/soh/Network/Anchor/EnemySync.h) | (Anchor) | Host-authoritative: deterministic key, AI off + colliders kept on clients, hits via the enemy's own damage code, deaths as synthetic lethal hits, local AI takeover on stream loss | — | Closest published match to our enemy plan; source-based |
| [HKMP](https://github.com/Extremelyd1/HKMP) | — | Scene host runs AI; others die only when allowed; HP not synced | — | Players synced in ~2 weeks; enemies took ~3.4 years |

## 3. Synthesis

### What the prior art agrees on

- Remote players are puppets that only play back received state.
- Each player owns their own avatar; progress merges in one direction only,
  seeded from one save.
- Shared enemies are where projects stall (see the table).
- Almost nobody synchronized blocking story cutscenes. The only real attempt
  (CoopAndreas) ports GTA:SA missions one at a time.

### Pitfalls and how to design around them

1. **Writes made outside the game loop crash or warp the game** — write only on
   the game thread, at a fixed point in the frame.
2. **Writes during load zones crash** — queue them until a state gate (fade,
   load, cutscene, pause, controllable) says it's safe.
3. **Mutating shared loader data breaks teardown** (Expert595's Goofy remap) —
   never edit shared descriptor tables in place; never let mod state reach a
   save.
4. **Puppets fight the game's AI** — gate the puppet's AI tick, motion
   requests and game-originated position writes.
5. **Contested ownership** — the host owns every enemy; rooms and entities
   carry generation numbers.
6. **Unstable ids and local randomness diverge** — [I] key enemies by room +
   `btl` program + spawn-entry index + object id, and type-check every message.
7. **HP drift** — the host sends absolute HP, never deltas.
8. **Deaths must go through the game's own path** — setting HP to 0 doesn't
   kill Sora; bosses need finishers. Deliver kills as synthetic lethal hits
   through the enemy's own damage routine.
9. **Synced flags don't update already-loaded objects** — apply story flags at
   room boundaries, or reload the room.
10. **Fixed pools run out** — reusing the friend slots avoids extra spawns [I].
11. **Game-version churn** — handshake on a build hash; keep offsets
    data-driven.
12. **Transport** — unreliable sequenced channel for continuous state, reliable
    ordered channel for events.

### Hardest for KH2 [I]

1. Puppeting Sora on other players' machines (camera, room triggers, Reaction
   Commands, chests and Game Over are keyed to the player actor). The plan's
   local-primary model (D3) avoids this, as Expert595 does.
2. The game rewrites the party under you: `Party` opcodes, Drive Forms (Valor
   "uses Goofy"), Summons, world guests.
3. Mirroring host-run enemy AI without source code.
4. A cutscene-heavy story with no prior art for a synchronized barrier.

One advantage: KH2's enemies already target party members, so friend-slot
puppets should work as aggro targets on the host.

### Local native-Sora probe, 2026-10-06 (VUH-1489)

One reviewed, protected Steam probe replaced GoA friend1's Donald selector
`01` with contextual playable selector `00` at RVA `0x9ACDF5`, reloaded once,
then restored `01` and immediately killed the owned game. KH2 spawned a second
native `P_EX100` Sora. Existing AvatarBridge logs bind the synth to that clone and show
sampled transform application; Goofy remained. The strict run stays FAIL:
its radius assertion included the native state before first pose application.
All four save files and the sandbox were unchanged.

GO for a default-off, known-room visual candidate, estimated at 1–2 focused
engineering days plus review and qualification; NO-GO for package enable.
The two Soras share native status/HP storage. Room lifetimes, multiple-owner
binding, teardown/reconnect and combat/spells remain gates. Contextual Roxas
and independent per-player selection were not tested. Every receiver needs
an agreed archetype/moveset and verified owner-to-native-actor binding;
existing character bytes do not provide this behavior.

Evidence and limits: `build/rig/native-avatar-slot-spike-20261006-01/root-result.md`
and run `20261006-165347_native_sora_goa_friend1_replacement_once_1`.
