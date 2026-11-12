import io, re

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

INSTALLED = ('Client built and installed on 2026-11-11 19:22 as an unpublished candidate (exe sha256 b77d6e60..., job 1794441713356806500; '
             'the game folder dw exe, map and pdb were replaced and game_dx12_ship_replay_local_dw.exe was remade from it; the previous exe is backed up in out/dw_backup/20261111-191715-nov8-candidate). '
             'It passed compile, link and the pipeline\'s five build-integrity gates; the gameplay gates were waived because one of them needs the remote server, which is down. NOT RUN in game')
assert '|' not in INSTALLED

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
def patch(row, old, new, cells_expected):
    hit = [i for i, l in enumerate(lines) if l.startswith('| ' + row + ' |')]
    assert len(hit) == 1, row
    assert lines[hit[0]].count(old) == 1, (row, old)
    assert len(lines[hit[0]].split(' | ')) == cells_expected
    lines[hit[0]] = lines[hit[0]].replace(old, new)
patch('7.14', 'Applying it needs a client build, and the normal client build publishes a release, so it is the owner\'s call.', INSTALLED + '.', 5)
patch('7.15', 'Client side written and registered in the client source, compile-checked, NOT built and not run:', 'Client side written and registered in the client source. ' + INSTALLED + '. What it adds:', 5)
patch('F.11', 'Applied to the client source and compile-checked; NOT built (the client build is blocked by the client project\'s state) and not run.', 'Applied to the client source. ' + INSTALLED + '.', 4)
save(p, nl.join(lines))

p = 'docs/TEST_CHECKLIST.md'
s, nl = load(p)
a = '## In game: F5 cold start to the loading screen (2026-11-11), not seen in game'
assert s.count(a) == 1
sec = [
 '## In game: the new client exe (installed 2026-11-11 19:22), not seen in game',
 '',
 'The local client was rebuilt with three additions (cold start, game-mode list, debug overlay). Do these first.',
 '',
 '- [ ] Start `game_dx12_ship_replay_local_dw.exe` by hand, with the local host running, with no arguments. It boots',
 '      and reaches the menus as before. If it does not start or crashes: run the rollback line below and report it.',
 '- [ ] In the game console type `iw8r_mapDebug`. It is a known dvar with value 0.',
 '- [ ] Close the game. In the editor press F5 on a map: the F5 lines say "the client was asked to boot straight into',
 '      the map", then lines that start with `client:` (request, signed in, multiplayer active, front end ready,',
 '      hosting, start requested, loading), and you see a dark screen and then the map loading screen.',
 '- [ ] Rollback, if needed (game closed): `"C:\\Users\\user\\AppData\\Local\\Programs\\Python\\Python314\\python.exe" -B',
 '      E:\\IW8-Radiant\\tools\\dw_candidate_install.py --rollback E:\\IW8-Radiant\\out\\dw_backup\\20261111-191715-nov8-candidate`',
 '',
]
s = s.replace(a, nl.join(sec) + nl + a)
for old, new in [
 ('## In game: map debug overlay (2026-11-11), needs the new client build, not seen in game', '## In game: map debug overlay (2026-11-11), with the new client exe, not seen in game'),
 ('## In game: game modes of a custom map (2026-11-11), needs the new client build, not seen in game', '## In game: game modes of a custom map (2026-11-11), with the new client exe, not seen in game'),
 ('- [ ] With a client built from the changed `DW_MapAvailability.cpp`: a map with only TDM spawns still offers Team', '- [ ] A map with only TDM spawns still offers Team'),
]:
    assert s.count(old) == 1, old[:40]
    s = s.replace(old, new)
save(p, s)

p = 'docs/PROGRESS.md'
s, nl = load(p)
a = '  - Still open: impacts (F.12), the script-played loop of an own sound (F.6), reverb, probes, game modes, killstreak'
assert s.count(a) == 1
add = [
 '  - CLIENT EXE INSTALLED 2026-11-11 19:22 (unpublished candidate b77d6e60..., job',
 '    E:\\DW\\IW8\\data\\dw-client\\jobs\\1794441713356806500). How: tools/dw_candidate_build.py (prepare + compile + link),',
 '    then the game folder dw exe was swapped to the published release for verification (one gate,',
 '    test-gulag-carryforward-integration.py, reads the installed exe in its own process), then',
 '    dw_candidate_build.py --verify <job> --waive-gameplay (five integrity gates; test-map-publication.py needs the',
 '    droplet, which is down; 23 gameplay gates had passed before it in a full run, list in',
 '    out/dw_candidate_full_run_progress.json), then tools/dw_candidate_install.py <job>. Previous exe + map + pdb:',
 '    out/dw_backup/20261111-191715-nov8-candidate (rollback: dw_candidate_install.py --rollback <that folder>).',
 '    The junction to the Codex toolchain folder was removed again. data/dw-client/latest-job.json now names this job',
 '    as awaiting publication: nothing was published. NOT RUN in game.',
]
s = s.replace(a, nl.join(add) + nl + a)
save(p, s)

p = 'C:/Users/user/.claude/projects/E--IW8-Radiant/memory/dw-client-build-rules.md'
s = io.open(p, encoding='utf-8', newline='').read()
start = s.index('- State on 2026-11-11:')
end = s.index('- Quick check without the pipeline:')
new = ('- Done once on 2026-11-11 (candidate b77d6e60 installed 19:22, not run in game). The working sequence, all from\n'
       '  E:\\IW8-Radiant\\tools with Python314: (1) junction C:\\Users\\user\\AppData\\Local\\ReplayCompilerWorkstation ->\n'
       '  ...\\Packages\\OpenAI.Codex_2p2nqsd0c76g0\\LocalCache\\Local\\ReplayCompilerWorkstation (the owner builds from the Codex\n'
       '  app, whose AppData\\Local is redirected there); remove it afterwards with cmd /c rmdir. (2) dw_candidate_build.py\n'
       '  (needs about 9 GB free on E:; rerun on "SQLite committed while checking snapshot reuse"). (3) game closed: back up\n'
       '  the game folder dw exe/map/pdb, copy data\\dw-client\\game_dx12_ship_replay_dw.exe (published) over the dw exe.\n'
       '  (4) dw_candidate_build.py --verify <job> --waive-gameplay (test-map-publication.py SSHes to the droplet).\n'
       '  (5) dw_candidate_install.py <job>. New natives: functions by catalog name, data globals as gl_<older VA> unless\n'
       '  the name is in the job\'s builds\\...\\catalog-snapshot\\function-catalog.json.\n')
s = s[:start] + new + s[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
