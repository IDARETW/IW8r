import io

# TEST_CHECKLIST: the web page checks of this round (no game needed).
# Superseded by docs_checklist_web_fix.py: the items live in the "In the editor" section.

p = 'E:/IW8-Radiant/docs/TEST_CHECKLIST.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '## In game (optional, 2026-11-13): an own map with and without baked lightmaps'
assert s.count(a) == 1 and 'In a browser (no game needed, 2026-11-13)' not in s
b = '''## In a browser (no game needed, 2026-11-13): the web page

Tools > Web Server on, then open the address the status bar shows. Each point was run by me in a browser against a
running editor; this list is for your own look.

- [ ] The page has the editor's menu bar. View > Game Lighting from the page switches the editor's camera view.
- [ ] "Find a command": type "shadow"; Sun Shadow Detail is listed greyed ("opens a dialog on the editor's screen").
- [ ] Compile > Sun & Sky opens a form with the map's sun; change the pitch, Apply, and the editor's sun moves;
      Undo in the page puts it back.
- [ ] Tools > Game Modes: pick "Warzone: Battle Royale Trios", "Carry in this map", OK; worldspawn gets
      `iw8r_mode_dvars`. Tools > Check Map lists the same findings as the editor's dialog.
- [ ] View box > "Editor view (live)": the editor's own picture; drag to look, W A S D to fly, click selects.
      A model clicked in Assets lands on the surface at the centre of the picture.
- [ ] A big map (Cinder Rail) opens in the page within a few seconds.

'''.replace('\n', nl)
i = s.index(a)
s = s[:i] + b + s[i:]
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
