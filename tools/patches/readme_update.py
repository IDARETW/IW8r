import io

p = 'README.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
start = s.index('**Status:**')
end = s.index('## Build')
new = '''**Status (2026-11-12):** a complete editor and build tool. It edits maps (brushes, patches, entities, prefabs,
materials, models, lights, effects, sound, scripts without code), builds them into the game's own zones and checks
them with the game's loaders offline, starts them in the game (F5), and edits the stock maps, Warzone included
(entities, spawns, loot, buy stations, added models and brushes, installed as patch files). Many newer features are
built and checked offline but **not yet seen in game**; the feature list says which.

| Document | What it holds |
|---|---|
| [docs/features/FEATURES.md](docs/features/FEATURES.md) | **The full feature list, with screenshots** |
| [docs/USER_GUIDE.md](docs/USER_GUIDE.md) | How to use each feature |
| [docs/TEST_CHECKLIST.md](docs/TEST_CHECKLIST.md) | The in-game checks that only the game can answer |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Every item with its status (done, partial, open) and where its proof is |
| [docs/PROGRESS.md](docs/PROGRESS.md) | Working notes for whoever continues the work |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Target build and evidence, components, IW8 format facts, licensing |
| [docs/research/](docs/research/) | Evidence reports (game formats, converter work, Warzone, stock map editing, collision tiles, ...) |

## What it does (short)

- **Editor**: Qt 6 with dockable views; Direct3D 11 camera with textured, lit (sun shadows), fog, wind and effect
  previews; 2D views; Radiant-style brush, patch, clipper, CSG, vertex, UV and terrain tools; lossless `.map` files.
- **Assets**: browsers for every stock material, model, image, effect and sound of the game; custom materials,
  models (OBJ / glTF), sounds and effects; prefab bundles with parameters.
- **Gameplay**: spawns and every core game mode's objects, Warzone game mode presets, loot points, buy stations,
  vehicles, triggers, doors, lifts and other behaviours without scripting, stock scripted objects (doors, cones, cars).
- **Build**: F7 builds the map into the game's zones (x64-zt converter), with collision in one piece or as the game's
  streamed tiles for big maps, and tests every zone with the game's own loaders (ACTS); F5 plays it.
- **Stock maps**: open any stock map with its world and props, change entities, spawns, loot points, buy stations,
  add models and brushes, hide props, rebuild in seconds, install as patch files and restore.

'''
s = s[:start] + new.replace('\n', nl) + s[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
