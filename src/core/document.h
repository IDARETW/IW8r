// IW8 Radiant Suite - editable document: map model + selection + grid + undo/redo.
//
// Every mutation goes through a Transaction made of snapshot operations. Undo
// restores the exact prior object (including its original text), so
// "edit, undo, save" reproduces the original file byte for byte.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "map_text.h"

namespace iw8r {

// Reference to something selectable: a primitive (entity + item) or a whole point entity (item = 0).
struct SelRef {
    Id entity = kNoId;
    Id item = kNoId;
    bool operator<(const SelRef& o) const { return entity != o.entity ? entity < o.entity : item < o.item; }
    bool operator==(const SelRef& o) const { return entity == o.entity && item == o.item; }
};

// One copy made by Document::duplicateSelection: `prim` transforms each copied primitive, `ent` transforms a copied
// point entity (origin, angles). A callback returning false aborts the whole duplicate (nothing changes).
struct DuplicateTransform {
    std::function<bool(Primitive&)> prim;
    std::function<bool(Entity&)> ent;
};

// A single brush face (Radiant Ctrl+Shift+click face selection). `face` indexes Primitive::faces.
struct FaceRef {
    Id entity = kNoId;
    Id item = kNoId;
    uint32_t face = 0;
    bool operator<(const FaceRef& o) const {
        if (entity != o.entity) return entity < o.entity;
        return item != o.item ? item < o.item : face < o.face;
    }
    bool operator==(const FaceRef& o) const { return entity == o.entity && item == o.item && face == o.face; }
};

struct Op {
    enum class Type { ReplaceItem, InsertItem, RemoveItem, InsertEntity, RemoveEntity, ReplaceHeader };
    Type type = Type::ReplaceItem;
    Id entity = kNoId;
    Id item = kNoId;
    size_t index = 0;  // position for insert/remove
    std::optional<EntityItem> before, after;
    std::optional<Entity> entBefore, entAfter;
    std::optional<std::string> headerBefore, headerAfter;  // ReplaceHeader (layer declarations)
};

struct Transaction {
    std::string name;
    std::vector<Op> ops;
    std::set<SelRef> selBefore, selAfter;
};

class Document {
public:
    // ---- file ----
    bool load(const std::string& path, std::string* err = nullptr);
    bool loadFromText(const std::string& text, const std::string& virtualPath, std::string* err = nullptr);
    bool save(std::string* err = nullptr);                                  // to current path
    bool saveAs(const std::string& path, std::string* err = nullptr);       // switch to new path
    bool saveCopy(const std::string& path, std::string* err = nullptr) const;  // keep current path
    std::string text() const { return map_.text(); }
    const std::string& path() const { return path_; }
    bool modified() const { return undoPos_ != savedPos_ || savedPosLost_; }

    const MapFile& map() const { return map_; }
    uint64_t revision() const { return revision_; }  // bumps on every change (render caches)
    uint64_t loadSerial() const { return loadSerial_; }  // bumps on every load (another map, even at the same path)

    // ---- lookup ----
    Entity* findEntity(Id id);
    const Entity* findEntity(Id id) const;
    EntityItem* findItem(Id entity, Id item);
    const EntityItem* findItem(Id entity, Id item) const;
    Entity* worldspawn();

    // ---- grid ----
    double grid() const { return grid_; }
    void setGrid(double g);
    void gridUp();
    void gridDown();

    // ---- selection ----
    const std::set<SelRef>& selection() const { return sel_; }
    void select(const SelRef& r, bool additive);
    void toggle(const SelRef& r);
    void deselectAll();
    bool isSelected(const SelRef& r) const { return sel_.count(r) != 0; }
    // Expands a click on a primitive to the unit Radiant selects (whole entity for brush entities).
    std::vector<SelRef> selectionUnit(const SelRef& r) const;
    AABB selectionBounds() const;
    // The entity shown in the property inspector (first selected, else worldspawn).
    Id inspectedEntity() const;

    // ---- face selection (Radiant Ctrl+Shift+click in the camera) ----
    // Exclusive with the primitive/entity selection: selecting faces clears it and selecting primitives clears
    // the faces. Like Radiant, face selection is not part of undo; faces whose brush disappears are dropped.
    const std::set<FaceRef>& faceSelection() const { return faceSel_; }
    bool isFaceSelected(const FaceRef& f) const { return faceSel_.count(f) != 0; }
    void toggleFace(const FaceRef& f);
    void selectFace(const FaceRef& f, bool additive);
    void clearFaceSelection();

