#include "json_lite.h"

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace iw8r::json {

const Value* Value::get(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& [k, v] : obj)
        if (k == key) return &v;
    return nullptr;
}

namespace {

// Build false: the same checks and errors, nothing stored (validate()).
template <bool Build>
struct Parser {
    std::string_view s;
    size_t i = 0;
    std::string err;
    int depth = 0;

    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    }
    bool fail(const char* m) {
        if (err.empty()) err = std::string(m) + " at byte " + std::to_string(i);
        return false;
    }
    static void appendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
        else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    }
    bool hex4(uint32_t& v) {
        if (i + 4 > s.size()) return fail("truncated \\u escape");
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
            else return fail("bad \\u escape");
        }
        return true;
    }
    // `out` is not touched when Build is false.
    bool str(std::string& out) {
        if (s[i] != '"') return fail("expected string");
        ++i;
        while (i < s.size()) {
            if constexpr (Build) {
                // Copy the plain run up to the next quote or backslash at once.
                size_t j = i;
                while (j < s.size() && s[j] != '"' && s[j] != '\\') ++j;
                out.append(s.data() + i, j - i);
                i = j;
            } else {
                while (i < s.size() && s[i] != '"' && s[i] != '\\') ++i;
            }
            if (i >= s.size()) break;
            const char c = s[i++];
            if (c == '"') return true;
            if (i >= s.size()) break;
            const char e = s[i++];
            switch (e) {
                case '"': if constexpr (Build) out += '"'; break;
                case '\\': if constexpr (Build) out += '\\'; break;
                case '/': if constexpr (Build) out += '/'; break;
                case 'b': if constexpr (Build) out += '\b'; break;
                case 'f': if constexpr (Build) out += '\f'; break;
                case 'n': if constexpr (Build) out += '\n'; break;
                case 'r': if constexpr (Build) out += '\r'; break;
                case 't': if constexpr (Build) out += '\t'; break;
                case 'u': {
                    uint32_t cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        cp = 0xFFFD;  // lone low surrogate: UTF-8 cannot encode it
                    } else if (cp >= 0xD800 && cp <= 0xDBFF) {
                        // A high surrogate pairs only with a following low-surrogate escape. Otherwise it is
                        // U+FFFD and the following escape (if any) is read on its own.
                        const size_t save = i;
                        uint32_t lo = 0;
                        if (i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                            i += 2;
                            if (!hex4(lo)) return false;
                        }
                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else {
                            i = save;
                            cp = 0xFFFD;
                        }
                    }
                    if constexpr (Build) appendUtf8(out, cp);
                    break;
                }
                default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }
    // `v` is null when Build is false.
    bool value(Value* v) {
        if (++depth > 512) return fail("nesting too deep");
        ws();
        if (i >= s.size()) return fail("unexpected end");
        const char c = s[i];
        bool ok = true;
        std::string scratch;  // keys and strings when not building (str() leaves it empty)
        if (c == '{') {
            if constexpr (Build) v->type = Value::Type::Object;
            ++i;
            ws();
            if (i < s.size() && s[i] == '}') { ++i; --depth; return true; }
            while (ok) {
                ws();
                if constexpr (Build) {
                    std::string k;
                    if (i >= s.size() || !str(k)) return fail("expected key");
                    ws();
                    if (i >= s.size() || s[i] != ':') return fail("expected ':'");
                    ++i;
                    v->obj.emplace_back(std::move(k), Value{});
                    if (!value(&v->obj.back().second)) return false;
                } else {
                    if (i >= s.size() || !str(scratch)) return fail("expected key");
                    ws();
                    if (i >= s.size() || s[i] != ':') return fail("expected ':'");
                    ++i;
                    if (!value(nullptr)) return false;
                }
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; break; }
                return fail("expected ',' or '}'");
            }
        } else if (c == '[') {
            if constexpr (Build) v->type = Value::Type::Array;
            ++i;
            ws();
            if (i < s.size() && s[i] == ']') { ++i; --depth; return true; }
            while (true) {
                if constexpr (Build) {
                    v->arr.emplace_back();
                    if (!value(&v->arr.back())) return false;
                } else {
                    if (!value(nullptr)) return false;
                }
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; break; }
                return fail("expected ',' or ']'");
            }
        } else if (c == '"') {
            if constexpr (Build) {
                v->type = Value::Type::String;
                ok = str(v->str);
            } else {
                ok = str(scratch);
            }
        } else if (s.compare(i, 4, "true") == 0) {
            if constexpr (Build) { v->type = Value::Type::Bool; v->b = true; }
            i += 4;
        } else if (s.compare(i, 5, "false") == 0) {
            if constexpr (Build) { v->type = Value::Type::Bool; v->b = false; }
            i += 5;
        } else if (s.compare(i, 4, "null") == 0) {
            if constexpr (Build) v->type = Value::Type::Null;
            i += 4;
        } else {
            size_t j = i;
            while (j < s.size() && (s[j] == '-' || s[j] == '+' || s[j] == '.' || s[j] == 'e' || s[j] == 'E' || (s[j] >= '0' && s[j] <= '9'))) ++j;
            if (j == i) return fail("unexpected character");
            double num = 0;
            auto r = std::from_chars(s.data() + i, s.data() + j, num);
            if (r.ec != std::errc()) return fail("bad number");
            if constexpr (Build) { v->type = Value::Type::Number; v->num = num; }
            i = j;
        }
        --depth;
        return ok;
    }
};

