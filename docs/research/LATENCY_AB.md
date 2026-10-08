# Interpolation delay qualification and default decision

The selected defaults are avatar 80 ms and enemy cursor 6 host frames. The
previous defaults were 120 ms / 9 frames. Both completed A/B jobs used the same
moving-reversal fixture and ADOPTed collector, with baseline and candidate arms,
launch guards, protected-save baselines and owned-process closure. Coverage
(>=95%), read brackets (<=10 ms), stable identity and saves passed in both jobs.

The 50/4 candidate reduced measured avatar lag to 55–65 ms and p95 position
error to 34–35 game units, but avatar underruns ranged from 5.24% to 11.55%
across the four phase/owner rows, with 23 enemy cursor underrun frames. The
80/6 candidate measured 85–100 ms, p95 error 44–51 units, avatar underruns
0.12–0.61%, and zero enemy underrun frames. Lead selected the middle point.
The middle run's baseline had 86 enemy underrun frames and three release/retake
events; the candidate had zero of each. These are short LOOPBACK observations;
they do not qualify Steam/internet jitter or promise the same underrun rates
elsewhere. Delays remain bounded environment overrides (runtime README).

Lag is the fitted stream delay, not input-to-photon latency. Position error is
same-time source/puppet disagreement in game units. Correction counts are the
collector's >30-unit proxy and enemy cursor events, not verified visual snaps.
Enemy sparse-lag estimates and native mirror position errors below are separate
from the avatar rows. Underruns count samples/frames, not lost packets. Full
fit/gap/interval details remain in each comparison.json alongside comparison.md.

The complete comparison.md receipts follow, preserved verbatim. Source paths
and hashes identify the measurements used for this decision. The middle job's
PRIOR_ART.md closure difference was the lead's concurrent docs fast-forward;
the lead confirmed it benign. No default change was tested in a separate live
job: the same 80/6 behavior was qualified via the overrides in the middle A/B.

## 50 ms / 4 frames

Source: `build/rig/latency-ab-20261007-01/output/comparison.md`

SHA-256: `405631a8764666f1b1888e79b0e770057ef94cbefb4926ee07b0ba18e1ebf6cf`

# Latency A/B results

| Arm / phase / owner | Lag ms | Same-time error median / p95 / max | Correction proxy >30u | Avatar underrun samples |
|---|---:|---:|---:|---:|
| baseline / goa / 0 | 135 | 0.051 / 66.046 / 114.963 | 0 | 0/1969 |
| baseline / goa / 1 | 115 | 0.059 / 56.284 / 114.691 | 0 | 0/1629 |
| baseline / enemy / 0 | 130 | 0.067 / 66.041 / 112.581 | 0 | 0/1602 |
| baseline / enemy / 1 | 130 | 0.079 / 65.101 / 71.962 | 0 | 0/1603 |

## baseline enemy

Best sparse lag: 14 host frames. Live error: {'lagFrames': 0, 'n': 4224, 'median': 4.79668142985574, 'p95': 181.54040392937029, 'max': 192.85270936604707, 'rmse': 66.06744737046738}.
Cursor corrections, underruns, release/retake: `{"accepted": 321, "catchups": 1, "holds": 0, "releases": 0, "resets": 0, "retakes": 0, "sampledIntervalMs": 16081.0943, "snaps": 0, "underrunFrames": 0}`.
| candidate / goa / 0 | 65 | 0.021 / 34.208 / 59.781 | 0 | 128/1929 |
| candidate / goa / 1 | 65 | 0.054 / 35.038 / 54.163 | 0 | 147/1634 |
| candidate / enemy / 0 | 55 | 0.054 / 33.670 / 64.709 | 0 | 185/1602 |
| candidate / enemy / 1 | 65 | 0.052 / 35.114 / 39.415 | 0 | 84/1604 |

## candidate enemy

Best sparse lag: 9 host frames. Live error: {'lagFrames': 0, 'n': 4157, 'median': 4.2885663976059005, 'p95': 124.21133756786851, 'max': 194.26581383384675, 'rmse': 45.44404058944424}.
Cursor corrections, underruns, release/retake: `{"accepted": 314, "catchups": 0, "holds": 0, "releases": 0, "resets": 0, "retakes": 0, "sampledIntervalMs": 16064.267600000001, "snaps": 0, "underrunFrames": 23}`.

All coverage, read bracket, identity and save checks passed. Full moving-only fits, lag-corrected error, gaps and interval lengths are retained in comparison.json.
Correction counts are explicitly defined proxies/cursor events; they are not a pixel-latency measurement.


## 80 ms / 6 frames

Source: `build/rig/latency-ab-middle-20261007-01/output/comparison.md`

SHA-256: `f8a41552c33eeafe8a08f6685ec9a0cec12dc2d1feeae7da6796d57af7c3b9ad`

# Latency A/B results

| Arm / phase / owner | Lag ms | Same-time error median / p95 / max | Correction proxy >30u | Avatar underrun samples |
|---|---:|---:|---:|---:|
| baseline / goa / 0 | 135 | 0.027 / 66.742 / 74.982 | 0 | 0/1901 |
| baseline / goa / 1 | 135 | 0.047 / 68.010 / 77.512 | 0 | 0/1614 |
| baseline / enemy / 0 | 130 | 0.059 / 65.972 / 108.718 | 0 | 91/1610 |
| baseline / enemy / 1 | 120 | 0.058 / 61.828 / 89.276 | 1 | 0/1613 |

## baseline enemy

Best sparse lag: 13 host frames. Live error: {'lagFrames': 0, 'n': 3416, 'median': 4.571882093924817, 'p95': 172.697495702588, 'max': 194.5348180437985, 'rmse': 62.04548825734239}.
Cursor corrections, underruns, release/retake: `{"accepted": 288, "catchups": 4, "holds": 3, "releases": 3, "resets": 0, "retakes": 3, "sampledIntervalMs": 16032.174900000002, "snaps": 1, "underrunFrames": 86}`.
| candidate / goa / 0 | 85 | 0.043 / 44.305 / 79.126 | 0 | 12/1976 |
| candidate / goa / 1 | 95 | 0.053 / 48.182 / 78.263 | 0 | 5/1662 |
| candidate / enemy / 0 | 95 | 0.071 / 50.430 / 92.986 | 1 | 7/1614 |
| candidate / enemy / 1 | 100 | 0.053 / 51.025 / 58.439 | 0 | 2/1609 |

## candidate enemy

Best sparse lag: 10 host frames. Live error: {'lagFrames': 0, 'n': 4808, 'median': 4.200627145524432, 'p95': 138.88421803842832, 'max': 219.9781545878465, 'rmse': 53.010725411916134}.
Cursor corrections, underruns, release/retake: `{"accepted": 322, "catchups": 2, "holds": 1, "releases": 0, "resets": 0, "retakes": 0, "sampledIntervalMs": 16111.7955, "snaps": 0, "underrunFrames": 0}`.

All coverage, read bracket, identity and save checks passed. Full moving-only fits, lag-corrected error, gaps and interval lengths are retained in comparison.json.
Correction counts are explicitly defined proxies/cursor events; they are not a pixel-latency measurement.

