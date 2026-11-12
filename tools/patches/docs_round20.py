import io

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
hit = [i for i, l in enumerate(lines) if l.startswith('| F.17 |')]
assert len(hit) == 1
cells = lines[hit[0]].split(' | ')
assert len(cells) == 4
add = (' Third report (2026-11-11 night, mp_rust: "im still not seeing all geometry in stock maps"; research 84). The earlier fix was checked with a test switch that loads the stock reference, and the real menu path never did: '
       'File, Open IW8 Stock Map loaded the entities only, so no world surface and no prop was ever requested. Fixed: opening a stock map now loads its world and props (setting stock/referenceOnOpen). '
       'Also fixed by the helper agent: a prop whose model has not arrived is drawn as an amber marker cube and a failed one as a red cube; a permanent status-bar label shows models ready of total, with the failed ones and their reasons; '
       'models that had been marked failed for good by a copy error are tried again; models whose first detail level sits in another zone now load; batches chain inside the model cache and stop with a reason when a tool is missing; redraws no longer re-read every model file. '
       'Measured with a private empty cache: mp_rust 514 of 514 models in about 20 s (before: 511, 3 failed), mp_backlot2 939 of 939 in about 30 s. '
       'Tests: core stock_prop_fetch (8), headless --stock-props-test and --model-chain-test. Open: the marker cubes still appear in reflection-probe captures taken while a reference is shown. NOT SEEN in the owner\'s window.')
assert '|' not in add
lines[hit[0]] = ' | '.join(cells[:3]) + ' | ' + cells[3].rstrip(' |') + add + ' |'
save(p, nl.join(lines))

p = 'docs/TEST_CHECKLIST.md'
s, nl = load(p)
a = '## In game: showcase 2 (2026-11-11 night), not seen in game'
assert s.count(a) == 1
sec = [
 '## In game: stock maps show their geometry (2026-11-11 night), editor only, not seen',
 '',
 '- [ ] Restart the editor. File > Open IW8 Stock Map > mp_rust. The world appears at once; props appear over the',
 '      next half minute or so (longer the first time). Props still loading are amber cubes; failed ones are red.',
 '- [ ] The status bar has a permanent "Stock props: N of M models ready" label while this runs, and a final count.',
 '- [ ] The same with mp_backlot2.',
 '',
]
s = s.replace(a, nl.join(sec) + nl + a)
save(p, s)

p = 'docs/PROGRESS.md'
s, nl = load(p)
a = '  - Still open: impacts (F.12), the script-played loop of an own sound (F.6), reverb, probes, game modes, killstreak'
assert s.count(a) == 1
add = [
 '  - STOCK MAP GEOMETRY (F.17 third report, research 84): MainWindow::openStockZone now calls loadStockReference',
 '    (the menu path never did; earlier checks used the IW8R_SMOKE_REFERENCE switch). Helper agent: marker cubes for',
 '    props not ready / failed, permanent status label (stockPropStatus / updateStockPropLabel), ModelCache batch',
 '    chain (startBatch / finishBatch / batchEnded), core stock_prop_fetch.*, headless --stock-props-test <zone> and',
 '    --model-chain-test. Lesson: verify the path the owner actually uses, not a test switch.',
 '    Tests at 21:45: core 1103 / 0 (137,302 checks), GUI 664 / 0.',
]
s = s.replace(a, nl.join(add) + nl + a)
save(p, s)
print('ok')
