#pragma once

#include <filesystem>
#include <string>

namespace buch::tui::add_book {

int run(const std::string& google_books_api_key, const std::filesystem::path& database_path);

} // namespace buch::tui::add_book
