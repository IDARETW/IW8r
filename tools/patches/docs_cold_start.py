import io

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

# --- ROADMAP: new row after 7.13
p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
hit = [i for i, l in enumerate(lines) if l.startswith('| 7.13 |')]
assert len(hit) == 1
status = ('partial (2026-11-11, owner request "the map loading screen should be the first thing i see"; research 76 and 77). '
          'Editor side done offline: with Tool Paths launch empty F5 starts the recompiled local client (the default was the stock exe, which cannot load custom maps); '
          'it checks the local online-services host at 127.0.0.1:8085 and starts it when it does not answer; it selects Multiplayer on the client\'s sign-in line instead of a fixed 15 s wait, '
          'hosts the private lobby after 1.5 s instead of 5 s, starts with xpartygo 1 (no 8 s lobby countdown), polls every 100 ms and stamps every F5 line with the time since F5. '
          'A cover card (src/qtapp/devmap_cover) sits over the game window from the launch until the map begins to load, so the boot screens, mode select, menu and lobby are not seen; '
          'checked on the real desktop with IW8Radiant.exe --cover-check (sits on the rectangle, topmost, click-through, does not take focus). '
          'Client side written but NOT applied: docs/research/77_client (DW_Devmap.cpp and two one-line diffs for the client source) reads --devmap from the command line, enters Multiplayer in Com_Init, '
          'draws its own dark cover and runs the lobby start in process with no polling; the editor sends that request only to an exe that contains the DW_DEVMAP marker and then only watches. '
          'Applying it needs a client build, and the normal client build publishes a release, so it is the owner\'s call. Tests: core devmap_* (5 tests), GUI section F5 devmap runner. NOT SEEN in game')
assert '|' not in status
lines.insert(hit[0] + 1, '| 7.14 | Cold start: F5 from a closed game to the map loading screen, with no menu on screen | new | ' + status + ' | M, **U** |')
save(p, nl.join(lines))

# --- TEST_CHECKLIST: new section before the second-playtest section
p = 'docs/TEST_CHECKLIST.md'
s, nl = load(p)
a = '## In game: fixes after the second playtest (2026-11-11 evening), not seen in game'
assert s.count(a) == 1
sec = [
 '## In game: F5 cold start to the loading screen (2026-11-11), not seen in game',
 '',
 'Start with Replay closed. The local online-services host may be closed too. Map: `out/agent/mp_iw8r_fix1/mp_iw8r_fix1.map`.',
 '',
 '- [ ] Press F5. The Compile dock shows lines that start with `F5 +<seconds> s:`. One of them says which exe is',
 '      started: it must be `game_dx12_ship_replay_local_dw.exe`.',
 '- [ ] If the host was closed, a line says it is being started, then that it answers. The game starts after that.',
 '- [ ] A dark card "Loading mp_iw8r_fix1" covers the game window from the start. You do not see the splash screens,',
 '      the mode select, the Multiplayer menu or the lobby.',
 '- [ ] The card closes when the map loading screen begins. There is no lobby countdown. You spawn in Team Deathmatch.',
 '- [ ] The last line is `F5 +<seconds> s: In game on mp_iw8r_fix1.` Write down that time.',
 '- [ ] If a menu stays on screen, or the card stays for more than 3 minutes: copy every `F5 +` line and send it.',
 '- [ ] If the card does not hide the game (for example the game runs in exclusive full screen), say so.',
 '- [ ] Say whether menu music or a video sound is heard while the card is up (sound is not covered).',
 '- [ ] The card can be switched off: `IW8Radiant.ini`, section `[devmap]`, `cover=false`.',
 '',
]
s = s.replace(a, nl.join(sec) + nl + a)
save(p, s)

