#pragma once

#include <filesystem>

namespace buch::tui::browse_books {

enum class Result {
    BackToMainMenu,
    Quit,
};

Result run(const std::filesystem::path& database_path);

} // namespace buch::tui::browse_books