template <bool Build>
bool run(std::string_view text, Value* v, std::string* err) {
    Parser<Build> p{text};
    if (!p.value(v)) {
        if (err) *err = p.err;
        return false;
    }
    p.ws();
    if (p.i != text.size()) {
        if (err) *err = "trailing data at byte " + std::to_string(p.i);
        return false;
    }
    return true;
}

bool isWs(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

size_t skipWs(std::string_view t, size_t i) {
    while (i < t.size() && isWs(t[i])) ++i;
    return i;
}

// One past the closing quote of the string whose opening quote is at t[i], or npos.
size_t stringEnd(std::string_view t, size_t i) {
    for (++i; i < t.size(); ++i) {
        if (t[i] == '\\') ++i;
        else if (t[i] == '"') return i + 1;
    }
    return std::string_view::npos;
}

}  // namespace

std::optional<Value> parse(const std::string& text, std::string* err) { return parseView(text, err); }

std::optional<Value> parseView(std::string_view text, std::string* err) {
    Value v;
    if (!run<true>(text, &v, err)) return std::nullopt;
    return v;
}

bool validate(std::string_view text, std::string* err) { return run<false>(text, nullptr, err); }

size_t valueEnd(std::string_view t, size_t pos) {
    const size_t n = t.size();
    size_t i = skipWs(t, pos);
    if (i >= n) return std::string_view::npos;
    const char c = t[i];
    if (c == '"') return stringEnd(t, i);
    if (c == '{' || c == '[') {
        size_t depth = 0;
        while (i < n) {
            const char d = t[i];
            if (d == '"') {
                i = stringEnd(t, i);
                if (i == std::string_view::npos) return i;
                continue;
            }
            if (d == '{' || d == '[') {
                ++depth;
            } else if (d == '}' || d == ']') {
                if (--depth == 0) return i + 1;
            }
            ++i;
        }
        return std::string_view::npos;
    }
    // A scalar: up to the next delimiter.
    while (i < n && t[i] != ',' && t[i] != '}' && t[i] != ']' && !isWs(t[i])) ++i;
    return i;
}

// ---- View ----

View::View(std::string_view text) {
    const size_t b = skipWs(text, 0);
    if (b >= text.size()) return;
    const size_t e = valueEnd(text, b);
    if (e == std::string_view::npos || e <= b) return;
    text_ = text.substr(b, e - b);
}

Value::Type View::type() const {
    if (text_.empty()) return Value::Type::Null;
    switch (text_[0]) {
        case '{': return Value::Type::Object;
        case '[': return Value::Type::Array;
        case '"': return Value::Type::String;
        case 't':
        case 'f': return Value::Type::Bool;
        case 'n': return Value::Type::Null;
        default: return Value::Type::Number;
    }
}

