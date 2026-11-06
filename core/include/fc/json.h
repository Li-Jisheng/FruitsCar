// 供关卡加载器与各序列化器使用的最小化、零依赖 JSON DOM + 解析器。错误信息
// 会携带字节偏移、行号与列号（E1012 证据）。
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace fc {
namespace json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool() const { return bool_; }
    double asNumber() const { return number_; }
    const std::string& asString() const { return string_; }
    const std::vector<Value>& items() const { return array_; }
    std::vector<Value>& items() { return array_; }
    const std::vector<std::pair<std::string, Value>>& members() const { return object_; }

    // 当键不存在、或本值不是 object 时返回 nullptr。
    const Value* find(const std::string& key) const;
    bool has(const std::string& key) const { return find(key) != nullptr; }

    static Value makeNull();
    static Value makeBool(bool v);
    static Value makeNumber(double v);
    static Value makeString(std::string v);
    static Value makeArray();
    static Value makeObject();

    void push(Value v) { array_.push_back(std::move(v)); }
    void set(const std::string& key, Value v);

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Value> array_;
    std::vector<std::pair<std::string, Value>> object_;
};

struct ParseError {
    std::string message;
    int line = 0;
    int column = 0;
    std::size_t offset = 0;
    bool ok() const { return message.empty(); }
};

// 解析一份完整的 JSON 文档。失败时返回 false 并填充 `error`。
bool parse(const std::string& text, Value& out, ParseError& error);

// 从已剥除 UTF-8 BOM 的字符串中读取 JSON 文档。
bool parseWithBom(const std::string& text, Value& out, ParseError& error);

}  // namespace json
}  // namespace fc
