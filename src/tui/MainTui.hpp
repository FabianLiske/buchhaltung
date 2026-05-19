#pragma once

namespace buch::tui {

enum class MainMenuAction {
    Quit,
    AddBook,
    BrowseBooks,
};

MainMenuAction run_main_tui();

} // namespace buch::tui
