import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

# the table index of each instance
patch('src/core/stock_smodels.h', [(
 '    double scale = 1;                                    // uniform scale from smodelInstanceData (raw value)\n};\n',
 '    double scale = 1;                                    // uniform scale from smodelInstanceData (raw value)\n'
 '    uint32_t index = 0;                                  // its row in GfxWorld.smodels.smodelInstanceData (11.5: hide)\n};\n')])
patch('src/core/stock_smodels.cpp', [(
 '            s.scale = f32At(inst, o + 20);\n            out.push_back(std::move(s));\n',
 '            s.scale = f32At(inst, o + 20);\n            s.index = uint32_t(k);\n            out.push_back(std::move(s));\n')])

# the reference display leaves hidden props out
patch('src/qtapp/main_window.cpp', [(
 '    for (const StockModelInstance& in : *refProps_) {\n',
 '    const std::vector<uint32_t> hiddenProps = stockEditHiddenProps(ctx_.doc.map());  // 11.5: hidden in the rebuilt map\n'
 '    for (const StockModelInstance& in : *refProps_) {\n'
 '        if (std::binary_search(hiddenProps.begin(), hiddenProps.end(), in.index)) continue;\n'),
 ('    addCommand(file, "RebuildStockZone", "Rebuild Stock Zone (dump script, slow)...", QKeySequence(), [this] {\n',
  '    addCommand(file, "HideStockProps", "Hide Stock Props In Selection", QKeySequence(), [this] { hideStockPropsInSelection(); });\n'
  '    addCommand(file, "ShowStockProps", "Show All Stock Props", QKeySequence(), [this] { showAllStockProps(); });\n'
  '    addCommand(file, "RebuildStockZone", "Rebuild Stock Zone (dump script, slow)...", QKeySequence(), [this] {\n'),
])
s = io.open('src/qtapp/main_window.cpp', encoding='utf-8', newline='').read()
if '#include "stock_zone_edit.h"' not in s:
    nl = '\r\n' if '\r\n' in s else '\n'
    a = '#include "stock_spawns.h"'
    assert s.count(a) == 1
    s = s.replace(a, a + nl + '#include "stock_zone_edit.h"')
    io.open('src/qtapp/main_window.cpp', 'w', encoding='utf-8', newline='').write(s)

patch('src/qtapp/main_window.h', [(
 '    void rebuildStockZoneFast();\n',
 '    void rebuildStockZoneFast();\n'
 '    // 11.5: the stock props (static models of the reference) whose origin lies inside the selection bounds are\n'
 '    // hidden in the rebuilt map (worldspawn iw8r_hide_smodels) and leave the reference display. Returns how many\n'
 '    // were added; -1 without a stock reference or a selection.\n'
 '    int hideStockPropsInSelection();\n'
 '    int showAllStockProps();  // clears the list; returns how many were hidden\n')])

s = io.open('src/qtapp/main_window_stock_edit.cpp', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '}  // namespace iw8r::qt'
assert s.count(a) == 1
add = '''int MainWindow::hideStockPropsInSelection() {
    if (stockZone_.isEmpty() || !refProps_ || refPropZone_ != referenceZone_) {
        ctx_.setStatus("Hide Stock Props: open a stock map and wait for its props first (File > Open IW8 Stock Map)");
        return -1;
    }
    const AABB box = ctx_.doc.selectionBounds();
    if (!box.valid()) {
        ctx_.setStatus("Hide Stock Props: select a brush that encloses the props to hide (the brush itself is not built into a stock map)");
        return -1;
    }
    std::vector<uint32_t> hidden = stockEditHiddenProps(ctx_.doc.map());
    const size_t before = hidden.size();
    for (const StockModelInstance& in : *refProps_) {
        const Vec3& o = in.origin;
        if (o.x < box.mins.x || o.y < box.mins.y || o.z < box.mins.z || o.x > box.maxs.x || o.y > box.maxs.y || o.z > box.maxs.z) continue;
        hidden.push_back(in.index);
    }
    std::sort(hidden.begin(), hidden.end());
    hidden.erase(std::unique(hidden.begin(), hidden.end()), hidden.end());
    const int added = int(hidden.size() - before);
    if (added == 0) {
        ctx_.setStatus("Hide Stock Props: no stock prop has its origin inside the selection");
        return 0;
    }
    std::string list;
    for (const uint32_t i : hidden) list += (list.empty() ? "" : " ") + std::to_string(i);
    for (const Entity& e : ctx_.doc.map().entities)
        if (e.isWorldspawn()) {
            ctx_.doc.setKeyValue(e.id, "iw8r_hide_smodels", list);
            break;
        }
    rebuildReferenceProps();
    ctx_.documentEdited(QString("%1 stock prop(s) hidden (%2 in all): File > Rebuild Stock Zone leaves them out; their collision stays")
                            .arg(added).arg(hidden.size()));
    return added;
}

int MainWindow::showAllStockProps() {
    const std::vector<uint32_t> hidden = stockEditHiddenProps(ctx_.doc.map());
    if (hidden.empty()) {
        ctx_.setStatus("Show All Stock Props: no stock prop is hidden");
        return 0;
    }
    for (const Entity& e : ctx_.doc.map().entities)
        if (e.isWorldspawn()) {
            ctx_.doc.removeKey(e.id, "iw8r_hide_smodels");
            break;
        }
    rebuildReferenceProps();
    ctx_.documentEdited(QString("%1 hidden stock prop(s) are shown again").arg(hidden.size()));
    return int(hidden.size());
}

'''.replace('\n', nl)
s = s.replace(a, add + a)
if '#include <algorithm>' not in s:
    s = s.replace('#include "main_window.h"', '#include "main_window.h"' + nl + nl + '#include <algorithm>', 1)
io.open('src/qtapp/main_window_stock_edit.cpp', 'w', encoding='utf-8', newline='').write(s)
print('ok')
