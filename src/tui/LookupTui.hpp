#pragma once

#include <filesystem>
#include <string>

namespace buch::tui {

int run_lookup_tui(const std::string& google_books_api_key, const std::filesystem::path& database_path);

} // namespace buch::tui