    // ---- edits (each pushes one undoable transaction; return false when nothing changed) ----
    bool translateSelection(const Vec3& delta, const std::string& name = "Move");
    // Moves each ref by its own delta (align, distribute): one undo step, the selection is unchanged. A brush
    // entity's ref moves that brush; a point entity's ref moves its origin. Returns false when nothing moved.
    bool moveRefs(const std::vector<std::pair<SelRef, Vec3>>& moves, const std::string& name = "Move");
    // Radiant texture lock (Shift+T): moves keep each brush face's texture fixed to the brush.
    bool textureLock = false;
    bool setKeyValue(Id entity, const std::string& key, const std::string& value);
    bool removeKey(Id entity, const std::string& key);
    bool deleteSelection();
    bool cloneSelection(const Vec3& offset);  // Radiant Space: duplicate, offset, select the copies
    // Macro recording (9.10): when set, each successful move / clone / delete of the selection and each key set or
    // removed on a selected entity reports the equivalent command-script line (command_script.h). Nested calls
    // report only the outermost edit. The UI clears it while a script runs so replays are not recorded again.
    std::function<void(const std::string& line)> macroSink;
    // Layout duplicates (array, radial, mirror): one copy set per entry of `copies`, all in ONE undo step, and every
    // copy becomes the selection. Copies follow cloneSelection's rules (whole point and brush entities, single
    // worldspawn primitives). Returns copies.size(), or 0 when nothing changed. Used instead of cloneSelection +
    // translate because a grouped sequence keeps only its last selection on redo.
    size_t duplicateSelection(const std::vector<DuplicateTransform>& copies, const std::string& name = "Duplicate");
    void invertSelection();                   // Radiant I

    // One undoable step that replaces, deletes or inserts primitives (brush tools: create, clip, CSG,
    // hollow, resize, rotate). item == kNoId inserts `prim` at the end of `entity`; prim == nullopt deletes.
    // The resulting/new primitives become the selection. Returns false when nothing changed.
    struct PrimEdit {
        Id entity = kNoId;
        Id item = kNoId;
        std::optional<Primitive> prim;
    };
    // selectResults = false keeps the current selection (face-level edits such as per-face materials).
    bool editPrimitives(const std::vector<PrimEdit>& edits, const std::string& name, bool selectResults = true);
    // New entity (point entity when prims is empty), inserted after the last entity and selected.
    Id addEntity(const std::vector<std::pair<std::string, std::string>>& keyValues, std::vector<Primitive> prims,
                 const std::string& name = "Create Entity");
    // Move the selected worldspawn brushes into a new brush entity of `classname` (Radiant "make entity").
    Id groupSelection(const std::string& classname);
    // Move the selected brush entities' primitives back to worldspawn and remove the entities.
    bool ungroupSelection();
    // Replaces the text before the first entity (the `iwmap 4` line and layer declarations). One undo step.
    bool setHeader(const std::string& header, const std::string& name);
    // Primitive text helpers for tools: the document's line ending.
    const std::string& eol() const { return map_.eol; }

    // Interactive drag: preview without history, then commit as one transaction.
    void beginDrag();
    void updateDrag(const Vec3& totalDelta);
    bool endDrag(bool commit);
    bool dragging() const { return drag_ != nullptr; }

    // ---- undo ----
    bool canUndo() const { return undoPos_ > 0; }
    bool canRedo() const { return undoPos_ < history_.size(); }
    bool undo();
    bool redo();
    const std::vector<Transaction>& history() const { return history_; }
    // Compound edit: every transaction pushed between beginGroup and the matching endGroup becomes ONE
    // undo step called `name` (prefab explode, find & replace, multi-stage tools). Groups nest; the
    // outermost name wins. An empty group leaves the history unchanged.
    void beginGroup(const std::string& name);
    void endGroup();
    bool inGroup() const { return groupDepth_ > 0; }
    size_t undoPosition() const { return undoPos_; }
    size_t undoLimit = 1024;

private:
    bool setKeyValueRaw(Id entity, const std::string& key, const std::string& value);
    bool removeKeyRaw(Id entity, const std::string& key);
    bool deleteSelectionRaw();
    bool cloneSelectionRaw(const Vec3& offset);
    bool entityInSelection(Id entity) const;
    void emitMacro(bool ok, const std::string& line);
    int macroDepth_ = 0;
    void push(Transaction&& t);
    void applyOp(const Op& op, bool forward);
    void touch() { ++revision_; }
    std::optional<size_t> itemIndex(const Entity& e, Id item) const;
    std::optional<size_t> entityIndex(Id id) const;
    void translateRef(const SelRef& r, const Vec3& delta);

    MapFile map_;
    std::string path_;
    Id nextId_ = 1;
    uint64_t itemRev_ = 1;
    void bump(EntityItem& it) { it.rev = ++itemRev_; }
    uint64_t revision_ = 1;
    uint64_t loadSerial_ = 0;
    double grid_ = 8;
    std::set<SelRef> sel_;
    std::set<FaceRef> faceSel_;
    void pruneFaceSelection();  // drop faces whose primitive or face index no longer exists
    std::vector<Transaction> history_;
    size_t undoPos_ = 0, savedPos_ = 0;
    bool savedPosLost_ = false;
    int groupDepth_ = 0;
    std::string groupName_;
    std::optional<size_t> groupIndex_;  // history index of the open group's transaction

    struct DragState {
        std::vector<std::pair<SelRef, EntityItem>> before;  // snapshot of affected items
        Vec3 delta;
    };
    std::unique_ptr<DragState> drag_;
};

// Structured comparison of two maps parsed from text (used by tests and `iw8r verify`).
struct MapDiff {
    struct Change {
        size_t entity = 0;  // entity index
        size_t item = 0;    // item index within entity
        std::string kind;   // "brush-moved", "patch-moved", "brush-changed", "kv-changed", "kv-added", "kv-removed", "other"
        std::string detail;
    };
    bool structureEqual = true;  // same entity/item counts
    std::vector<Change> changes;
    size_t identicalItems = 0;
};
MapDiff diffMaps(const MapFile& a, const MapFile& b);

}  // namespace iw8r
