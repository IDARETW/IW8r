// IW8 Radiant Suite - lossless reader/writer for Radiant "iwmap 4" .map source files.
//
// Design rule: every byte of the input is owned by exactly one piece of the model
// (file header, entity prefix/open/close text, item lines, primitive lines, trailer).
// Serializing an unmodified model therefore reproduces the input byte for byte, and
// editing one primitive rewrites only that primitive's changed lines. Fields the
// editor does not understand (layers, toolFlags, contents, smoothing, texture and
// lightmap parameters, vertex colours, ...) are never re-encoded.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "geometry.h"

namespace iw8r {

using Id = uint32_t;
constexpr Id kNoId = 0;

// One source line: text without terminator plus its exact terminator ("\r\n", "\n" or "").
struct TextLine {
    std::string text;
    std::string eol;
};

struct BrushFace {
    size_t line = 0;      // index into Primitive::lines
    std::string indent;   // whitespace before the first '('
    Vec3 p[3];            // the three plane points
    std::string suffix;   // everything after the third ')' (material, texture and lightmap params)
    std::string material() const;  // first token of suffix
};

struct PatchVertex {
    size_t line = 0;  // index into Primitive::lines
    size_t token = 0; // index of the "v" token within that line
    int row = 0;      // row block index inside the patch
    int col = 0;      // position within the row
    Vec3 pos;
};

enum class PrimKind { Brush, Patch, Unknown };

struct Primitive {
    Id id = kNoId;
    PrimKind kind = PrimKind::Unknown;
    std::vector<TextLine> lines;  // full text: leading comments, braces, body
    bool dirty = false;           // true once any line was regenerated

    // Parsed views (indices refer to `lines`).
    std::vector<BrushFace> faces;    // Brush
    std::string patchKeyword;        // Patch: "curve", "mesh", "terrain", ...
    std::string patchMaterial;       // Patch: first material line
    std::vector<PatchVertex> verts;  // Patch control points

    std::string text() const;
    void translate(const Vec3& delta);
    // Replaces the material of one face (faceIndex >= 0) or of every face / the patch (faceIndex < 0),
    // keeping all other suffix fields byte-exact. Returns true when something changed.
    bool setMaterial(const std::string& material, int faceIndex = -1);
    // Texture fields of a brush face (the five numbers after the material: texture width, height,
    // x offset, y offset, rotation). Unset optionals are left as written. faceIndex < 0 = every face.
    struct TexFields {
        std::optional<double> width, height, xoff, yoff, rot;
    };
    bool setTextureFields(const TexFields& f, int faceIndex = -1);
    // Reads them back (nullopt when the face has fewer fields than expected).
    std::optional<std::array<double, 5>> textureFields(size_t faceIndex) const;
    std::vector<Plane> planes() const;  // Brush only; degenerate faces skipped
    AABB bounds() const;
};

// How a key is spelled on its line. Radiant source maps use quoted names. IW8 runtime entity
// strings (MapEnts::entityString in Replay 1.20) use canonical script-string ids, e.g.
// `212 "worldspawn"` (212 = classname), and `0 name "value"` for non-canonical keys.
enum class KeyForm { Quoted, Canonical, ScriptString };

struct KeyValueLine {
    std::string key;       // as written: name (Quoted/ScriptString) or decimal id (Canonical)
    KeyForm form = KeyForm::Quoted;
    uint32_t keyId = 0;    // Canonical only
    std::string value;
    std::string prefix;    // line text before the value's opening quote (kept verbatim on edit)
    TextLine line;         // original or regenerated text
    bool dirty = false;
    std::string name() const;  // resolved key name (canonical ids via the IW8 key table)
    void set(const std::string& newValue);
};

// IW8 canonical key table (id <-> name). A few ids are built in with evidence; the full table is
// loaded from a TSV generated on the mapper's machine (tools/gen_iw8_keytable.py).
class KeyTable {
public:
    static KeyTable& global();
    bool loadTsv(const std::string& path, std::string* err = nullptr);
    const std::string* name(uint32_t id) const;
    std::optional<uint32_t> id(const std::string& name) const;
    size_t size() const;

private:
    KeyTable();
    struct Impl;
    Impl* impl_;
};

struct OpaqueLine {
    TextLine line;
};

struct EntityItem {
    Id id = kNoId;
    uint64_t rev = 0;  // bumped by the document on every mutation (render caches key on id + rev)
    std::variant<KeyValueLine, Primitive, OpaqueLine> v;

    bool isKeyValue() const { return std::holds_alternative<KeyValueLine>(v); }
    bool isPrimitive() const { return std::holds_alternative<Primitive>(v); }
    KeyValueLine* kv() { return std::get_if<KeyValueLine>(&v); }
    const KeyValueLine* kv() const { return std::get_if<KeyValueLine>(&v); }
    Primitive* prim() { return std::get_if<Primitive>(&v); }
    const Primitive* prim() const { return std::get_if<Primitive>(&v); }
    std::string text() const;
};

struct Entity {
    Id id = kNoId;
    std::string prefix;     // depth-0 text owned by this entity (e.g. "// entity 3\r\n")
    TextLine open;          // the "{" line
    std::vector<EntityItem> items;
    TextLine close;         // the "}" line

    const std::string* value(const std::string& key) const;  // by resolved key name
    bool usesCanonicalKeys() const;                           // IW8 runtime entity-string spelling
    std::string classname() const;
    std::optional<Vec3> origin() const;
    bool isWorldspawn() const { return classname() == "worldspawn"; }
    std::string text() const;
};

struct MapFile {
    std::string header;   // "iwmap 4" + layer lines (+ anything before the first entity)
    std::vector<Entity> entities;
    std::string trailer;  // anything after the last entity
    std::string eol = "\r\n";  // dominant line terminator, used for new lines

    std::string text() const;
};

struct ParseError {
    size_t line = 0;  // 1-based
    std::string message;
};

struct ParseResult {
    std::optional<MapFile> map;
    std::optional<ParseError> error;
};

// Parses map text. IDs are assigned from `nextId` (incremented) for entities and items.
ParseResult parseMap(const std::string& text, Id& nextId);

// Re-parses a primitive's lines (after its lines were replaced) to refresh parsed views.
void reparsePrimitive(Primitive& prim);

// Whitespace-separated tokens of a line.
std::vector<std::string> splitTokens(const std::string& line);

// Number formatting used for regenerated coordinates (shortest round-trip, no exponent).
std::string formatNumber(double v);
// Shortest text that round-trips a float32 (IW8 runtime vectors are float32).
std::string formatFloat32(float v);

// Builds a key/value line: "key" "value", or `<id> "value"` when `canonical` and the key table knows the name.
KeyValueLine makeKeyValue(const std::string& key, const std::string& value, const std::string& eol, bool canonical = false);

bool readFile(const std::string& path, std::string& out);
// Writes via a temporary file and an atomic replace. Returns false and sets `err` on failure.
bool writeFileAtomic(const std::string& path, const std::string& data, std::string* err = nullptr);

}  // namespace iw8r
