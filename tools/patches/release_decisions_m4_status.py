import io

# Packaging decisions 1-4 of RELEASE_CHECK settled with their defaults (the installer installs for the current user
# only, so the settings file beside the exe is always writable), and the state of every Milestone 4 item written into
# PLAN_REMAINING from the ROADMAP rows.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    write(p, s.replace(a, b))


patch('tools/installer/iw8radiant.iss', '''; Per-user install by default (no administrator rights, %LOCALAPPDATA%\\Programs); the wizard offers all-users.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
''', '''; Per-user install only (no administrator rights, %LOCALAPPDATA%\\Programs). The editor keeps IW8Radiant.ini and
; docs\\test_results.json beside its exe; under Program Files those writes fail, so the wizard offers no all-users
; install (RELEASE_CHECK decision 3, 2026-11-14).
PrivilegesRequired=lowest
''')

s = read('docs/RELEASE_CHECK.md')
nl = '\r\n' if '\r\n' in s else '\n'
at = s.index('## Decisions still needed')
s = s[:at] + '''## Decisions (settled 2026-11-14 with these defaults; say so if you want another)

1. HTTPS: the package ships no certificate and HTTPS is off by default. A certificate is made on the machine that
   needs one with `tools/make_web_cert.ps1` (Tools > Web Server Settings names the files). The installer makes none:
   a key made at install time would sit in a folder every later update overwrites.
2. Thumbnail packs: not shipped. They are pictures of game assets (84 MB for the two full packs), and the package
   holds nothing made from game files. The editor makes them on the machine (Tools > Thumbnail Packs).
3. `docs\\test_results.json` and `IW8Radiant.ini` are written beside the exe. The installer therefore installs for
   the current user only (`%LOCALAPPDATA%\\Programs\\IW8 Radiant Suite`, no administrator rights); the "all users"
   choice, which would put the suite under Program Files where those writes fail, is no longer offered
   (`tools/installer/iw8radiant.iss`).
4. ACTS-dependent features (relight, stock entity rebuild, thumbnail packs from ACTS) only work where ACTS and the
   game files are installed. The package does not include them, which is intended.
'''.replace('\n', nl)
write('docs/RELEASE_CHECK.md', s)

P = 'docs/PLAN_REMAINING.md'
s = read(P)
nl = '\r\n' if '\r\n' in s else '\n'
a = s.index('## Milestone 4')
b = s.index('## Milestone 5')
m4 = '''## Milestone 4 — The remaining pieces
Ordered by the owner's scope. ROADMAP rows are in brackets; each row there has the full state and the proofs.
State on 2026-11-14, one line each. "Offline" = built and proven without the game; "game" = only a look in the game
can take it further (docs/TEST_CHECKLIST.md has the steps).

- **Stock map editing**
  - native brushes inside a stock world (11.4): done offline (added brushes are built as models of a helper map and
    carried in); game.
  - Warzone removals (11.5 / 13.5): done offline for the main map zone; streamed tiles cannot be edited (the client
    loads no unsigned tile, research 116): decision recorded in 2.3.
  - name-only `script_model` references (4.7): not built; stock models are carried as `iw8r_stock_*` copies, which
    works for every model.
  - world materials from other maps (4.9 / 4.15): partial by design (research 51): a name only resolves against zones
    loaded while the map plays; other materials are rebuilt from their images.
  - streamed JSON parse memory (9.4): done (measured).
  - catalogue gaps (4.25): partial (research 52); game for the smallest image parts.
- **Warzone**
  - doors outside their area box, editing streamed entries: blocked with the streamed tiles (see above).
  - `common_br_mp` for an own map, the ATV textures, vehicles and buy stations on a custom map (13.4): done offline
    (start entities, switches, loot, vehicles, buy stations); game.
- **Map and mode creation**
  - templates and the flag prop (6.2), preset dvars (13.2), infil and match cameras (6.19), scripted objects (6.13),
    water, ladders and mantles (6.8), doors (6.24), nav view and blockers (6.23), animated models (4.21): built
    offline, each "partial" in ROADMAP only for its look in the game.
  - new game types (13.6): partial (research 91): a new game type name starts and the map script shapes it; a stock
    game type script cannot be replaced by a map zone.
  - killstreak airspace and compass (6.21 / F.14): open; research 75 found no per-map key for it. Game.
  - fonts and LUI (4.27): HUD from the map script works with stock icons; own fonts and menus are not built
    (deferred, as the plan allowed).
- **Audio and effects**
  - the looping sound on an `fx_origin` (F.6), impacts (F.12): fixed offline; game.
  - the reverb zone (F.7), PA speakers and music states (6.22), ambient sound (6.7): built offline; reverb was not
    heard in the second playtest (F.7 open); game.
  - the Effect Editor (8.2): done. Hot update (7.7): built; needs the running game.
- **Assets**
  - techset state words (12.1), winding (4.11), physics assets (4.20), the tint rule (F.13): done offline.
  - layered blend painting (5.9 / 8.11): vertex paint on patches is built; painting layer blends on brushes is not.
- **Live Link:** camera angles (7.3), capture (7.6 / 7.11), overlay fields (7.15): built against the owner's client;
  each needs the running game to go further.
- **Performance and UX**
  - per-item buffers (3.11): done (kept camera geometry, changed per item).
  - preview decode off the GUI thread, one process per validation run, the region outline: not built; none blocks a
    workflow (decode is budgeted per frame, validation runs per zone, the region hides what is outside it).
- **Packaging**
  - RELEASE_CHECK decisions 1-4: settled with defaults on 2026-11-14 (docs/RELEASE_CHECK.md).
  - exe MD5 check (9.1): done (First-Run Setup).
  - final installer: rebuilt with every milestone; clean-machine install: the owner's.
- **Docs and tests**
  - FEATURE_MATRIX, MILESTONES, stale ROADMAP rows and home-folder paths in the docs: done 2026-11-14.
  - USER_GUIDE web section: done 2026-11-13 / 14.
  - fixtures: a missing fixture counts as a skip in the suite's last line (tests/test_harness.h), so a silent skip
    is no longer possible; `out/m2-x64zt` and `out/m2-script-check` are present.

'''.replace('\n', nl)
write(P, s[:a] + m4 + s[b:])
print('ok')
