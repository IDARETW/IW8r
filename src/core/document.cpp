#include "document.h"
#include "tex_projection.h"
#include "command_script.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace iw8r {

// ------------------------------------------------------------------ file

bool Document::loadFromText(const std::string& text, const std::string& virtualPath, std::string* err) {
    Id next = 1;
    ParseResult r = parseMap(text, next);
    if (!r.map) {
        if (err) *err = virtualPath + ":" + std::to_string(r.error->line) + ": " + r.error->message;
        return false;
    }
    map_ = std::move(*r.map);
    nextId_ = next;
    path_ = virtualPath;
    sel_.clear();
    history_.clear();
    undoPos_ = savedPos_ = 0;
    savedPosLost_ = false;
    drag_.reset();
    touch();
    return true;
}

bool Document::load(const std::string& path, std::string* err) {
    std::string text;
    if (!readFile(path, text)) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    return loadFromText(text, path, err);
}

namespace {
void backupExisting(const std::string& path) {
#ifdef _WIN32
    auto widen = [](const std::string& s) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
        return w;
    };
    const std::wstring src = widen(path);
    if (GetFileAttributesW(src.c_str()) != INVALID_FILE_ATTRIBUTES) CopyFileW(src.c_str(), widen(path + ".bak").c_str(), FALSE);
#else
    (void)path;
#endif
}
}  // namespace

bool Document::save(std::string* err) {
    if (path_.empty()) {
        if (err) *err = "document has no path";
        return false;
    }
    backupExisting(path_);
    if (!writeFileAtomic(path_, map_.text(), err)) return false;
    savedPos_ = undoPos_;
    savedPosLost_ = false;
    return true;
}

bool Document::saveAs(const std::string& path, std::string* err) {
    if (!writeFileAtomic(path, map_.text(), err)) return false;
    path_ = path;
    savedPos_ = undoPos_;
    savedPosLost_ = false;
    return true;
}

bool Document::saveCopy(const std::string& path, std::string* err) const {
    return writeFileAtomic(path, map_.text(), err);
}

// ------------------------------------------------------------------ lookup

std::optional<size_t> Document::entityIndex(Id id) const {
    for (size_t i = 0; i < map_.entities.size(); ++i)
        if (map_.entities[i].id == id) return i;
    return std::nullopt;
}

std::optional<size_t> Document::itemIndex(const Entity& e, Id item) const {
    for (size_t i = 0; i < e.items.size(); ++i)
        if (e.items[i].id == item) return i;
    return std::nullopt;
}

Entity* Document::findEntity(Id id) {
    auto i = entityIndex(id);
    return i ? &map_.entities[*i] : nullptr;
}
const Entity* Document::findEntity(Id id) const {
    auto i = entityIndex(id);
    return i ? &map_.entities[*i] : nullptr;
}

EntityItem* Document::findItem(Id entity, Id item) {
    Entity* e = findEntity(entity);
    if (!e) return nullptr;
    auto i = itemIndex(*e, item);
    return i ? &e->items[*i] : nullptr;
}
const EntityItem* Document::findItem(Id entity, Id item) const {
    const Entity* e = findEntity(entity);
    if (!e) return nullptr;
    auto i = itemIndex(*e, item);
    return i ? &e->items[*i] : nullptr;
}

Entity* Document::worldspawn() {
    for (auto& e : map_.entities)
        if (e.isWorldspawn()) return &e;
    return nullptr;
}

// ------------------------------------------------------------------ grid

void Document::setGrid(double g) { grid_ = std::clamp(g, 0.125, 4096.0); }
void Document::gridUp() { setGrid(grid_ * 2); }
void Document::gridDown() { setGrid(grid_ / 2); }

// ------------------------------------------------------------------ selection

std::vector<SelRef> Document::selectionUnit(const SelRef& r) const {
    const Entity* e = findEntity(r.entity);
    if (!e) return {};
    if (r.item == kNoId || e->isWorldspawn()) return {r};
    std::vector<SelRef> out;
    for (const auto& it : e->items)
        if (it.isPrimitive()) out.push_back({e->id, it.id});
    if (out.empty()) out.push_back({e->id, kNoId});
    return out;
}

void Document::toggleFace(const FaceRef& f) {
    sel_.clear();
    if (!faceSel_.erase(f)) faceSel_.insert(f);
    touch();
}

void Document::selectFace(const FaceRef& f, bool additive) {
    sel_.clear();
    if (!additive) faceSel_.clear();
    faceSel_.insert(f);
    touch();
}

void Document::clearFaceSelection() {
    if (faceSel_.empty()) return;
    faceSel_.clear();
    touch();
}

void Document::pruneFaceSelection() {
    for (auto it = faceSel_.begin(); it != faceSel_.end();) {
        const EntityItem* item = findItem(it->entity, it->item);
        const Primitive* p = item ? item->prim() : nullptr;
        if (!p || p->kind != PrimKind::Brush || it->face >= p->faces.size()) it = faceSel_.erase(it);
        else ++it;
    }
}

void Document::select(const SelRef& r, bool additive) {
    faceSel_.clear();
    if (!additive) sel_.clear();
    for (const auto& u : selectionUnit(r)) sel_.insert(u);
    touch();
}

