import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/qtapp/main_window_stock_edit.cpp', [
 ('''    // 13.5: placed objects (buy stations) that were moved or added as `scriptable` entities.''',
  '''    // 11.5: world geometry removed inside iw8r_remove_world boxes (drawing and collision; the sun shadow re-baked
    // unless worldspawn iw8r_remove_world_sun is keep). Research 96.
    std::vector<std::string> rmNotes;
    const auto rmBoxes = documentWorldRemovals(ctx_.doc.map(), &rmNotes);
    const bool rmRebake = stockWorldRemovalRebakesSun(ctx_.doc.map());
    const std::string removeWorld = stockWorldRemovalJson(rmBoxes, rmRebake, QDir::toNativeSeparators(sunActs).toStdString());
    if (notes) {
        for (const std::string& n : rmNotes) *notes << qs(n);
        if (!rmBoxes.empty()) {
            *notes << QString("%1 world removal box(es): the stock world inside them stops drawing and colliding%2; the baked "
                              "lighting and the occlusion data keep it (things behind it may still be culled from some views)")
                          .arg(rmBoxes.size())
                          .arg(rmRebake ? ", and the sun shadow is baked again without it" : "");
            if (rmRebake && (sunActs.isEmpty() || !QFileInfo::exists(sunActs)))
                *notes << "ACTS is not set, so the converter looks for IW8_ACTS; without it the rebuild stops at the sun shadow";
        }
    }

    // 13.5: placed objects (buy stations) that were moved or added as `scriptable` entities.'''),
 ('''                                           &placed.json, &stockPatches, &removeCollision, &lootBoxes, &sun, &replaceMaterials);''',
  '''                                           &placed.json, &stockPatches, &removeCollision, &lootBoxes, &sun, &replaceMaterials,
                                           &removeWorld);'''),
])
patch('src/core/compile_iw8.cpp', [
 ('''        if (isLootBoxClass(cls)) continue;  // loot boxes go to the edits file (stock_loot.h)''',
  '''        if (isLootBoxClass(cls)) continue;  // loot boxes go to the edits file (stock_loot.h)
        if (isRemoveWorldClass(cls)) continue;  // world removal boxes go to the edits file (stock_zone_edit.h)'''),
 ('''            if (isLootBoxClass(lcls)) continue;  // loot clusters and zones: boxes of the collision json (below)''',
  '''            if (isLootBoxClass(lcls)) continue;  // loot clusters and zones: boxes of the collision json (below)
            if (isRemoveWorldClass(lcls)) continue;  // stock map edits only (stock_zone_edit.h): nothing in an own map'''),
])
s = io.open('src/core/compile_iw8.cpp', encoding='utf-8', newline='').read()
if '#include "stock_zone_edit.h"' not in s:
    s = s.replace('#include "stock_loot.h"', '#include "stock_loot.h"\n#include "stock_zone_edit.h"', 1)
    io.open('src/core/compile_iw8.cpp', 'w', encoding='utf-8', newline='').write(s)
print('ok')
