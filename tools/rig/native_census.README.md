# Native actor census adapter

`native_census.actor_roots(reader, entities, check=...)` validates native
ObjEntry, status roots and team, then brackets both roots again. It retains
every supplied actor, including PRIZE (type 22), scenery and weapon props.
The native descriptor name is decoded as ASCII and must match the supplied
name. Classification uses that authenticated native name; native `M_`/`B_`
prefixes require enemy HP validation even outside enemy object types.
Non-combat rows are labeled `non-combat`; their nullable status block is never
dereferenced for HP. Player and enemy candidates still require readable,
matching HP/max HP. Torn roots and read errors refuse, including for enemies.

The caller owns complete chain enumeration, session/ownership binding, native
reader lifetime, deadlines and cancellation. This helper does not establish
those guarantees, authenticate a remote player, launch a process or write RAM.
It does not change any party row or qualify practice B.

Run `python -m unittest discover -s tests -p test_native_census.py`.
The fixture retains actor descriptors from the failed passive B census;
tests project those descriptors into synthetic memory, not a live RAM replay.
The source path and SHA are recorded in the fixture. No live run is required.
