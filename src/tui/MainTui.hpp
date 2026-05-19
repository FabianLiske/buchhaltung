#pragma once

namespace buch::tui {

enum class MainMenuAction {
    Quit,
    AddBook,
};

MainMenuAction run_main_tui();

} // namespace buch::tui
