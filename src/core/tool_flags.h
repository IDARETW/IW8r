// IW8 Radiant Suite - brush `toolFlags X;` directive lines (doc 02, brush-level directives).
//
// Names are read and written as their text (case kept). Only the toolFlags lines change; every other
// line of the brush, including `contents`, `layer` and unknown directives, keeps its bytes. The compiler
// does not read toolFlags, so the names are for the editor and for round trips.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "document.h"
#include "map_text.h"

namespace iw8r {

struct ToolFlagDef {
    std::string name;
    std::string help;    // one line for the inspector / tooltip
    std::string source;  // where the name was seen (file and section)
};

// Flag names with evidence in the repo or the research corpus. Names not listed here still round-trip.
const std::vector<ToolFlagDef>& knownToolFlags();

// Names of the `toolFlags` lines of a primitive, in the order written (duplicates kept).
std::vector<std::string> readToolFlags(const Primitive& brush);

// The brush with exactly the toolFlags lines `flags` asks for, in that order. Lines whose name is already
// wanted keep their text and terminator; missing names get a new " toolFlags NAME;" line with `eol`; the
// block sits where the first old toolFlags line was, else after the opening brace (and after any `layer`
// line that follows it). Duplicate names are written once. Returns the brush unchanged when the names
// already match, and nullopt when `brush` is not a brush, a name is empty or contains ';' or a line break,
// or there is no body.
std::optional<Primitive> withToolFlags(const Primitive& brush, const std::vector<std::string>& flags, const std::string& eol);

// Adds (on) or removes (off) one flag on every selected brush, as one undo step. The selection is kept.
// Returns false when no brush changed.
bool setToolFlagOnSelection(Document& doc, const std::string& flag, bool on);

}  // namespace iw8r
