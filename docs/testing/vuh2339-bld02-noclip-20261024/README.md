# VUH-2339 bld replay without clips: bot01r-ac5280e-bld02-noclip (2026-10-24)

This run separates bld's hardware from run-to-run variance (lead #19). It is the same `ac5280e` products
and bot01 replay as bld01 (`../vuh2339-bld01-20261024/`), with every clip removed:
- the scenario variant `net_twilight_thorn_bots_noclip.json` (included here) drops steps 28, 29 and 33
  (`clip`, `clip`, `clip_wait`) and the `clips` lists on the 11 fight blocks;
- the fight block timing, samples and asserts are unchanged.

**Gate:** admitted at launch level at 05:07:25Z (B signed in, 41 min idle) and finished at 05:23:34Z.
No reclaim, no needs-verify, lease released. **Closing checks:** install unchanged (538 files), saves
unchanged, safety passed.

| run | host fps (overall / p10) | 202 HP/s | host 432s (in band) | host chip drops | Thorn |
|---|---|---|---|---|---|
| PC1 step 3 | 59.8 / 56.6 | 0.84 | 34 (19) | 6 / 37 HP | min 175 |
| bld01, clips on | 50.6 / 30.4 | 0.19 | 15 (8) | 2 / 10 HP | min 207 |
| bld02, clips off | 59.9 / 58.5 | 1.10 | 33 (18) | 9 / 47 HP | second opening, killed |

fps is measured from the host pilot.jsonl frame/time pairs. The chip columns are `chip_by_drop.py`.

bld02 ended on step 68's assert ("Both seats must receive native Thorn attacks"), after the kill. PC1 step
3 and bld01 ended on step 42's pilot-failure assert.

**Reading:** without clips, bld holds 60 fps and scores in the bisect's strong band. With two 30 fps clip
recordings during the fight, bld's frame rate fell to 17–40 fps, and the run landed in the weak band.
bld therefore qualifies for this replay with clip recording off. Its two-instance clip capture costs
frames that PC1's doesn't.
