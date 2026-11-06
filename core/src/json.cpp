#include "fc/json.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace fc {
namespace json {

Value Value::makeNull() { return Value(); }

Value Value::makeBool(bool v) {
    Value out;
    out.type_ = Type::Bool;
    out.bool_ = v;
    return out;
}

Value Value::makeNumber(double v) {
    Value out;
    out.type_ = Type::Number;
    out.number_ = v;
    return out;
}

Value Value::makeString(std::string v) {
    Value out;
    out.type_ = Type::String;
    out.string_ = std::move(v);
    return out;
}

Value Value::makeArray() {
    Value out;
    out.type_ = Type::Array;
    return out;
}

Value Value::makeObject() {
    Value out;
    out.type_ = Type::Object;
    return out;
}

void Value::set(const std::string& key, Value v) {
    for (auto& member : object_) {
        if (member.first == key) {
            member.second = std::move(v);
            return;
        }
    }
    object_.emplace_back(key, std::move(v));
}

const Value* Value::find(const std::string& key) const {
    if (type_ != Type::Object) {
        return nullptr;
    }
    for (const auto& member : object_) {
        if (member.first == key) {
            return &member.second;
        }
    }
    return nullptr;
}

namespace {

constexpr int kMaxDepth = 64;

struct Parser {
    const std::string& text;
    std::size_t pos = 0;
    ParseError error;
    int depth = 0;

    explicit Parser(const std::string& input) : text(input) {}

    bool failed() const { return !error.ok(); }

    bool fail(const std::string& message, std::size_t at) {
        if (failed()) {
            return false;
        }
        error.message = message;
        error.offset = at;
        int line = 1;
        int column = 1;
        for (std::size_t i = 0; i < at && i < text.size(); ++i) {
            if (text[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        error.line = line;
        error.column = column;
        return false;
    }

    void skipWhitespace() {
        while (pos < text.size()) {
            const char ch = text[pos];
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                ++pos;
            } else {
                break;
            }
        }
    }

    bool parseValue(Value& out);

    bool parseObject(Value& out) {
        if (depth >= kMaxDepth) {
            return fail("nesting too deep", pos);
        }
        ++depth;
        const std::size_t start = pos;
        ++pos;  // '{'
        out = Value::makeObject();
        skipWhitespace();
        if (pos < text.size() && text[pos] == '}') {
            ++pos;
            --depth;
            return true;
        }
        for (;;) {
            skipWhitespace();
            if (pos >= text.size() || text[pos] != '"') {
                --depth;
                return fail("expected object key", pos);
            }
            std::string key;
            if (!parseString(key)) {
                --depth;
                return false;
            }
            skipWhitespace();
            if (pos >= text.size() || text[pos] != ':') {
                --depth;
                return fail("expected ':' after object key", pos);
            }
            ++pos;
            Value member;
            if (!parseValue(member)) {
                --depth;
                return false;
            }
            out.set(key, std::move(member));
            skipWhitespace();
            if (pos < text.size() && text[pos] == ',') {
                ++pos;
                continue;
            }
            if (pos < text.size() && text[pos] == '}') {
                ++pos;
                --depth;
                return true;
            }
            --depth;
            return fail("expected ',' or '}' in object", pos < text.size() ? pos : start);
        }
    }

    bool parseArray(Value& out) {
        if (depth >= kMaxDepth) {
            return fail("nesting too deep", pos);
        }
        ++depth;
        ++pos;  // '['
        out = Value::makeArray();
        skipWhitespace();
        if (pos < text.size() && text[pos] == ']') {
            ++pos;
            --depth;
            return true;
        }
        for (;;) {
            Value item;
            if (!parseValue(item)) {
                --depth;
                return false;
            }
            out.push(std::move(item));
            skipWhitespace();
            if (pos < text.size() && text[pos] == ',') {
                ++pos;
                continue;
            }
            if (pos < text.size() && text[pos] == ']') {
                ++pos;
                --depth;
                return true;
            }
            --depth;
            return fail("expected ',' or ']' in array", pos);
        }
    }

    bool parseString(std::string& out) {
        out.clear();
        ++pos;  // 起始引号
        while (pos < text.size()) {
            const unsigned char ch = static_cast<unsigned char>(text[pos]);
            if (ch == '"') {
                ++pos;
                return true;
            }
            if (ch == '\\') {
                ++pos;
                if (pos >= text.size()) {
                    return fail("unterminated escape", pos);
                }
                const char esc = text[pos++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        if (pos + 4 > text.size()) {
                            return fail("truncated \\u escape", pos);
                        }
                        unsigned int code = 0;
                        for (int i = 0; i < 4; ++i) {
                            const char h = text[pos + static_cast<std::size_t>(i)];
                            code <<= 4;
                            if (h >= '0' && h <= '9') {
                                code += static_cast<unsigned int>(h - '0');
                            } else if (h >= 'a' && h <= 'f') {
                                code += static_cast<unsigned int>(h - 'a' + 10);
                            } else if (h >= 'A' && h <= 'F') {
                                code += static_cast<unsigned int>(h - 'A' + 10);
                            } else {
                                return fail("invalid \\u escape", pos);
                            }
                        }
                        pos += 4;
                        appendUtf8(out, static_cast<unsigned int>(code));
                        break;
                    }
                    default:
                        return fail("invalid escape sequence", pos);
                }
                continue;
            }
            if (ch < 0x20) {
                return fail("control character in string", pos);
            }
            out.push_back(text[pos]);
            ++pos;
        }
        return fail("unterminated string", pos);
    }

    static void appendUtf8(std::string& out, unsigned int code) {
        if (code < 0x80u) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800u) {
            out.push_back(static_cast<char>(0xC0u | (code >> 6)));
            out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xE0u | (code >> 12)));
            out.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
        }
    }