# --- USER_GUIDE: section 8
p = 'docs/USER_GUIDE.md'
s, nl = load(p)
a = '3. In Replay: Local Play, **F6** opens the custom-map browser (R rescans); pick your map and start the match.'
assert s.count(a) == 1
b = [
 '3. **F5** builds, deploys and puts you in the map (see "F5 from a closed game" below). You can also load the map by',
 '   hand in Replay, from a private match lobby you host.',
]
s = s.replace(a, nl.join(b))
a = '## 9. Live Link'
assert s.count(a) == 1
sec = [
 '### F5 from a closed game',
 '',
 'Press F5 with Replay closed. The editor builds the map and deploys it. Then it does the rest by itself:',
 '',
 '1. It picks the game exe. Tools > Tool Paths "launch" is used when it is set. When it is empty, the editor looks in',
 '   the game folder and takes `game_dx12_ship_replay_local_dw.exe`, then another recompiled client, then the stock exe',
 '   (with a warning: the stock exe cannot load custom maps).',
 '2. For the local client it checks that the local online-services host answers at `http://127.0.0.1:8085/health`.',
 '   If not, it starts the host and waits up to 60 seconds for it.',
 '3. It starts the game, selects Multiplayer, hosts a private match, sets your map and Team Deathmatch, and starts',
 '   the match with no countdown.',
 '',
 'While this runs, a dark card with the map name covers the game window. It closes when the map loading screen',
 'begins, so the loading screen is the first thing you see of the game. The card never takes the keyboard or the',
 'mouse. Every step is listed in the Compile dock with the time since F5.',
 '',
 'Settings (file `IW8Radiant.ini`, section `[devmap]`; all optional):',
 '',
 '| Key | Default | Meaning |',
 '|---|---|---|',
 '| `gametype` | `war` | The game mode F5 starts |',
 '| `cover` | `true` | Show the cover card |',
 '| `hostCheck` | `true` | Check the local host before the game starts |',
 '| `hostCommand` | the Host-Local-Server script with `-NoLaunch` | What starts the host. `-` means never start it |',
 '| `hostHealth` | `http://127.0.0.1:8085/health` | Where the host answers |',
 '| `startRequest` | `true` | Ask a client that supports it to start the map by itself |',
 '| `bots` | `0` | Bots for a client that supports the start request |',
 '',
 'This has not been seen in game yet.',
 '',
]
s = s.replace(a, nl.join(sec) + nl + a)
save(p, s)

# --- PROGRESS
p = 'docs/PROGRESS.md'
s, nl = load(p)
a = '  - Still open: impacts (F.12), the script-played loop of an own sound (F.6), reverb, probes, game modes, killstreak'
assert s.count(a) == 1
add = [
 '  - COLD START (roadmap 7.14; research 76 and 77; owner: "the map loading screen should be the first thing i see").',
 '    Editor side built: core devmap_plan (xpartygo 1, LaunchLog sign-in signal, pickLaunchExe, healthIsDwHost,',
 '    startRequestArgs, coverWanted), qtapp devmap_launch (100 ms tick, host check and start, exe choice, timeline',
 '    stamps "F5 +N s:", DW_DEVMAP client lines) and devmap_cover (the card over the game window; real-desktop check:',
 '    IW8Radiant.exe --cover-check --out <file>). Client side is only a proposal in docs/research/77_client',
 '    (DW_Devmap.cpp + 2 diffs for E:\\DW\\IW8). DO NOT apply, build or deploy it from this session: the normal client',
 '    build (build-game.ps1) PUBLISHES a release to the primary server; -ValidateOnly compiles without publishing.',
 '    The owner decides. Patch scripts of this round are kept in tools/patches/. Tests: core 1071 / 0, GUI 647 / 0.',
]
s = s.replace(a, nl.join(add) + nl + a)
save(p, s)

# --- research 77: addendum
p = 'docs/research/77_loading_screen_first.md'
s, nl = load(p)
add = [
 '',
 '---',
 '',
 '## Addendum (main session, 2026-11-11): what was built in the editor, and one change to the client file',
 '',
 '- The editor now has its own cover, so the requirement holds with the client the owner runs today: a card',
 '  (`src/qtapp/devmap_cover.*`) sits exactly over the game window from the launch until the map begins to load',
 '  (`devmap::coverWanted`: until the start request, then only while the front end is still on screen, at most 4 s).',
 '  It is an editor window: topmost, click-through, never focused. Checked on the real desktop with',
 '  `IW8Radiant.exe --cover-check`. It cannot be seen over a game in exclusive full screen. NOT SEEN with the game.',
 '- The editor follows the contract of section 5: it passes `--devmap <id> --gametype <name> [--bots <n>]` only to an',
 '  exe that contains the text `DW_DEVMAP `, reads the `DW_DEVMAP` lines of `replay-dw-client.log` after the launch',
 '  offset, sends no commands while the client works, and takes over with its own console-command path after a',
 '  `failed:` line. A line that starts with `start` or `loading` is the start request for the cover.',
 '- Change to `77_client/DW_Devmap.cpp` after the agent\'s report: the direct path and the `dw_devmap` command now need',
 '  the development opt-in the client already has (`replay-dw-test.enabled`, read like `DW_LocalPlatform.cpp`',
 '  `TestFrame`). Without it a `--devmap-path direct` request logs one line and uses the lobby path. The lobby path',
 '  is not gated: it only automates the menus. The file was compiled again with the DW flags (no warnings) and its',
 '  undefined game and client symbols were checked against `game_dx12_ship_replay_dw.map` (each found once).',
 '  With this change test L1 reads: `dw_devmap` prints its usage line only when `replay-dw-test.enabled` exists.',
 '- Nothing was applied to `E:\\DW\\IW8`. Applying means a client build, and the normal build publishes a release',
 '  (`docs/DW_CLIENT_BUILD.txt`), so it waits for the owner.',
]
s = s.rstrip('\r\n') + nl + nl.join(add) + nl
save(p, s)
print('ok')
