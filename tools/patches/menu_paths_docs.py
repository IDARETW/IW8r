import io
import re
import sys

# The File menu's submenus (2026-11-15): docs and messages that name the old paths get the new ones.
#   File > Relight Stock Map ...        -> File > Stock Map > Relight Stock Map ...
#   File > Export Overview Image ...    -> File > Export > Export Overview Image ...
#   File > Insert Prefab ...            -> File > Prefabs > Insert Prefab ...
# Usage: py -3 -I menu_paths_docs.py <file> [<file> ...]

STOCK = ['Relight Stock Map', 'Edit Compiled Spawns', 'Edit Loot Points', 'Edit Placed Objects', 'Load Streamed World',
         'Rebuild Stock', 'Install Edited Stock Map', 'Restore Stock Map', 'First-Person Look', 'Hide Stock Props',
         'Show All Stock Props']
PREFABS = ['Export Prefab', 'Import Prefab', 'Insert Prefab', 'Explode', 'Prefab Parameters', 'Save Selection As',
           'Make Selected Prefab Unique', 'Convert Plain', 'Open Bundle Folder', 'Rotate Selected Prefab']
EXPORT = ['Export Overview', 'Export Camera Image', 'Export Turntable', 'Export Map Package']


def fix(text):
    n = 0
    for group, items in (('Prefabs', PREFABS), ('Stock Map', STOCK), ('Export', EXPORT)):
        for it in items:
            # "File > X" and "File &gt; X" (HTML in messages), not already "File > <group> > X".
            for sep in (' > ', ' &gt; '):
                pat = re.compile(re.escape('File' + sep + it))
                text, k = pat.subn('File' + sep + group + sep + it, text)
                n += k
    return text, n


total = 0
for path in sys.argv[1:]:
    s = io.open(path, encoding='utf-8', newline='').read()
    out, n = fix(s)
    if n:
        io.open(path, 'w', encoding='utf-8', newline='').write(out)
        print(path, n)
        total += n
print('total', total)
