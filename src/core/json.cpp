#include "core/json.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>

namespace lite {
namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

void appendUtf8(std::string& out, unsigned int cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    bool run(Json& value, std::string& error) {
        skipSpace();
        if (!parseValue(value, 0)) {
            error = error_;
            return false;
        }
        skipSpace();
        if (pos_ != text_.size()) {
            fail("unexpected trailing characters");
            error = error_;
            return false;
        }
        return true;
    }

private:
    bool fail(const std::string& message) {
        if (error_.empty()) {
            int line = 1, col = 1;
            for (std::size_t i = 0; i < pos_ && i < text_.size(); ++i) {
                if (text_[i] == '\n') {
                    line++;
                    col = 1;
                } else if (text_[i] != '\r') {
                    col++;
                }
            }
            error_ = message + " at line " + std::to_string(line) + ", col " + std::to_string(col);
        }
        return false;
    }

    void skipSpace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++pos_;
            else break;
        }
    }

    bool parseValue(Json& value, int depth) {
        if (depth > 256) return fail("nesting is too deep");
        skipSpace();
        if (pos_ >= text_.size()) return fail("unexpected end of input");
        const char c = text_[pos_];
        switch (c) {
        case 'n':
            if (!literal("null")) return false;
            value = Json(nullptr);
            return true;
        case 't':
            if (!literal("true")) return false;
            value = Json(true);
            return true;
        case 'f':
            if (!literal("false")) return false;
            value = Json(false);
            return true;
        case '"': {
            std::string text;
            if (!parseString(text)) return false;
            value = Json(std::move(text));
            return true;
        }
        case '[':
            return parseArray(value, depth);
        case '{':
            return parseObject(value, depth);
        default:
            if (c == '-' || isDigit(c)) return parseNumber(value);
            return fail("unexpected character");
        }
    }

    bool literal(const char* expected) {
        std::size_t length = 0;
        while (expected[length] != '\0') ++length;
        if (text_.substr(pos_, length) != std::string_view(expected, length)) return fail("invalid literal");
        pos_ += length;
        return true;
    }

    bool parseString(std::string& out) {
        if (text_[pos_] != '"') return fail("expected string");
        ++pos_;
        while (pos_ < text_.size()) {
            const unsigned char c = static_cast<unsigned char>(text_[pos_++]);
            if (c == '"') return true;
            if (c < 0x20) return fail("unescaped control character in string");
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                continue;
            }
            if (pos_ >= text_.size()) return fail("unfinished escape sequence");
            const char esc = text_[pos_++];
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
                unsigned int cp = 0;
                if (!parseHex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (pos_ + 1 >= text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') {
                        return fail("unpaired high surrogate");
                    }
                    pos_ += 2;
                    unsigned int low = 0;
                    if (!parseHex4(low)) return false;
                    if (low < 0xDC00 || low > 0xDFFF) return fail("invalid low surrogate");
                    cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return fail("unpaired low surrogate");
                }
                appendUtf8(out, cp);
                break;
            }
            default:
                return fail("invalid escape sequence");
            }
        }
        return fail("unterminated string");
    }

    bool parseHex4(unsigned int& value) {
        if (pos_ + 4 > text_.size()) return fail("short unicode escape");
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const int v = hexValue(text_[pos_++]);
            if (v < 0) return fail("invalid unicode escape");
            value = (value << 4) | static_cast<unsigned int>(v);
        }
        return true;
    }

    bool parseNumber(Json& value) {
        const std::size_t start = pos_;
        if (text_[pos_] == '-') ++pos_;
        if (pos_ >= text_.size()) return fail("incomplete number");
        if (text_[pos_] == '0') {
            ++pos_;
            if (pos_ < text_.size() && isDigit(text_[pos_])) return fail("leading zero in number");
        } else if (text_[pos_] >= '1' && text_[pos_] <= '9') {
            while (pos_ < text_.size() && isDigit(text_[pos_])) ++pos_;
        } else {
            return fail("invalid number");
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            if (pos_ >= text_.size() || !isDigit(text_[pos_])) return fail("missing fraction digits");
            while (pos_ < text_.size() && isDigit(text_[pos_])) ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            if (pos_ >= text_.size() || !isDigit(text_[pos_])) return fail("missing exponent digits");
            while (pos_ < text_.size() && isDigit(text_[pos_])) ++pos_;
        }
        value = Json::number(std::string(text_.substr(start, pos_ - start)));
        return true;
    }

    bool parseArray(Json& value, int depth) {
        ++pos_; // [
        value = Json::array();
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            Json item;
            if (!parseValue(item, depth + 1)) return false;
            value.items().push_back(std::move(item));
            skipSpace();
            if (pos_ >= text_.size()) return fail("unterminated array");
            if (text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            if (text_[pos_] != ',') return fail("expected ',' in array");
            ++pos_;
            skipSpace();
        }
    }

    bool parseObject(Json& value, int depth) {
        ++pos_; // {
        value = Json::object();
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            skipSpace();
            if (pos_ >= text_.size() || text_[pos_] != '"') return fail("expected object key");
            std::string key;
            if (!parseString(key)) return false;
            skipSpace();
            if (pos_ >= text_.size() || text_[pos_] != ':') return fail("expected ':' after object key");
            ++pos_;
            Json member;
            if (!parseValue(member, depth + 1)) return false;
            value.members().emplace_back(std::move(key), std::move(member));
            skipSpace();
            if (pos_ >= text_.size()) return fail("unterminated object");
            if (text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            if (text_[pos_] != ',') return fail("expected ',' in object");
            ++pos_;
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    std::string error_;
};

void escapeString(std::string& out, const std::string& value) {
    out.push_back('"');
    for (const unsigned char c : value) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04X", static_cast<unsigned int>(c));
                out += buffer;
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    out.push_back('"');
}

} // namespace

