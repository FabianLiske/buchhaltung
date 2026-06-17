#pragma once

#include <optional>
#include <string>

namespace buch::services {

std::string normalize_isbn(const std::string& value);
bool is_valid_isbn(const std::string& normalized);
std::optional<std::string> isbn_10_to_13(const std::string& isbn_10);
std::optional<std::string> isbn_13_to_10(const std::string& isbn_13);

} // namespace buch::services
