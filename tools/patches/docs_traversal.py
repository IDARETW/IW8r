import io


def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')


def rep(p, pairs):
    s, nl = load(p)
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
for i, line in enumerate(lines):
    if line.startswith('| 6.23 |'):
        a = 'traversal authoring and bot path test open'
        assert a in line
        lines[i] = line.replace(a, 'traversal authoring (2026-11-12): Primitives > Traversal For Bots places a node_negotiation_begin / _end pair with one of the nine animations the stock maps use (core traversal.*; Check Map flags a begin without an animation or with one no stock map uses); a test map built: "1 authored traversals as Begin/End pathnodes with a negotiation link". Bot path test open')
io.open(p, 'w', encoding='utf-8', newline='').write(nl.join(lines))

s, nl = load('docs/features/FEATURES.md')
i = s.index('### Spawns')
j = s.index(nl + '### ', i + 5)
s = s[:j] + nl + '''- Traversals for bots: Primitives > Traversal For Bots places a begin / end node pair with one of the nine animations the stock maps use (jump down 40 / 80 / 136 / 160, climb up 40 / 80, jump across 100, ladder up, window ledge); move the nodes to the edge and the landing. The build writes it into the navigation mesh; Check Map flags a begin node without an animation. (tested offline; bots not seen in game)''' + s[j:]
io.open('docs/features/FEATURES.md', 'w', encoding='utf-8', newline='').write(s)

s, nl = load('docs/USER_GUIDE.md')
i = s.index('### Triggers for the map script')
s = s[:i] + '''### Traversals for bots

Bots find their way on the navigation mesh the build makes from the floors. For a drop, a climb, a gap or a ladder
they need a traversal: Primitives > Traversal For Bots asks for the animation (one of the nine the stock maps use) and
the facing, and places a `node_negotiation_begin` with its `node_negotiation_end` at the 2D view centre. Move the begin
node to the edge where the bot starts and the end node to where it lands. Check Map warns about a begin without an
end or without an animation.

''' + s[i:]
io.open('docs/USER_GUIDE.md', 'w', encoding='utf-8', newline='').write(s)

rep('CHANGELOG.md', [
 ('''- **Bridge Two Faces**''', '''- **Traversals for bots** (Primitives menu): a begin / end node pair with a stock animation, written into the
  navigation mesh by the build.
- **Bridge Two Faces**'''),
])
print('ok')
