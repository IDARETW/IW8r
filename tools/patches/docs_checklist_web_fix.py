import io

# The checklist's parser (core verification) knows two kinds of sections: "In the editor" and "In game ...". The
# web page checks belong to the first: they move there, under a plain lead line instead of a heading of their own.

p = 'E:/IW8-Radiant/docs/TEST_CHECKLIST.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
head = '## In a browser (no game needed, 2026-11-13): the web page' + nl
nxt = '## In game (optional, 2026-11-13): an own map with and without baked lightmaps'
i, j = s.index(head), s.index(nxt)
block = s[i + len(head):j].strip(nl)
s = s[:i] + s[j:]
lines = block.split(nl)
# The intro paragraph, then the items.
k = next(n for n, line in enumerate(lines) if line.startswith('- [ ]'))
items = nl.join(lines[k:])
lead = ('**The web page (2026-11-13; no game needed).** Tools > Web Server on, then open the address the status bar shows.' + nl +
        'Each point was run by me in a browser against a running editor; this list is for your own look.' + nl + nl)
first_game = s.index('## In game')
s = s[:first_game] + lead + items + nl + nl + s[first_game:]
for script in ('E:/IW8-Radiant/tools/patches/docs_checklist_web.py',):
    t = io.open(script, encoding='utf-8', newline='').read()
    t = t.replace('# TEST_CHECKLIST: the web page checks of this round (no game needed).',
                  '# TEST_CHECKLIST: the web page checks of this round (no game needed).\n# Superseded by docs_checklist_web_fix.py: the items live in the "In the editor" section.')
    io.open(script, 'w', encoding='utf-8', newline='').write(t)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
