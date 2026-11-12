#include "autosave_history.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <system_error>

namespace fs = std::filesystem;

namespace iw8r {
namespace {

// yyyyMMdd-HHmmss: 15 chars, digits everywhere except '-' at index 8.
bool isStamp(const std::string& s) {
    if (s.size() != 15) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i == 8) {
            if (s[i] != '-') return false;
        } else if (s[i] < '0' || s[i] > '9') {
            return false;
        }
    }
    return true;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool hasPathSeparator(const std::string& s) {
    return s.find_first_of("/\\") != std::string::npos;
}

bool isKnownExt(const std::string& ext) {
    return ext == "map" || ext == "ents";
}

}  // namespace

std::string autosaveFileName(const std::string& base, const std::string& stamp, const std::string& ext) {
    if (base.empty() || hasPathSeparator(base)) return {};
    if (!isKnownExt(ext)) return {};
    if (!stamp.empty() && !isStamp(stamp)) return {};
    std::string name = base;
    if (!stamp.empty()) name += "." + stamp;
    name += ".autosave.";
    name += ext;
    return name;
}

bool parseAutosaveFileName(const std::string& fileName, AutosaveEntry& out) {
    if (fileName.empty() || hasPathSeparator(fileName)) return false;

    std::string ext;
    if (endsWith(fileName, ".map")) {
        ext = "map";
    } else if (endsWith(fileName, ".ents")) {
        ext = "ents";
    } else {
        return false;
    }

    // Strip ".<ext>" then the ".autosave" tag; what remains is "<base>" or "<base>.<stamp>".
    std::string head = fileName.substr(0, fileName.size() - ext.size() - 1);
    const std::string tag = ".autosave";
    if (!endsWith(head, tag)) return false;
    head.resize(head.size() - tag.size());

    // A bare ".<stamp>" has an empty base: reject it rather than read it as a legacy dotted base.
    if (head.size() == 16 && head[0] == '.' && isStamp(head.substr(1))) return false;

    std::string stamp;
    // A stamped head is "<base>.<15-char stamp>", so it needs at least one base char plus the dot.
    if (head.size() > 16 && head[head.size() - 16] == '.') {
        std::string candidate = head.substr(head.size() - 15);
        if (isStamp(candidate)) {
            stamp = candidate;
            head.resize(head.size() - 16);
        }
    }
    if (head.empty()) return false;

    out.base = head;
    out.stamp = stamp;
    out.ext = ext;
    return true;
}

std::vector<AutosaveEntry> listAutosaves(const std::string& dir, const std::string& baseFilter) {
    std::vector<AutosaveEntry> out;
    std::error_code ec;
    fs::directory_iterator it(fs::path(dir), ec);
    if (ec) return out;
    const fs::directory_iterator end;

    while (it != end) {
        std::error_code fec;
        const bool regular = it->is_regular_file(fec);
        if (!fec && regular) {
            AutosaveEntry e;
            if (parseAutosaveFileName(it->path().filename().string(), e) &&
                (baseFilter.empty() || e.base == baseFilter)) {
                e.path = it->path().string();
                std::error_code sec;
                const uintmax_t size = it->file_size(sec);
                e.bytes = sec ? 0 : size;
                out.push_back(e);
            }
        }
        it.increment(ec);
        if (ec) break;
    }

    std::sort(out.begin(), out.end(), [](const AutosaveEntry& a, const AutosaveEntry& b) {
        const bool aStamped = !a.stamp.empty();
        const bool bStamped = !b.stamp.empty();
        if (aStamped != bStamped) return aStamped;  // stamped before legacy
        if (a.stamp != b.stamp) return a.stamp > b.stamp;  // newest first (fixed-width format sorts as text)
        if (a.base != b.base) return a.base < b.base;
        return a.ext < b.ext;
    });
    return out;
}

std::vector<std::string> autosavesToPrune(const std::vector<AutosaveEntry>& entries, size_t keepPerMap) {
    std::map<std::string, std::vector<const AutosaveEntry*>> byBase;
    for (const AutosaveEntry& e : entries) {
        if (!e.stamp.empty()) byBase[e.base].push_back(&e);
    }

    std::vector<std::string> out;
    for (auto& [base, list] : byBase) {
        (void)base;
        std::stable_sort(list.begin(), list.end(), [](const AutosaveEntry* a, const AutosaveEntry* b) {
            if (a->stamp != b->stamp) return a->stamp > b->stamp;  // newest first
            return a->ext < b->ext;
        });
        for (size_t i = keepPerMap; i < list.size(); ++i) out.push_back(list[i]->path);
    }
    return out;
}

bool hasFreeSpace(const std::string& path, uintmax_t minFreeBytes, uintmax_t* freeBytes) {
    std::error_code ec;
    const fs::space_info info = fs::space(fs::path(path), ec);
    if (ec) {
        if (freeBytes) *freeBytes = 0;
        return true;  // unknown volume: do not block saving
    }
    if (freeBytes) *freeBytes = static_cast<uintmax_t>(info.available);
    return static_cast<uintmax_t>(info.available) >= minFreeBytes;
}

}  // namespace iw8r
