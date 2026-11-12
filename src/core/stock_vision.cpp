#include "stock_vision.h"

#include <set>

namespace iw8r {

namespace {

// One line `KEY "value"` -> (key, value); false for anything else.
bool parseLine(const std::string& line, std::string& key, std::string& value, size_t* valueStart = nullptr, size_t* valueEnd = nullptr) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    const size_t ks = i;
    while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '"') ++i;
    if (i == ks) return false;
    key = line.substr(ks, i - ks);
    const size_t q1 = line.find('"', i);
    if (q1 == std::string::npos) return false;
    const size_t q2 = line.find('"', q1 + 1);
    if (q2 == std::string::npos) return false;
    value = line.substr(q1 + 1, q2 - q1 - 1);
    if (valueStart) *valueStart = q1 + 1;
    if (valueEnd) *valueEnd = q2;
    return true;
}

}  // namespace

std::map<std::string, std::string> readVisionValues(const std::string& text) {
    std::map<std::string, std::string> out;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string k, v;
        if (parseLine(line, k, v)) out[k] = v;
        if (end == text.size()) break;
        pos = end + 1;
    }
    return out;
}

std::string applyVisionEdits(const std::string& text, const std::map<std::string, std::string>& edits, int* changed,
                             std::vector<std::string>* missing) {
    std::string out;
    std::set<std::string> seen;
    int n = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        const bool last = end == std::string::npos;
        if (last) end = text.size();
        std::string line = text.substr(pos, end - pos);
        std::string k, v;
        size_t vs = 0, ve = 0;
        if (parseLine(line, k, v, &vs, &ve))
            if (auto it = edits.find(k); it != edits.end()) {
                seen.insert(k);
                if (it->second != v) {
                    line = line.substr(0, vs) + it->second + line.substr(ve);
                    ++n;
                }
            }
        out += line;
        if (last) break;
        out += '\n';
        pos = end + 1;
    }
    if (changed) *changed = n;
    if (missing)
        for (const auto& [k, v] : edits)
            if (!seen.count(k)) missing->push_back(k);
    return out;
}

}  // namespace iw8r