void Document::toggle(const SelRef& r) {
    faceSel_.clear();
    const auto unit = selectionUnit(r);
    const bool allSelected = !unit.empty() && std::all_of(unit.begin(), unit.end(), [&](const SelRef& u) { return sel_.count(u); });
    for (const auto& u : unit) {
        if (allSelected) sel_.erase(u);
        else sel_.insert(u);
    }
    touch();
}

void Document::deselectAll() {
    if (sel_.empty() && faceSel_.empty()) return;
    sel_.clear();
    faceSel_.clear();
    touch();
}

AABB Document::selectionBounds() const {
    AABB b;
    for (const auto& r : sel_) {
        if (r.item == kNoId) {
            if (const Entity* e = findEntity(r.entity))
                if (auto o = e->origin()) {
                    b.add(*o - Vec3{8, 8, 8});
                    b.add(*o + Vec3{8, 8, 8});
                }
            continue;
        }
        if (const EntityItem* it = findItem(r.entity, r.item))
            if (const Primitive* p = it->prim()) b.add(p->bounds());
    }
    return b;
}

Id Document::inspectedEntity() const {
    if (!sel_.empty()) return sel_.begin()->entity;
    for (const auto& e : map_.entities)
        if (e.isWorldspawn()) return e.id;
    return map_.entities.empty() ? kNoId : map_.entities.front().id;
}

// ------------------------------------------------------------------ undo core

void Document::applyOp(const Op& op, bool forward) {
    switch (op.type) {
        case Op::Type::ReplaceHeader: {
            map_.header = forward ? *op.headerAfter : *op.headerBefore;
            break;
        }
        case Op::Type::ReplaceItem: {
            if (EntityItem* it = findItem(op.entity, op.item)) *it = forward ? *op.after : *op.before;
            break;
        }
        case Op::Type::InsertItem:
        case Op::Type::RemoveItem: {
            Entity* e = findEntity(op.entity);
            if (!e) break;
            const bool insert = (op.type == Op::Type::InsertItem) == forward;
            if (insert) {
                const EntityItem& src = (op.type == Op::Type::InsertItem) ? *op.after : *op.before;
                e->items.insert(e->items.begin() + (std::ptrdiff_t)std::min(op.index, e->items.size()), src);
            } else if (auto i = itemIndex(*e, op.item)) {
                e->items.erase(e->items.begin() + (std::ptrdiff_t)*i);
            }
            break;
        }
        case Op::Type::InsertEntity:
        case Op::Type::RemoveEntity: {
            const bool insert = (op.type == Op::Type::InsertEntity) == forward;
            if (insert) {
                const Entity& src = (op.type == Op::Type::InsertEntity) ? *op.entAfter : *op.entBefore;
                map_.entities.insert(map_.entities.begin() + (std::ptrdiff_t)std::min(op.index, map_.entities.size()), src);
            } else if (auto i = entityIndex(op.entity)) {
                map_.entities.erase(map_.entities.begin() + (std::ptrdiff_t)*i);
            }
            break;
        }
    }
}

bool Document::setHeader(const std::string& header, const std::string& name) {
    if (header == map_.header) return false;
    Transaction t;
    t.name = name;
    t.selBefore = t.selAfter = sel_;
    Op op;
    op.type = Op::Type::ReplaceHeader;
    op.headerBefore = map_.header;
    op.headerAfter = header;
    applyOp(op, true);
    t.ops.push_back(std::move(op));
    push(std::move(t));
    return true;
}

void Document::beginGroup(const std::string& name) {
    if (drag_) endDrag(true);
    if (groupDepth_++ == 0) {
        groupName_ = name;
        groupIndex_.reset();
    }
}

void Document::endGroup() {
    if (groupDepth_ == 0) return;
    if (--groupDepth_ == 0) groupIndex_.reset();
}

void Document::push(Transaction&& t) {
    struct Prune {
        Document* d;
        ~Prune() { d->pruneFaceSelection(); }
    } prune{this};
    if (groupDepth_ > 0 && groupIndex_ && *groupIndex_ + 1 == undoPos_ && undoPos_ == history_.size()) {
        // Merge into the open group's step: ops replay in order, undo replays them in reverse.
        Transaction& g = history_[*groupIndex_];
        for (auto& op : t.ops) g.ops.push_back(std::move(op));
        g.selAfter = std::move(t.selAfter);
        touch();
        return;
    }
    if (groupDepth_ > 0) t.name = groupName_;
    if (undoPos_ < history_.size()) {
        if (savedPos_ > undoPos_) savedPosLost_ = true;
        history_.resize(undoPos_);
    }
    history_.push_back(std::move(t));
    ++undoPos_;
    while (history_.size() > undoLimit && !history_.empty()) {
        history_.erase(history_.begin());
        --undoPos_;
        if (savedPos_ == 0) savedPosLost_ = true;
        else --savedPos_;
    }
    if (groupDepth_ > 0) groupIndex_ = history_.size() - 1;  // the group step is the newest one
    touch();
}

bool Document::undo() {
    if (drag_) endDrag(false);
    if (!canUndo()) return false;
    const Transaction& t = history_[undoPos_ - 1];
    for (auto it = t.ops.rbegin(); it != t.ops.rend(); ++it) applyOp(*it, false);
    sel_ = t.selBefore;
    --undoPos_;
    pruneFaceSelection();
    touch();
    return true;
}

