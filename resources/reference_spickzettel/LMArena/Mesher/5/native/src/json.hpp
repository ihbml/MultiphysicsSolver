// Minimal dependency-free JSON reader/writer for the mesher core.
// Deliberately small: we only need to ingest a spec and emit a manifest.
#pragma once

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mj {

struct Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;

enum class Type { Null, Bool, Number, String, Arr, Obj };

struct Value {
  Type type = Type::Null;
  bool b = false;
  double num = 0.0;
  std::string str;
  std::shared_ptr<Array> arr;
  std::shared_ptr<Object> obj;

  static Value null() { return Value(); }
  static Value boolean(bool v) {
    Value r;
    r.type = Type::Bool;
    r.b = v;
    return r;
  }
  static Value number(double v) {
    Value r;
    r.type = Type::Number;
    r.num = v;
    return r;
  }
  static Value string(std::string v) {
    Value r;
    r.type = Type::String;
    r.str = std::move(v);
    return r;
  }
  static Value array() {
    Value r;
    r.type = Type::Arr;
    r.arr = std::make_shared<Array>();
    return r;
  }
  static Value object() {
    Value r;
    r.type = Type::Obj;
    r.obj = std::make_shared<Object>();
    return r;
  }

  bool isNull() const { return type == Type::Null; }
  bool isObj() const { return type == Type::Obj; }
  bool isArr() const { return type == Type::Arr; }
  size_t size() const {
    if (type == Type::Arr) return arr ? arr->size() : 0;
    if (type == Type::Obj) return obj ? obj->size() : 0;
    return 0;
  }

  const Value& at(const std::string& key) const {
    static const Value nullValue;
    if (type != Type::Obj || !obj) return nullValue;
    auto it = obj->find(key);
    return it == obj->end() ? nullValue : it->second;
  }
  const Value& operator[](const std::string& key) const { return at(key); }
  const Value& at(size_t i) const {
    static const Value nullValue;
    if (type != Type::Arr || !arr || i >= arr->size()) return nullValue;
    return (*arr)[i];
  }
  const Value& operator[](size_t i) const { return at(i); }

  double asDouble(double def = 0.0) const {
    if (type == Type::Number) return num;
    if (type == Type::Bool) return b ? 1.0 : 0.0;
    if (type == Type::String) {
      try {
        return std::stod(str);
      } catch (...) {
        return def;
      }
    }
    return def;
  }
  int asInt(int def = 0) const {
    if (type == Type::Number) return static_cast<int>(num);
    if (type == Type::Bool) return b ? 1 : 0;
    return def;
  }
  bool asBool(bool def = false) const {
    if (type == Type::Bool) return b;
    if (type == Type::Number) return num != 0.0;
    if (type == Type::String) return str == "true" || str == "1" || str == "yes";
    return def;
  }
  std::string asString(const std::string& def = "") const {
    if (type == Type::String) return str;
    if (type == Type::Number) {
      std::ostringstream os;
      os << num;
      return os.str();
    }
    return def;
  }
  const Array* asArray() const { return (type == Type::Arr && arr) ? arr.get() : nullptr; }
  const Object* asObject() const { return (type == Type::Obj && obj) ? obj.get() : nullptr; }
  // Convenience: [x, y]
  double vecElem(size_t i, double def = 0.0) const { return at(i).asDouble(def); }
};

class Parser {
 public:
  explicit Parser(const std::string& text) : s_(text) {}

  Value parse() {
    skipWs();
    Value v = parseValue();
    skipWs();
    return v;
  }

 private:
  const std::string& s_;
  size_t i_ = 0;

  [[noreturn]] void fail(const std::string& msg) const {
    throw std::runtime_error("JSON parse error at offset " + std::to_string(i_) + ": " + msg);
  }

  void skipWs() {
    while (i_ < s_.size()) {
      char c = s_[i_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++i_;
      } else if (c == '/' && i_ + 1 < s_.size() && s_[i_ + 1] == '/') {
        while (i_ < s_.size() && s_[i_] != '\n') ++i_;
      } else {
        break;
      }
    }
  }

  Value parseValue() {
    skipWs();
    if (i_ >= s_.size()) fail("unexpected end of input");
    char c = s_[i_];
    if (c == '{') return parseObject();
    if (c == '[') return parseArray();
    if (c == '"') return Value::string(parseString());
    if (c == 't' || c == 'f') return parseBool();
    if (c == 'n') return parseNull();
    return parseNumber();
  }

  Value parseObject() {
    Value v = Value::object();
    ++i_;  // {
    skipWs();
    if (i_ < s_.size() && s_[i_] == '}') {
      ++i_;
      return v;
    }
    while (i_ < s_.size()) {
      skipWs();
      if (s_[i_] != '"') fail("expected string key");
      std::string key = parseString();
      skipWs();
      if (i_ >= s_.size() || s_[i_] != ':') fail("expected ':'");
      ++i_;
      Value child = parseValue();
      (*v.obj)[key] = std::move(child);
      skipWs();
      if (i_ < s_.size() && s_[i_] == ',') {
        ++i_;
        continue;
      }
      if (i_ < s_.size() && s_[i_] == '}') {
        ++i_;
        return v;
      }
      fail("expected ',' or '}'");
    }
    fail("unterminated object");
  }

  Value parseArray() {
    Value v = Value::array();
    ++i_;  // [
    skipWs();
    if (i_ < s_.size() && s_[i_] == ']') {
      ++i_;
      return v;
    }
    while (i_ < s_.size()) {
      v.arr->push_back(parseValue());
      skipWs();
      if (i_ < s_.size() && s_[i_] == ',') {
        ++i_;
        continue;
      }
      if (i_ < s_.size() && s_[i_] == ']') {
        ++i_;
        return v;
      }
      fail("expected ',' or ']'");
    }
    fail("unterminated array");
  }

  std::string parseString() {
    ++i_;  // opening quote
    std::string out;
    while (i_ < s_.size()) {
      char c = s_[i_++];
      if (c == '"') return out;
      if (c == '\\') {
        if (i_ >= s_.size()) break;
        char e = s_[i_++];
        switch (e) {
          case 'n': out.push_back('\n'); break;
          case 't': out.push_back('\t'); break;
          case 'r': out.push_back('\r'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case '/': out.push_back('/'); break;
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case 'u': {
            if (i_ + 4 > s_.size()) fail("bad \\u escape");
            unsigned cp = static_cast<unsigned>(std::stoul(s_.substr(i_, 4), nullptr, 16));
            i_ += 4;
            if (cp < 0x80) {
              out.push_back(static_cast<char>(cp));
            } else if (cp < 0x800) {
              out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
              out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
              out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
              out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
              out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            break;
          }
          default: out.push_back(e);
        }
      } else {
        out.push_back(c);
      }
    }
    fail("unterminated string");
  }

  Value parseBool() {
    if (s_.compare(i_, 4, "true") == 0) {
      i_ += 4;
      return Value::boolean(true);
    }
    if (s_.compare(i_, 5, "false") == 0) {
      i_ += 5;
      return Value::boolean(false);
    }
    fail("invalid literal");
  }

  Value parseNull() {
    if (s_.compare(i_, 4, "null") == 0) {
      i_ += 4;
      return Value::null();
    }
    fail("invalid literal");
  }

  Value parseNumber() {
    const char* start = s_.c_str() + i_;
    char* end = nullptr;
    double d = std::strtod(start, &end);
    if (end == start) fail("invalid number");
    i_ += static_cast<size_t>(end - start);
    return Value::number(d);
  }
};

inline Value parse(const std::string& text) {
  Parser p(text);
  return p.parse();
}

inline std::string escape(const std::string& in) {
  std::string out;
  out.reserve(in.size() + 8);
  for (char c : in) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(c);
        }
    }
  }
  return out;
}

