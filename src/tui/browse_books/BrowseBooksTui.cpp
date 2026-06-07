#include "tui/browse_books/BrowseBooksTui.hpp"

#include "db/Database.hpp"
#include "db/Migrations.hpp"
#include "services/BookImportService.hpp"
#include "services/LibraryBrowseService.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace buch::tui::browse_books {
namespace {

using namespace ftxui;

enum class ScreenMode {
    View = 0,
    EditWork = 1,
    EditEdition = 2,
    EditCopy = 3,
    ConfirmDelete = 4,
};

enum class DeleteTarget {
    None,
    Work,
    Edition,
    Copy,
};

std::string join(const std::vector<std::string>& values) {
    std::ostringstream output;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            output << ", ";
        }
        output << values[index];
    }
    return output.str();
}

std::string optional_text(const std::optional<std::string>& value) {
    return value.has_value() && !value->empty() ? *value : "-";
}

std::string optional_input(const std::optional<std::string>& value) {
    return value.value_or("");
}

std::string optional_number(const std::optional<int>& value) {
    return value.has_value() ? std::to_string(*value) : "-";
}

std::string optional_number_input(const std::optional<int>& value) {
    return value.has_value() ? std::to_string(*value) : "";
}

std::optional<int> optional_int(std::string value) {
    if (const auto trimmed = services::optional_trimmed(std::move(value)); trimmed.has_value()) {
        int result = 0;
        const auto parse_result = std::from_chars(trimmed->data(), trimmed->data() + trimmed->size(), result);
        if (parse_result.ec != std::errc{} || parse_result.ptr != trimmed->data() + trimmed->size()) {
            throw std::runtime_error("Keine gültige Ganzzahl: " + *trimmed);
        }
        return result;
    }
    return std::nullopt;
}

Element labeled_input(std::string label, const Component& input) {
    return hbox({
        text(std::move(label)) | size(WIDTH, EQUAL, 20),
        input->Render() | flex,
    });
}

Element detail_line(std::string label, std::string value) {
    return hbox({
        text(std::move(label)) | size(WIDTH, EQUAL, 18),
        text(value.empty() ? "-" : value) | flex,
    });
}

std::string work_label(const services::WorkListItem& work) {
    auto label = work.title;
    const auto authors = join(work.authors);
    if (!authors.empty()) {
        label += " (" + authors + ")";
    }
    if (!work.series.empty()) {
        label += " | " + work.series;
    }
    label += " | " + std::to_string(work.edition_count) + " Ed., "
        + std::to_string(work.copy_count) + " Ex.";
    return label;
}

std::string edition_label(const services::EditionListItem& edition) {
    auto label = edition.title + " [" + edition.isbn + "]";
    if (edition.publication_year.has_value()) {
        label += " (" + std::to_string(*edition.publication_year) + ")";
    }
    label += " | " + std::to_string(edition.copy_count) + " Ex.";
    return label;
}

std::string copy_label(const services::CopyListItem& copy) {
    return optional_text(copy.location_path)
        + " | Pos. " + optional_number(copy.position_in_location)
        + " | Zustand " + optional_text(copy.condition)
        + " | von " + optional_text(copy.borrowed_from)
        + " | an " + optional_text(copy.lent_to);
}

} // namespace

