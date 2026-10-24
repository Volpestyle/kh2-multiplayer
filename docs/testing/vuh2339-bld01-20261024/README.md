# VUH-2339 bld qualifying replay: bot01r-ac5280e-bld01 (2026-10-24)

The bot01 replay at ac5280e, using PC1's governed products copied unchanged and staged on bld.

**Gate.** Steam account B was signed in. The gate admitted the job at launch level at 04:41:43Z, after
956 s of physical idle. The job finished at 04:57:52Z. There was no reclaim and no needs-verify, and the
lease was released.

**Closing checks** (terminal.json): install content unchanged (538 files), saves unchanged (6),
safety passed, relay port free, nothing owned left running. A later verify-after.ps1 run (no -Clear)
agrees; see verify-after-20261024T050008Z.json.

**Score** (Linden's VUH-2617 scripts; the per-drop metric is chip_by_drop.py):

| run | 202 HP/s | host 432s (in band) | host chip drops | Thorn min | end |
|---|---|---|---|---|---|
| PC1 step 3 (20261023-075321) | 0.84 | 34 (19) | 6 / 37 HP | 175 | step 42 pilot-fail assert |
| bld01 (20261023-234445 local) | 0.19 | 15 (8) | 2 / 10 HP | 207 | step 42 pilot-fail assert |

bld01 falls in the bisect README's "weak, noisy band" (the same as aea7726 r2: 2 / 10 HP, 0.25/s), so it
does **not** match PC1's strong ac5280e result. The README also reports large per-run variance on a
single build (aea7726: 1.64 vs 0.25 HP/s).

**Frame pacing differs.** Taken from host pilot.jsonl frame/time pairs:
- PC1: 59.8 fps overall, median 60, p10 56.6.
- bld: 50.6 fps overall, median 59, p10 30.4.

bld dropped to 17–40 fps for sustained stretches in the fight (host frames ~22.7k–23.8k), with two KH2
instances and their clips recording on the RTX 3080. No Steam download overlapped the run.