// Streaming writer: fast enough for large preview arrays, no DOM overhead.
class Writer {
 public:
  explicit Writer(size_t reserveBytes = 1 << 16) { out_.reserve(reserveBytes); }

  // state per open container: 0 = first element, 1 = need comma, 2 = key just
  // written (the value that follows must not emit a separator)
  void objBegin() {
    comma();
    out_.push_back('{');
    depth_.push_back(0);
  }
  void objEnd() {
    depth_.pop_back();
    out_.push_back('}');
  }
  void arrBegin() {
    comma();
    out_.push_back('[');
    depth_.push_back(0);
  }
  void arrEnd() {
    depth_.pop_back();
    out_.push_back(']');
  }
  void key(const std::string& k) {
    comma();
    out_.push_back('"');
    out_ += escape(k);
    out_.push_back('"');
    out_.push_back(':');
    depth_.back() = 2;
  }
  void number(double v) {
    comma();
    if (!std::isfinite(v)) v = 0.0;
    char buf[40];
    const auto r = std::to_chars(buf, buf + sizeof(buf), v);
    out_.append(buf, static_cast<size_t>(r.ptr - buf));
  }
  void integer(long long v) {
    comma();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", v);
    out_ += buf;
  }
  void boolean(bool v) {
    comma();
    out_ += v ? "true" : "false";
  }
  void string(const std::string& v) {
    comma();
    out_.push_back('"');
    out_ += escape(v);
    out_.push_back('"');
  }
  void null() {
    comma();
    out_ += "null";
  }
  void raw(const std::string& json) {
    comma();
    out_ += json;
  }
  // Flat numeric array, compact formatting.
  void numberArray(const double* data, size_t n, int stride = 1, int offset = 0) {
    comma();
    out_.push_back('[');
    char buf[40];
    for (size_t i = 0; i < n; ++i) {
      double v = data[i * stride + offset];
      if (!std::isfinite(v)) v = 0.0;
      if (i) out_.push_back(',');
      const auto r = std::to_chars(buf, buf + sizeof(buf), v);
      out_.append(buf, static_cast<size_t>(r.ptr - buf));
    }
    out_.push_back(']');
  }
  void intArray(const int* data, size_t n, int stride = 1, int offset = 0) {
    comma();
    out_.push_back('[');
    char buf[32];
    for (size_t i = 0; i < n; ++i) {
      if (i) out_.push_back(',');
      std::snprintf(buf, sizeof(buf), "%d", data[i * stride + offset]);
      out_ += buf;
    }
    out_.push_back(']');
  }
  // Convenience helpers ---------------------------------------------------
  void kv(const std::string& k, double v) {
    key(k);
    number(v);
  }
  void kv(const std::string& k, long long v) {
    key(k);
    integer(v);
  }
  void kv(const std::string& k, int v) {
    key(k);
    integer(v);
  }
  void kv(const std::string& k, bool v) {
    key(k);
    boolean(v);
  }
  void kv(const std::string& k, const std::string& v) {
    key(k);
    string(v);
  }
  void kv(const std::string& k, const char* v) {
    key(k);
    string(std::string(v));
  }
  void kvRaw(const std::string& k, const std::string& json) {
    key(k);
    raw(json);
  }

  const std::string& str() const { return out_; }
  std::string take() { return std::move(out_); }

 private:
  void comma() {
    if (depth_.empty()) return;
    int& st = depth_.back();
    if (st == 2) {
      st = 1;
      return;
    }
    if (st == 1) out_.push_back(',');
    st = 1;
  }
  std::string out_;
  std::vector<int> depth_;
};

}  // namespace mj