bool Document::redo() {
    if (drag_) endDrag(false);
    if (!canRedo()) return false;
    const Transaction& t = history_[undoPos_];
    for (const auto& op : t.ops) applyOp(op, true);
    sel_ = t.selAfter;
    ++undoPos_;
    pruneFaceSelection();
    touch();
    return true;
}

// ------------------------------------------------------------------ edits

namespace {
std::string vecText(const Vec3& v) { return formatNumber(v.x) + " " + formatNumber(v.y) + " " + formatNumber(v.z); }
}  // namespace

void Document::translateRef(const SelRef& r, const Vec3& delta) {
    if (r.item != kNoId) {
        if (EntityItem* it = findItem(r.entity, r.item))
            if (Primitive* p = it->prim()) {
                if (textureLock && p->kind == PrimKind::Brush) {
                    const Primitive before = *p;
                    p->translate(delta);
                    lockPrimitiveTextures(before, *p, [&](const Vec3& v) { return v + delta; });
                } else {
                    p->translate(delta);
                }
                bump(*it);
            }
        return;
    }
    Entity* e = findEntity(r.entity);
    if (!e) return;
    for (auto& it : e->items)
        if (KeyValueLine* kv = it.kv(); kv && kv->name() == "origin") {
            if (auto o = e->origin()) {
                kv->set(vecText(*o + delta));
                bump(it);
            }
            return;
        }
}

namespace {
// Item id that a selection ref mutates when translated (origin key for point entities).
Id mutatedItem(const Entity& e, const SelRef& r) {
    if (r.item != kNoId) return r.item;
    for (const auto& it : e.items)
        if (const auto* kv = it.kv(); kv && kv->name() == "origin") return it.id;
    return kNoId;
}
}  // namespace

bool Document::entityInSelection(Id entity) const {
    for (const SelRef& r : sel_)
        if (r.entity == entity) return true;
    return false;
}

void Document::emitMacro(bool ok, const std::string& line) {
    if (ok && macroSink && macroDepth_ == 0) macroSink(line);
}

bool Document::setKeyValue(Id entity, const std::string& key, const std::string& value) {
    ++macroDepth_;
    const bool ok = setKeyValueRaw(entity, key, value);
    --macroDepth_;
    emitMacro(ok && entityInSelection(entity), cmdSet(key, value));
    return ok;
}

bool Document::removeKey(Id entity, const std::string& key) {
    ++macroDepth_;
    const bool ok = removeKeyRaw(entity, key);
    --macroDepth_;
    emitMacro(ok && entityInSelection(entity), cmdRemove(key));
    return ok;
}

bool Document::deleteSelection() {
    ++macroDepth_;
    const bool ok = deleteSelectionRaw();
    --macroDepth_;
    emitMacro(ok, cmdDelete());
    return ok;
}

bool Document::cloneSelection(const Vec3& offset) {
    ++macroDepth_;
    const bool ok = cloneSelectionRaw(offset);
    --macroDepth_;
    emitMacro(ok, cmdClone(offset));
    return ok;
}

bool Document::translateSelection(const Vec3& delta, const std::string& name) {
    if (sel_.empty() || delta == Vec3{}) return false;
    std::vector<std::pair<SelRef, Vec3>> moves;
    for (const auto& r : sel_) moves.push_back({r, delta});
    ++macroDepth_;
    const bool ok = moveRefs(moves, name);
    --macroDepth_;
    emitMacro(ok, cmdMove(delta));
    return ok;
}

bool Document::moveRefs(const std::vector<std::pair<SelRef, Vec3>>& moves, const std::string& name) {
    Transaction t;
    t.name = name;
    t.selBefore = t.selAfter = sel_;
    std::set<Id> done;
    for (const auto& [r, delta] : moves) {
        if (delta == Vec3{}) continue;
        Entity* e = findEntity(r.entity);
        if (!e) continue;
        const Id item = mutatedItem(*e, r);
        if (item == kNoId || done.count(item)) continue;
        EntityItem* it = findItem(r.entity, item);
        if (!it) continue;
        Op op;
        op.type = Op::Type::ReplaceItem;
        op.entity = r.entity;
        op.item = item;
        op.before = *it;
        translateRef(r, delta);
        op.after = *findItem(r.entity, item);
        done.insert(item);
        // A primitive the move cannot change (unparsable planes, an empty patch) is not an edit: no history entry,
        // so translateSelection returns false when nothing moved (found by the parser fuzz tests).
        if (op.after && op.before && op.after->text() == op.before->text()) continue;
        t.ops.push_back(std::move(op));
    }
    if (t.ops.empty()) return false;
    push(std::move(t));
    return true;
}

