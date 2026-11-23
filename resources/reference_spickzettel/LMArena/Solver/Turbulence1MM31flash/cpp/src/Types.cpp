// =====================================================================================
//  mt::Types.cpp -- dependency free JSON writer used by the verification harness.
// =====================================================================================
#include "mt/Types.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace mt {

std::string fmt(double v, int prec) {
    if (!std::isfinite(v)) return "null";
    std::ostringstream os;
    os << std::setprecision(prec) << v;
    return os.str();
}

Json Json::number(double v) {
    Json j;
    j.type_ = Type::Raw;
    j.raw_ = fmt(v, 12);
    return j;
}

Json Json::integer(long long v) {
    Json j;
    j.type_ = Type::Raw;
    j.raw_ = std::to_string(v);
    return j;
}

Json Json::boolean(bool v) {
    Json j;
    j.type_ = Type::Raw;
    j.raw_ = v ? "true" : "false";
    return j;
}

Json Json::string(const std::string& v) {
    Json j;
    j.type_ = Type::Raw;
    std::string out = "\"";
    for (char c : v) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    out += "\"";
    j.raw_ = out;
    return j;
}

Json& Json::set(const std::string& key, const Json& v) {
    type_ = Type::Object;
    for (auto& kv : members_) {
        if (kv.first == key) { kv.second = v; return *this; }
    }
    members_.emplace_back(key, v);
    return *this;
}

Json& Json::push(const Json& v) {
    type_ = Type::Array;
    elements_.push_back(v);
    return *this;
}

std::string Json::dump(int indent) const {
    const std::string pad(static_cast<std::size_t>(indent) * 2, ' ');
    const std::string pad2(static_cast<std::size_t>(indent + 1) * 2, ' ');
    switch (type_) {
        case Type::Raw:   return raw_;
        case Type::Array: {
            if (elements_.empty()) return "[]";
            std::string out = "[\n";
            for (std::size_t i = 0; i < elements_.size(); ++i) {
                out += pad2 + elements_[i].dump(indent + 1);
                if (i + 1 < elements_.size()) out += ",";
                out += "\n";
            }
            out += pad + "]";
            return out;
        }
        case Type::Object:
        default: {
            if (members_.empty()) return "{}";
            std::string out = "{\n";
            for (std::size_t i = 0; i < members_.size(); ++i) {
                out += pad2 + "\"" + members_[i].first + "\": " + members_[i].second.dump(indent + 1);
                if (i + 1 < members_.size()) out += ",";
                out += "\n";
            }
            out += pad + "}";
            return out;
        }
    }
}

} // namespace mt
