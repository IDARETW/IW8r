// Minimal JSON DOM reader (RFC 8259 subset sufficient for asset dumps). Read-only.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
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

}  // namespace iw8r::json
