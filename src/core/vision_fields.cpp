#include "vision_fields.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

namespace iw8r {

namespace {

const std::vector<VisionField>& table() {
    static const std::vector<VisionField> t = {
#include "vision_fields.inc"
    };
    return t;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> splitWords(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        size_t b = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i > b) out.push_back(s.substr(b, i - b));
    }
    return out;
}

// Number syntax the converter accepts: optional sign, digits with an optional point, optional exponent.
bool isNumberText(const std::string& t) {
    const size_t n = t.size();
    size_t i = 0, digits = 0;
    if (i < n && (t[i] == '+' || t[i] == '-')) ++i;
    while (i < n && std::isdigit(static_cast<unsigned char>(t[i]))) ++i, ++digits;
    if (i < n && t[i] == '.') {
        ++i;
        while (i < n && std::isdigit(static_cast<unsigned char>(t[i]))) ++i, ++digits;
    }
    if (digits == 0) return false;
    if (i < n && (t[i] == 'e' || t[i] == 'E')) {
        ++i;
        if (i < n && (t[i] == '+' || t[i] == '-')) ++i;
        size_t expDigits = 0;
        while (i < n && std::isdigit(static_cast<unsigned char>(t[i]))) ++i, ++expDigits;
        if (expDigits == 0) return false;
    }
    return i == n;
}

void setErr(std::string* err, const std::string& msg) {
    if (err) *err = msg;
}

}  // namespace

const std::vector<VisionField>& visionFields() { return table(); }

const VisionField* findVisionField(const std::string& name) {
    static const std::unordered_map<std::string, const VisionField*> index = [] {
        std::unordered_map<std::string, const VisionField*> m;
        for (const VisionField& f : table()) m.emplace(lower(f.name), &f);
        return m;
    }();
    if (name.empty()) return nullptr;
    const auto it = index.find(lower(name));
    return it == index.end() ? nullptr : it->second;
}

bool parseVisionOverrides(const std::string& text, std::vector<std::pair<std::string, std::string>>& out, std::string* err) {
    out.clear();
    size_t start = 0;
    while (true) {
        const size_t end = text.find(';', start);
        const std::string item = trim(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (!item.empty()) {
            const size_t eq = item.find('=');
            if (eq == std::string::npos) {
                setErr(err, "'" + item + "' has no '=' (use name=value)");
                out.clear();
                return false;
            }
            const std::string name = trim(item.substr(0, eq));
            const std::string value = trim(item.substr(eq + 1));
            if (name.empty()) {
                setErr(err, "'" + item + "' has an empty name");
                out.clear();
                return false;
            }
            if (value.empty()) {
                setErr(err, "'" + name + "' has no value");
                out.clear();
                return false;
            }
            out.emplace_back(name, value);
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

std::string formatVisionOverrides(const std::vector<std::pair<std::string, std::string>>& pairs) {
    std::string out;
    for (const auto& p : pairs) {
        if (!out.empty()) out += ';';
        out += p.first + "=" + p.second;
    }
    return out;
}

std::string checkVisionValue(const VisionField& f, const std::string& value) {
    const std::string type = f.type;
    const std::vector<std::string> words = splitWords(value);
    if (type == "bool") {
        if (words.size() == 1 && isNumberText(words[0])) {
            const double d = std::strtod(words[0].c_str(), nullptr);
            if (d == 0.0 || d == 1.0) return "";
        }
        return "needs 0 or 1";
    }
    size_t want = 0;
    if (type == "scalar") {
        want = 1;
    } else if (type == "vector") {
        want = 3;
    } else {
        return "type " + type + " cannot be set with a number";
    }
    if (words.size() != want) return want == 1 ? "needs one number" : "needs three numbers (x y z)";
    for (const std::string& w : words) {
        if (!isNumberText(w)) return "'" + w + "' is not a number";
        if (!std::isfinite(std::strtod(w.c_str(), nullptr))) return "'" + w + "' is out of range";
    }
    return "";
}

}  // namespace iw8r