Json::Json(double value) : type_(Type::Number) {
    if (!std::isfinite(value)) {
        text_ = "null";
        type_ = Type::Null;
        return;
    }
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    text_ = buffer;
}

Json Json::number(std::string raw) {
    Json value;
    value.type_ = Type::Number;
    value.text_ = std::move(raw);
    return value;
}

Json Json::array() {
    Json value;
    value.type_ = Type::Array;
    return value;
}

Json Json::object() {
    Json value;
    value.type_ = Type::Object;
    return value;
}

Json Json::parse(std::string_view text, std::string* error) {
    Json value;
    std::string localError;
    Parser parser(text);
    if (!parser.run(value, localError)) {
        if (error) *error = localError;
        return Json(nullptr);
    }
    if (error) error->clear();
    return value;
}

Json Json::parseFile(const std::string& utf8Path, std::string* error) {
    std::ifstream input(utf8Path, std::ios::binary);
    if (!input) {
        if (error) *error = "cannot open file";
        return Json(nullptr);
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return parse(buffer.str(), error);
}

std::int64_t Json::asInt64(std::int64_t fallback) const {
    if (type_ != Type::Number) return fallback;
    errno = 0;
    char* end = nullptr;
    const long long value = std::strtoll(text_.c_str(), &end, 10);
    if (errno == ERANGE || end == text_.c_str() || (end && *end != '\0')) return fallback;
    return static_cast<std::int64_t>(value);
}

double Json::asDouble(double fallback) const {
    if (type_ != Type::Number) return fallback;
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(text_.c_str(), &end);
    if (end == text_.c_str() || (end && *end != '\0')) return fallback;
    return value;
}

Json& Json::push(Json value) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        array_.clear();
    }
    array_.push_back(std::move(value));
    return array_.back();
}

Json& Json::set(std::string key, Json value) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        object_.clear();
    }
    for (auto& member : object_) {
        if (member.first == key) {
            member.second = std::move(value);
            return member.second;
        }
    }
    object_.emplace_back(std::move(key), std::move(value));
    return object_.back().second;
}

Json& Json::operator[](const std::string& key) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        object_.clear();
    }
    for (auto& member : object_) {
        if (member.first == key) return member.second;
    }
    object_.emplace_back(key, Json(nullptr));
    return object_.back().second;
}

const Json* Json::find(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& member : object_) {
        if (member.first == key) return &member.second;
    }
    return nullptr;
}

Json* Json::find(const std::string& key) {
    if (type_ != Type::Object) return nullptr;
    for (auto& member : object_) {
        if (member.first == key) return &member.second;
    }
    return nullptr;
}

std::string Json::getString(const std::string& key, const std::string& fallback) const {
    const Json* value = find(key);
    return value ? value->asString(fallback) : fallback;
}

std::int64_t Json::getInt(const std::string& key, std::int64_t fallback) const {
    const Json* value = find(key);
    return value ? value->asInt64(fallback) : fallback;
}

bool Json::getBool(const std::string& key, bool fallback) const {
    const Json* value = find(key);
    return value ? value->asBool(fallback) : fallback;
}

Json Json::getArray(const std::string& key) const {
    const Json* value = find(key);
    return value && value->isArray() ? *value : Json::array();
}

