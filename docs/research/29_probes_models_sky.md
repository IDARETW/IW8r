# 29. Reflection probes, static models and sky in the x64-zt extraction

Scope: roadmap 5.5 (reflection probes), 4.8 (static models) and 5.4 (sky), as written by `src/core/compile_iw8.cpp`
into `world.json` (contract: report 15 §2.1, §2.2, §7). x64-zt (`E:\mw124\x64-zt`) was read only. Line numbers are from
that checkout.

## 1. Reflection probes

- Each `reflection_probe` entity becomes one `world.reflection_probes[]` entry, in map order, at most 256 (the contract
  limit, report 15 §2.1). Entry N has image `<id>_probe<N>` and origin = entity origin (a missing origin goes to the
  world centre with a warning).
- Each probe image is written as `extracted/images/<id>_probe<N>.dds`: the neutral grey 64x64 cube with 7 mips that the
  compiler wrote before for probe 0 (`buildProbeDds`).
- With no entity, the default probe is unchanged: one probe `<id>_probe` at the world centre, `images/<id>_probe.dds`.
- Each surface gets the probe nearest to the centre of its vertex bounds (squared distance, first probe wins a tie).
- `dpvs.cells[0].reflection_probes` lists every probe index.
- `reflection_probe` entities are not written to `.ents`.

Known x64-zt behaviour to check in game (not changed here):
- The converter skips source probe 0 when there are two or more probes, and keeps it only when it is the sole probe
  (`iw3_render_assets.cpp:5520-5531`: `firstProbe = size > 1 ? 1 : 0`). It documents index 0 as R_CreateDefaultProbe's
  flat image. So when entities exist, the first entity probe is not drawn as an authored capture.
- Surfaces carry source indices into the probe table (`ReflectionProbeTag`, `iw3_fastfile.cpp:3944-3948`, used at
  `:6552-6554`), while the output table is compacted (`plan.reflectionProbes`, `:6692`). With two or more probes the
  output index can be one less than the source index. This was not verified on a real zone.
- The task spec (index 0 is the first entity when entities exist) was kept. Prepending a default probe would avoid the
  skip, but changes the probe count the spec asks for. Decide after an in-game check.

## 2. Static models (`misc_model`)

- A `misc_model` whose `model` names a custom model (same rule as `script_model`, doc 22: a name matching
  `[a-z0-9_-]{1,64}` with `<name>.obj` under `opt.modelRoots`) becomes a `world.models[]` placement. The model is
  written by `writeExtractionModel` as `extracted/xmodel/<name>.json` and `.replay.json`. Two entities with the same model
  share one xmodel.
- Placement fields (x64-zt reads them at `iw3_fastfile.cpp:3743-3825`):
  - `model` (string) names the xmodel (`:3739-3766` caches the models, `:3768-3770` reads each placement's name).
  - `origin` `[x,y,z]` is the entity origin (`:3795`).
  - `axis` is three rows `[forward, left, up]`, read at `:3822-3825`.
  - `scale` is the `modelscale` key (default 1, must be positive, `:3817-3819`).
  - `reflection_probe` is the nearest probe to the origin (`:3790-3794` requires it to be a valid index).
- Axis from `angles` (pitch yaw roll, degrees) uses x64-zt's `AnglesToAxis` (`iw3_fastfile.cpp:11658-11666`). For
  `0 90 0` this gives forward `(0,1,0)`, left `(-1,0,0)`, up `(0,0,1)`. `compile_world_tests` checks this.
- `mins`/`maxs` are not written. Without them the converter uses the bounds centre for lighting and warns
  ("static model(s) lack IW3 lighting inputs"). This is a warning only.
- The `.ents` writer never gets `misc_model`: x64-zt projects it away (`src/games/iw8/dump/maps.cpp:570-574`), so the
  placement is the only copy.
- A stock `misc_model` name (or one without an `.obj` in the model folders) is skipped with a warning. Stock xmodel export
  is not supported by the x64-zt route yet (report 15 §3.6, doc 17).

## 3. Sky

- The compiler reads `skyname` and then `sky` on worldspawn. A non-empty value is reported and `world.sky` stays `""`.
- Reason: x64-zt reads `world.at("sky")` (`iw3_render_assets.cpp:6186`) and loads `images/<sky>.dds` through
  `LoadImage`, which throws `missing IW3 image` when no file is found (`iw3_render_assets.cpp:1340`). A non-empty
  name without its cubemap files would fail the whole build. Only the empty name has a path that works: it gets a dark
  placeholder cubemap (`:6189-6193`).
- Superseded by doc 33: the compiler now converts a provided sky DDS (stock skies via ACTS) into `images/<name>.dds` and names it in `world.sky`.

## 4. Tests and checks

- `tests/compile_world_tests.cpp`: two probes with nearest assignment, the default probe without entities, a custom
  `misc_model` (axis for `0 90 0`, scale, placement, stock name skipped), and the sky key.
- End to end (`out/world-test`): the sample room with two `reflection_probe` entities and one `misc_model` of
  `cube_test` (OBJ from `out/custom-model-test`). `iw8r compile` wrote `images/mp_iw8r_world_probe0.dds` and `_probe1.dds`,
  `world.reflection_probes` = 2, eight surfaces (7 on probe 0, 1 on probe 1), `world.models` = 1 with axis `[[0,1,0],[-1,0,0],[0,0,1]]`,
  and `xmodel/cube_test.json`. `zonetool buildextraction` produced `mp_iw8r_world.ff` with `smodel_0` registered and
  `1 static models`. ACTS `fastfile --test` passed on all five zones (section 5).

## 5. ACTS result for the end-to-end zones

| Zone | Assets loaded | Tested | Failed |
|---|---|---|---|
| `mp_iw8r_world.ff` | 810 | 22 | 0 |
| `techsets_mp_iw8r_world.ff` | 105 | 4 | 0 |
| `eng_mp_iw8r_world.ff` | 9 | 2 | 0 |
| `ww_mp_iw8r_world.ff` | 2 | 1 | 0 |
| `srv_mp_iw8r_world.ff` | 0 | 0 | 0 |

These are structural and load checks. Probe and model placement are not verified in game (report 15 §5 open items).
