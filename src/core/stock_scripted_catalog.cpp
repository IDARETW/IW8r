// IW8 Radiant Suite - stock scripted object catalog (roadmap 6.13). See scripted_objects.h.
#include "stock_scripted_catalog.h"

#include <climits>
#include <fstream>
#include <sstream>

namespace iw8r {

namespace {

constexpr size_t kColumns = 8;

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == '\t') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

std::vector<std::string> splitCommas(const std::string& text) {
    std::vector<std::string> out;
    if (text.empty()) return out;
    std::string cur;
    for (char c : text) {
        if (c == ',') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

bool parseCount(const std::string& text, int* out) {
    if (text.empty()) return false;
    long long v = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
        if (v > INT_MAX) return false;
    }
    *out = static_cast<int>(v);
    return true;
}

// Sets *err (if given) to "line N: what" and returns false, so callers can write `return fail(...)`.
bool fail(std::string* err, size_t lineNo, const std::string& what) {
    if (err) {
        std::ostringstream os;
        os << "line " << lineNo << ": " << what;
        *err = os.str();
    }
    return false;
}

}  // namespace

bool scriptedObjectWildcardMatch(const std::string& pattern, const std::string& text) {
    // Greedy match with one backtrack point, the usual '*'-only glob algorithm.
    const size_t npos = std::string::npos;
    size_t p = 0, t = 0, star = npos, mark = 0;
    while (t < text.size()) {
        if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = t;
        } else if (p < pattern.size() && pattern[p] == text[t]) {
            ++p;
            ++t;
        } else if (star != npos) {
            p = star + 1;
            t = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

bool ScriptedObjectCatalog::load(const std::string& path, std::string* err) {
    kinds_.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    std::vector<ScriptedObjectKind> parsed;
    std::string line;
    size_t lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        const std::vector<std::string> cols = splitTabs(line);
        if (cols.size() != kColumns) {
            return fail(err, lineNo, "expected 8 tab-separated columns, found " + std::to_string(cols.size()));
        }
        ScriptedObjectKind row;
        row.kind = cols[0];
        row.classname = cols[1];
        row.key = cols[2];
        row.valuePattern = cols[3];
        row.handledBy = cols[4];
        row.worksInCustom = cols[5];
        if (row.kind.empty()) return fail(err, lineNo, "empty kind");
        if (row.classname.empty()) return fail(err, lineNo, "empty classname");
        if (row.key.empty()) return fail(err, lineNo, "empty key (use '-' for none)");
        if (row.valuePattern.empty()) return fail(err, lineNo, "empty value_pattern");
        if (row.worksInCustom != "yes" && row.worksInCustom != "no" && row.worksInCustom != "unknown") {
            return fail(err, lineNo, "works_in_custom_map must be yes, no or unknown, got '" + row.worksInCustom + "'");
        }
        row.maps = splitCommas(cols[6]);
        for (const std::string& m : row.maps) {
            if (m.empty()) return fail(err, lineNo, "empty map name in maps column");
        }
        if (!parseCount(cols[7], &row.count)) return fail(err, lineNo, "count must be a non-negative integer");
        parsed.push_back(std::move(row));
    }
    if (in.bad()) {
        if (err) *err = "read error in " + path;
        return false;
    }
    kinds_ = std::move(parsed);
    return true;
}

std::vector<const ScriptedObjectKind*> ScriptedObjectCatalog::match(
    const std::map<std::string, std::string>& entityKeys) const {
    std::vector<const ScriptedObjectKind*> out;
    std::string classname;
    const auto cls = entityKeys.find("classname");
    if (cls != entityKeys.end()) classname = cls->second;

    for (const ScriptedObjectKind& k : kinds_) {
        if (!scriptedObjectWildcardMatch(k.classname, classname)) continue;
        if (k.key == "-") {
            out.push_back(&k);
            continue;
        }
        const auto it = entityKeys.find(k.key);
        if (it == entityKeys.end()) continue;
        if (scriptedObjectWildcardMatch(k.valuePattern, it->second)) out.push_back(&k);
    }
    return out;
}

}  // namespace iw8r
