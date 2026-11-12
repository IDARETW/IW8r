// IW8 Radiant Suite - technique set body (ACTS export) and plain-English notes from a technique set name.
// Research 69 documents the export. Fields kept here are the ones that were read from real stock sets:
//   fields.name                       -> TechsetDetail::name
//   fields.materialType.name          -> TechsetDetail::materialType
//   fields.maskedTechniques.values[k].values[0]  -> one TechniqueDetail per slot (bit k of techniqueMask)
//     name.string                     -> TechniqueDetail::name
//     vertex/hull/domain/pixelShader.name -> TechniqueDetail::shaders (non-empty names only, hashes as exported)
//     args.values.size()              -> TechniqueDetail::argumentCount
//     stateBits, defaultStateBits, pipelineStateCount, colorRtCount, precompiledIndex -> TechniqueDetail::stateSummary
// Parsing is tolerant: a missing field gives an empty value and a note, never a crash. Malformed JSON or a non-object
// root returns false with an error.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace iw8r {

struct TechniqueDetail {
    std::string name;                   // "TECHNIQUE_LIT_FORWARDPLUS_BITMASK_<hash>" as exported
    std::string typeName;               // "TECHNIQUE_LIT_FORWARDPLUS_BITMASK" (enum name, may be empty)
    std::vector<std::string> shaders;   // stage shader names that are set, e.g. ",83caf..." (hashes, not file names)
    size_t argumentCount = 0;           // length of args.values
    std::string stateSummary;           // raw state values, one line, e.g. "PSOs 1, RTs 2, blend 0 (empty), other 2049"
};

struct TechsetDetail {
    std::string name;                   // fields.name.string
    std::string materialType;           // fields.materialType.name, e.g. "MTL_TYPE_MODEL_SELFVIS"
    std::vector<TechniqueDetail> techniques;
    std::vector<std::string> notes;     // missing fields, unexpected root type, etc.
};

// Parses an ACTS technique set JSON export (format "mw19-asset-json", pool "techset") into `out`.
// Returns false and sets *err (when non-null) for invalid JSON or a root that is not an object.
bool parseTechsetExport(const std::string& jsonText, TechsetDetail& out, std::string* err);

// Plain-English lines from a technique set name (research 66 grammar), joined with '\n'. Uses parseTechset and
// materialBlendName from techset_info / material_blend. Example lines:
//   "Name: mo/lit_3_lit_rpl_ta1_802000_..."
//   "Family: lit (world and model surfaces)"
//   "Blend: opaque"
//   "Table: ta1"
//   "State word: 802000 (not decoded)"
//   "Editor draws it: yes"
std::string describeTechsetName(const std::string& techsetName);

}  // namespace iw8r
