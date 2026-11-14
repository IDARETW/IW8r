import io

# Occlusion bake: opaque brushes that are not axis-aligned boxes (slanted, many-sided) give the box found inside them
# (core occluder_box.h). The box lies inside the brush, so it hides at most what the brush hides.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, n=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    write(p, s.replace(a, b))


C = 'src/core/compile_iw8.cpp'
patch(C, '''    // game's Umbra hides what lies behind them. Only axis-aligned boxes whose faces are all opaque (or caulk) count;
    // anything else hides nothing. Without the key the converter writes its one-cell tome (nothing hidden by Umbra).''',
      '''    // game's Umbra hides what lies behind them. Brushes whose faces are all opaque (or caulk) count: an axis-aligned
    // box as it is, any other convex brush by the box found inside it (core occluder_box.h; at least 8 units on every
    // side, which leaves out thin slanted walls). Without the key the converter writes its one-cell tome.''')
patch(C, '''        size_t count = 0, skewed = 0, open = 0;''', '''        size_t count = 0, skewed = 0, fitted = 0, open = 0;''')
patch(C, '''            if (!isBox) {
                ++skewed;
                continue;
            }
            if (count) boxes += ",";
            boxes += "[" + num(b.bounds.mins.x) + "," + num(b.bounds.mins.y) + "," + num(b.bounds.mins.z) + "," + num(b.bounds.maxs.x) + "," +
                     num(b.bounds.maxs.y) + "," + num(b.bounds.maxs.z) + "]";
            ++count;''', '''            AABB occluder = b.bounds;
            if (!isBox) {
                std::vector<Vec3> corners;
                for (const Winding& w : b.wins) corners.insert(corners.end(), w.begin(), w.end());
                const std::optional<AABB> in = inscribedBox(b.planes, corners);
                if (!in || in->maxs.x - in->mins.x < 8 || in->maxs.y - in->mins.y < 8 || in->maxs.z - in->mins.z < 8) {
                    ++skewed;
                    continue;
                }
                occluder = *in;
                ++fitted;
            }
            if (count) boxes += ",";
            boxes += "[" + num(occluder.mins.x) + "," + num(occluder.mins.y) + "," + num(occluder.mins.z) + "," + num(occluder.maxs.x) + "," +
                     num(occluder.maxs.y) + "," + num(occluder.maxs.z) + "]";
            ++count;''')
patch(C, '''" occluder box(es) from the opaque box brushes (" + std::to_string(skewed) +
                       " opaque brush(es) are not axis-aligned boxes and " + std::to_string(open) +
                       " are not opaque: they hide nothing)" +''', '''" occluder box(es) from the opaque brushes (" + std::to_string(fitted) +
                       " of them fitted inside brushes that are not axis-aligned boxes; " + std::to_string(skewed) +
                       " opaque brush(es) are too thin or too slanted to hold a box and " + std::to_string(open) +
                       " are not opaque: they hide nothing)" +''')
s = read(C)
if '#include "occluder_box.h"' not in s:
    s = s.replace('#include "compile_iw8.h"', '#include "compile_iw8.h"\n#include "occluder_box.h"', 1)
    assert '#include "occluder_box.h"' in s
write(C, s)
print('ok')
