import io
import json
import subprocess
import sys

# Entity definitions: worldspawn iw8r_haze and the lamp key iw8r_volumetric (research 126), in the overrides file;
# then the generated definitions are rebuilt.

ROOT = 'E:/IW8-Radiant/'
p = ROOT + 'data/iw8_entity_defs_overrides.json'
text = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in text else '\n'
j = json.loads(text)


def find_classes(node, out):
    # Every object that maps key names to key definitions and already has the anchor key.
    if isinstance(node, dict):
        for k, v in node.items():
            if isinstance(v, dict) and ('iw8r_shadows' in v or 'iw8r_vision' in v or 'iw8r_fog' in v):
                out.append((k, v, node))
            find_classes(v, out)
    elif isinstance(node, list):
        for v in node:
            find_classes(v, out)


found = []
find_classes(j, found)
lamps = [v for k, v, parent in found if 'iw8r_shadows' in v]
assert len(lamps) >= 2, len(lamps)
for keys in lamps:
    keys['iw8r_volumetric'] = {
        'type': 'bool',
        'default': '0',
        'description': '1 makes the lamp light the volumetric air: a glow around it and beams in fog. The game adds a lamp '
                       'to the air only when it is flagged; stock maps flag their feature lamps, not every lamp. It shows only '
                       'when the map has volumetric air (worldspawn iw8r_haze, or volumetricAirDensity in iw8r_vision). '
                       'Written as volumetric. Research 126. Not seen in game.',
        'required': False,
        'examples': ['1'],
    }
print('lamp classes:', len(lamps))

# worldspawn: where the map-wide keys live. Find the class object named worldspawn and its key map.
def find_named(node, name):
    if isinstance(node, dict):
        if name in node and isinstance(node[name], dict):
            return node[name]
        for v in node.values():
            r = find_named(v, name)
            if r is not None:
                return r
    elif isinstance(node, list):
        for v in node:
            r = find_named(v, name)
            if r is not None:
                return r
    return None


ws = find_named(j, 'worldspawn')
assert ws is not None, 'no worldspawn class in the overrides'
keymap = None
for k, v in ws.items():
    if isinstance(v, dict) and any(isinstance(x, dict) and 'type' in x for x in v.values()):
        keymap = v
assert keymap is not None, list(ws.keys())
ids = 'desert, clear, overcast, dusk, fog'
keymap['iw8r_haze'] = {
    'type': 'string',
    'default': '',
    'description': 'The game\'s own fog and volumetric air: "<preset> [amount]". Presets: ' + ids + '. Amount 0.1 to 4 '
                   '(1 = as designed) scales how thick it is. The game fogs the map and the sky itself (distance and '
                   'ground fog with a glow toward the sun, blended into the low sky), and fills the air with volumetric '
                   'fog that the sun lights and shadows (light shafts) and lamps with iw8r_volumetric 1 glow in. It '
                   'replaces iw8r_fog. Any single value can be changed in iw8r_vision (fog..., volumetric..., '
                   'veilStrength), which wins over the preset. Map > Atmosphere edits it; Game Lighting previews it. '
                   'Research 126. Not seen in game.',
    'required': False,
    'examples': ['desert', 'clear 0.5', 'fog 1.5'],
}
out = json.dumps(j, indent=2, ensure_ascii=False)
io.open(p, 'w', encoding='utf-8', newline='').write(out.replace('\n', nl) + nl)
print('worldspawn keys now:', len(keymap))
r = subprocess.run([sys.executable, '-I', ROOT + 'tools/gen_iw8_entity_defs.py'], capture_output=True, text=True, cwd=ROOT)
print(r.stdout[-400:], r.stderr[-400:])