namespace {

// Walks the members of the object text `t` ('{' at 0): fn(rawKeyText including quotes, value view). False when
// malformed. fn returns false to stop.
template <class F>
bool walkMembers(std::string_view t, F&& fn) {
    if (t.empty() || t[0] != '{') return false;
    size_t i = skipWs(t, 1);
    if (i < t.size() && t[i] == '}') return true;
    while (i < t.size()) {
        if (t[i] != '"') return false;
        const size_t ke = stringEnd(t, i);
        if (ke == std::string_view::npos) return false;
        const std::string_view rawKey = t.substr(i, ke - i);
        i = skipWs(t, ke);
        if (i >= t.size() || t[i] != ':') return false;
        i = skipWs(t, i + 1);
        const size_t ve = valueEnd(t, i);
        if (ve == std::string_view::npos || ve <= i) return false;
        View v(t.substr(i, ve - i));
        if (!fn(rawKey, v)) return true;
        i = skipWs(t, ve);
        if (i < t.size() && t[i] == ',') {
            i = skipWs(t, i + 1);
            continue;
        }
        return i < t.size() && t[i] == '}';
    }
    return false;
}

// The decoded key of a raw key text (with its quotes).
std::string decodeKey(std::string_view rawKey) {
    if (rawKey.find('\\') == std::string_view::npos) return std::string(rawKey.substr(1, rawKey.size() - 2));
    const auto v = parseView(rawKey);
    return v && v->string() ? *v->string() : std::string();
}

bool keyEquals(std::string_view rawKey, std::string_view key) {
    if (rawKey.find('\\') == std::string_view::npos) return rawKey.substr(1, rawKey.size() - 2) == key;
    return decodeKey(rawKey) == key;
}

}  // namespace

View View::get(std::string_view key) const {
    View found;
    walkMembers(text_, [&](std::string_view rawKey, View v) {
        if (!keyEquals(rawKey, key)) return true;
        found = v;
        return false;
    });
    return found;
}

bool View::forEachMember(const std::function<bool(const std::string& key, View value)>& fn) const {
    return walkMembers(text_, [&](std::string_view rawKey, View v) { return fn(decodeKey(rawKey), v); });
}

bool View::forEachElement(const std::function<bool(View element)>& fn) const {
    const std::string_view t = text_;
    if (t.empty() || t[0] != '[') return false;
    size_t i = skipWs(t, 1);
    if (i < t.size() && t[i] == ']') return true;
    while (i < t.size()) {
        const size_t ve = valueEnd(t, i);
        if (ve == std::string_view::npos || ve <= i) return false;
        if (!fn(View(t.substr(i, ve - i)))) return true;
        i = skipWs(t, ve);
        if (i < t.size() && t[i] == ',') {
            i = skipWs(t, i + 1);
            continue;
        }
        return i < t.size() && t[i] == ']';
    }
    return false;
}

std::optional<double> View::number() const {
    if (type() != Value::Type::Number) return std::nullopt;
    const auto v = parseView(text_);
    return v ? v->number() : std::nullopt;
}

bool View::rawString(std::string_view& out) const {
    if (text_.size() < 2 || text_[0] != '"' || text_.back() != '"') return false;
    const std::string_view inner = text_.substr(1, text_.size() - 2);
    if (inner.find('\\') != std::string_view::npos) return false;
    out = inner;
    return true;
}

bool View::string(std::string& out) const {
    std::string_view raw;
    if (rawString(raw)) {
        out.assign(raw);
        return true;
    }
    if (type() != Value::Type::String) return false;
    auto v = parseView(text_);
    if (!v || !v->string()) return false;
    out = std::move(v->str);
    return true;
}

// ---- FileText ----

FileText::~FileText() { close(); }

void FileText::close() {
#ifdef _WIN32
    if (base_) UnmapViewOfFile(base_);
    if (mapping_) CloseHandle(mapping_);
    if (file_) CloseHandle(file_);
#endif
    base_ = nullptr;
    mapping_ = nullptr;
    file_ = nullptr;
    view_ = {};
    std::string().swap(copy_);
}

bool FileText::open(const std::filesystem::path& path) {
    close();
#ifdef _WIN32
    // Others may read (and delete) the file meanwhile, not write it: a mapped file must not shrink under the view.
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size{};
        if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && uint64_t(size.QuadPart) <= uint64_t(SIZE_MAX)) {
            HANDLE m = CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
            const void* b = m ? MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0) : nullptr;
            if (b) {
                file_ = f;
                mapping_ = m;
                base_ = b;
                view_ = std::string_view(static_cast<const char*>(b), size_t(size.QuadPart));
                return true;
            }
            if (m) CloseHandle(m);
        }
        CloseHandle(f);
    }
#endif
    // Fallback: the whole file in memory (also an empty file, which cannot be mapped).
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    copy_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    view_ = copy_;
    return true;
}

}  // namespace iw8r::json
