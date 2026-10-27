# Retained terminal event excerpts

Each `.log` contains exact selected lines, prefixed with their original
one-based line number, from the read-only Juno run named in `sources.json`.
The source hashes pin the original bytes, not a reconstructed success trace.
Selection retains host funnel kills, death publications, hall mission snapshots,
gameplay census holds, and final-body absence/HP observations. Calling's single
line retains the matching leave symptom.

These are inputs to `MissionMirrorTest.cpp`, not acceptance evidence of the
candidate. In particular `193245` has no wave-two native kill receipt; replay
must not manufacture one. See `docs/testing/vuh2884-native-terminal/README.md`.
