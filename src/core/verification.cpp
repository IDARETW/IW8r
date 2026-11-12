// In-game verification matrix (roadmap 1.13). See verification.h.
#include "verification.h"

#include "json_lite.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace iw8r {
namespace {

namespace fs = std::filesystem;

// Roadmap links for checklist items whose text names the feature with certainty but gives no roadmap number.
// Each needle is a lowercase substring of the item text; items are comma-separated roadmap numbers.
struct MapEntry {
    const char* needle;
    const char* items;
};
const MapEntry kRoadmapMap[] = {
    {"normal and gloss maps", "3.2,4.14"},           // in game: builds with the _n&_g packs; shader
    {"textured view with normal maps", "3.2"},       // editor: normal and gloss shading
    {"time of day", "5.19"},                         // Compile > Time of Day presets
    {"command palette", "9.13"},                     // Ctrl+Shift+P
    {"outliner dock", "2.29"},
    {"brush > arrange", "2.25"},
    {"brush > spline", "2.26"},
    {"primitives > triggers", "6.17"},
    {"callout zone", "6.20"},
    {"compare with map", "2.28"},
    {"script dock:", "6.11"},                        // script editor dock
    {"insert behaviour", "6.12"},                    // behaviour library
    {"script dock's behaviours", "6.12"},
    {"material info / model info", "4.16,4.18"},     // material and model inspectors
    {"drag a model, effect or sound", "4.24"},
    {"camera mode: wireframe", "3.13"},              // view modes
    {"camera mode > contents", "3.13"},
    {"light entities light the floor per pixel", "3.14"},
};

const char* const kSchema = "iw8r-verification-1";

bool isValidResult(const std::string& r) {
    return r == "ok" || r == "wrong" || r == "not-tested";
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string stripCr(const std::string& s) {
    return !s.empty() && s.back() == '\r' ? s.substr(0, s.size() - 1) : s;
}

// Keeps a note or date on one line (the suffix in the checklist is one line).
std::string oneLine(std::string s) {
    for (char& c : s)
        if (c == '\r' || c == '\n' || c == '\t') c = ' ';
    return s;
}

// Splits on '\n' and keeps any '\r', so joining with '\n' gives back the exact input bytes.
std::vector<std::string> splitLines(const std::string& md) {
    std::vector<std::string> lines;
    size_t start = 0;
    for (size_t i = 0; i < md.size(); ++i) {
        if (md[i] == '\n') {
            lines.push_back(md.substr(start, i - start));
            start = i + 1;
        }
    }
    lines.push_back(md.substr(start));
    return lines;
}

std::string joinLines(const std::vector<std::string>& lines) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out += '\n';
        out += lines[i];
    }
    return out;
}

