import io

# The worldspawn key iw8r_mount_edges in the entity definitions (the inspector's help).

p = 'E:/IW8-Radiant/data/iw8_entity_defs_overrides.json'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '''          "examples": [
            "2",
            "3.5"
          ]
        }
      }
    },
    "fx_origin": {'''.replace('\n', nl)
b = '''          "examples": [
            "2",
            "3.5"
          ]
        },
        "iw8r_mount_edges": {
          "type": "bool",
          "default": "1",
          "description": "Mount edges: where a player can rest a weapon on cover. The game mounts only on edges the map lists. The build finds them on the map's visible solid brushes: outside wall corners at least 32 units tall and ledge tops at least 16 units long, where nothing covers them and there is room to stand. Brushes side by side or stacked count as one wall. Clip brushes and placed models hide edges but give none. 0 builds the map without them. Not seen in game.",
          "required": false,
          "examples": [
            "0"
          ]
        }
      }
    },
    "fx_origin": {'''.replace('\n', nl)
assert s.count(a) == 1, s.count(a)
io.open(p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))
print('ok')
