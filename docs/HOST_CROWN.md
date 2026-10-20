# Host crown

The host (network slot 0) wears a crown on every co-op screen. Since VUH-2480 it is KH2 Final
Mix's own crown, the one Sora earns with the three Proofs, attached to the host's head bone.
The painted rectangles from VUH-2415/VUH-2455 remain as the fallback.

## The game's crown (from game data, build 15194255)

- **Mesh.** `obj/P_EX100.mdlx` (`kh2_sixth.hed`), BAR type 4 model, skeletal group index 3:
  mesh 1, part 1, 48 triangles, alpha, texture 2. All 74 strip vertices are rigid on bone 140,
  which the model's skeleton data names `PART_HEAD` (BoneReferences[0]). There is no separate
  crown object; the mesh is always drawn and normally invisible.
- **Texture animation.** The model texture's third `TEXA` block fills texture 2 (64x64) with one
  of four sprites by slot: 154 bronze, 155 silver, 156 gold, 164 fully transparent (the default
  animation). The remastered asset carries them as a 256x1024 uncompressed BGRA DDS.
- **Unlock.** `FUN_1403A9E20` counts how many of items 593, 594 and 595 the player holds
  (`FUN_1403C4380(id, 100)`: key-item flag bits at `0x9ACF70`), indexes the byte table
  `{164, 154, 155, 156}` at RVA `0x5C4CA0`, and sets that texture-animation slot through
  `FUN_1403CD8A0` -> `FUN_1401A87A0` (actor `+0x7B0` model). The game's own tables
  (`03system.bin` item rows, `msg/us/sys.bar`) name the items Proof of Connection, Proof of
  Nonexistence and Proof of Peace ("...Perhaps it has changed Sora."). One proof is bronze, two
  silver, three gold. The co-op crown is always gold and changes nothing in the save.

## Sora wears the game's own crown (VUH-2502)

