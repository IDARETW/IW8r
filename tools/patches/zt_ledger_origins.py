import io
import sys

# Converter (x64-zt worktree): the output ledger knows two kinds of asset the editor's builds now have: the mask
# pictures of volumetric boxes (they were listed as UNEXPLAINED) and an edge list that holds mount edges too.
# Argument: the converter checkout (default the IW8 Radiant worktree).

ROOT = (sys.argv[1] if len(sys.argv) > 1 else 'C:/zsw-iw8r/scriptables').rstrip('/') + '/'
C = ROOT + 'src/convert/iw3_iw8/convert.cpp'
s = io.open(C, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (s.count(a), a[:70])
    s = s.replace(a, b)


rep('''                else if (name.starts_with("hud_"))
                    origin = "HUD image of the map";
                break;''', '''                else if (name.starts_with("hud_"))
                    origin = "HUD image of the map";
                else if (name.starts_with("mw120r/" + map + "_volumetric_mask_"))
                    origin = "mask picture of a volumetric box (world volumetrics)";
                break;''')
rep('''                origin = "IW3 ladders (clipmap)";''', '''                origin = "ladders and mount edges (collision)";''')
io.open(C, 'w', encoding='utf-8', newline='').write(s)
print('ok')
