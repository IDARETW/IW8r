"""Runs the offline proof scripts of tools/ one after another against one converter and prints one line each (the
RESULT line or the exit code). Each proof's work folder is deleted after it ran (they are large).

Usage: py -3 -I tools/run_offline_proofs.py <work root> [--exe <zonetool.exe>] [--extraction <folder> <map id>]
Default exe: the live converter; default extraction: out/scr-br/iw8build/mp_scrbr/extraction mp_scrbr."""
import os, shutil, subprocess, sys, time

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
args = sys.argv[1:]
exe = r'E:\mw124\x64-zt\build\bin\x64\Release\zonetool.exe'
extraction, map_id = os.path.join(root, 'out', 'scr-br', 'iw8build', 'mp_scrbr', 'extraction'), 'mp_scrbr'
if '--exe' in args:
    exe = args[args.index('--exe') + 1]
    del args[args.index('--exe'):args.index('--exe') + 2]
if '--extraction' in args:
    i = args.index('--extraction')
    extraction, map_id = os.path.abspath(args[i + 1]), args[i + 2]
    del args[i:i + 3]
work_root = os.path.abspath(args[0])

proofs = [
    ('stock_edit_test', ['--exe', exe]),
    ('stock_patch_test', ['--exe', exe]),
    ('smodel_collision_remove_test', ['--exe', exe]),
    ('stock_world_remove_test', ['--exe', exe]),
    ('warzone_loot_edit_test', ['--exe', exe]),  # Quarry: it deletes ten points
    ('warzone_loot_boxes_test', ['--exe', exe]),
    ('warzone_instances_edit_test', ['--exe', exe]),  # Quarry's buy stations
    ('stock_sun_edit_test', ['--exe', exe]),
    ('stock_sun_direction_test', ['--exe', exe]),
    ('warzone_instances_remove_test', ['--exe', exe]),
    ('dynent_move_test', ['--exe', exe]),
    ('dynent_copy_remove_test', ['--exe', exe]),
    ('stock_sky_swap_test', ['--exe', exe]),
    ('stock_retexture_test', ['--exe', exe]),
    ('lightmap_bake_build_test', []),  # an editor map through --compile (the editor exe, not --exe)
    ('umbra_bake_build_test', []),  # the occlusion bake of an editor map, read back from the built zone
    ('hud_image_build_test', ['--exe', exe]),  # a map's own HUD image, built through the editor with this converter
    ('map_strings_build_test', ['--exe', exe]),  # a map's own text, read back from eng_<map>
    ('airspace_build_test', ['--exe', exe]),  # a built map's occlusion data reaches as far as a stock map's
    ('probe_capture_build_test', ['--exe', exe]),  # a built map's probes: coloured, at the stock probes' level
    ('warzone_custom_loot_test', None),
    ('warzone_custom_loot_boxes_test', None),
    ('scriptable_limits_test', None),
    ('vehicle_carry_test', None),
]
for name, extra in proofs:
    work = os.path.join(work_root, name)
    if extra is None:
        flag = '--new' if name in ('warzone_custom_loot_test', 'warzone_custom_loot_boxes_test', 'scriptable_limits_test', 'vehicle_carry_test') else '--exe'
        cmd = [sys.executable, '-I', os.path.join(here, name + '.py'), extraction, map_id, work, flag, exe]
    else:
        cmd = [sys.executable, '-I', os.path.join(here, name + '.py'), work] + extra
    start = time.time()
    r = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    out = (r.stdout + r.stderr).splitlines()
    result = [l for l in out if l.startswith('RESULT')]
    fails = [l.strip() for l in out if l.strip().startswith('FAIL')]
    print('%-34s %-28s %4.0f s %s' % (name, result[-1] if result else 'exit %d (no RESULT line)' % r.returncode, time.time() - start,
                                       ('; '.join(fails[:3]))[:300]), flush=True)
    if not result:
        print('    ' + '\n    '.join(out[-8:]), flush=True)
    shutil.rmtree(work, ignore_errors=True)
