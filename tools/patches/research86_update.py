import io
p = 'docs/research/86_scriptable_objects_plan.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
lines = s.split(nl)
def state(step, text):
    hit = [i for i, l in enumerate(lines) if l.startswith('| ' + step + ' |')]
    assert len(hit) == 1, step
    cells = lines[hit[0]].split(' | ')
    cells[-1] = text + ' |'
    lines[hit[0]] = ' | '.join(cells)
state('F2b', 'done, commit 04995144')
state('F2c', 'done, commit 04995144 (rules below, checked on 21 maps)')
state('F2d', 'not needed for a first build: one leaf that holds every instance is written, as for the two dynamic-entity populations')
state('F2e', 'done, commit 04995144 (`ReadScriptableInstances`)')
state('E1', 'done: class `scriptable`, catalogue `data/asset_catalog/iw8_scriptables.tsv`; drawn as a box (no model yet)')
state('E2', 'done for instances (`scriptable_instances` in the collision JSON); the `system_init` call waits for F3, since no door can be placed before it')
s = nl.join(lines)
add = '''
## Result of 2026-11-11 (late): instances are built; what is known now

Nothing here has been seen in game.

### The dynamic-entity side (was "not solved")

Decoded from the `dynentitylist` of mp_shipment and mp_backlot2, then checked against all 21 stock multiplayer maps
by `tools/build_scriptable_catalog.py`:

- **Every dynamic entity of a stock map belongs to a scriptable.** There is no other kind in these maps. Two types:
  `DYNENT_TYPE_SCRIPTABLEINST` (3) and `DYNENT_TYPE_SCRIPTABLEPHYSICS` (4). Each entry carries the instance's index
  (`scriptableMapIndex`), a sub index, and `scriptableMaxPartCount` = the definition's `maxNumDynEntPartsBase`.
- **Physics entries**: one per dynamic entity the definition requires (`maxNumDynEntsRequired`; cone 1, cars 1 to 5),
  sub index 0, 1, 2. Priority always-active (255), spawned active, on the list's no-spatial chain.
  `MapEnts.dynEntNoSpatialCount[0]` is their number.
- **Instance entry**: at most one per instance. Radial priority 0 in spatial population 0, or, for doors, priority 3
  (sparse occluder) with collision flags 1 in population 1. Which definitions have one is NOT derivable from the header
  numbers or flags that were tried (a rule on the flag bits failed on 155 of 509 observed definitions), so the
  catalogue records what stock maps do per definition (`instance_dynent`, column `source` = observed).
- Client id 0 / 0, no model pointer, pose = the instance's origin and the quaternion of its angles.
- `dynEntMaxPosePartCount` = the largest `scriptableMaxPartCount`; `dynEntsWithExtraPosePartsCount` = the entries
  above 1 (shipment 47 / 163, backlot2 39 / 194: exact).
- Stock maps also carry 64 reserved physics entries (instance index 500000, 4 parts) and two 64-entry reserved-dynent
  tables. `ffMemCost` 1240 = the 216-byte block + those two tables (1024 bytes). The converter writes neither (as
  before this change, when maps already ran without them) and sets `ffMemCost` 216.

### The map-wide counts on 21 maps

`serverInstanceCount`, `serverFixedPartCount` and `serverUsablePartCount` equal the sums of the table above plus the
loot count on every one of the 21 maps (battle-royale maps included). The physics limits equal the number of door
(sparse-occluder) instances on the core maps, but not everywhere: mp_emporium has 18 against 11 doors, the tutorial
and battle-royale maps differ too. So other definitions add to the limits and the property that decides it is still
unknown. The catalogue therefore marks a definition `verified` only when it was observed on a map whose limits are
fully explained by its doors.

### What can be placed

- 837 stock definitions are catalogued; 73 are in a zone every multiplayer match has loaded (`common_core_mp`,
  `common_base_mp`, `common_mp`, `global_core_mp`): game-mode objects, equipment and killstreak objects.
- Stock maps reference `hardpoint_chevron` from `common_core_mp` with a comma name, the same name-only reference the
  converter now writes. That is the one definition that is always loaded AND verified: the editor builds it.
- `scriptable_auto_ascender` (`common_mp`) is observed only on battle-royale maps, whose limits are unexplained: not
  built.
- Doors, cones, cars, tyres, radios and the other world props live in stock map zones only. Placing one needs step
  F3. The build leaves such an entity out and says why.

### Step F3, for whoever continues

A definition must be carried into the map with its models, collision and effects. Two routes:

1. The fork's own dump-and-rebuild path: `zonetool dumpzone <stock map>` writes `scriptable/<name>.iw8`, and the
   generic writer (`zone/writer.cpp`, `neutral/scriptable.cpp`) can write a definition back. It writes whole zones
   through its own stream class, not through the native `ZoneWriter` the map build uses, so the definition would have
   to go either through an adapter (the generated `stream_scriptabledef` over the native buffer) or into one of the
   map's side zones.
2. A native copier like `iw8::stock::Copier` (ACTS export with native bytes, pointers relocated): generic over the
   ACTS node tree, with the stream order taken from `asset_streams.cpp`.

Either way the models of the definition must come along (the editor's stock-model route can do that part), and the
result can only be proven in the game: the load order of name-only references, the door's collision (F4) and the
`system_init` call (F1, already in the converter) all need a run. This is the one part of the scripted-object work
that cannot be finished offline.

### Proof and tools

- `tools/scriptable_offline_test.py <extraction> <map id> <work folder>`: (1) the old and the new converter give
  byte-identical zones for a map without scriptables (checked on a small map and on showcase 2); (2) with six
  instances (chevron, door, cone, car profiles) ACTS loads the map zone with 0 failed and reads back every record,
  count, script string and chain. 38 checks pass.
- End to end: `out/scr-test/mp_scrtest.map` with three chevrons and one cone builds headless: 3 instances written,
  the cone left out with its reason, all five loader lines 0 failed.
- `tools/build_scriptable_catalog.py` (exports in `out/scriptable_study`), `tools/scriptable_dynents.py`,
  `tools/scriptable_counts.py`, `tools/acts_export.py`.
'''
s = s.rstrip() + nl + add.replace('\n', nl)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
