import io

# Mount edges: the key sits before the dynamic model keys (older tests read the end of the collision file), and the
# log line does not use the words "left out" (a test counts those for lights).

ROOT = 'E:/IW8-Radiant/'
C = 'src/core/compile_iw8.cpp'
s = io.open(ROOT + C, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (s.count(a), a[:70])
    s = s.replace(a, b)


rep('''\\"triangle_contents_schema\\":\\"material-partitions-v1\\","
           "\\"dynamic_model_count\\":" + std::to_string(dynModelCount)''',
    '''\\"triangle_contents_schema\\":\\"material-partitions-v1\\"" +
           mountEdgesKey + ",\\"dynamic_model_count\\":" + std::to_string(dynModelCount)''')
rep('''           (collisionTiles ? ",\\"collision_tiles\\":true" : "") + mountEdgesKey +
''', '''           (collisionTiles ? ",\\"collision_tiles\\":true" : "") +
''')
rep('''" covered edge(s) left out"''', '''" covered edge(s) not counted"''')
io.open(ROOT + C, 'w', encoding='utf-8', newline='').write(s)
print('ok')
