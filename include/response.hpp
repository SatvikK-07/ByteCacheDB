#pragma once

#include <optional>
#include <string>
#include <vector>

namespace bytecachedb::response {

std::string simple_string(const std::string& value);
std::string error(const std::string& message);
std::string integer(long long value);
std::string bulk_string(const std::string& value);
std::string null_bulk_string();
std::string array(const std::vector<std::string>& values);
std::string array(const std::vector<std::optional<std::string>>& values);

} // namespace bytecachedb::response
