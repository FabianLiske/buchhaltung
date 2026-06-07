#include "tui/MainTui.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

namespace buch::tui {
namespace {

using namespace ftxui;

} // namespace

MainMenuAction run_main_tui() {
    auto screen = ScreenInteractive::Fullscreen();
    auto action = MainMenuAction::Quit;
    auto exit = screen.ExitLoopClosure();

    auto add_book_button = Button("Buch hinzufügen", [&] {
        action = MainMenuAction::AddBook;
        exit();
    });

    auto browse_books_button = Button("Werke browsen", [&] {
        action = MainMenuAction::BrowseBooks;
        exit();
    });

    auto quit_button = Button("Beenden", [&] {
        action = MainMenuAction::Quit;
        exit();
    });

    auto menu = Container::Vertical({
        add_book_button,
        browse_books_button,
        quit_button,
    });

    auto renderer = Renderer(menu, [&] {
        return vbox({
            text("Buchhaltung") | bold | center,
            separator(),
            vbox({
                filler(),
                text("Hauptmenü") | center,
                separator(),
                add_book_button->Render() | center,
                browse_books_button->Render() | center,
                quit_button->Render() | center,
                filler(),
            }) | flex,
            separator(),
        }) | border;
    });

    screen.Loop(renderer);
    return action;
}

} // namespace buch::tui
