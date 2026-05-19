#pragma once

#include <filesystem>
#include <string>

namespace buch::tui::add_book {

enum class Result {
    BackToMainMenu,
    Quit,
};

Result run(const std::string& google_books_api_key, const std::filesystem::path& database_path);

} // namespace buch::tui::add_book