bool Document::setKeyValueRaw(Id entity, const std::string& key, const std::string& value) {
    Entity* e = findEntity(entity);
    if (!e || key.empty()) return false;
    Transaction t;
    t.name = "Set \"" + key + "\"";
    t.selBefore = t.selAfter = sel_;
    for (auto& it : e->items) {
        if (KeyValueLine* kv = it.kv(); kv && kv->name() == key) {
            if (kv->value == value) return false;
            Op op;
            op.type = Op::Type::ReplaceItem;
            op.entity = entity;
            op.item = it.id;
            op.before = it;
            kv->set(value);
            bump(it);
            op.after = it;
            t.ops.push_back(std::move(op));
            push(std::move(t));
            return true;
        }
    }
    // New key: insert after the last key/value line (Radiant writes keys before primitives).
    size_t index = 0;
    for (size_t i = 0; i < e->items.size(); ++i)
        if (e->items[i].isKeyValue()) index = i + 1;
    EntityItem item;
    item.id = nextId_++;
    item.v = makeKeyValue(key, value, map_.eol, e->usesCanonicalKeys());
    bump(item);
    Op op;
    op.type = Op::Type::InsertItem;
    op.entity = entity;
    op.item = item.id;
    op.index = index;
    op.after = item;
    applyOp(op, true);
    t.ops.push_back(std::move(op));
    push(std::move(t));
    return true;
}

bool Document::removeKeyRaw(Id entity, const std::string& key) {
    Entity* e = findEntity(entity);
    if (!e || key == "classname") return false;
    for (size_t i = 0; i < e->items.size(); ++i) {
        if (const KeyValueLine* kv = e->items[i].kv(); kv && kv->name() == key) {
            Transaction t;
            t.name = "Remove \"" + key + "\"";
            t.selBefore = t.selAfter = sel_;
            Op op;
            op.type = Op::Type::RemoveItem;
            op.entity = entity;
            op.item = e->items[i].id;
            op.index = i;
            op.before = e->items[i];
            applyOp(op, true);
            t.ops.push_back(std::move(op));
            push(std::move(t));
            return true;
        }
    }
    return false;
}

bool Document::deleteSelectionRaw() {
    if (sel_.empty()) return false;
    Transaction t;
    t.name = "Delete";
    t.selBefore = sel_;
    // Group by entity.
    std::set<Id> wholeEntities;
    std::unordered_map<Id, std::set<Id>> prims;
    for (const auto& r : sel_) {
        if (r.item == kNoId) wholeEntities.insert(r.entity);
        else prims[r.entity].insert(r.item);
    }
    for (auto& [eid, items] : prims) {
        Entity* e = findEntity(eid);
        if (!e) continue;
        size_t primCount = 0;
        for (const auto& it : e->items) primCount += it.isPrimitive();
        if (!e->isWorldspawn() && primCount == items.size()) {
            wholeEntities.insert(eid);  // brush entity loses every brush: delete the entity
            continue;
        }
        for (Id item : items) {
            auto idx = itemIndex(*e, item);
            if (!idx) continue;
            Op op;
            op.type = Op::Type::RemoveItem;
            op.entity = eid;
            op.item = item;
            op.index = *idx;
            op.before = e->items[*idx];
            applyOp(op, true);
            t.ops.push_back(std::move(op));
        }
    }
    for (Id eid : wholeEntities) {
        auto idx = entityIndex(eid);
        if (!idx || map_.entities[*idx].isWorldspawn()) continue;
        Op op;
        op.type = Op::Type::RemoveEntity;
        op.entity = eid;
        op.index = *idx;
        op.entBefore = map_.entities[*idx];
        applyOp(op, true);
        t.ops.push_back(std::move(op));
    }
    if (t.ops.empty()) return false;
    sel_.clear();
    t.selAfter = sel_;
    push(std::move(t));
    return true;
}

bool Document::cloneSelectionRaw(const Vec3& offset) {
    if (sel_.empty()) return false;
    Transaction t;
    t.name = "Clone";
    t.selBefore = sel_;
    std::set<SelRef> newSel;
    std::set<Id> wholeEntities;
    std::vector<SelRef> prims;
    for (const auto& r : sel_) {
        const Entity* e = findEntity(r.entity);
        if (!e) continue;
        if (r.item == kNoId || !e->isWorldspawn()) wholeEntities.insert(r.entity);
        else prims.push_back(r);
    }
    for (const auto& r : prims) {
        Entity* e = findEntity(r.entity);
        auto idx = e ? itemIndex(*e, r.item) : std::nullopt;
        if (!idx) continue;
        EntityItem copy = e->items[*idx];
        copy.id = nextId_++;
        if (Primitive* p = copy.prim()) {
            p->id = copy.id;
            p->translate(offset);
        }
        bump(copy);
        Op op;
        op.type = Op::Type::InsertItem;
        op.entity = r.entity;
        op.item = copy.id;
        op.index = *idx + 1;
        op.after = copy;
        applyOp(op, true);
        newSel.insert({r.entity, copy.id});
        t.ops.push_back(std::move(op));
    }
    for (Id eid : wholeEntities) {
        auto idx = entityIndex(eid);
        if (!idx || map_.entities[*idx].isWorldspawn()) continue;
        Entity copy = map_.entities[*idx];
        copy.id = nextId_++;
        bool hasPrim = false;
        for (auto& it : copy.items) {
            it.id = nextId_++;
            if (Primitive* p = it.prim()) {
                p->id = it.id;
                p->translate(offset);
                hasPrim = true;
            }
            bump(it);
        }
        Op op;
        op.type = Op::Type::InsertEntity;
        op.entity = copy.id;
        op.index = map_.entities.size();
        op.entAfter = copy;
        applyOp(op, true);
        t.ops.push_back(std::move(op));
        if (!hasPrim) {
            sel_ = {SelRef{copy.id, kNoId}};
            translateRef({copy.id, kNoId}, offset);
            t.ops.back().entAfter = *findEntity(copy.id);
            newSel.insert({copy.id, kNoId});
        } else {
            for (const auto& it : copy.items)
                if (it.isPrimitive()) newSel.insert({copy.id, it.id});
        }
    }
    if (t.ops.empty()) return false;
    sel_ = newSel;
    t.selAfter = sel_;
    push(std::move(t));
    return true;
}