std::string slugOf(const std::string& s) {
    std::string out;
    bool sep = false;
    for (unsigned char c : s) {
        if (std::isalnum(c)) {
            out += static_cast<char>(std::tolower(c));
            sep = false;
        } else if (!out.empty() && !sep) {
            out += '_';
            sep = true;
        }
    }
    if (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

void addUnique(std::vector<std::string>& v, const std::string& x) {
    if (std::find(v.begin(), v.end(), x) == v.end()) v.push_back(x);
}

// Roadmap numbers written as L.N (L = 1..11, N one or two digits), e.g. "(3.2)" or "5.13".
// Version strings such as 1.20.4.7623265 are not matched.
std::vector<std::string> mentionedRoadmapItems(const std::string& text) {
    std::vector<std::string> out;
    const size_t n = text.size();
    auto digit = [&](size_t k) { return k < n && std::isdigit(static_cast<unsigned char>(text[k])) != 0; };
    for (size_t i = 0; i < n; ++i) {
        if (!digit(i) || (i > 0 && (digit(i - 1) || text[i - 1] == '.'))) continue;
        size_t j = i;
        while (j < n && j - i < 2 && digit(j)) ++j;
        if (j >= n || text[j] != '.') continue;
        const size_t d = j + 1;
        size_t k = d;
        while (k < n && k - d < 2 && digit(k)) ++k;
        // A following "." is a sentence end unless a digit follows it (1.20.4 is a version).
        if (k == d || digit(k) || (k < n && text[k] == '.' && digit(k + 1))) continue;
        const int layer = std::atoi(text.substr(i, j - i).c_str());
        if (layer < 1 || layer > 11) continue;
        addUnique(out, text.substr(i, k - i));
        i = k - 1;
    }
    return out;
}

void addMapped(const std::string& text, std::vector<std::string>& out) {
    const std::string low = lower(text);
    for (const MapEntry& e : kRoadmapMap) {
        if (low.find(e.needle) == std::string::npos) continue;
        std::string items = e.items;
        size_t start = 0;
        while (start <= items.size()) {
            size_t comma = items.find(',', start);
            if (comma == std::string::npos) comma = items.size();
            addUnique(out, items.substr(start, comma - start));
            start = comma + 1;
        }
    }
}

struct Scanned {
    CheckItem item;
    size_t firstLine = 0;  // the "- [ ]" line
    size_t lastLine = 0;   // last line of the item (a wrong suffix goes here)
    size_t boxCol = 3;     // column of the box character on firstLine
};

std::vector<Scanned> scan(const std::vector<std::string>& lines) {
    std::vector<Scanned> out;
    std::string section;
    std::set<std::string> used;
    const size_t n = lines.size();
    for (size_t li = 0; li < n; ++li) {
        const std::string line = stripCr(lines[li]);
        if (line.rfind("## ", 0) == 0) {
            section = trim(line.substr(3));
            continue;
        }
        if (line.size() < 5 || line.compare(0, 3, "- [") != 0 || (line[3] != ' ' && line[3] != 'x' && line[3] != 'X') ||
            line[4] != ']')
            continue;

        Scanned s;
        s.firstLine = li;
        s.lastLine = li;
        s.item.section = section;
        s.item.checked = line[3] != ' ';
        std::string text = trim(line.substr(5));

        // Continuation lines: indented, not blank, not a new bullet or heading.
        for (size_t lj = li + 1; lj < n; ++lj) {
            const std::string cont = stripCr(lines[lj]);
            if (cont.empty() || (cont[0] != ' ' && cont[0] != '\t')) break;
            const std::string t = trim(cont);
            if (t.empty() || t.rfind("- ", 0) == 0 || t.rfind("## ", 0) == 0) break;
            text += " " + t;
            s.lastLine = lj;
        }
        li = s.lastLine;

        std::string name;
        if (text.rfind("**", 0) == 0) {
            const size_t close = text.find("**", 2);
            if (close != std::string::npos) {
                name = text.substr(2, close - 2);
                text = trim(text.substr(close + 2));
                if (!text.empty() && text[0] == ':') text = trim(text.substr(1));
            }
        }
        s.item.text = text;

        std::string base = slugOf(name);
        if (base.empty()) {
            std::istringstream words(text);
            std::string w, first;
            int count = 0;
            while (count < 8 && words >> w) {
                if (count) first += ' ';
                first += w;
                ++count;
            }
            base = slugOf(first);
        }
        if (base.empty()) base = "item";
        std::string id = base;
        for (int k = 2; used.count(id); ++k) id = base + "_" + std::to_string(k);
        used.insert(id);
        s.item.id = id;

        s.item.roadmapItems = mentionedRoadmapItems(text);
        addMapped(text, s.item.roadmapItems);
        out.push_back(std::move(s));
    }
    return out;
}

// Latest result per id: newest date wins; on equal dates the later entry wins.
std::map<std::string, const CheckResult*> latestById(const std::vector<CheckResult>& results) {
    std::map<std::string, const CheckResult*> latest;
    for (const CheckResult& r : results) {
        auto it = latest.find(r.id);
        if (it == latest.end() || r.date >= it->second->date) latest[r.id] = &r;
    }
    return latest;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 15];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string strField(const json::Value& obj, const char* key) {
    const json::Value* v = obj.get(key);
    return v && v->string() ? *v->string() : std::string();
}

}  // namespace

std::vector<CheckItem> parseChecklist(const std::string& markdown) {
    std::vector<CheckItem> items;
    for (Scanned& s : scan(splitLines(markdown))) items.push_back(std::move(s.item));
    return items;
}

bool loadResults(const std::string& path, std::vector<CheckResult>& out, std::string& err) {
    out.clear();
    std::error_code ec;
    if (!fs::exists(path, ec)) return true;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "cannot open " + path;
        return false;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    std::string perr;
    const auto root = json::parse(buf.str(), &perr);
    if (!root || root->type != json::Value::Type::Object) {
        err = "bad JSON in " + path + (perr.empty() ? std::string() : ": " + perr);
        return false;
    }
    const json::Value* arr = root->get("results");
    if (!arr || arr->type != json::Value::Type::Array) {
        err = "no results array in " + path;
        return false;
    }
    for (size_t i = 0; i < arr->arr.size(); ++i) {
        const json::Value& e = arr->arr[i];
        if (e.type != json::Value::Type::Object) {
            err = "result " + std::to_string(i) + " is not an object";
            out.clear();
            return false;
        }
        CheckResult r;
        r.id = strField(e, "id");
        r.result = strField(e, "result");
        r.build = strField(e, "build");
        r.date = strField(e, "date");
        r.note = strField(e, "note");
        if (r.id.empty() || !isValidResult(r.result)) {
            err = "result " + std::to_string(i) + " has no id or a result other than ok / wrong / not-tested";
            out.clear();
            return false;
        }
        out.push_back(std::move(r));
    }
    return true;
}

bool saveResults(const std::string& path, const std::vector<CheckResult>& results, std::string& err) {
    for (const CheckResult& r : results) {
        if (!isValidResult(r.result)) {
            err = "bad result '" + r.result + "' for " + r.id;
            return false;
        }
    }
    std::string js = "{\n  \"schema\": \"" + std::string(kSchema) + "\",\n  \"results\": [";
    for (size_t i = 0; i < results.size(); ++i) {
        const CheckResult& r = results[i];
        js += i ? ",\n    " : "\n    ";
        js += "{\"id\": \"" + jsonEscape(r.id) + "\", \"result\": \"" + jsonEscape(r.result) + "\", \"build\": \"" +
              jsonEscape(r.build) + "\", \"date\": \"" + jsonEscape(r.date) + "\", \"note\": \"" + jsonEscape(r.note) +
              "\"}";
    }
    js += results.empty() ? "]\n}\n" : "\n  ]\n}\n";

    const fs::path target(path);
    std::error_code ec;
    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path(), ec);
        if (ec) {
            err = "cannot create " + target.parent_path().string();
            return false;
        }
    }
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            err = "cannot write " + tmp.string();
            return false;
        }
        out << js;
        if (!out) {
            err = "write failed for " + tmp.string();
            return false;
        }
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        err = "cannot replace " + target.string();
        return false;
    }
    return true;
}

