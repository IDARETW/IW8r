import io, re

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

p = 'docs/USER_GUIDE.md'
s, nl = load(p)
pairs = [
 ("3. The editor needs no proxy to edit, build or deploy. Custom maps are loaded by Replay's F6 custom-map browser.",
  ["3. The editor needs no proxy DLL. Custom maps are loaded by the recompiled Replay client",
   "   (`game_dx12_ship_replay_local_dw.exe` or `game_dx12_ship_replay_dw.exe`); the stock exe cannot load them.",
   "   Leave **Launch exe** empty and F5 picks the recompiled client in the game folder by itself."]),
 ("- **Direct (no proxy)** works with any exe in the game folder, including recompiled ones (it reads their address map):",
  ["- **Game link** works with any exe in the game folder, including recompiled ones (it reads their address map):"]),
 ("- The upper sections use the mw120r proxy's control file (status, select the deployed map, `set` dvars).",
  ["- **Quick match...** starts a private match on the deployed map in a mode you pick, with bots. From a lobby you host",
   "  it starts at once with no countdown. From the Multiplayer menu it hosts a private match first and starts when the",
   "  lobby is up. **Load map in game** starts the deployed map from a lobby you host.",
   "- The sections for the old mw120r proxy (status, select the deployed map, `set` dvars) appear only when its DLL",
   "  (`XInput9_1_0.dll`) is in the game folder. Without it they are hidden and nothing is written for it."]),
]
for a, b in pairs:
    assert s.count(a) == 1, a[:50]
    s = s.replace(a, nl.join(b))
a = '   running, except for the map that is currently loaded. The previous package is kept in `.proxy\\backups`'
assert s.count(a) == 1
save(p, s)

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
def setStatus(row, status):
    assert '|' not in status
    hit = [i for i, l in enumerate(lines) if l.startswith('| ' + row + ' |')]
    assert len(hit) == 1, row
    cells = lines[hit[0]].split(' | ')
    assert len(cells) == 5, (row, len(cells))
    cells[3] = status
    lines[hit[0]] = ' | '.join(cells)
setStatus('7.12', 'done offline (2026-11-11): Live Link > Quick match picks a mode and bot counts and now starts the match itself. From a lobby you host it sends the map, the mode, the bot dvars (bot_MaxNumEnemyBots / AllyBots, mp_bots_sec_before_lobby_autofill and the recompiled client\'s replay_dw_botCount) and xpartygo 1 (no countdown) as one command text. From the Multiplayer menu it also sends xblive_privatematch and xstartprivatematch, then starts when the lobby we host has been up for 1.5 s (30 s limit). In any other state it only sets the values and says so. The start commands are the ones research 76 verified in code; the bot effect and the menu-less lobby are NOT SEEN in game')
save(p, nl.join(lines))

p = 'docs/PROGRESS.md'
s, nl = load(p)
a = '  - Still open: impacts (F.12), the script-played loop of an own sound (F.6), reverb, probes, game modes, killstreak'
assert s.count(a) == 1
add = [
 '  - Client side of the cold start: DW_Devmap.cpp and its two registration lines ARE now in E:\\DW\\IW8 (owner: "get',
 '    the devmap integration ready to use"). The non-publishing client build is blocked by the DW project\'s own state',
 '    (see memory dw-client-build-rules); the owner has to build and install the client. An Opus agent is writing',
 '    DW_MapDebug.cpp (a map-development debug overlay dvar) and docs/research/78_map_debug_overlay.md.',
 '  - Proxy leftovers removed from what the mapper sees: deploy and launch messages no longer name the F6 browser; the',
 '    Live Link dock hides the proxy sections and writes no control.ini unless XInput9_1_0.dll is in the game folder',
 '    (LiveLinkPanel::proxyInstalledIn; setting live/proxyControls forces them on); Quick match starts the match',
 '    (7.12). Tests: core 1071 / 0, GUI 652 / 0.',
]
s = s.replace(a, nl.join(add) + nl + a)
save(p, s)
print('ok')