size_t Document::duplicateSelection(const std::vector<DuplicateTransform>& copies, const std::string& name) {
    if (sel_.empty() || copies.empty()) return 0;
    // Sources as cloneSelection: whole point and brush entities, and single worldspawn primitives.
    std::set<Id> wholeEntities;
    std::vector<SelRef> prims;
    for (const auto& r : sel_) {
        const Entity* e = findEntity(r.entity);
        if (!e) continue;
        if (r.item == kNoId || !e->isWorldspawn()) wholeEntities.insert(r.entity);
        else prims.push_back(r);
    }
    // Phase 1 builds every copy without touching the map, so a failing callback leaves the document unchanged.
    struct NewItem {
        Id entity;
        EntityItem item;
    };
    std::vector<NewItem> newItems;
    std::vector<Entity> newEntities;
    for (const auto& xf : copies) {
        for (const auto& r : prims) {
            const Entity* e = findEntity(r.entity);
            auto idx = e ? itemIndex(*e, r.item) : std::nullopt;
            if (!idx) continue;
            EntityItem copy = e->items[*idx];
            copy.id = nextId_++;
            if (Primitive* p = copy.prim()) {
                p->id = copy.id;
                if (xf.prim && !xf.prim(*p)) return 0;
            }
            bump(copy);
            newItems.push_back({r.entity, copy});
        }
        for (Id eid : wholeEntities) {
            auto idx = entityIndex(eid);
            if (!idx || map_.entities[*idx].isWorldspawn()) continue;
            Entity copy = map_.entities[*idx];
            copy.id = nextId_++;
            bool hasPrim = false;
            for (const auto& it : copy.items) hasPrim |= it.isPrimitive();
            // Point entities only: the entity callback sees the keys before the copy's item ids are assigned.
            if (!hasPrim && xf.ent && !xf.ent(copy)) return 0;
            for (auto& it : copy.items) {
                it.id = nextId_++;
                if (Primitive* p = it.prim()) {
                    p->id = it.id;
                    if (xf.prim && !xf.prim(*p)) return 0;
                }
                bump(it);
            }
            newEntities.push_back(std::move(copy));
        }
    }
    Transaction t;
    t.name = name;
    t.selBefore = sel_;
    std::set<SelRef> newSel;
    for (auto& ni : newItems) {
        Entity* e = findEntity(ni.entity);
        if (!e) continue;
        Op op;
        op.type = Op::Type::InsertItem;
        op.entity = ni.entity;
        op.item = ni.item.id;
        op.index = e->items.size();
        op.after = ni.item;
        applyOp(op, true);
        newSel.insert({ni.entity, ni.item.id});
        t.ops.push_back(std::move(op));
    }
    for (const auto& ne : newEntities) {
        Op op;
        op.type = Op::Type::InsertEntity;
        op.entity = ne.id;
        op.index = map_.entities.size();
        op.entAfter = ne;
        applyOp(op, true);
        bool hasPrim = false;
        for (const auto& it : ne.items)
            if (it.isPrimitive()) {
                hasPrim = true;
                newSel.insert({ne.id, it.id});
            }
        if (!hasPrim) newSel.insert({ne.id, kNoId});
        t.ops.push_back(std::move(op));
    }
    if (t.ops.empty()) return 0;
    sel_ = newSel;
    t.selAfter = sel_;
    push(std::move(t));
    return copies.size();
}

bool Document::editPrimitives(const std::vector<PrimEdit>& edits, const std::string& name, bool selectResults) {
    if (edits.empty()) return false;
    Transaction t;
    t.name = name;
    t.selBefore = sel_;
    std::set<SelRef> newSel;
    for (const auto& e : edits) {
        Entity* ent = findEntity(e.entity);
        if (!ent) continue;
        if (e.item == kNoId) {
            if (!e.prim) continue;
            EntityItem item;
            item.id = nextId_++;
            item.v = *e.prim;
            item.prim()->id = item.id;
            bump(item);
            Op op;
            op.type = Op::Type::InsertItem;
            op.entity = e.entity;
            op.item = item.id;
            op.index = ent->items.size();
            op.after = item;
            applyOp(op, true);
            newSel.insert({e.entity, item.id});
            t.ops.push_back(std::move(op));
            continue;
        }
        auto idx = itemIndex(*ent, e.item);
        if (!idx) continue;
        if (!e.prim) {
            Op op;
            op.type = Op::Type::RemoveItem;
            op.entity = e.entity;
            op.item = e.item;
            op.index = *idx;
            op.before = ent->items[*idx];
            applyOp(op, true);
            t.ops.push_back(std::move(op));
            continue;
        }
        Op op;
        op.type = Op::Type::ReplaceItem;
        op.entity = e.entity;
        op.item = e.item;
        op.before = ent->items[*idx];
        EntityItem replaced = ent->items[*idx];
        replaced.v = *e.prim;
        replaced.prim()->id = replaced.id;
        replaced.prim()->dirty = true;
        bump(replaced);
        op.after = replaced;
        applyOp(op, true);
        newSel.insert({e.entity, e.item});
        t.ops.push_back(std::move(op));
    }
    if (t.ops.empty()) return false;
    if (selectResults) sel_ = newSel;
    t.selAfter = sel_;
    push(std::move(t));
    return true;
}

