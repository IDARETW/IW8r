#include "json_lite.h"

#include <charconv>
#include <cstdlib>

namespace iw8r::json {

const Value* Value::get(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& [k, v] : obj)
        if (k == key) return &v;
    return nullptr;
}

namespace {

struct Parser {
    const std::string& s;
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
    bool str(std::string& out) {
        if (s[i] != '"') return fail("expected string");
        ++i;
        while (i < s.size()) {
            const char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) break;
            const char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
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
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }
    bool value(Value& v) {
        if (++depth > 512) return fail("nesting too deep");
        ws();
        if (i >= s.size()) return fail("unexpected end");
        const char c = s[i];
        bool ok = true;
        if (c == '{') {
            v.type = Value::Type::Object;
            ++i;
            ws();
            if (i < s.size() && s[i] == '}') { ++i; --depth; return true; }
            while (ok) {
                ws();
                std::string k;
                if (i >= s.size() || !str(k)) return fail("expected key");
                ws();
                if (i >= s.size() || s[i] != ':') return fail("expected ':'");
                ++i;
                v.obj.emplace_back(std::move(k), Value{});
                if (!value(v.obj.back().second)) return false;
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; break; }
                return fail("expected ',' or '}'");
            }
        } else if (c == '[') {
            v.type = Value::Type::Array;
            ++i;
            ws();
            if (i < s.size() && s[i] == ']') { ++i; --depth; return true; }
            while (true) {
                v.arr.emplace_back();
                if (!value(v.arr.back())) return false;
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; break; }
                return fail("expected ',' or ']'");
            }
        } else if (c == '"') {
            v.type = Value::Type::String;
            ok = str(v.str);
        } else if (s.compare(i, 4, "true") == 0) { v.type = Value::Type::Bool; v.b = true; i += 4; }
        else if (s.compare(i, 5, "false") == 0) { v.type = Value::Type::Bool; v.b = false; i += 5; }
        else if (s.compare(i, 4, "null") == 0) { v.type = Value::Type::Null; i += 4; }
        else {
            size_t j = i;
            while (j < s.size() && (s[j] == '-' || s[j] == '+' || s[j] == '.' || s[j] == 'e' || s[j] == 'E' || (s[j] >= '0' && s[j] <= '9'))) ++j;
            if (j == i) return fail("unexpected character");
            v.type = Value::Type::Number;
            auto r = std::from_chars(s.data() + i, s.data() + j, v.num);
            if (r.ec != std::errc()) return fail("bad number");
            i = j;
        }
        --depth;
        return ok;
    }
};

}  // namespace

std::optional<Value> parse(const std::string& text, std::string* err) {
    Parser p{text};
    Value v;
    if (!p.value(v)) {
        if (err) *err = p.err;
        return std::nullopt;
    }
    p.ws();
    if (p.i != text.size()) {
        if (err) *err = "trailing data at byte " + std::to_string(p.i);
        return std::nullopt;
    }
    return v;
}

}  // namespace iw8r::json
