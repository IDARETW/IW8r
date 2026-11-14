import io
import re
import sys

# PROGRESS.md: the airspace fix (research 121) and the parking line. Arguments: repo commit before this one, converter
# commit, core checks, core tests, GUI checks, time the parked map's zones were rebuilt.

P = 'E:/IW8-Radiant/docs/PROGRESS.md'
prev, zt, checks, tests, gui, rebuilt = sys.argv[1:7]
s = io.open(P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
m = re.search(r'  Parking state 2026-11-14 \(after trigger hints and the second review\):[^\r\n]*', s)
assert m
note = '''  Airspace (research 121, F.14): the offline proof of iw8r_airspace (tools/airspace_build_test.py, env test
  umbra_built_zone_airspace) showed the airspace never reached the tome. Read in the Replay exe: a camera outside the
  tome's box fails the query (Umbra::PortalCuller::init 0x141F9FBB0, error 5), the static masks are all set
  (R_Umbra_ResetStaticVisibilityMasks 0x1418F1EE0) and scene entities are tested against the cleared buffer
  (R_Umbra_CullSceneEnts 0x1418CC4A0). Stock tomes reach at least 131072 from the origin (report test
  umbra_zone_airspace_report: IW8R_TEST_GFXMAP_JSON + IW8R_TEST_AIR_PROBE=1). Fix: compile_iw8.cpp kAirspaceDefault
  131072 (up / around / below, kAirspaceMax 524288), converter ''' + zt + ''' BuildUmbraTome unions m.cells bounds
  (tools/patches/zt_tome_airspace.py). The custom-loot and vehicle-carry proofs compare the live converter with the
  new one byte for byte, so they fail on the map zone until the deploy (only gfx_map umbraTomeData differs).
'''.replace('\n', nl)
park = ('  Parking state 2026-11-14 (after the airspace fix): repo at the commit after ' + prev + ', converter live at ' + zt +
        ', suites core ' + checks + ' checks / ' + tests + ' tests and GUI ' + gui + ' checks, 0 failures; 23 offline proofs pass; zones of'
        ' mp_cinderrail_t rebuilt ' + rebuilt + ' with that converter (x64zt_output; its occlusion data reaches 131072 units past the map);'
        ' installer out/installer/IW8RadiantSuite-Setup-0.1.0.exe and out/package/IW8RadiantSuite-20261114.zip rebuilt from it, no ini and'
        ' no game file inside, the packaged exe passes --smoke-test (give it a map and --out: without them it opens as a normal editor).'
        ' The editor is left open on out/agent/mp_cinderrail_toon (that map takes about 30 s to open; the title says untitled.map until then).')
s = s[:m.start()] + note + park + s[m.end():]
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('ok')