Id Document::addEntity(const std::vector<std::pair<std::string, std::string>>& keyValues, std::vector<Primitive> prims,
                       const std::string& name) {
    Entity ent;
    ent.id = nextId_++;
    ent.prefix = "// entity " + std::to_string(map_.entities.size()) + map_.eol;
    ent.open = {"{", map_.eol};
    ent.close = {"}", map_.eol};
    for (const auto& [k, v] : keyValues) {
        EntityItem it;
        it.id = nextId_++;
        it.v = makeKeyValue(k, v, map_.eol);
        bump(it);
        ent.items.push_back(std::move(it));
    }
    std::set<SelRef> newSel;
    for (auto& p : prims) {
        EntityItem it;
        it.id = nextId_++;
        p.id = it.id;
        it.v = std::move(p);
        bump(it);
        newSel.insert({ent.id, it.id});
        ent.items.push_back(std::move(it));
    }
    if (newSel.empty()) newSel.insert({ent.id, kNoId});
    Transaction t;
    t.name = name;
    t.selBefore = sel_;
    Op op;
    op.type = Op::Type::InsertEntity;
    op.entity = ent.id;
    op.index = map_.entities.size();
    op.entAfter = ent;
    applyOp(op, true);
    t.ops.push_back(std::move(op));
    sel_ = newSel;
    t.selAfter = sel_;
    push(std::move(t));
    return ent.id;
}

Id Document::groupSelection(const std::string& classname) {
    Entity* world = worldspawn();
    if (!world || classname.empty()) return kNoId;
    std::vector<Primitive> prims;
    std::vector<std::pair<size_t, Id>> removed;  // (index, item) in worldspawn
    for (const auto& r : sel_)
        if (r.entity == world->id && r.item != kNoId)
            if (auto idx = itemIndex(*world, r.item)) removed.push_back({*idx, r.item});
    if (removed.empty()) return kNoId;
    std::sort(removed.begin(), removed.end());
    Transaction t;
    t.name = "Make " + classname;
    t.selBefore = sel_;
    for (auto it = removed.rbegin(); it != removed.rend(); ++it) {
        Op op;
        op.type = Op::Type::RemoveItem;
        op.entity = world->id;
        op.item = it->second;
        op.index = it->first;
        op.before = world->items[it->first];
        if (const Primitive* p = op.before->prim()) prims.insert(prims.begin(), *p);
        applyOp(op, true);
        t.ops.push_back(std::move(op));
    }
    Entity ent;
    ent.id = nextId_++;
    ent.prefix = "// entity " + std::to_string(map_.entities.size()) + map_.eol;
    ent.open = {"{", map_.eol};
    ent.close = {"}", map_.eol};
    EntityItem cls;
    cls.id = nextId_++;
    cls.v = makeKeyValue("classname", classname, map_.eol);
    bump(cls);
    ent.items.push_back(std::move(cls));
    std::set<SelRef> newSel;
    for (auto& p : prims) {
        EntityItem it;
        it.id = nextId_++;
        p.id = it.id;
        it.v = std::move(p);
        bump(it);
        newSel.insert({ent.id, it.id});
        ent.items.push_back(std::move(it));
    }
    Op op;
    op.type = Op::Type::InsertEntity;
    op.entity = ent.id;
    op.index = map_.entities.size();
    op.entAfter = ent;
    applyOp(op, true);
    t.ops.push_back(std::move(op));
    sel_ = newSel;
    t.selAfter = sel_;
    push(std::move(t));
    return ent.id;
}

bool Document::ungroupSelection() {
    Entity* world = worldspawn();
    if (!world) return false;
    std::set<Id> ents;
    for (const auto& r : sel_)
        if (r.entity != world->id) ents.insert(r.entity);
    Transaction t;
    t.name = "Ungroup";
    t.selBefore = sel_;
    std::set<SelRef> newSel;
    for (Id eid : ents) {
        world = worldspawn();  // entity removals below may move entities in memory
        auto idx = entityIndex(eid);
        if (!idx || !world) continue;
        Entity copy = map_.entities[*idx];
        bool hasPrim = false;
        for (const auto& it : copy.items) hasPrim |= it.isPrimitive();
        if (!hasPrim) continue;
        for (const auto& it : copy.items) {
            const Primitive* p = it.prim();
            if (!p) continue;
            EntityItem moved;
            moved.id = nextId_++;
            moved.v = *p;
            moved.prim()->id = moved.id;
            bump(moved);
            Op op;
            op.type = Op::Type::InsertItem;
            op.entity = world->id;
            op.item = moved.id;
            op.index = world->items.size();
            op.after = moved;
            applyOp(op, true);
            newSel.insert({world->id, moved.id});
            t.ops.push_back(std::move(op));
        }
        Op rm;
        rm.type = Op::Type::RemoveEntity;
        rm.entity = eid;
        rm.index = *entityIndex(eid);
        rm.entBefore = copy;
        applyOp(rm, true);
        t.ops.push_back(std::move(rm));
    }
    if (t.ops.empty()) return false;
    sel_ = newSel;
    t.selAfter = sel_;
    push(std::move(t));
    return true;
}

