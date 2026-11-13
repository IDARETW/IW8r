import io


def edit(p, a, b):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b in s:
        return
    assert s.count(a) == 1, (p, a[:60])
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


edit('CHANGELOG.md', '''## 2026-11-13

''', '''## 2026-11-13

- **Verdansk opens and runs without running the machine out of memory, at about 20 ms a frame.** It used to climb past
  14 GB and close (out of memory) a few minutes after opening; it now peaks at about 12 GB. Its frame went from 286 ms
  (since the stock lamps' shadows) to about 20 ms once the lamp shadows are drawn (more while other programs fill the
  graphics card's memory). What changed:
  - Props are kept as indexed meshes, a fifth of the memory (video memory for props 788 -> 161 MB), and the light of a
    streamed tile that arrives no longer rebuilds every prop: only their light is updated.
  - Lamp shadow maps and the near sun shadow draw only the props in their view (they drew all 1.2 million placements).
  - Projecting decals onto the props kept 8 GB for a moment; it now copies only the triangles that touch a decal.
  - The texture budget also follows the machine's memory (2 GB on a 16 GB machine), emissive images included, and
    materials that share one stock image share its texture. Windows counts video memory against the machine's memory.
  - A step that runs out of memory (a background job or the window itself) now stops with a message and is tried
    again, instead of closing the editor. Windows had about 14 GB committed by other programs during these tests.
  - Streamed tiles reach 14,000 units (48 tiles) on machines under 24 GB, 20,000 (96) above; stock/streamRadius and
    stock/streamLimit still override.
  - Very many editor lines (Verdansk's 1.2 million entity outlines) are drawn without antialiasing: 7 ms -> 0.2 ms.
  - Prop levels of detail now arrive on Verdansk: one name that points into another zone used to fail whole batches.
  - The 2D views no longer stall on Warzone maps: models arriving no longer re-read all 47,545 entities each time.
''')
edit('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- Verdansk memory and frame (2026-11-13, evening). Diagnosis tools: the bench's memory.log now also holds every log
  line ("<s> S <text>") and, every 10 s, "gpu memory MB: props a+b world c extras d streams e textures f lightmaps g
  probes h shadows i targets j" (SceneRenderer::gpuMemoryReport); IW8R_BENCH_PROFILE adds "prop rebuild memory: ..."
  per rebuild; tools/stack_snapshot.py <pid> samples the busiest threads' stacks of a running editor (DbgHelp,
  no debugger needed); tools/symbolize_trace.py resolves abort-<tick>.txt. Windows charges video memory to the
  process's commit (commit - working set ~ the GPU's committed memory), so VRAM counts against the 16 GB machine.
  Findings and fixes: (1) projectPropDecals held ~8 GB for ~20 s (world-space copies of every prop near any decal
  volume) -> only the prop-only volumes, only triangles touching their boxes, receives() decided once per material;
  (2) prop rebuilds on every tile light grid -> relightReferenceProps (PropLightKey per placement per mesh,
  SceneRenderer::setReferenceTexInstanceLight rebuilds only the instance buffer); refPropTrisKnown_ skips reading
  models known to be past the 4M-triangle budget (8,869 -> 577 models read per rebuild); (3) props indexed:
  PropVertex 28 B + PropMeshData indices (16-bit when possible), DrawIndexedInstanced, InstancedTexMesh::mesh shared
  with refPropBuilt_ (no copy) -> kept LOD0 640 -> 146 MB, GPU props 788 -> 161 MB; (4) texture budget =
  min(2/5 VRAM, RAM/8) (2,021 MB here), emissive images under the budget (BC when possible), untouched BC colour
  images shared by file (texByPathBc_); (5) guardedWorker (src/qtapp/worker_guard.h) around every detached worker
  in main_window / camera_view / world_cache; the terrain-layer worker keeps its layers on bad_alloc; loadStockLightmap
  runs one atlas build at a time; (6) the lamp shadow maps and the near sun cascade drew every prop placement:
  GpuTexInstanced::cells (placements sorted stably into 2,048-unit cells, `order` maps the caller's placement order;
  LOD groups keep a common order) and drawReferenceTexInstancesShadow(view) draws the runs of cells in the view
  (Verdansk 286 -> 28.7 ms over 8 frames, 20.2 ms over 60: the first frames fill the lamp maps 6 at a time); bench
  line "shadows_<scenario> near_cascade=N lamp_maps=N"; the visible-placement buffer holds the large-on-screen
  placements first and the pre-pass draws that prefix (bigInst gone: one full-size instance buffer less); (7) LOD batches all failed
  ("Invalid asset name in request": ACTS refuses names with a comma, i.e. another zone's surfs) -> such LODs fail on
  their own; failed LODs no longer drop a kept mesh; (8) model / LOD arrivals invalidated the scene each (a full sync
  of 47,545 entities) -> invalidateSceneSoon (one per 300 ms burst, LODs only in the LOD preview); grid views draw
  dots for entities under 3 px and no antialiasing past 20,000 items; (9) GuardedApplication::notify (main.cpp)
  catches std::bad_alloc from any event (logged, event dropped, retried later); (10) streamTileReach(): 14,000 / 48
  tiles under 24 GB of RAM (keep 1.15x / +25 %); (11) rsLinesFast_ (no line AA) past 500k line vertices. Prop
  triangle budget: 16 M crashed (833k placements as meshes: instance data ~300 B a placement, GPU 859+877 MB), 8 M
  crashed at 12.6 GB process commit with ~14 GB committed by other programs; kept at 4 M. Verdansk raised camera:
  peak commit 14.5 GB (crash) -> 12.0 GB, no crash; 20.2 ms steady (60 frames). GPU timings vary a lot with other
  programs' video memory (dwm 2.6 GB, others 3.5 GB on the 8 GB card during the runs). Open: 948k placements past the
  prop triangle budget are not drawn (lighter instance data first: half-float light, no CPU rows copy); MapEnts
  model entities are flat per-entity copies (streams 527 MB); streamed-tile CPU copies (~1 GB) are kept for re-upload.
''')
print('ok')