Sora's models carry the crown mesh themselves (`P_EX100` group 3, and every form and world skin). For
those rows (`Character::nativeMesh`) nothing is drawn over the frame. While the crown gate is open,
the game thread sets the wearer's texture-animation slot to gold 156 through `FUN_1403CD8A0(actor,
slot)`, as the Proof unlock does (`FUN_1403A9E20` -> `FUN_1403CD8A0` -> `FUN_1401A87A0`). The game then
draws its own crown with its own depth, sorting and effects. It is re-applied every 30 frames in case
the game resets it. When the gate closes or the wearer changes it is restored, only on a still-live
actor with the same model:
- a local Sora runs `FUN_1403A9E20` again and gets back his own Proof crown, whatever its colour;
- a remote copy goes back to the transparent slot 164.

No save or Proof state changes. The game thread logs `[crown-native] on|off|wearer ...`.

Live on PC1 (slot 4, Sora in the Beast's Castle Parlor, host + friend loopback, runs
`20261019-212133` and `20261019-212428`): `[crown-native] on` for the host's Sora and no overlay
draw. In the host's camera-orbit take the gold crown sits on Sora's head and is partly hidden behind
Donald.

## How Roxas's crown is drawn

1. **Asset** (`inject/src/CrownAsset.cpp`, pure parsing in `common/include/kh2coop/CrownProp.hpp`).
   At DLL start a background thread reads the player's own install: it finds `obj/P_EX100.mdlx` by
   MD5 in `Image\dt\kh2_*.hed`, reads that asset from the `.pkg`, decrypts and inflates it (OpenKH
   `OpenKh.Egs` format, MIT; the 256-byte key table is read from the running game at RVA
   `0x713540` and checked by SHA-256, so we ship no key and no game data), decodes the group
   (VIF unpack as in OpenKH's `VifUnpacker`/`VpuPacket`), checks the crown `TEXA` block and cuts
   out the gold sprite. About 125 ms; `[crown-asset] ready ...` or `[crown-asset] unavailable: ...`.
2. **Head** (`CrownInputs.hpp ReadHostHead`, game thread, own SEH frames). For the crowned actor
   (the local player on the host's screen; the host's remote actor on a friend's) it reads the
   objentry model name (`+0x918` -> `+0x08`), looks up the character row, and copies the bone's
   model-space matrix (`*(*(actor+0x7B0)+0x50) + 0x40*bone`), the actor's world matrix
   (`actor+0x808`) and the frame's view x projection (RVA `0xAD6CF0`). Head world = bone with its Y
   and Z columns negated, times the actor matrix: what `FUN_1403B50F0`/`FUN_1401A7AF0` compute.
   The head must be finite and within 400 units of the actor.
3. **Draw** (`SceneCrown.cpp`, VUH-2502). The crown's head-local vertices go through head x
   view-projection on the CPU. Each vertex gets the game's own scene depth (`z = w - 4`, see "Depth and
   the HUD"), and the triangles are recorded into the game's frame as alpha-blended textured triangles.
   They test and write the scene depth, so hair, scenery and characters in front of the head hide the
   crown, and the effects, particles and HUD drawn after it go over it. Behind the near plane, nothing
   is drawn. The draw uses no input layout or descriptor heap: the vertices and the gold sprite are
   root SRV buffers, and the shader samples the sprite bilinearly by hand. D3D12 objects are built on
   first use, with shaders compiled by `d3dcompiler_47.dll`. `[crown-scene] first crown in the game's
   frame (...)` is logged once.
   A frame with no insertion point (no scene bundle seen, an unknown pipeline, root bindings it can't
   restore) keeps the older Present-time draw over the finished frame (`RenderHook.cpp`).
   `KH2COOP_CROWN_SCENE=0` forces that path.

The VUH-2455 gate is unchanged: the crown shows only in network co-op field play with no event,
cutscene or menu, with the camera on the local player, and 4 s after an event. Any failure (asset,
unknown model, faulting read, pipeline) falls back to the painted marker; `KH2COOP_HOST_CROWN=0`
turns both off.

## Characters

Each row also has `pitchDeg`, a tilt back about the head bone's Z axis (its side-to-side axis) through
the centre of the crown's base. Roxas's `kRoxasPitch` stays 0 until James picks one of the VUH-2502
candidates (0, 10 or 20 degrees). Sora's rows use the game's own crown, which the pitch doesn't touch.

The crown is authored in Sora's head-bone space. `crownprop::Characters` maps a gameplay model
(objentry name) to its head bone and an offset in that bone's space. A row is added only when the
model's own skeleton puts that bone at exactly Sora's bind head frame (0, 141.2, -3.7, same axes),
checked from the installed models, and an offline render of the model in bind pose shows the crown
sitting on its head. Every row drops the crown 4.5 units along the head bone's X (its up
axis), `kCrownDrop`: placed natively it floated a few units above the hair, and James asked for it
lower (2026-10-19). The value comes from bind-pose renders and in-game stills of Roxas and Sora
from behind, in front and to the side:

| Models | Head bone | Offset | Notes |
|---|---|---|---|
| `P_EX100` (Sora) | 140 | -4.5 X | the crown's own model; live on PC1 |
| `P_EX100_BTLF/_MAGF/_TRIF/_ULTF/_HTLF/_KH1F` (Valor, Wisdom, Master, Final, Anti, Limit) | 140 | -4.5 X | each form model carries this crown group itself |
| `P_EX100_NM/_TR/_WI/_XM` and their six forms each | 140 | -4.5 X | Halloween Town, Space Paranoids, Timeless River, Christmas Town; clears the helmet and hood |
| `P_EX110` (Roxas), `P_EX110_BTLF` (dual-wield Roxas) | 113 | -4.5 X | Roxas live on PC1 |

Everything else keeps the painted marker. Mickey (`P_EX200`, `P_EX220`) has a different, much
smaller head frame (bone 82/115 at 56.7 units) and would need a tuned transform and scale; he is
playable only as a party kit, so no row until that is verified live. Donald and Goofy are never the
host's gameplay model, and the Lion King form (`P_LK100`) has no row.

## Events and close-ups

In close-ups the game may hide the gameplay model and play a separate event model. The crown never
follows either model there: it is drawn only while the VUH-2455 gate is open (network co-op field
play, no event context, no cutscene, no menu, camera on the local player's actor), and after an event
or cutscene it waits 4 s of open gate before it shows again. So the whole event, including its
close-ups and the hand-back, is crown-free on every screen.

## Depth and the HUD

A one-frame trace of the game's D3D12 recording on FM-02 (Computer Room save point, 2026-10-19) shows
how the frame is built. The trace was diagnostic and is not in the tree. An earlier note here said the
scene was D3D11; that was wrong. The game's only `D3D11CreateDevice` call (flags `0x820`, BGRA and video
support, no context) is the movie device.

- The scene is D3D12: one direct command list per frame executes about 90 bundles (type 1) in order.
  The bundles are recorded each frame when the scene changes and re-executed while it is static, so a
  trace can catch a frame with no bundle recording at all.
- Scene depth is a 1920x1080 `D32_FLOAT_S8X24` view, cleared to 1.0 at the start of the list, tested
  `LESS_EQUAL` (not reversed-Z).
- Main-target bundle order: world (opaque, depth write), characters (alpha blend, depth write, back-face
  cull), effects such as the save-point light pillars (depth test, no write), particles, post-process
  quads, then the HUD (depth func `ALWAYS`, writing depth). The characters are also drawn a second time
  into a 256x256 target with its own depth.
- The list clears the scene depth again just before `Close`. So the depth buffer is empty, and was
  overwritten by the HUD, long before Present: a Present-time draw cannot depth-test against the scene.
- Camera: the bundles' root CBV 2 holds the row-vector view matrix, and root CBV 0 (floats 20-35) holds
  the projection: x 1.299, y 2.309 (16:9), `z_clip = z_view - 4`, `w = z_view`. The CPU matrix at
  `0xAD6CF0` matches in x, y and w but carries `z_view - 2`, so its depth is not the GPU's.

So the Roxas crown (`SceneCrown.cpp`) is recorded into that direct list at one of two points:
- just before the first scene-target bundle whose first pipeline tests depth without writing it (the
  effects); or
- if the frame has none, just before the list binds a different depth target after the scene.

The scene target is the depth view, at least 1024 wide, of the first bundle that writes depth.
`SceneCrown` hooks pipeline creation (depth flags and formats per PSO), depth-view creation (width) and
the command-list calls that record bundles and root bindings. Before recording the crown it knows the
list's root signature and root arguments, with each bundle's bindings carried over to the calling list
as D3D12 bundles do. After the draw it sets them back, along with the list's pipeline. A list that used
root constants or root SRV/UAV views (KH2 uses only descriptor tables and root CBVs) gets no crown, and
the Present-time draw stands in. The game's camera comes from the CPU view-projection at `0xAD6CF0`
with the GPU's near distance (`crownprop::SceneNear` = 4). Reading the projection constant buffer
instead would follow a different near plane, but nothing shows one changes.

## Limits

- `SceneNear` (4) was read from FM-02's projection constant buffer. If a room used another near plane,
  the crown's depth would be off by the difference. That would show as the crown poking through or
  sinking into hair, not as a missing crown.
- Frames that fall back to the Present-time draw (and the painted marker) are not hidden by scenery
  and draw over the HUD and the game's effects.
- A host Sora who holds Proofs wears gold during co-op field play; his own colour comes back when the
  gate closes.
- On a friend's screen outside the prologue, a host Sora's body can be a party puppet with another
  model (in the Beast's Castle Parlor it is `P_EX020`, Goofy's actor). It has no row, so the friend sees
  the painted marker.
- On a friend's screen in the Roxas days without the prologue party (for example FM-02), the host has
  no model there, so the friend sees the marker.
- `0x713540` and `0xAD6CF0` are build-specific. A changed build fails the key-table hash (marker) but
  a moved view-projection is only caught by its sanity checks.

## Evidence

- Offline: the C++ loader matched the Python port of OpenKH on the installed asset (74 vertices
  within 5e-6, identical triangles, byte-identical gold texture). `tests/CrownPropTest.cpp` covers
  inflate, the EGS format, group decode, sprites, the head read and clip triangles on synthetic data.
- Live, PC1, FM-02 host and friend on loopback (2026-10-19, `crown_pair` scratch fixture, PASS):
  `[crown] present=... mesh=1 character=1 vertices=144` on the host, the gold crown on Roxas's head
  while he walks, no input beyond the pad mailbox, no save; protected saves unchanged.
- Live, PC1, the unskipped prologue party (Roxas + Roxas, `net_prologue_party` cut after event 97 with
  host clips, 2026-10-19): through the Armor Shop event 104 the host's crown is absent in every shot,
  including the medium shots of Roxas; the gate closed at its start and reopened after control came
  back at event 97, when the host's Roxas wears it again and the friend's clone does not.
- Live, PC1, slot 4 (Sora, LV 8, Beast's Castle) host + friend: `mesh=1 character=0`, the crown on
  Sora, including a gameplay camera right behind his head. Valor Form could not be triggered from the
  command menu there, so the form rows are verified from game data and offline renders only.