void Document::invertSelection() {
    std::set<SelRef> inv;
    for (const auto& e : map_.entities) {
        bool hasPrim = false;
        for (const auto& it : e.items)
            if (it.isPrimitive()) {
                hasPrim = true;
                if (!sel_.count({e.id, it.id})) inv.insert({e.id, it.id});
            }
        if (!hasPrim && !e.isWorldspawn() && !sel_.count({e.id, kNoId})) inv.insert({e.id, kNoId});
    }
    sel_ = std::move(inv);
    touch();
}

// ------------------------------------------------------------------ drag

void Document::beginDrag() {
    drag_ = std::make_unique<DragState>();
    std::set<Id> done;
    for (const auto& r : sel_) {
        Entity* e = findEntity(r.entity);
        if (!e) continue;
        const Id item = mutatedItem(*e, r);
        if (item == kNoId || done.count(item)) continue;
        if (EntityItem* it = findItem(r.entity, item)) {
            drag_->before.push_back({r, *it});
            done.insert(item);
        }
    }
}

void Document::updateDrag(const Vec3& totalDelta) {
    if (!drag_ || drag_->delta == totalDelta) return;
    for (auto& [r, snap] : drag_->before) {
        if (EntityItem* it = findItem(r.entity, snap.id)) *it = snap;
        translateRef(r, totalDelta);
    }
    drag_->delta = totalDelta;
    touch();
}

bool Document::endDrag(bool commit) {
    if (!drag_) return false;
    std::unique_ptr<DragState> d = std::move(drag_);
    if (!commit || d->delta == Vec3{}) {
        for (auto& [r, snap] : d->before)
            if (EntityItem* it = findItem(r.entity, snap.id)) *it = snap;
        touch();
        return false;
    }
    Transaction t;
    t.name = "Move";
    t.selBefore = t.selAfter = sel_;
    for (auto& [r, snap] : d->before) {
        const EntityItem* it = findItem(r.entity, snap.id);
        if (!it) continue;
        Op op;
        op.type = Op::Type::ReplaceItem;
        op.entity = r.entity;
        op.item = snap.id;
        op.before = snap;
        op.after = *it;
        t.ops.push_back(std::move(op));
    }
    push(std::move(t));
    return true;
}

// ------------------------------------------------------------------ diff

namespace {

bool sameDelta(const Vec3& a, const Vec3& b) {
    return std::fabs(a.x - b.x) < 1e-6 && std::fabs(a.y - b.y) < 1e-6 && std::fabs(a.z - b.z) < 1e-6;
}

void classifyPair(const EntityItem& a, const EntityItem& b, MapDiff::Change& c) {
    const Primitive* pa = a.prim();
    const Primitive* pb = b.prim();
    if (pa && pb && pa->kind == PrimKind::Brush && pb->kind == PrimKind::Brush) {
        if (pa->faces.size() != pb->faces.size() || pa->lines.size() != pb->lines.size()) {
            c.kind = "brush-changed";
            c.detail = "face count " + std::to_string(pa->faces.size()) + " -> " + std::to_string(pb->faces.size());
            return;
        }
        std::optional<Vec3> delta;
        for (size_t f = 0; f < pa->faces.size(); ++f) {
            const auto& fa = pa->faces[f];
            const auto& fb = pb->faces[f];
            if (fa.suffix != fb.suffix) {
                c.kind = "brush-changed";
                c.detail = "face " + std::to_string(f) + " material/texture fields: '" + fa.material() + "' -> '" + fb.material() + "'";
                return;
            }
            for (int k = 0; k < 3; ++k) {
                const Vec3 d = fb.p[k] - fa.p[k];
                if (!delta) delta = d;
                else if (!sameDelta(*delta, d)) {
                    c.kind = "brush-changed";
                    c.detail = "face " + std::to_string(f) + " points changed non-uniformly";
                    return;
                }
            }
        }
        // Non-face lines must be identical.
        std::set<size_t> faceLines;
        for (const auto& f : pa->faces) faceLines.insert(f.line);
        for (size_t l = 0; l < pa->lines.size(); ++l)
            if (!faceLines.count(l) && pa->lines[l].text + pa->lines[l].eol != pb->lines[l].text + pb->lines[l].eol) {
                c.kind = "brush-changed";
                c.detail = "non-face line " + std::to_string(l) + " changed";
                return;
            }
        c.kind = "brush-moved";
        c.detail = delta ? (formatNumber(delta->x) + " " + formatNumber(delta->y) + " " + formatNumber(delta->z)) : "0 0 0";
        return;
    }
    if (pa && pb && pa->kind == PrimKind::Patch && pb->kind == PrimKind::Patch) {
        if (pa->verts.size() != pb->verts.size() || pa->lines.size() != pb->lines.size()) {
            c.kind = "patch-changed";
            return;
        }
        std::optional<Vec3> delta;
        for (size_t v = 0; v < pa->verts.size(); ++v) {
            const Vec3 d = pb->verts[v].pos - pa->verts[v].pos;
            if (!delta) delta = d;
            else if (!sameDelta(*delta, d)) { c.kind = "patch-changed"; c.detail = "non-uniform"; return; }
        }
        // All non-coordinate tokens must match.
        for (size_t l = 0; l < pa->lines.size(); ++l) {
            auto ta = splitTokens(pa->lines[l].text), tb = splitTokens(pb->lines[l].text);
            if (ta.size() != tb.size()) { c.kind = "patch-changed"; return; }
            std::set<size_t> coordTok;
            for (const auto& pv : pa->verts)
                if (pv.line == l) for (size_t k = 1; k <= 3; ++k) coordTok.insert(pv.token + k);
            for (size_t k = 0; k < ta.size(); ++k)
                if (!coordTok.count(k) && ta[k] != tb[k]) { c.kind = "patch-changed"; c.detail = "token " + ta[k] + " -> " + tb[k]; return; }
        }
        c.kind = "patch-moved";
        c.detail = delta ? (formatNumber(delta->x) + " " + formatNumber(delta->y) + " " + formatNumber(delta->z)) : "0 0 0";
        return;
    }
    const KeyValueLine* ka = a.kv();
    const KeyValueLine* kb = b.kv();
    if (ka && kb && ka->key == kb->key) {
        c.kind = "kv-changed";
        c.detail = "\"" + ka->name() + "\": \"" + ka->value + "\" -> \"" + kb->value + "\"";
        return;
    }
    c.kind = "other";
}

std::string itemLabel(const EntityItem& it) {
    if (const auto* kv = it.kv()) return "\"" + kv->name() + "\" \"" + kv->value + "\"";
    if (const auto* p = it.prim()) return p->kind == PrimKind::Brush ? "brush" : (p->kind == PrimKind::Patch ? "patch" : "primitive");
    return "line";
}

}  // namespace

