#include "fc/types.h"

#include <cmath>
#include <cstdio>

namespace fc {

std::uint64_t fnv1a64(const std::string& text) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : text) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::uint64_t fnv1a64Update(std::uint64_t hash, const std::string& text) {
    for (unsigned char ch : text) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string formatFixed3(double value) {
    char buffer[64];
    if (value != value) {  // NaN
        return "0.000";
    }
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    std::string out(buffer);
    if (out == "-0.000") {
        out = "0.000";
    }
    return out;
}

std::string formatFixed4(double value) {
    char buffer[64];
    if (value != value) {  // NaN
        return "0.0000";
    }
    std::snprintf(buffer, sizeof(buffer), "%.4f", value);
    std::string out(buffer);
    if (out == "-0.0000") {
        out = "0.0000";
    }
    return out;
}

std::string jsonQuote(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (unsigned char ch : text) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned int>(ch));
                    out += buffer;
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    out.push_back('"');
    return out;
}

}  // namespace fc
