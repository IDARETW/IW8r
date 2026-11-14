import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


M = 'src/qtapp/main_window.cpp'
patch(M, '''        withClutter = *refClutter_;
        withClutter.insert(withClutter.end(), refProps_->begin(), refProps_->end());
        drawProps = &withClutter;
    }''', '''        withClutter = *refClutter_;
        withClutter.insert(withClutter.end(), refProps_->begin(), refProps_->end());
        drawProps = &withClutter;
    }
    // The map's scripted objects (scriptableMapEnts: doors, lamps, signal lights, targets; 106 on Speedball) as their
    // definition's model, read-only like the static props, unless File > Edit Placed Objects made them entities (then
    // the document draws them). Owner 2026-11-13: Speedball's signal lights were missing from the viewport.
    if (!stockPlaced_.empty() && !hasPlacedObjects(ctx_.doc.map()) && ctx_.scene.scriptableModelFor) {
        if (drawProps != &withClutter) withClutter = *refProps_, drawProps = &withClutter;
        for (const StockPlacedObject& o : stockPlaced_) {
            const std::string model = ctx_.scene.scriptableModelFor(o.definition);
            if (model.empty()) continue;
            StockModelInstance in;
            in.model = model;
            in.origin = o.origin;
            in.axis = anglesToAxis(o.angles);
            in.index = 0x80000000u | o.index;  // not a static model row (the hidden-props list names those)
            withClutter.push_back(std::move(in));
        }
    }''')
s = io.open(M, encoding='utf-8', newline='').read()
if '#include "stock_placed.h"' not in s:
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('#include "stock_terrain_layers.h"', '#include "stock_terrain_layers.h"' + nl + '#include "stock_placed.h"', 1)
    io.open(M, 'w', encoding='utf-8', newline='').write(s)
print('ok')