MapDiff diffMaps(const MapFile& a, const MapFile& b) {
    MapDiff d;
    if (a.header != b.header) d.changes.push_back({0, 0, "other", "header changed"});
    if (a.trailer != b.trailer) d.changes.push_back({0, 0, "other", "trailer changed"});
    if (a.entities.size() != b.entities.size()) {
        d.structureEqual = false;
        d.changes.push_back({0, 0, "other", "entity count " + std::to_string(a.entities.size()) + " -> " + std::to_string(b.entities.size())});
        return d;
    }
    for (size_t e = 0; e < a.entities.size(); ++e) {
        const auto& ea = a.entities[e];
        const auto& eb = b.entities[e];
        if (ea.prefix != eb.prefix || ea.open.text != eb.open.text || ea.close.text != eb.close.text)
            d.changes.push_back({e, 0, "other", "entity framing changed"});
        size_t i = 0, j = 0;
        if (ea.items.size() != eb.items.size()) d.structureEqual = false;
        constexpr size_t kLook = 64;
        while (i < ea.items.size() || j < eb.items.size()) {
            if (i < ea.items.size() && j < eb.items.size()) {
                const std::string ta = ea.items[i].text(), tb = eb.items[j].text();
                if (ta == tb) { ++d.identicalItems; ++i; ++j; continue; }
                if (ea.items.size() != eb.items.size()) {
                    // Look for re-synchronisation to detect insertions/removals.
                    size_t k = 1;
                    for (; k < kLook && j + k < eb.items.size(); ++k)
                        if (eb.items[j + k].text() == ta) break;
                    if (j + k < eb.items.size() && k < kLook) {
                        for (size_t n = 0; n < k; ++n) d.changes.push_back({e, j + n, eb.items[j + n].isKeyValue() ? "kv-added" : "item-added", itemLabel(eb.items[j + n])});
                        j += k;
                        continue;
                    }
                    for (k = 1; k < kLook && i + k < ea.items.size(); ++k)
                        if (ea.items[i + k].text() == tb) break;
                    if (i + k < ea.items.size() && k < kLook) {
                        for (size_t n = 0; n < k; ++n) d.changes.push_back({e, i + n, ea.items[i + n].isKeyValue() ? "kv-removed" : "item-removed", itemLabel(ea.items[i + n])});
                        i += k;
                        continue;
                    }
                }
                MapDiff::Change c{e, i, "other", ""};
                classifyPair(ea.items[i], eb.items[j], c);
                d.changes.push_back(c);
                ++i; ++j;
            } else if (i < ea.items.size()) {
                d.changes.push_back({e, i, ea.items[i].isKeyValue() ? "kv-removed" : "item-removed", itemLabel(ea.items[i])});
                ++i;
            } else {
                d.changes.push_back({e, j, eb.items[j].isKeyValue() ? "kv-added" : "item-added", itemLabel(eb.items[j])});
                ++j;
            }
        }
    }
    return d;
}

}  // namespace iw8r