Result run(const std::filesystem::path& database_path) {
    db::Database database(database_path);
    db::apply_migrations(database);
    services::LibraryBrowseService browse_service{database};

    int mode_index = static_cast<int>(ScreenMode::View);
    int selected_work = 0;
    int selected_edition = 0;
    int selected_copy = 0;

    std::string text_filter;
    std::string author_filter;
    std::string series_filter;
    std::string status_filter;
    std::string status;
    DeleteTarget delete_target = DeleteTarget::None;
    std::string delete_id;
    std::string delete_label;

    std::vector<services::WorkListItem> works;
    std::vector<std::string> work_options;
    std::vector<services::EditionListItem> editions;
    std::vector<std::string> edition_options;
    std::vector<services::CopyListItem> copies;
    std::vector<std::string> copy_options;

    std::string work_title;
    std::string work_authors;
    std::string work_original_title;
    std::string work_original_language;
    std::string work_first_year;
    std::string work_description;
    std::string work_age_rating;
    std::string work_notes;

    std::string edition_title;
    std::string edition_subtitle;
    std::string edition_language;
    std::string edition_publisher;
    std::string edition_year;
    std::string edition_name;
    std::string edition_format;
    std::string edition_pages;
    std::string edition_cover_url;
    std::string edition_notes;

    std::string copy_location;
    std::string copy_position;
    std::string copy_condition;
    std::string copy_acquired_date;
    std::string copy_acquired_where;
    std::string copy_borrowed_from;
    std::string copy_lent_to;
    std::string copy_notes;

    auto selected_work_id = [&]() -> std::string {
        if (selected_work >= 0 && selected_work < static_cast<int>(works.size())) {
            return works[selected_work].id;
        }
        return "";
    };

    auto selected_edition_isbn = [&]() -> std::string {
        if (selected_edition >= 0 && selected_edition < static_cast<int>(editions.size())) {
            return editions[selected_edition].isbn;
        }
        return "";
    };

    auto selected_copy_id = [&]() -> std::string {
        if (selected_copy >= 0 && selected_copy < static_cast<int>(copies.size())) {
            return copies[selected_copy].id;
        }
        return "";
    };

    auto reload_copies = [&] {
        const auto previous_copy_id = selected_copy_id();
        copies.clear();
        copy_options.clear();
        selected_copy = 0;

        const auto isbn = selected_edition_isbn();
        if (isbn.empty()) {
            return;
        }

        copies = browse_service.list_copies_for_edition(isbn);
        for (const auto& copy : copies) {
            copy_options.push_back(copy_label(copy));
        }

        if (!previous_copy_id.empty()) {
            const auto found = std::find_if(copies.begin(), copies.end(), [&](const auto& copy) {
                return copy.id == previous_copy_id;
            });
            if (found != copies.end()) {
                selected_copy = static_cast<int>(std::distance(copies.begin(), found));
            }
        }
    };

    auto reload_editions = [&] {
        const auto previous_isbn = selected_edition_isbn();
        editions.clear();
        edition_options.clear();
        selected_edition = 0;

        const auto work_id = selected_work_id();
        if (work_id.empty()) {
            copies.clear();
            copy_options.clear();
            return;
        }

        editions = browse_service.list_editions_for_work(work_id);
        for (const auto& edition : editions) {
            edition_options.push_back(edition_label(edition));
        }

        if (!previous_isbn.empty()) {
            const auto found = std::find_if(editions.begin(), editions.end(), [&](const auto& edition) {
                return edition.isbn == previous_isbn;
            });
            if (found != editions.end()) {
                selected_edition = static_cast<int>(std::distance(editions.begin(), found));
            }
        }

        reload_copies();
    };

    auto reload_works = [&] {
        const auto previous_work_id = selected_work_id();
        works = browse_service.list_works(services::WorkFilters{
            .text = text_filter,
            .author = author_filter,
            .series = series_filter,
            .reading_status = status_filter,
        });
        work_options.clear();
        selected_work = 0;

        for (const auto& work : works) {
            work_options.push_back(work_label(work));
        }

        if (!previous_work_id.empty()) {
            const auto found = std::find_if(works.begin(), works.end(), [&](const auto& work) {
                return work.id == previous_work_id;
            });
            if (found != works.end()) {
                selected_work = static_cast<int>(std::distance(works.begin(), found));
            }
        }

        reload_editions();
        status = std::to_string(works.size()) + " Werke geladen.";
    };

    auto clear_filters = [&] {
        text_filter.clear();
        author_filter.clear();
        series_filter.clear();
        status_filter.clear();
        reload_works();
    };

    reload_works();

    auto screen = ScreenInteractive::Fullscreen();
    auto result = Result::BackToMainMenu;
    auto exit = screen.ExitLoopClosure();

    auto text_filter_input = Input(&text_filter, "Titel, ISBN oder Volltext");
    auto author_filter_input = Input(&author_filter, "Autor");
    auto series_filter_input = Input(&series_filter, "Serie");
    auto status_filter_input = Input(&status_filter, "Lesestatus");

    auto works_menu = Menu(&work_options, &selected_work);
    auto editions_menu = Menu(&edition_options, &selected_edition);
    auto copies_menu = Menu(&copy_options, &selected_copy);

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

    auto search_button = Button("Filtern", reload_works);
    auto clear_button = Button("Leeren", clear_filters);
    auto back_button = Button("Zurück", [&] {
        result = Result::BackToMainMenu;
        exit();
    });
    auto quit_button = Button("Beenden", [&] {
        result = Result::Quit;
        exit();
    });

    auto work_title_input = Input(&work_title, "Titel");
    auto work_authors_input = Input(&work_authors, "Autor, Autor");
    auto work_original_title_input = Input(&work_original_title, "Originaltitel");
    auto work_original_language_input = Input(&work_original_language, "Originalsprache");
    auto work_first_year_input = Input(&work_first_year, "Jahr");
    auto work_description_input = Input(&work_description, "Beschreibung");
    auto work_age_rating_input = Input(&work_age_rating, "Altersfreigabe");
    auto work_notes_input = Input(&work_notes, "Notizen");

    auto edition_title_input = Input(&edition_title, "Titel");
    auto edition_subtitle_input = Input(&edition_subtitle, "Untertitel");
    auto edition_language_input = Input(&edition_language, "de");
    auto edition_publisher_input = Input(&edition_publisher, "Verlag");
    auto edition_year_input = Input(&edition_year, "Jahr");
    auto edition_name_input = Input(&edition_name, "Ausgabe");
    auto edition_format_input = Input(&edition_format, "Format");
    auto edition_pages_input = Input(&edition_pages, "Seiten");
    auto edition_cover_url_input = Input(&edition_cover_url, "Cover-URL");
    auto edition_notes_input = Input(&edition_notes, "Notizen");

    auto copy_location_input = Input(&copy_location, "Wohnzimmer / Regal / Fach");
    auto copy_position_input = Input(&copy_position, "Position");
    auto copy_condition_input = Input(&copy_condition, "Zustand");
    auto copy_acquired_date_input = Input(&copy_acquired_date, "YYYY-MM-DD");
    auto copy_acquired_where_input = Input(&copy_acquired_where, "Quelle");
    auto copy_borrowed_from_input = Input(&copy_borrowed_from, "Geliehen von");
    auto copy_lent_to_input = Input(&copy_lent_to, "Verliehen an");
    auto copy_notes_input = Input(&copy_notes, "Notizen");

    auto load_work_editor = [&] {
        const auto work_id = selected_work_id();
        if (work_id.empty()) {
            status = "Kein Werk ausgewählt.";
            return;
        }
        const auto details = browse_service.get_work(work_id);
        if (!details.has_value()) {
            status = "Werk wurde nicht gefunden.";
            return;
        }
        work_title = details->canonical_title;
        work_authors = join(details->authors);
        work_original_title = optional_input(details->original_title);
        work_original_language = optional_input(details->original_language_code);
        work_first_year = optional_number_input(details->first_published_year);
        work_description = optional_input(details->description);
        work_age_rating = optional_input(details->age_rating);
        work_notes = optional_input(details->notes);
        mode_index = static_cast<int>(ScreenMode::EditWork);
    };

    auto load_edition_editor = [&] {
        const auto isbn = selected_edition_isbn();
        if (isbn.empty()) {
            status = "Keine Edition ausgewählt.";
            return;
        }
        const auto details = browse_service.get_edition(isbn);
        if (!details.has_value()) {
            status = "Edition wurde nicht gefunden.";
            return;
        }
        edition_title = details->title;
        edition_subtitle = optional_input(details->subtitle);
        edition_language = details->language_code;
        edition_publisher = optional_input(details->publisher);
        edition_year = optional_number_input(details->publication_year);
        edition_name = optional_input(details->edition_name);
        edition_format = optional_input(details->format);
        edition_pages = optional_number_input(details->page_count);
        edition_cover_url = optional_input(details->cover_url);
        edition_notes = optional_input(details->notes);
        mode_index = static_cast<int>(ScreenMode::EditEdition);
    };

    auto load_copy_editor = [&] {
        const auto copy_id = selected_copy_id();
        if (copy_id.empty()) {
            status = "Kein Exemplar ausgewählt.";
            return;
        }
        const auto details = browse_service.get_copy(copy_id);
        if (!details.has_value()) {
            status = "Exemplar wurde nicht gefunden.";
            return;
        }
        copy_location = optional_input(details->location_path);
        copy_position = optional_number_input(details->position_in_location);
        copy_condition = optional_input(details->condition);
        copy_acquired_date = optional_input(details->acquired_date);
        copy_acquired_where = optional_input(details->acquired_where);
        copy_borrowed_from = optional_input(details->borrowed_from);
        copy_lent_to = optional_input(details->lent_to);
        copy_notes = optional_input(details->notes);
        mode_index = static_cast<int>(ScreenMode::EditCopy);
    };

    auto cancel_edit = [&] {
        mode_index = static_cast<int>(ScreenMode::View);
        status = "Bearbeitung abgebrochen.";
    };

    auto save_work = [&] {
        try {
            const auto work_id = selected_work_id();
            browse_service.update_work(services::WorkUpdate{
                .id = work_id,
                .canonical_title = work_title,
                .original_title = services::optional_trimmed(work_original_title),
                .original_language_code = services::optional_trimmed(work_original_language),
                .first_published_year = optional_int(work_first_year),
                .description = services::optional_trimmed(work_description),
                .age_rating = services::optional_trimmed(work_age_rating),
                .notes = services::optional_trimmed(work_notes),
                .authors = services::split_list(work_authors),
            });
            mode_index = static_cast<int>(ScreenMode::View);
            reload_works();
            status = "Werk gespeichert.";
        } catch (const std::exception& error) {
            status = std::string{"Werk speichern fehlgeschlagen: "} + error.what();
        }
    };

    auto save_edition = [&] {
        try {
            const auto isbn = selected_edition_isbn();
            browse_service.update_edition(services::EditionUpdate{
                .isbn = isbn,
                .title = edition_title,
                .subtitle = services::optional_trimmed(edition_subtitle),
                .language_code = edition_language.empty() ? "und" : edition_language,
                .publisher = services::optional_trimmed(edition_publisher),
                .publication_year = optional_int(edition_year),
                .edition_name = services::optional_trimmed(edition_name),
                .format = services::optional_trimmed(edition_format),
                .page_count = optional_int(edition_pages),
                .cover_url = services::optional_trimmed(edition_cover_url),
                .notes = services::optional_trimmed(edition_notes),
            });
            mode_index = static_cast<int>(ScreenMode::View);
            reload_editions();
            reload_works();
            status = "Edition gespeichert.";
        } catch (const std::exception& error) {
            status = std::string{"Edition speichern fehlgeschlagen: "} + error.what();
        }
    };

    auto save_copy = [&] {
        try {
            const auto copy_id = selected_copy_id();
            browse_service.update_copy(services::CopyUpdate{
                .id = copy_id,
                .location_path = services::optional_trimmed(copy_location),
                .position_in_location = optional_int(copy_position),
                .condition = services::optional_trimmed(copy_condition),
                .acquired_date = services::optional_trimmed(copy_acquired_date),
                .acquired_where = services::optional_trimmed(copy_acquired_where),
                .borrowed_from = services::optional_trimmed(copy_borrowed_from),
                .lent_to = services::optional_trimmed(copy_lent_to),
                .notes = services::optional_trimmed(copy_notes),
            });
            mode_index = static_cast<int>(ScreenMode::View);
            reload_copies();
            status = "Exemplar gespeichert.";
        } catch (const std::exception& error) {
            status = std::string{"Exemplar speichern fehlgeschlagen: "} + error.what();
        }
    };

    auto ask_delete_work = [&] {
        if (selected_work < 0 || selected_work >= static_cast<int>(works.size())) {
            status = "Kein Werk ausgewählt.";
            return;
        }

        delete_target = DeleteTarget::Work;
        delete_id = selected_work_id();
        delete_label = works[selected_work].title + " (" + std::to_string(works[selected_work].edition_count)
            + " Editionen, " + std::to_string(works[selected_work].copy_count) + " Exemplare)";
        mode_index = static_cast<int>(ScreenMode::ConfirmDelete);
    };

    auto ask_delete_edition = [&] {
        if (selected_edition < 0 || selected_edition >= static_cast<int>(editions.size())) {
            status = "Keine Edition ausgewählt.";
            return;
        }

        delete_target = DeleteTarget::Edition;
        delete_id = selected_edition_isbn();
        delete_label = editions[selected_edition].title + " [" + editions[selected_edition].isbn + "] ("
            + std::to_string(editions[selected_edition].copy_count) + " Exemplare)";
        mode_index = static_cast<int>(ScreenMode::ConfirmDelete);
    };

    auto ask_delete_copy = [&] {
        if (selected_copy < 0 || selected_copy >= static_cast<int>(copies.size())) {
            status = "Kein Exemplar ausgewählt.";
            return;
        }

        delete_target = DeleteTarget::Copy;
        delete_id = selected_copy_id();
        delete_label = copy_label(copies[selected_copy]);
        mode_index = static_cast<int>(ScreenMode::ConfirmDelete);
    };

    auto cancel_delete = [&] {
        delete_target = DeleteTarget::None;
        delete_id.clear();
        delete_label.clear();
        mode_index = static_cast<int>(ScreenMode::View);
        status = "Löschen abgebrochen.";
    };

    auto confirm_delete = [&] {
        try {
            if (delete_target == DeleteTarget::Work) {
                browse_service.delete_work(delete_id);
                delete_target = DeleteTarget::None;
                delete_id.clear();
                delete_label.clear();
                mode_index = static_cast<int>(ScreenMode::View);
                reload_works();
                status = "Werk gelöscht.";
                return;
            }

            if (delete_target == DeleteTarget::Edition) {
                browse_service.delete_edition(delete_id);
                delete_target = DeleteTarget::None;
                delete_id.clear();
                delete_label.clear();
                mode_index = static_cast<int>(ScreenMode::View);
                reload_works();
                status = "Edition gelöscht.";
                return;
            }

            if (delete_target == DeleteTarget::Copy) {
                browse_service.delete_copy(delete_id);
                delete_target = DeleteTarget::None;
                delete_id.clear();
                delete_label.clear();
                mode_index = static_cast<int>(ScreenMode::View);
                reload_copies();
                reload_works();
                status = "Exemplar gelöscht.";
                return;
            }

            status = "Nichts zum Löschen ausgewählt.";
            mode_index = static_cast<int>(ScreenMode::View);
        } catch (const std::exception& error) {
            status = std::string{"Löschen fehlgeschlagen: "} + error.what();
        }
    };

    auto edit_work_button = Button("Werk bearbeiten", load_work_editor);
    auto edit_edition_button = Button("Edition bearbeiten", load_edition_editor);
    auto edit_copy_button = Button("Exemplar bearbeiten", load_copy_editor);
    auto delete_work_button = Button("Werk löschen", ask_delete_work);
    auto delete_edition_button = Button("Edition löschen", ask_delete_edition);
    auto delete_copy_button = Button("Exemplar löschen", ask_delete_copy);
    auto cancel_work_button = Button("Abbrechen", cancel_edit);
    auto cancel_edition_button = Button("Abbrechen", cancel_edit);
    auto cancel_copy_button = Button("Abbrechen", cancel_edit);
    auto save_work_button = Button("Werk speichern", save_work);
    auto save_edition_button = Button("Edition speichern", save_edition);
    auto save_copy_button = Button("Exemplar speichern", save_copy);
    auto confirm_delete_button = Button("Endgültig löschen", confirm_delete);
    auto cancel_delete_button = Button("Abbrechen", cancel_delete);

    auto view_container = Container::Vertical({
        Container::Horizontal({text_filter_input, author_filter_input}),
        Container::Horizontal({series_filter_input, status_filter_input, search_button, clear_button}),
        Container::Horizontal({works_component, editions_component, copies_menu}),
        Container::Horizontal({edit_work_button, edit_edition_button, edit_copy_button}),
        Container::Horizontal({delete_work_button, delete_edition_button, delete_copy_button, back_button, quit_button}),
    });

    auto edit_work_container = Container::Vertical({
        work_title_input,
        work_authors_input,
        work_original_title_input,
        work_original_language_input,
        work_first_year_input,
        work_description_input,
        work_age_rating_input,
        work_notes_input,
        Container::Horizontal({save_work_button, cancel_work_button}),
    });

    auto edit_edition_container = Container::Vertical({
        edition_title_input,
        edition_subtitle_input,
        edition_language_input,
        edition_publisher_input,
        edition_year_input,
        edition_name_input,
        edition_format_input,
        edition_pages_input,
        edition_cover_url_input,
        edition_notes_input,
        Container::Horizontal({save_edition_button, cancel_edition_button}),
    });

    auto edit_copy_container = Container::Vertical({
        copy_location_input,
        copy_position_input,
        copy_condition_input,
        copy_acquired_date_input,
        copy_acquired_where_input,
        copy_borrowed_from_input,
        copy_lent_to_input,
        copy_notes_input,
        Container::Horizontal({save_copy_button, cancel_copy_button}),
    });

    auto confirm_delete_container = Container::Vertical({
        confirm_delete_button,
        cancel_delete_button,
    });

    auto mode_container = Container::Tab({
        view_container,
        edit_work_container,
        edit_edition_container,
        edit_copy_container,
        confirm_delete_container,
    }, &mode_index);

    auto renderer = Renderer(mode_container, [&] {
        const bool has_work = selected_work >= 0 && selected_work < static_cast<int>(works.size());
        const bool has_edition = selected_edition >= 0 && selected_edition < static_cast<int>(editions.size());
        const bool has_copy = selected_copy >= 0 && selected_copy < static_cast<int>(copies.size());

        Element body;
        if (mode_index == static_cast<int>(ScreenMode::View)) {
            Element work_details = text("Kein Werk ausgewählt") | dim;
            if (has_work) {
                if (const auto details = browse_service.get_work(works[selected_work].id); details.has_value()) {
                    work_details = vbox({
                        text(details->canonical_title) | bold,
                        detail_line("Autoren", join(details->authors)),
                        detail_line("Original", optional_text(details->original_title)),
                        detail_line("Erstjahr", optional_number(details->first_published_year)),
                        detail_line("Serie", details->series),
                        detail_line("Genre", details->genres),
                        detail_line("Lesestatus", details->reading_status),
                        detail_line("Notizen", optional_text(details->notes)),
                    });
                }
            }

            Element edition_details = text("Keine Edition ausgewählt") | dim;
            if (has_edition) {
                const auto& edition = editions[selected_edition];
                edition_details = vbox({
                    text(edition.title) | bold,
                    detail_line("ISBN", edition.isbn),
                    detail_line("Sprache", edition.language_code),
                    detail_line("Verlag", optional_text(edition.publisher)),
                    detail_line("Jahr", optional_number(edition.publication_year)),
                    detail_line("Edition", optional_text(edition.edition_name)),
                    detail_line("Format", optional_text(edition.format)),
                    detail_line("Seiten", optional_number(edition.page_count)),
                });
            }

            Element copy_details = text("Kein Exemplar ausgewählt") | dim;
            if (has_copy) {
                const auto& copy = copies[selected_copy];
                copy_details = vbox({
                    detail_line("Standort", optional_text(copy.location_path)),
                    detail_line("Position", optional_number(copy.position_in_location)),
                    detail_line("Zustand", optional_text(copy.condition)),
                    detail_line("Gekauft am", optional_text(copy.acquired_date)),
                    detail_line("Gekauft bei", optional_text(copy.acquired_where)),
                    detail_line("Geliehen von", optional_text(copy.borrowed_from)),
                    detail_line("Verliehen an", optional_text(copy.lent_to)),
                    detail_line("Notizen", optional_text(copy.notes)),
                });
            }

            body = vbox({
                hbox({
                    text("Suche") | size(WIDTH, EQUAL, 10),
                    text_filter_input->Render() | flex,
                    text("  "),
                    text("Autor") | size(WIDTH, EQUAL, 8),
                    author_filter_input->Render() | flex,
                }),
                hbox({
                    text("Serie") | size(WIDTH, EQUAL, 10),
                    series_filter_input->Render() | flex,
                    text("  "),
                    text("Status") | size(WIDTH, EQUAL, 8),
                    status_filter_input->Render() | flex,
                    text(" "),
                    search_button->Render(),
                    text(" "),
                    clear_button->Render(),
                }),
                separator(),
                hbox({
                    vbox({
                        text("Werke") | bold,
                        works_component->Render() | border | yframe | flex,
                    }) | size(WIDTH, EQUAL, 46),
                    separator(),
                    vbox({
                        text("Werk") | bold,
                        work_details | border | size(HEIGHT, LESS_THAN, 11),
                        text("Editionen") | bold,
                        editions_component->Render() | border | yframe | flex,
                    }) | size(WIDTH, EQUAL, 50),
                    separator(),
                    vbox({
                        text("Edition") | bold,
                        edition_details | border | size(HEIGHT, LESS_THAN, 10),
                        text("Exemplare") | bold,
                        copies_menu->Render() | border | yframe | flex,
                        text("Exemplar") | bold,
                        copy_details | border | size(HEIGHT, LESS_THAN, 10),
                    }) | flex,
                }) | flex,
                separator(),
                hbox({
                    edit_work_button->Render(),
                    text(" "),
                    edit_edition_button->Render(),
                    text(" "),
                    edit_copy_button->Render(),
                    text("  "),
                    delete_work_button->Render(),
                    text(" "),
                    delete_edition_button->Render(),
                    text(" "),
                    delete_copy_button->Render(),
                    text(" "),
                    back_button->Render(),
                    text(" "),
                    quit_button->Render(),
                    text("  "),
                    text(status) | dim,
                }),
            });
        } else if (mode_index == static_cast<int>(ScreenMode::EditWork)) {
            body = vbox({
                text("Werk bearbeiten") | bold,
                separator(),
                labeled_input("Titel", work_title_input),
                labeled_input("Autoren", work_authors_input),
                labeled_input("Originaltitel", work_original_title_input),
                labeled_input("Originalsprache", work_original_language_input),
                labeled_input("Erstjahr", work_first_year_input),
                labeled_input("Beschreibung", work_description_input),
                labeled_input("Altersfreigabe", work_age_rating_input),
                labeled_input("Notizen", work_notes_input),
                filler(),
                separator(),
                hbox({save_work_button->Render(), text(" "), cancel_work_button->Render(), text("  "), text(status) | dim}),
            });
        } else if (mode_index == static_cast<int>(ScreenMode::EditEdition)) {
            body = vbox({
                text("Edition bearbeiten") | bold,
                separator(),
                detail_line("ISBN", selected_edition_isbn()),
                labeled_input("Titel", edition_title_input),
                labeled_input("Untertitel", edition_subtitle_input),
                labeled_input("Sprache", edition_language_input),
                labeled_input("Verlag", edition_publisher_input),
                labeled_input("Jahr", edition_year_input),
                labeled_input("Edition", edition_name_input),
                labeled_input("Format", edition_format_input),
                labeled_input("Seiten", edition_pages_input),
                labeled_input("Cover", edition_cover_url_input),
                labeled_input("Notizen", edition_notes_input),
                filler(),
                separator(),
                hbox({save_edition_button->Render(), text(" "), cancel_edition_button->Render(), text("  "), text(status) | dim}),
            });
        } else if (mode_index == static_cast<int>(ScreenMode::EditCopy)) {
            body = vbox({
                text("Exemplar bearbeiten") | bold,
                separator(),
                detail_line("ID", selected_copy_id()),
                labeled_input("Standort", copy_location_input),
                labeled_input("Position", copy_position_input),
                labeled_input("Zustand", copy_condition_input),
                labeled_input("Gekauft am", copy_acquired_date_input),
                labeled_input("Gekauft bei", copy_acquired_where_input),
                labeled_input("Geliehen von", copy_borrowed_from_input),
                labeled_input("Verliehen an", copy_lent_to_input),
                labeled_input("Notizen", copy_notes_input),
                filler(),
                separator(),
                hbox({save_copy_button->Render(), text(" "), cancel_copy_button->Render(), text("  "), text(status) | dim}),
            });
        } else {
            std::string delete_kind = "Eintrag";
            std::string consequence = "Dieser Eintrag wird gelöscht.";
            if (delete_target == DeleteTarget::Work) {
                delete_kind = "Werk";
                consequence = "Alle zugehörigen Editionen, Exemplare, Lesestatus-, Genre-, Serien- und Autorenverknüpfungen werden entfernt.";
            } else if (delete_target == DeleteTarget::Edition) {
                delete_kind = "Edition";
                consequence = "Alle Exemplare und Editions-Mitwirkenden dieser Edition werden entfernt.";
            } else if (delete_target == DeleteTarget::Copy) {
                delete_kind = "Exemplar";
                consequence = "Nur dieses Exemplar wird entfernt.";
            }

            body = vbox({
                text(delete_kind + " löschen") | bold,
                separator(),
                text(delete_label.empty() ? delete_id : delete_label) | bold,
                separator(),
                paragraph(consequence),
                filler(),
                separator(),
                hbox({
                    confirm_delete_button->Render(),
                    text(" "),
                    cancel_delete_button->Render(),
                    text("  "),
                    text(status) | dim,
                }),
            });
        }

        return vbox({
            text("Buchhaltung - Werke verwalten") | bold | center,
            separator(),
            body | flex,
        }) | border;
    });

    screen.Loop(renderer);
    return result;
}

} // namespace buch::tui::browse_books
