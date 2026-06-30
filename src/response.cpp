#include "response.hpp"

#include <sstream>

namespace bytecachedb::response {

namespace {
constexpr const char* kCrlf = "\r\n";
}

std::string simple_string(const std::string& value) {
    return "+" + value + kCrlf;
}

std::string error(const std::string& message) {
    return "-ERR " + message + kCrlf;
}

std::string integer(long long value) {
    return ":" + std::to_string(value) + kCrlf;
}

std::string bulk_string(const std::string& value) {
    return "$" + std::to_string(value.size()) + kCrlf + value + kCrlf;
}

std::string null_bulk_string() {
    return "$-1\r\n";
}

std::string array(const std::vector<std::string>& values) {
    std::ostringstream out;
    out << "*" << values.size() << kCrlf;
    for (const auto& value : values) {
        out << bulk_string(value);
    }
    return out.str();
}

std::string array(const std::vector<std::optional<std::string>>& values) {
    std::ostringstream out;
    out << "*" << values.size() << kCrlf;
    for (const auto& value : values) {
        if (value.has_value()) {
            out << bulk_string(*value);
        } else {
            out << null_bulk_string();
        }
    }
    return out.str();
}

} // namespace bytecachedb::response
