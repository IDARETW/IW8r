import json, io

p = 'data/iw8_entity_defs_overrides.json'
raw = io.open(p, encoding='utf-8').read()
d = json.loads(raw)
classes = d['classes']
assert 'iw8r_loot_cluster' not in classes and 'iw8r_loot_zone' not in classes
classes['iw8r_loot_cluster'] = {
    'description': "A new loot cluster on a Warzone stock map: a box (the bounds of this entity's brushes) that groups the "
                   "loot points inside it. File > Rebuild Stock Zone adds it after the map's own clusters; loot points "
                   "inside it whose cluster is auto join it. The map's own clusters win where boxes overlap. Not kept in "
                   "the entity text. Not seen in game.",
    'kind': 'brush',
    'color': [0.2, 0.9, 0.35],
    'buildAccepted': True,
    'keys': {},
}
classes['iw8r_loot_zone'] = {
    'description': "A new named loot zone on a Warzone stock map: a box (the bounds of this entity's brushes) whose name "
                   "the loot tables pick the loot by. File > Rebuild Stock Zone adds it after the map's own zones; loot "
                   "points and new clusters inside it take it. Use a name the map's loot tables know (a zone name of the "
                   "map, e.g. map_quarry). Not kept in the entity text. Not seen in game.",
    'kind': 'brush',
    'color': [0.95, 0.75, 0.2],
    'buildAccepted': True,
    'keys': {
        'name': {
            'type': 'string',
            'default': '',
            'description': "The zone name the loot tables pick the loot by (lower-case letters, digits and '_'). Empty: "
                           "the map's first zone name.",
            'required': False,
            'examples': ['map_quarry', 'map_hospital', 'tutorial_scavenger'],
        },
    },
}
io.open(p, 'w', encoding='utf-8', newline='\n').write(json.dumps(d, indent=2, ensure_ascii=False) + '\n')
print('ok')