Json Json::getObject(const std::string& key) const {
    const Json* value = find(key);
    return value && value->isObject() ? *value : Json::object();
}

std::string Json::dump(int indent) const {
    std::string out;
    dumpTo(out, indent, 0);
    return out;
}

void Json::dumpTo(std::string& out, int indent, int depth) const {
    switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += bool_ ? "true" : "false"; break;
    case Type::Number: out += text_.empty() ? "0" : text_; break;
    case Type::String: escapeString(out, text_); break;
    case Type::Array: {
        out.push_back('[');
        const bool pretty = indent >= 0 && !array_.empty();
        for (std::size_t i = 0; i < array_.size(); ++i) {
            if (i != 0) out.push_back(',');
            if (pretty) {
                out.push_back('\n');
                out.append(static_cast<std::size_t>(indent) * static_cast<std::size_t>(depth + 1), ' ');
            }
            array_[i].dumpTo(out, indent, depth + 1);
        }
        if (pretty) {
            out.push_back('\n');
            out.append(static_cast<std::size_t>(indent) * static_cast<std::size_t>(depth), ' ');
        }
        out.push_back(']');
        break;
    }
    case Type::Object: {
        out.push_back('{');
        const bool pretty = indent >= 0 && !object_.empty();
        for (std::size_t i = 0; i < object_.size(); ++i) {
            if (i != 0) out.push_back(',');
            if (pretty) {
                out.push_back('\n');
                out.append(static_cast<std::size_t>(indent) * static_cast<std::size_t>(depth + 1), ' ');
            }
            escapeString(out, object_[i].first);
            out.push_back(':');
            if (indent >= 0) out.push_back(' ');
            object_[i].second.dumpTo(out, indent, depth + 1);
        }
        if (pretty) {
            out.push_back('\n');
            out.append(static_cast<std::size_t>(indent) * static_cast<std::size_t>(depth), ' ');
        }
        out.push_back('}');
        break;
    }
    }
}

bool Json::saveFile(const std::string& utf8Path, int indent, std::string* error) const {
    std::ofstream output(utf8Path, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error) *error = "cannot create file";
        return false;
    }
    const std::string data = dump(indent) + "\n";
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!output) {
        if (error) *error = "cannot write file";
        return false;
    }
    return true;
}

bool Json::operator==(const Json& other) const {
    if (type_ != other.type_) return false;
    switch (type_) {
    case Type::Null: return true;
    case Type::Bool: return bool_ == other.bool_;
    case Type::Number: return text_ == other.text_ || asDouble() == other.asDouble();
    case Type::String: return text_ == other.text_;
    case Type::Array: return array_ == other.array_;
    case Type::Object: {
        if (object_.size() != other.object_.size()) return false;
        for (std::size_t i = 0; i < object_.size(); ++i) {
            if (object_[i].first != other.object_[i].first || !(object_[i].second == other.object_[i].second)) return false;
        }
        return true;
    }
    }
    return false;
}

std::string Json::format(const std::string& input, int indent) {
    if (input.empty()) return input;
    std::size_t start = input.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return input;
    if (input[start] != '{' && input[start] != '[') return input;

    std::string err;
    Json j = Json::parse(input, &err);
    if (err.empty() && (j.isObject() || j.isArray())) {
        return j.dump(indent);
    }
    return input;
}

std::string Json::minify(const std::string& input) {
    if (input.empty()) return input;
    std::size_t start = input.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return input;
    if (input[start] != '{' && input[start] != '[') return input;

    std::string err;
    Json j = Json::parse(input, &err);
    if (err.empty() && (j.isObject() || j.isArray())) {
        return j.dump(-1);
    }
    return input;
}

bool Json::validate(const std::string& input, std::string* error, int* errLine, int* errCol) {
    if (input.empty()) return true;
    std::size_t start = input.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return true;
    if (input[start] != '{' && input[start] != '[') {
        if (error) *error = "JSON root must be an object or array";
        if (errLine) *errLine = 1;
        if (errCol) *errCol = static_cast<int>(start + 1);
        return false;
    }
    std::string err;
    Json j = Json::parse(input, &err);
    if (!err.empty()) {
        if (error) *error = err;
        if (errLine || errCol) {
            std::size_t lPos = err.find("line ");
            if (lPos != std::string::npos && errLine) {
                *errLine = std::atoi(err.c_str() + lPos + 5);
            }
            std::size_t cPos = err.find("col ");
            if (cPos != std::string::npos && errCol) {
                *errCol = std::atoi(err.c_str() + cPos + 4);
            }
        }
        return false;
    }
    return true;
}

} // namespace lite
