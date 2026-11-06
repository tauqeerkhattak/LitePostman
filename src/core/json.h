#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lite {

// A deliberately small JSON DOM. Numbers keep their original spelling so that
// large integers and high-precision values survive a read/write round trip.
class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Array = std::vector<Json>;
    using Member = std::pair<std::string, Json>;
    using Object = std::vector<Member>;

    Json() = default;
    Json(std::nullptr_t) : type_(Type::Null) {}
    Json(bool value) : type_(Type::Bool), bool_(value) {}
    Json(int value) : type_(Type::Number), text_(std::to_string(value)) {}
    Json(std::int64_t value) : type_(Type::Number), text_(std::to_string(value)) {}
    Json(std::uint64_t value) : type_(Type::Number), text_(std::to_string(value)) {}
    Json(double value);
    Json(const char* value) : type_(Type::String), text_(value ? value : "") {}
    Json(std::string value) : type_(Type::String), text_(std::move(value)) {}
    Json(std::string_view value) : type_(Type::String), text_(value) {}

    static Json number(std::string raw);
    static Json array();
    static Json object();
    static Json parse(std::string_view text, std::string* error = nullptr);
    static Json parseFile(const std::string& utf8Path, std::string* error = nullptr);
    static std::string format(const std::string& input, int indent = 2);
    static std::string minify(const std::string& input);
    static bool validate(const std::string& input, std::string* error = nullptr, int* errLine = nullptr, int* errCol = nullptr);

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool fallback = false) const { return type_ == Type::Bool ? bool_ : fallback; }
    std::string asString(std::string fallback = {}) const { return type_ == Type::String || type_ == Type::Number ? text_ : std::move(fallback); }
    std::int64_t asInt64(std::int64_t fallback = 0) const;
    double asDouble(double fallback = 0.0) const;
    const std::string& rawNumber() const { return text_; }

    std::size_t size() const { return type_ == Type::Array ? array_.size() : (type_ == Type::Object ? object_.size() : 0); }
    bool empty() const { return size() == 0; }
    const Array& items() const { return array_; }
    Array& items() { return array_; }
    const Object& members() const { return object_; }
    Object& members() { return object_; }

    Json& push(Json value);
    Json& set(std::string key, Json value);
    Json& operator[](const std::string& key);
    const Json* find(const std::string& key) const;
    Json* find(const std::string& key);
    Json& operator[](std::size_t index) { return array_[index]; }
    const Json& operator[](std::size_t index) const { return array_[index]; }

    std::string getString(const std::string& key, const std::string& fallback = {}) const;
    std::int64_t getInt(const std::string& key, std::int64_t fallback = 0) const;
    bool getBool(const std::string& key, bool fallback = false) const;
    Json getArray(const std::string& key) const;
    Json getObject(const std::string& key) const;

    std::string dump(int indent = -1) const;
    bool saveFile(const std::string& utf8Path, int indent = 2, std::string* error = nullptr) const;

    bool operator==(const Json& other) const;

private:
    void dumpTo(std::string& out, int indent, int depth) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    std::string text_;
    Array array_;
    Object object_;
};

} // namespace lite