    bool parseNumber(Value& out) {
        const std::size_t start = pos;
        if (pos < text.size() && (text[pos] == '-' || text[pos] == '+')) {
            ++pos;
        }
        bool digits = false;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            ++pos;
            digits = true;
        }
        if (pos < text.size() && text[pos] == '.') {
            ++pos;
            while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
                ++pos;
                digits = true;
            }
        }
        if (!digits) {
            return fail("invalid number", start);
        }
        if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
            ++pos;
            if (pos < text.size() && (text[pos] == '-' || text[pos] == '+')) {
                ++pos;
            }
            while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
                ++pos;
            }
        }
        const std::string token = text.substr(start, pos - start);
        out = Value::makeNumber(std::strtod(token.c_str(), nullptr));
        return true;
    }
};

bool Parser::parseValue(Value& out) {
    skipWhitespace();
    if (pos >= text.size()) {
        return fail("unexpected end of input", pos);
    }
    const char ch = text[pos];
    switch (ch) {
        case '{':
            return parseObject(out);
        case '[':
            return parseArray(out);
        case '"': {
            std::string s;
            if (!parseString(s)) {
                return false;
            }
            out = Value::makeString(std::move(s));
            return true;
        }
        case 't':
            if (text.compare(pos, 4, "true") == 0) {
                pos += 4;
                out = Value::makeBool(true);
                return true;
            }
            return fail("invalid literal", pos);
        case 'f':
            if (text.compare(pos, 5, "false") == 0) {
                pos += 5;
                out = Value::makeBool(false);
                return true;
            }
            return fail("invalid literal", pos);
        case 'n':
            if (text.compare(pos, 4, "null") == 0) {
                pos += 4;
                out = Value::makeNull();
                return true;
            }
            return fail("invalid literal", pos);
        default:
            return parseNumber(out);
    }
}

std::size_t bomLength(const std::string& text) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        return 3;
    }
    return 0;
}

}  // namespace

bool parse(const std::string& text, Value& out, ParseError& error) {
    Parser parser(text);
    if (!parser.parseValue(out)) {
        error = parser.error;
        return false;
    }
    parser.skipWhitespace();
    if (parser.pos != text.size()) {
        parser.fail("trailing content after JSON value", parser.pos);
        error = parser.error;
        return false;
    }
    error = ParseError();
    return true;
}

bool parseWithBom(const std::string& text, Value& out, ParseError& error) {
    const std::size_t skip = bomLength(text);
    return parse(text.substr(skip), out, error);
}

}  // namespace json
}  // namespace fc
