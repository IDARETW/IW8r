import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/core/stock_placed.h', [
 ('''// instance of the same definition (the converter's editzone "scriptable_instances"). Nothing is removed.''',
  '''// instance of the same definition (the converter's editzone "scriptable_instances"). A deleted one is removed (the
// converter renumbers the dynamic entities that name later instances; checked on Quarry, tools/
// warzone_instances_remove_test.py). Removals happen only when the document holds placed objects at all (File > Edit
// Placed Objects was used), so a document without them never drops anything.'''),
 ('''    std::string json;  // {"move":[...],"add":[...]} for the edits file; "" when nothing changed
    size_t moved = 0, added = 0;''', '''    std::string json;  // {"move":[...],"add":[...],"remove":[...]} for the edits file; "" when nothing changed
    size_t moved = 0, added = 0, removed = 0;'''),
 ('''// catalogue. Notes say what cannot be done: a definition that is unknown, has dynamic entities or is not on the
// map yet, and stock objects that were deleted in the document (they stay).''', '''// catalogue. A stock instance of an editable kind with no entity left is removed. Notes say what cannot be done: a
// definition that is unknown, has dynamic entities or is not on the map yet.'''),
])
patch('src/core/stock_placed.cpp', [
 ('''    // Stock objects of an editable kind whose entity is gone: they cannot be dropped.
    size_t deleted = 0;
    for (const StockPlacedObject& o : stock) {
        if (seen.count(o.index)) continue;
        const ScriptableDefinition* def = catalog.find(o.definition);
        if (def && editablePlacedDefinition(*def)) ++deleted;
    }
    if (deleted)
        out.notes.push_back(std::to_string(deleted) + " placed object(s) were deleted in the document: a stock map cannot drop them (other objects "
                            "are numbered after them); they stay where they were. Move one out of the way instead");''',
  '''    // Stock objects of an editable kind whose entity is gone: removed (with the definition's numbers for the counts).
    std::string removes;
    for (const StockPlacedObject& o : stock) {
        if (seen.count(o.index)) continue;
        const ScriptableDefinition* def = catalog.find(o.definition);
        if (!def || !editablePlacedDefinition(*def)) continue;
        removes += std::string(out.removed ? "," : "") + "{\\"index\\":" + std::to_string(o.index) + ",\\"server_instanced_parts\\":" +
                   std::to_string(def->serverInstancedParts) + ",\\"usable_parts\\":" + std::to_string(def->usableParts) +
                   ",\\"physics_limit\\":" + (def->physicsLimit ? "true" : "false") + ",\\"physics_limit_detail\\":" +
                   (def->physicsLimitDetail ? "true" : "false") + "}";
        ++out.removed;
    }'''),
 ('''    if (out.moved || out.added) {
        out.json = "{";
        if (out.moved) out.json += "\\"move\\":[" + moves + "]";
        if (out.added) out.json += std::string(out.moved ? "," : "") + "\\"add\\":[" + adds + "]";
        out.json += "}";
        out.notes.insert(out.notes.begin(), std::to_string(out.moved) + " placed object(s) moved, " + std::to_string(out.added) + " added");
    }''', '''    if (out.moved || out.added || out.removed) {
        out.json = "{";
        if (out.moved) out.json += "\\"move\\":[" + moves + "]";
        if (out.added) out.json += std::string(out.moved ? "," : "") + "\\"add\\":[" + adds + "]";
        if (out.removed) out.json += std::string(out.moved || out.added ? "," : "") + "\\"remove\\":[" + removes + "]";
        out.json += "}";
        out.notes.insert(out.notes.begin(), std::to_string(out.moved) + " placed object(s) moved, " + std::to_string(out.added) + " added" +
                                                (out.removed ? ", " + std::to_string(out.removed) + " removed" : std::string()));
    }'''),
])
patch('tests/stock_placed_tests.cpp', [
 ('''        CHECK(e.moved == 1 && e.added == 2);
        CHECK(e.json ==
              "{\\"move\\":[{\\"index\\":0,\\"origin\\":[164,200,36],\\"angles\\":[0,90,0]}],"
              "\\"add\\":[{\\"like\\":0,\\"origin\\":[100,328,36],\\"angles\\":[0,45,0],\\"server_instanced_parts\\":1,\\"usable_parts\\":1,"
              "\\"physics_limit\\":true,\\"physics_limit_detail\\":true},"
              "{\\"like\\":0,\\"origin\\":[-156,200,44],\\"angles\\":[0,0,0],\\"server_instanced_parts\\":1,\\"usable_parts\\":1,"
              "\\"physics_limit\\":true,\\"physics_limit_detail\\":true}]}");''',
  '''        CHECK(e.moved == 1 && e.added == 2 && e.removed == 1);
        CHECK(e.json ==
              "{\\"move\\":[{\\"index\\":0,\\"origin\\":[164,200,36],\\"angles\\":[0,90,0]}],"
              "\\"add\\":[{\\"like\\":0,\\"origin\\":[100,328,36],\\"angles\\":[0,45,0],\\"server_instanced_parts\\":1,\\"usable_parts\\":1,"
              "\\"physics_limit\\":true,\\"physics_limit_detail\\":true},"
              "{\\"like\\":0,\\"origin\\":[-156,200,44],\\"angles\\":[0,0,0],\\"server_instanced_parts\\":1,\\"usable_parts\\":1,"
              "\\"physics_limit\\":true,\\"physics_limit_detail\\":true}],"
              "\\"remove\\":[{\\"index\\":2,\\"server_instanced_parts\\":1,\\"usable_parts\\":1,\\"physics_limit\\":true,\\"physics_limit_detail\\":true}]}");'''),
 ('''            counted |= n == "1 placed object(s) moved, 2 added";
            deleted |= n.find("1 placed object(s) were deleted") != std::string::npos;''',
  '''            counted |= n == "1 placed object(s) moved, 2 added, 1 removed";
            deleted = true;'''),
])
print('ok')
