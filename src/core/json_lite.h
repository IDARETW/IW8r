// Minimal JSON DOM reader (RFC 8259 subset sufficient for asset dumps). Read-only.
#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace iw8r::json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;  // insertion order preserved

    const Value* get(const std::string& key) const;   // object member or nullptr
    const Value* at(size_t i) const { return type == Type::Array && i < arr.size() ? &arr[i] : nullptr; }
    bool isNull() const { return type == Type::Null; }
    std::optional<double> number() const { return type == Type::Number ? std::optional<double>(num) : std::nullopt; }
    const std::string* string() const { return type == Type::String ? &str : nullptr; }
};

// Parses `text`. On failure returns nullopt and sets `err` (with byte offset).
std::optional<Value> parse(const std::string& text, std::string* err = nullptr);

// ---- Reading parts of a large text without a DOM of all of it (the 422 MB gfx_map export of mp_donetsk) ----

// parse() of a piece of text (no copy of it is made).
std::optional<Value> parseView(std::string_view text, std::string* err = nullptr);

// Checks `text` exactly as parse() does (same rules, same error text and byte offset) without building anything.
bool validate(std::string_view text, std::string* err = nullptr);

// One past the end of the JSON value that starts at text[pos] (leading whitespace skipped): strings honour escapes,
// arrays and objects are matched by bracket depth. npos when the text ends first. Not a validation.
size_t valueEnd(std::string_view text, size_t pos);

// One JSON value inside a larger text, read on demand: members and elements are found by scanning, nothing is
// copied. Meant for text that validate() accepted; on malformed text the lookups give invalid views or false.
class View {
public:
    View() = default;
    // The value at the start of `text` (leading whitespace skipped); invalid when there is none.
    explicit View(std::string_view text);

    bool valid() const { return !text_.empty(); }
    std::string_view text() const { return text_; }  // the value's own text, no surrounding whitespace
    Value::Type type() const;                         // from its first character (Null when invalid)
    bool isNull() const { return type() == Value::Type::Null; }

    // The first member named `key` (as Value::get), or an invalid view (not an object, or no such member).
    View get(std::string_view key) const;
    // Every member in order: fn(key, value). The key is decoded. Stops early (returns true) when fn returns false.
    // False when this is not an object or the text is malformed.
    bool forEachMember(const std::function<bool(const std::string& key, View value)>& fn) const;
    // Every element of an array in order. Stops early when fn returns false. False when not an array / malformed.
    bool forEachElement(const std::function<bool(View element)>& fn) const;

    // The value parsed (a small DOM of this value only).
    std::optional<Value> parse(std::string* err = nullptr) const { return parseView(text_, err); }
    // A number value as parse() reads it, nullopt for any other type.
    std::optional<double> number() const;
    // A string value: its content without the quotes when it holds no escape (then it equals the decoded string and
    // nothing is copied); false for other types or strings with escapes (use string() / parse() for those).
    bool rawString(std::string_view& out) const;
    // A string value decoded; false for other types.
    bool string(std::string& out) const;

private:
    std::string_view text_;
};

// A whole file as read-only text. On Windows the file is memory-mapped: its pages belong to the file cache and do not
// count against the process's commit (a 422 MB export costs no private memory). Elsewhere, or when the mapping fails
// (an empty file), it is read into memory. The file stays open (others may read it) until the object is destroyed.
class FileText {
public:
    FileText() = default;
    ~FileText();
    FileText(const FileText&) = delete;
    FileText& operator=(const FileText&) = delete;

    // False when the file cannot be opened or read.
    bool open(const std::filesystem::path& path);
    std::string_view view() const { return view_; }
    void close();

private:
    std::string_view view_;
    std::string copy_;     // the fallback
    void* file_ = nullptr;
    void* mapping_ = nullptr;
    const void* base_ = nullptr;
};

}  // namespace iw8r::json
