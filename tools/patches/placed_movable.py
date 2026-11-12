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
 ('''bool editablePlacedDefinition(const ScriptableDefinition& def);''', '''bool editablePlacedDefinition(const ScriptableDefinition& def);
// True for a definition whose instances can be moved (not copied or removed): every number is known and it is not a
// sparse occluder (a door). An ordinary dynamic entity of the instance follows the move (research 89 section 8; the
// converter refuses an instance whose dynamic entity is placed apart from it).
bool movablePlacedDefinition(const ScriptableDefinition& def);'''),
])
patch('src/core/stock_placed.cpp', [
 ('''bool editablePlacedDefinition(const ScriptableDefinition& def) { return def.verified && !def.instanceDynent && def.physicsDynents == 0; }''',
  '''bool editablePlacedDefinition(const ScriptableDefinition& def) { return def.verified && !def.instanceDynent && def.physicsDynents == 0; }
bool movablePlacedDefinition(const ScriptableDefinition& def) { return def.verified && !def.sparseOccluder; }'''),
 ('''            if (!def || !editablePlacedDefinition(*def)) {
                ++leftOut["'" + *dv + "' was moved, but it has physics entries of its own (or is not in the catalogue): it stays where it was"];''',
  '''            if (!def || !(editablePlacedDefinition(*def) || movablePlacedDefinition(*def))) {
                ++leftOut["'" + *dv + "' was moved, but it is a door or another sparse occluder (or not in the catalogue): it stays where it was"];'''),
 ('''        if (!def || !editablePlacedDefinition(*def)) continue;
        removes +=''', '''        if (!def || !editablePlacedDefinition(*def)) {
            if (def && movablePlacedDefinition(*def)) ++keptDeleted;
            continue;
        }
        removes +='''),
 ('''    std::string removes;
    for (const StockPlacedObject& o : stock) {''', '''    std::string removes;
    size_t keptDeleted = 0;
    for (const StockPlacedObject& o : stock) {'''),
 ('''    for (const auto& [reason, n] : leftOut) out.notes.push_back(std::to_string(n) + " placed object(s) left out: " + reason);''',
  '''    if (keptDeleted)
        out.notes.push_back(std::to_string(keptDeleted) + " placed object(s) with a dynamic entity were deleted in the document: those can be moved but "
                            "not removed yet; they stay");
    for (const auto& [reason, n] : leftOut) out.notes.push_back(std::to_string(n) + " placed object(s) left out: " + reason);'''),
])
patch('src/qtapp/main_window_stock_edit.cpp', [
 ('''        if (!def || !editablePlacedDefinition(*def)) continue;
        ctx_.doc.addEntity(placedObjectEntityKeys(o), {}, "Placed object " + o.definition);''',
  '''        if (!def || !(editablePlacedDefinition(*def) || movablePlacedDefinition(*def))) continue;
        ctx_.doc.addEntity(placedObjectEntityKeys(o), {}, "Placed object " + o.definition);'''),
 ('''        ctx_.setStatus(QString("Edit Placed Objects: none of the %1 placed objects of this map can be edited (only objects without physics "
                               "entries of their own, such as the Warzone buy station)").arg(stockPlaced_.size()));''',
  '''        ctx_.setStatus(QString("Edit Placed Objects: none of the %1 placed objects of this map can be edited (doors cannot yet)").arg(stockPlaced_.size()));'''),
 ('''    ctx_.documentEdited(QString("%1 of %2 placed objects are now entities (class scriptable): move them, or copy one for a new object; "
                                "Rebuild Stock Zone writes it. They cannot be removed").arg(n).arg(stockPlaced_.size()));''',
  '''    ctx_.documentEdited(QString("%1 of %2 placed objects are now entities (class scriptable): move them; objects without a dynamic entity "
                                "(buy stations) can also be copied or deleted. Rebuild Stock Zone writes it. Doors cannot be edited yet")
                            .arg(n).arg(stockPlaced_.size()));'''),
])
print('ok')
