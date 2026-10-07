# Enemy targeting of remote players (VUH-1515)

`KH2COOP_ENEMY_TARGET_REMOTE=1` (exactly `1`; default off). Set it on every machine of the
session. With the flag off nothing is hooked, swapped, written or sent. This flag and
`KH2COOP_PARTY_NATIVE=1` refuse each other (no joint fixture yet): enemy-target's `Configure` and
`PartyNative::Install` each stay off when the other flag is set.

## One damage path per mode

The host's `KH2COOP_ENEMY_MIRROR` decides the mode. The host carries the mode in every
`TargetAuthority` advertisement, so both sides agree.

| Mode | Host | Owner (client) | Who applies a Shadow's hit on the friend |
|---|---|---|---|
| **Forward** (mirror off) | Chooses targets, keeps the clone's team, forwards a zeroed clone hit as `RemoteHit` | Applies `RemoteHit`; cancels its local copy of the family's hits **only** while it holds a fresh Forward advertisement naming its slot | The host's simulation (`RemoteHit`) |
| **Mirror** (`KH2COOP_ENEMY_MIRROR=1`) | Chooses targets, keeps the clone's team. **No forward** | **No cancel** | The mirrored Shadow's own hitbox against the friend's real Sora, natively, so guard, dodge, i-frames and reactions apply |

## Host

- **The seam.** The bdscript syscall `target_search` (bank 1 index 121, `0x4303A0`) is replaced
  through its `.data` entry `exe+0x755B00`. The swap happens only after an exact 75-byte match and
  lasts for the host session.
- **What the replacement does.** It always runs the native selector first. Then it applies the
  policy only when the searching actor is a live, allowlisted enemy (type mob, team 2, HP > 0)
  **and** the native result is the canonical player form (mode 0 or 2, aux 0, naming the
  canonical player).
- **The write.** When a clone wins, the replacement writes `{clone handle, part 0}` into that
  enemy's `+0xBF8`. It saves the native handle and part, and puts both back on release.
- **Why the Shadow follows it.** Its `mode_battle` measures its attack distances to that slot in
  the same brain step: under 300 `btl_attack`, under 600 `btl_short`, otherwise `btl_long`.
- **Native team and collision.** A clone keeps its native team and collision **only while at least
  one tracked enemy holds our write naming it**. Otherwise it stays team 0 with no-collide, as on
  main, so non-allowlisted AI and bosses cannot hit it. While it is kept, any enemy can hit it, but
  those hits are zeroed (`RemoteVictim`), and only allowlisted ones are forwarded.
- **Forward mode.** A hit on a candidate clone from an allowlisted enemy is still zeroed by the
  existing DamagePolicy (`NonLocalPlayer -> ZeroHp/RemoteVictim`). After the zero succeeds it is
  forwarded once to the clone's owner as `RemoteHit`.
- **`TargetAuthority`** (reliable). It is sent on every change and at least every 60 frames.
  - It carries `{room tuple, seq, hostConnectionId, slotMask, familyMask, mode}`.
  - `slotMask` bit *s* is set only while the host is active, the syscall is swapped and slot *s*'s
    clone is a valid candidate.
  - On stop an empty mask is attempted, best effort only: in most stop cases the send is refused
    (the role is no longer Host, there is no session, or the generation is new). The guarantee is
    the owner's 1.5 s age-out. Per-slot changes while the host is active are sent at once.
  - The relay holds advertisements during an event hold (S8), as it drops RemoteHit there, so the
    owner stops cancelling within 1.5 s of an event.

## Owner (client)

- **Applying an admitted `RemoteHit`.** It goes to the canonical local player through the hooked
  native stat funnel: `ApplyStatDelta 0x3D2EB0`, with delta `-damage`, idx 0, no reaction.
- **Refusals.** A `RemoteHit` is refused when:
  - the player is not canonical;
  - a transition is pending;
  - an event or cutscene is running;
  - the player is dead or downed, or HP is 0;
  - the damage is outside `[1, 9999]`;
  - the ignore-hit timer `+0xD70` is above 0. The one exception is the i-frame episode our own
    cancel started: the timer was 0 before the cancelled hit, rose within 2 frames, and has only
    counted down since. That episode never applies during DownedSpike's revive grace. A guard,
    another hit or the revive grace raising the timer again ends the episode (S3/N2);
  - **Stale:** we have not been covered continuously for the last 1.5 s. This covers
    a delayed burst of RemoteHits after a downlink stall, whose local copies already landed (S7).
- **Local cancel** (`DamagePolicy Reason::HostEnemyAuthority`) requires all of:
  - role Client;
  - an allowlisted source family;
  - an advertisement from the current host connection, for our admitted epoch and room, at most
    1.5 s old, in Forward mode, with our slot bit and the family bit set. Its age is checked again
    at the cancel itself, and a pending transition clears the held view (N1);
  - **continuous coverage of at least 1.5 s** (S9), the same clock that gates applying a RemoteHit.
    During a warm-up, or after any lapse, local hits stay native and RemoteHits are dropped as Stale.
    Neither path then applies the attack twice, and the owner is not left immune.

  Anything else, including a missing, stale or refused advertisement, the host flag off, a refused
  swap, missing private status, a stale or ineligible clone, or Mirror mode, leaves the local hit
  native. Apart from the in-flight edge cases under Known limits, the owner is never immune.

