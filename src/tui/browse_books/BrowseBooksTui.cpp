#include "tui/browse_books/BrowseBooksTui.hpp"
#include "services/LibraryBrowseService.hpp"

#include "db/Database.hpp"
#include "db/Migrations.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <optional>
#include <string>
#include <vector>

namespace buch::tui::browse_books {
namespace {

using namespace ftxui;

std::string join(const std::vector<std::string>& values) {
    std::string result;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            result += ", ";
        }
        result += values[index];
    }
    return result;
}

std::string optional_text(const std::optional<std::string>& value) {
    return value.has_value() && !value->empty() ? *value : "-";
}

std::string optional_number(const std::optional<int>& value) {
    return value.has_value() ? std::to_string(*value) : "-";
}

std::string work_label(const services::WorkListItem& work) {
    auto label = work.title;
    const auto authors = join(work.authors);
    if (!authors.empty()) {
        label += " (" + authors + ")";
    }
    label += " - " + std::to_string(work.edition_count) + " Ed., "
        + std::to_string(work.copy_count) + " Ex.";
    return label;
}

std::string edition_label(const services::EditionListItem& edition) {
    auto label = edition.title + " [" + edition.isbn + "]";
    if (edition.publication_year.has_value()) {
        label += " (" + std::to_string(*edition.publication_year) + ")";
    }
    label += " - " + std::to_string(edition.copy_count) + " Ex.";
    return label;
}

} // namespace

Result run(const std::filesystem::path& database_path) {
    db::Database database(database_path);
    db::apply_migrations(database);
    services::LibraryBrowseService browse_service{database};

    std::string search_query;
    int selected_work = 0;
    int selected_edition = 0;

    std::vector<services::WorkListItem> works;
    std::vector<std::string> work_options;

    std::vector<services::EditionListItem> editions;
    std::vector<std::string> edition_options;

    std::vector<services::CopyListItem> copies;
    std::string status;

    auto reload_copies = [&] {
        copies.clear();

        if (selected_edition < 0 || selected_edition >= static_cast<int>(editions.size())) {
            return;
        }

        copies = browse_service.list_copies_for_edition(editions[selected_edition].isbn);
    };

    auto reload_editions = [&] {
        editions.clear();
        edition_options.clear();
        selected_edition = 0;

        if (selected_work < 0 || selected_work >= static_cast<int>(works.size())) {
            copies.clear();
            return;
        }

        editions = browse_service.list_editions_for_work(works[selected_work].id);
        for (const auto& edition : editions) {
            edition_options.push_back(edition_label(edition));
        }

        reload_copies();
    };

    auto reload_works = [&] {
        works = browse_service.list_works(search_query);
        work_options.clear();
        selected_work = 0;

        for (const auto& work : works) {
            work_options.push_back(work_label(work));
        }

        reload_editions();
        status = std::to_string(works.size()) + " Werke geladen.";
    };

    reload_works();

    auto screen = ScreenInteractive::TerminalOutput();
    auto result = Result::BackToMainMenu;
    auto exit = screen.ExitLoopClosure();

    auto search_input = Input(&search_query, "Titel oder Autor");
    auto search_button = Button("Suchen", reload_works);
    auto works_menu = Menu(&work_options, &selected_work);
    auto editions_menu = Menu(&edition_options, &selected_edition);

    auto works_component = CatchEvent(works_menu, [&](Event event) {
        const int previous = selected_work;
        const bool handled = works_menu->OnEvent(event);
        if (selected_work != previous) {
            reload_editions();
        }
        return handled;
    });

    auto editions_component = CatchEvent(editions_menu, [&](Event event) {
        const int previous = selected_edition;
        const bool handled = editions_menu->OnEvent(event);
        if (selected_edition != previous) {
            reload_copies();
        }
        return handled;
    });

    auto back_button = Button("Zurück", [&] {
        result = Result::BackToMainMenu;
        exit();
    });

    auto quit_button = Button("Beenden", [&] {
        result = Result::Quit;
        exit();
    });

    auto layout = Container::Vertical({
        Container::Horizontal({
            search_input,
            search_button,
        }),
        Container::Horizontal({
            works_component,
            editions_component,
        }),
        Container::Horizontal({
            back_button,
            quit_button,
        }),
    });

    auto renderer = Renderer(layout, [&] {
        const bool has_work = selected_work >= 0 && selected_work < static_cast<int>(works.size());
        const bool has_edition = selected_edition >= 0 && selected_edition < static_cast<int>(editions.size());

        Element work_details = text("Kein Werk ausgewählt") | dim;
        if (has_work) {
            const auto& work = works[selected_work];
            work_details = vbox({
                text(work.title) | bold,
                text("Autoren: " + join(work.authors)),
                text("Erstveröffentlichung: " + optional_number(work.first_published_year)),
                text("Editionen: " + std::to_string(work.edition_count)),
                text("Exemplare: " + std::to_string(work.copy_count)),
            });
        }

        Element edition_details = text("Keine Edition ausgewählt") | dim;
        if (has_edition) {
            const auto& edition = editions[selected_edition];
            edition_details = vbox({
                text(edition.title) | bold,
                text("ISBN: " + edition.isbn),
                text("Sprache: " + edition.language_code),
                text("Verlag: " + optional_text(edition.publisher)),
                text("Jahr: " + optional_number(edition.publication_year)),
                text("Edition: " + optional_text(edition.edition_name)),
                text("Format: " + optional_text(edition.format)),
                text("Seiten: " + optional_number(edition.page_count)),
            });
        }

        std::vector<Element> copy_lines;
        if (copies.empty()) {
            copy_lines.push_back(text("Keine Exemplare.") | dim);
        } else {
            for (const auto& copy : copies) {
                copy_lines.push_back(text(
                    optional_text(copy.location_path)
                    + " | Position " + optional_number(copy.position_in_location)
                    + " | Zustand " + optional_text(copy.condition)
                    + " | Geliehen von " + optional_text(copy.borrowed_from)
                    + " | Verliehen an " + optional_text(copy.lent_to)));
            }
        }

        return vbox({
            text("Buchhaltung - Werke browsen") | bold | center,
            separator(),
            hbox({
                text("Suche") | size(WIDTH, EQUAL, 8),
                search_input->Render() | flex,
                text(" "),
                search_button->Render(),
            }),
            separator(),
            hbox({
                vbox({
                    text("Werke") | bold,
                    works_component->Render() | border | flex,
                }) | size(WIDTH, EQUAL, 45),
                separator(),
                vbox({
                    text("Werk") | bold,
                    work_details | border,
                    text("Editionen") | bold,
                    editions_component->Render() | border | flex,
                }) | size(WIDTH, EQUAL, 45),
                separator(),
                vbox({
                    text("Edition") | bold,
                    edition_details | border,
                    text("Exemplare") | bold,
                    vbox(copy_lines) | border | flex,
                }) | flex,
            }) | flex,
            separator(),
            hbox({
                back_button->Render(),
                text(" "),
                quit_button->Render(),
                text("  "),
                text(status) | dim,
            }),
        }) | border;
    });

    screen.Loop(renderer);
    return result;
}

} // namespace buch::tui::browse_books
