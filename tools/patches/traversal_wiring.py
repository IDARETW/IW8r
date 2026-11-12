import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/qtapp/main_window.cpp', [
 ('''        addCommand(prims, "AddVolumetricBox", "Volumetric Box (iw8r_volume)", QKeySequence(), [this] {''',
  '''        addCommand(prims, "MakeTraversal", "Traversal For Bots (jump / climb / ladder)...", QKeySequence(), [this] {
            // 6.23: a node_negotiation_begin / _end pair with one of the nine stock animations, at the 2D view centre.
            QStringList items;
            for (const TraversalAnim& a : stockTraversalAnims()) items << QString("%1  (%2)").arg(a.title, a.name);
            bool ok = false;
            const QString picked = QInputDialog::getItem(this, "Traversal For Bots", "Animation the bot plays:", items, 0, false, &ok);
            if (!ok) return;
            const TraversalAnim* anim = nullptr;
            for (const TraversalAnim& a : stockTraversalAnims())
                if (picked.contains(QString("(%1)").arg(a.name))) anim = &a;
            if (!anim) return;
            const double yaw = QInputDialog::getDouble(this, "Traversal For Bots", "Facing (yaw, degrees):", 0, -360, 360, 0, &ok);
            if (!ok) return;
            std::set<std::string> names;
            for (const Entity& e : ctx_.doc.map().entities)
                if (const std::string* n = e.value("targetname")) names.insert(*n);
            std::string name;
            for (int i = 1; name.empty() || names.count(name) || names.count(name + "_end"); ++i) name = "traverse_" + std::to_string(i);
            const auto [beginKeys, endKeys] = traversalPairKeys(*anim, placementPoint(), yaw, name);
            ctx_.doc.beginGroup("Traversal For Bots");
            const Id b = ctx_.doc.addEntity(beginKeys, {}, "Traversal begin");
            const Id e = ctx_.doc.addEntity(endKeys, {}, "Traversal end");
            ctx_.doc.endGroup();
            ctx_.doc.deselectAll();
            ctx_.doc.select({b, kNoId}, false);
            ctx_.doc.select({e, kNoId}, true);
            ctx_.scene.invalidate();
            ctx_.documentEdited(QString("Traversal %1 (%2): move the begin node to the edge and the end node to where the bot lands; "
                                        "the build writes it into the navigation mesh").arg(QString::fromStdString(name), anim->name));
        });
        addCommand(prims, "AddVolumetricBox", "Volumetric Box (iw8r_volume)", QKeySequence(), [this] {'''),
])
s = io.open('src/qtapp/main_window.cpp', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if '#include "traversal.h"' not in s:
    s = s.replace('#include "tool_commands.h"' + nl, '#include "tool_commands.h"' + nl + '#include "traversal.h"' + nl, 1)
    assert '#include "traversal.h"' in s
io.open('src/qtapp/main_window.cpp', 'w', encoding='utf-8', newline='').write(s)

patch('src/core/link_graph.cpp', [
 ('''        if (cls == "node_negotiation_begin" && !pairedBegins.count(e.id))
            out.push_back({e.id, true, "node_negotiation_begin has no matching node_negotiation_end"});''',
  '''        if (cls == "node_negotiation_begin" && !pairedBegins.count(e.id))
            out.push_back({e.id, true, "node_negotiation_begin has no matching node_negotiation_end"});
        // 6.23: the animation a bot plays. Empty: the converter leaves the traversal out; not one of the nine the stock
        // maps use: whether bots have it is not known.
        if (cls == "node_negotiation_begin") {
            const std::string* anim = e.value("animscript");
            if (!anim || anim->empty())
                out.push_back({e.id, true, "node_negotiation_begin has no animscript (the build leaves the traversal out)"});
            else if (!findTraversalAnim(*anim))
                out.push_back({e.id, false, "animscript '" + *anim + "' is not one of the stock traversal animations (jump_down_80, "
                                            "jump_up_40, ladder_up, ...): bots may not have it"});
        }'''),
])
s = io.open('src/core/link_graph.cpp', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if '#include "traversal.h"' not in s:
    s = s.replace('#include "link_graph.h"' + nl, '#include "link_graph.h"' + nl + nl + '#include "traversal.h"' + nl, 1)
    assert '#include "traversal.h"' in s
io.open('src/core/link_graph.cpp', 'w', encoding='utf-8', newline='').write(s)
print('ok')