## Policy (pure, `EnemyTargetRemote.hpp`)

**Candidates.**
- The local player, if canonical and live:
  - listed (`+0x120 & 0x10080000` clear);
  - not dead (`+0x9B8` bit 2 clear);
  - HP > 0.

  A downed local player is skipped.
- Remote clones 1 and 2. Each one needs all of:
  - an active, player-class puppet;
  - a native status distinct from the local player's;
  - a live actor;
  - a fresh pose (within 30 frames, and not an `AvatarHeld` pose from a stalled stream, VUH-1787) from the same world and room;
  - an owner that is neither downed nor in a cutscene, in slot 1 or 2;
  - a list handle that resolves back to the clone.

**Choice.** Deterministic, at each native search:
- nearest by horizontal distance;
- a 180-frame minimum hold;
- 150 u hysteresis;
- at most 3 enemies per remote clone (the local player is uncapped);
- 64 tracked enemies (when full, the native result stands);
- ties go to the lower index.

## Families

Allowlist by object id: **Shadow `302` (`M_EX020`)** only. It is verified two ways:
- **Statically**, from its BDX: m_ex020 PC 2275/2290/2078.
- **Live**, in VUH-1515 attempt07, run `20261007-033207`.

Other ids keep the native behaviour. `get player` stays native, because the Shadow uses it only in
its leave check. No pre-update write is needed: nothing else rewrites the slot between searches.

## Private status

A clone must have its own native status. With the flag on,
`privatestatus::EnableEnemyTargetScope()` adds **verified rooms only**. Each room has its exact leaf
row (`SAVE+0x3534+4*world`): today only BC courtyard 05/06, with row `00/00/02/12`.
`KH2COOP_NATIVE_SORA_PRIVATE_STATUS=1` is still required on the host.

The VUH-1513 Roxas-kit row `00/03/02` is not covered. With that kit, a clone outside GoA is never a
candidate. Since the advertisement then never names the slot, the friend's local hits stay native.

## Teardown

| Event | What happens |
|---|---|
| Room change (transition or load serial, or a pending transition) | Assignments are forgotten without writes, because the enemies are being torn down. |
| Role leaves Host, the session ends, or the generation changes | An empty advertisement is attempted (best effort). On the game thread, every slot that still holds exactly our write gets the saved native handle and part. The syscall entry is restored by compare-exchange. A runtime that is killed outright does not reset the world bridge, so the DLL keeps its role and nothing is restored until a graceful end (relay stop or a handled signal) or Shutdown. |
| Shutdown | The syscall entry only. No actor writes off the game thread. Handles we wrote may stay in live enemies' slots; once the clone handle no longer resolves, `0x4319D0` re-selects the player. |

## Known limits (Forward mode)

**Deliberate choices:**
- **The host hits a neutral-input, latency-delayed clone.** The friend's own guard, dodge,
  i-frames, Second Chance and Once More are not consulted. Mirror mode keeps all of these.
- **Damage is computed against the clone's stats**, not the friend's. This is accepted for now.
- **No flinch on the owner** (`reactFlag 0`).
- **A lethal `RemoteHit` in a drive form or summon** takes the native, non-gated death branch, the
  same branches DownedSpike's Kill refuses. That matches a native lethal hit, but it is not
  "downed".
- **Local replica Shadows on the owner are cosmetic for the family** while the advertisement
  names the owner.

**Known windows:**
- **Double-damage window (S5).** When the owner's damage context cannot be captured,
  DamagePolicy returns Native before the cancel is consulted, while the host may still forward.
  This is rare: it happens only while the session or roster is changing.
- **Small immunity window.** When a clone stops being a candidate on the host, the owner keeps
  cancelling until the new mask arrives, about one RTT. It cancels at most until the advertisement
  ages out, 1.5 s.
- **Coverage cutover (S7/S9).** Cancel and apply share one 1.5 s coverage clock. Before it is
  ready, local family hits land natively and RemoteHits are dropped as Stale. After that, local
  hits are cancelled and RemoteHits apply. The remaining edges are the in-flight pairs that
  straddle a cutover. A host swing whose local copy landed just before readiness, and whose
  RemoteHit is processed just after, can apply twice. One whose local copy was cancelled just
  before a lapse, and whose RemoteHit arrives just after, is lost. Both are bounded by one RTT
  per cutover.
- **Host advertisement period.** It is time-based (500 ms), so a low frame rate on the host does
  not cause lapses.

## Fixture

`build/rig/vuh1515-enemy-target-remote-20261007-01/live-fixture-01/`: runner, judge, controls and
an offline dry run. Mode A is Forward and mode B is Mirror.
