import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    for a, b in pairs:
        assert s.count(a) == 1, (p, s.count(a), a[:60])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('data/prefabs/bundles/doors/stock_door_wood_single.iw8prefab/prefab.json', [
    ('stock door built as a scripted door from stock parts (the converter cannot write scriptables yet)',
     'stock door built as a scripted door from stock parts (a map cannot carry the stock door definition yet)'),
])
patch('src/core/prefab.cpp', [
    (' scriptable object(s) left out (the converter cannot write scriptable instances yet)',
     ' scriptable object(s) left out (scriptables inside a prefab are not built yet)'),
])
patch('tests/prefab_bundle_tests.cpp', [
    ('stock door built as a scripted door from stock parts (the converter cannot write "',
     'stock door built as a scripted door from stock parts (a map cannot carry the stock "'),
])
print('ok')
