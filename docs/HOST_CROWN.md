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

## How it is drawn

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
3. **Draw** (`RenderHook.cpp`, Present). The crown's head-local vertices go through head x
   view-projection on the CPU, are sorted far to near and drawn as alpha-blended textured triangles
   over the finished frame (D3D12 pipeline built on first use; shaders compiled with
   `d3dcompiler_47.dll`). Behind the near plane, nothing is drawn.

The VUH-2455 gate is unchanged: the crown shows only in network co-op field play with no event,
cutscene or menu, with the camera on the local player, and 4 s after an event. Any failure (asset,
unknown model, faulting read, pipeline) falls back to the painted marker; `KH2COOP_HOST_CROWN=0`
turns both off.

## Characters

The crown is authored in Sora's head-bone space. `crownprop::Characters` maps a gameplay model
(objentry name) to its head bone and an offset in that bone's space. A row is added only when the
model's own skeleton puts that bone at exactly Sora's bind head frame (0, 141.2, -3.7, same axes),
checked from the installed models, and an offline render of the model in bind pose shows the crown
sitting on its head:

| Models | Head bone | Offset | Notes |
|---|---|---|---|
| `P_EX100` (Sora) | 140 | 0 | the crown's own model; live on PC1 |
| `P_EX100_BTLF/_MAGF/_TRIF/_ULTF/_HTLF/_KH1F` (Valor, Wisdom, Master, Final, Anti, Limit) | 140 | 0 | each form model carries this crown group itself |
| `P_EX100_NM/_TR/_WI/_XM` and their six forms each | 140 | 0 | Halloween Town, Space Paranoids, Timeless River, Christmas Town; clears the helmet and hood |
| `P_EX110` (Roxas), `P_EX110_BTLF` (dual-wield Roxas) | 113 | 0 | Roxas live on PC1 |

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

Not done. A depth test against the game's scene depth would hide the crown behind scenery, but it
would not stop it drawing over the HUD: the HUD is drawn after the 3D pass and writes no depth. That
needs the crown recorded before the game's HUD command list, which means locating the game's
3D/HUD split in its D3D12 submission: more RE than this polish warrants.

## Limits

- Drawn over the finished frame without the game's depth buffer: the crown is not hidden by scenery
  or a character in front of the head, and it draws over the HUD (see above). The painted marker had
  the same.
- A host Sora who holds Proofs also shows the game's own crown at the same spot; ours is drawn over
  it, so he wears gold whatever his own colour.
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