std::string updateChecklistMarks(const std::string& markdown, const std::vector<CheckResult>& results) {
    std::vector<std::string> lines = splitLines(markdown);
    const std::vector<Scanned> items = scan(lines);
    const auto latest = latestById(results);
    for (const Scanned& s : items) {
        const CheckResult* r = nullptr;
        if (auto it = latest.find(s.item.id); it != latest.end()) r = it->second;
        const bool wrong = r && r->result == "wrong";
        const bool ok = r && r->result == "ok";

        // Drop the suffix an earlier run wrote; it is written again below while the item is still wrong.
        std::string& last = lines[s.lastLine];
        std::string cr;
        if (!last.empty() && last.back() == '\r') {
            cr = "\r";
            last.pop_back();
        }
        if (!last.empty() && last.back() == ')') {
            const size_t at = last.rfind(" (wrong ");
            if (at != std::string::npos) last.erase(at);
        }

        if (wrong || ok) lines[s.firstLine][s.boxCol] = wrong ? ' ' : 'x';
        if (wrong) {
            last += " (wrong " + oneLine(r->date);
            if (!r->note.empty()) last += ": " + oneLine(r->note);
            last += ")";
        }
        last += cr;
    }
    return joinLines(lines);
}

std::map<std::string, std::string> roadmapSeenInGame(const std::vector<CheckItem>& items,
                                                     const std::vector<CheckResult>& results) {
    const auto latest = latestById(results);
    std::map<std::string, std::vector<const CheckItem*>> byRoadmap;
    for (const CheckItem& item : items) {
        if (item.section.rfind("In game", 0) != 0) continue;
        for (const std::string& r : item.roadmapItems) byRoadmap[r].push_back(&item);
    }
    std::map<std::string, std::string> seen;
    for (const auto& [roadmap, list] : byRoadmap) {
        bool all = true;
        std::string date;
        for (const CheckItem* item : list) {
            auto it = latest.find(item->id);
            if (it == latest.end() || it->second->result != "ok") {
                all = false;
                break;
            }
            if (it->second->date > date) date = it->second->date;
        }
        if (all) seen[roadmap] = date.empty() ? "seen in game" : "seen in game " + date;
    }
    return seen;
}

}  // namespace iw8r
