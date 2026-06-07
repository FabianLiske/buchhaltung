#include "tui/add_book/AddBookTui.hpp"

#include "db/Database.hpp"
#include "db/Migrations.hpp"
#include "lookup/GoogleBooksLookup.hpp"
#include "services/BookImportService.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace buch::tui::add_book {
namespace {

using namespace ftxui;

std::string optional_text(const std::optional<std::string>& value) {
    return value.value_or("");
}

std::string optional_number(const std::optional<int>& value) {
    if (!value.has_value()) {
        return "";
    }
    return std::to_string(*value);
}

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

Element labeled_input(std::string label, const Component& input) {
    return hbox({
        text(std::move(label)) | size(WIDTH, EQUAL, 20),
        input->Render() | flex,
    });
}

Element readonly_field(std::string label, const std::string& value) {
    return hbox({
        text(std::move(label)) | size(WIDTH, EQUAL, 20),
        text(value.empty() ? "-" : value) | flex,
    });
}

Element help_line(std::string value) {
    return text(std::move(value)) | dim;
}

std::string work_option_label(const services::WorkSuggestion& suggestion) {
    const auto authors = join(suggestion.authors);
    if (authors.empty()) {
        return suggestion.title;
    }
    return suggestion.title + " (" + authors + ")";
}

} // namespace

Result run(const std::string& google_books_api_key, const std::filesystem::path& database_path) {
    db::Database database{database_path};
    db::apply_migrations(database);
    services::BookImportService import_service{database};

    auto screen = ScreenInteractive::Fullscreen();
    auto result = Result::Quit;
    auto exit = screen.ExitLoopClosure();

    int tab_index = 0;
    std::vector<std::string> tabs = {
        "ISBN",
        "Edition",
        "Werk",
        "Serie",
        "Einordnung",
        "Exemplar",
        "Review",
    };

    std::string status = "ISBN eingeben. Wenn die Edition existiert, wird nur ein neues Exemplar angelegt.";
    bool edition_exists = false;
    std::string existing_edition_work_id;
    std::string existing_edition_summary;

    std::string isbn;
    std::string google_volume_id;
    std::string title;
    std::string subtitle;
    std::string authors;
    std::string edition_contributors;
    std::string publisher;
    std::string published_date;
    std::string language;
    std::string edition_name;
    std::string edition_format;
    std::string isbn_10;
    std::string isbn_13;
    std::string page_count;
    std::string edition_notes;
    std::string categories;
    std::string maturity_rating;
    std::string cover_url;
    std::string info_link;
    std::string canonical_link;
    std::string description;

    std::string canonical_title;
    std::string original_title;
    std::string original_language_code;
    std::string work_notes;

    int selected_work = 0;
    std::vector<std::string> work_options = {"Neues Werk anlegen"};
    std::vector<std::string> work_ids = {""};

    int selected_series = 0;
    std::vector<std::string> series_options = {"Keine Serie", "Neue Serie anlegen"};
    std::vector<std::string> series_ids = {"", ""};
    std::string new_series_name;
    std::string series_original_title;
    std::string series_description;
    std::string series_notes;
    std::string series_season;
    std::string series_position;
    std::string series_position_label;
    std::string work_series_notes;

    std::string parent_genre;
    std::string genre;
    std::string genre_description;
    std::string genre_notes;
    int selected_reading_status = 0;
    std::vector<std::string> reading_status_options = {
        "Kein Status",
        "ungelesen",
        "angefangen",
        "gelesen",
        "abgebrochen",
        "pausiert",
        "Wunschliste",
    };
    std::string reading_started_date;
    std::string reading_finished_date;
    std::string rating;
    std::string reading_notes;

    std::string location_path;
    std::string location_description;
    std::string location_notes;
    std::string location_visual_x;
    std::string location_visual_y;
    std::string location_visual_z;
    std::string location_visual_width;
    std::string location_visual_height;
    std::string location_visual_depth;
    std::string position_in_location;
    std::string condition;
    std::string acquired_date;
    std::string acquired_where;
    std::string borrowed_from;
    std::string lent_to;
    std::string copy_notes;

    const auto reload_series_options = [&] {
        series_options = {"Keine Serie", "Neue Serie anlegen"};
        series_ids = {"", ""};
        for (const auto& series : import_service.list_series()) {
            series_options.push_back(series.name);
            series_ids.push_back(series.id);
        }
        if (selected_series >= static_cast<int>(series_options.size())) {
            selected_series = 0;
        }
    };

    const auto reload_work_suggestions = [&] {
        work_options = {"Neues Werk anlegen"};
        work_ids = {""};

        for (const auto& suggestion : import_service.suggest_works_by_authors(services::split_list(authors))) {
            work_options.push_back(work_option_label(suggestion));
            work_ids.push_back(suggestion.id);
        }
        selected_work = 0;
    };

    reload_series_options();

    const auto clear_form = [&] {
        tab_index = 0;
        edition_exists = false;
        existing_edition_work_id.clear();
        existing_edition_summary.clear();

        isbn.clear();
        google_volume_id.clear();
        title.clear();
        subtitle.clear();
        authors.clear();
        edition_contributors.clear();
        publisher.clear();
        published_date.clear();
        language.clear();
        edition_name.clear();
        edition_format.clear();
        isbn_10.clear();
        isbn_13.clear();
        page_count.clear();
        edition_notes.clear();
        categories.clear();
        maturity_rating.clear();
        cover_url.clear();
        info_link.clear();
        canonical_link.clear();
        description.clear();

        canonical_title.clear();
        original_title.clear();
        original_language_code.clear();
        work_notes.clear();

        selected_work = 0;
        work_options = {"Neues Werk anlegen"};
        work_ids = {""};

        selected_series = 0;
        new_series_name.clear();
        series_original_title.clear();
        series_description.clear();
        series_notes.clear();
        series_season.clear();
        series_position.clear();
        series_position_label.clear();
        work_series_notes.clear();

        parent_genre.clear();
        genre.clear();
        genre_description.clear();
        genre_notes.clear();
        selected_reading_status = 0;
        reading_started_date.clear();
        reading_finished_date.clear();
        rating.clear();
        reading_notes.clear();

        location_path.clear();
        location_description.clear();
        location_notes.clear();
        location_visual_x.clear();
        location_visual_y.clear();
        location_visual_z.clear();
        location_visual_width.clear();
        location_visual_height.clear();
        location_visual_depth.clear();
        position_in_location.clear();
        condition.clear();
        acquired_date.clear();
        acquired_where.clear();
        borrowed_from.clear();
        lent_to.clear();
        copy_notes.clear();

        reload_series_options();
    };

    std::function<void()> lookup_action;
    auto isbn_input_option = InputOption::Default();
    isbn_input_option.multiline = false;
    isbn_input_option.on_enter = [&] {
        if (lookup_action) {
            lookup_action();
        }
    };
    auto isbn_input = Input(&isbn, "978...", isbn_input_option);
    auto title_input = Input(&title, "Titel");
    auto subtitle_input = Input(&subtitle, "Untertitel");
    auto authors_input = Input(&authors, "Autoren, getrennt mit Komma");
    auto edition_contributors_input = Input(&edition_contributors, "translator: Name; illustrator: Name");
    auto publisher_input = Input(&publisher, "Verlag");
    auto published_date_input = Input(&published_date, "YYYY-MM-DD");
    auto language_input = Input(&language, "de");
    auto edition_name_input = Input(&edition_name, "Ausgabe, z. B. 2. Auflage");
    auto edition_format_input = Input(&edition_format, "Hardcover, Taschenbuch, eBook");
    auto page_count_input = Input(&page_count, "Seiten");
    auto edition_notes_input = Input(&edition_notes, "Notizen zur Edition");
    auto categories_input = Input(&categories, "API-Kategorien");
    auto description_input = Input(&description, "Beschreibung");
    auto canonical_title_input = Input(&canonical_title, "Titel des Werks");
    auto original_title_input = Input(&original_title, "Originaltitel");
    auto original_language_code_input = Input(&original_language_code, "Originalsprache");
    auto work_notes_input = Input(&work_notes, "Notizen zum Werk");
    auto work_radio = Radiobox(&work_options, &selected_work);
    auto series_radio = Radiobox(&series_options, &selected_series);
    auto new_series_name_input = Input(&new_series_name, "Name der neuen Serie");
    auto series_original_title_input = Input(&series_original_title, "Originaltitel der Serie");
    auto series_description_input = Input(&series_description, "Beschreibung der Serie");
    auto series_notes_input = Input(&series_notes, "Notizen zur Serie");
    auto series_season_input = Input(&series_season, "Staffel");
    auto series_position_input = Input(&series_position, "Position/Band");
    auto series_position_label_input = Input(&series_position_label, "Label, z. B. 1.5 oder Sonderband");
    auto work_series_notes_input = Input(&work_series_notes, "Notizen zur Reihenzuordnung");
    auto parent_genre_input = Input(&parent_genre, "z. B. Fantasy");
    auto genre_input = Input(&genre, "z. B. Urban Fantasy");
    auto genre_description_input = Input(&genre_description, "Genre-Beschreibung");
    auto genre_notes_input = Input(&genre_notes, "Genre-Notizen");
    auto reading_status_radio = Radiobox(&reading_status_options, &selected_reading_status);
    auto reading_started_date_input = Input(&reading_started_date, "YYYY-MM-DD");
    auto reading_finished_date_input = Input(&reading_finished_date, "YYYY-MM-DD");
    auto rating_input = Input(&rating, "1-5");
    auto reading_notes_input = Input(&reading_notes, "Notizen zum Lesestatus");
    auto location_input = Input(&location_path, "Wohnzimmer / Regal 1 / Fach A");
    auto location_description_input = Input(&location_description, "Beschreibung des letzten Standorts");
    auto location_notes_input = Input(&location_notes, "Notizen zum Standort");
    auto location_visual_x_input = Input(&location_visual_x, "x");
    auto location_visual_y_input = Input(&location_visual_y, "y");
    auto location_visual_z_input = Input(&location_visual_z, "z");
    auto location_visual_width_input = Input(&location_visual_width, "Breite");
    auto location_visual_height_input = Input(&location_visual_height, "Höhe");
    auto location_visual_depth_input = Input(&location_visual_depth, "Tiefe");
    auto position_input = Input(&position_in_location, "Position");
    auto condition_input = Input(&condition, "Zustand");
    auto acquired_date_input = Input(&acquired_date, "YYYY-MM-DD");
    auto acquired_where_input = Input(&acquired_where, "Quelle");
    auto borrowed_from_input = Input(&borrowed_from, "Geliehen von");
    auto lent_to_input = Input(&lent_to, "Verliehen an");
    auto copy_notes_input = Input(&copy_notes, "Notizen zum Exemplar");

    auto make_request = [&] {
        services::ImportRequest request;
        request.isbn = isbn_13.empty() ? isbn : isbn_13;
        request.title = title;
        request.subtitle = services::optional_trimmed(subtitle);
        request.authors = services::split_list(authors);
        request.edition_contributors = services::parse_contributors(edition_contributors, "contributor");
        request.publisher = services::optional_trimmed(publisher);
        request.published_date = services::optional_trimmed(published_date);
        request.language_code = language.empty() ? "und" : language;
        request.edition_name = services::optional_trimmed(edition_name);
        request.format = services::optional_trimmed(edition_format);
        request.page_count = services::optional_int(page_count);
        request.cover_url = services::optional_trimmed(cover_url);
        request.edition_notes = services::optional_trimmed(edition_notes);
        request.description = services::optional_trimmed(description);
        request.age_rating = services::optional_trimmed(maturity_rating);
        request.canonical_title = services::optional_trimmed(canonical_title.empty() ? title : canonical_title);
        request.original_title = services::optional_trimmed(original_title);
        request.original_language_code = services::optional_trimmed(original_language_code);
        request.work_notes = services::optional_trimmed(work_notes);

        if (edition_exists) {
            request.existing_work_id = existing_edition_work_id;
        } else if (selected_work > 0 && selected_work < static_cast<int>(work_ids.size())) {
            request.existing_work_id = work_ids[selected_work];
        }

        if (selected_series == 1) {
            request.new_series_name = services::optional_trimmed(new_series_name);
        } else if (selected_series > 1 && selected_series < static_cast<int>(series_ids.size())) {
            request.existing_series_id = series_ids[selected_series];
        }
        request.series_original_title = services::optional_trimmed(series_original_title);
        request.series_description = services::optional_trimmed(series_description);
        request.series_notes = services::optional_trimmed(series_notes);
        request.series_season = services::optional_int(series_season);
        request.series_position = services::optional_double(series_position);
        request.series_position_label = services::optional_trimmed(series_position_label);
        request.work_series_notes = services::optional_trimmed(work_series_notes);

        request.parent_genre_name = services::optional_trimmed(parent_genre);
        request.genre_name = services::optional_trimmed(genre);
        request.genre_description = services::optional_trimmed(genre_description);
        request.genre_notes = services::optional_trimmed(genre_notes);
        if (selected_reading_status > 0 && selected_reading_status < static_cast<int>(reading_status_options.size())) {
            request.reading_status = reading_status_options[selected_reading_status];
        }
        request.reading_started_date = services::optional_trimmed(reading_started_date);
        request.reading_finished_date = services::optional_trimmed(reading_finished_date);
        request.rating = services::optional_int(rating);
        request.reading_notes = services::optional_trimmed(reading_notes);

        request.location_path = services::optional_trimmed(location_path);
        request.location_description = services::optional_trimmed(location_description);
        request.location_notes = services::optional_trimmed(location_notes);
        request.location_visual_x = services::optional_double(location_visual_x);
        request.location_visual_y = services::optional_double(location_visual_y);
        request.location_visual_z = services::optional_double(location_visual_z);
        request.location_visual_width = services::optional_double(location_visual_width);
        request.location_visual_height = services::optional_double(location_visual_height);
        request.location_visual_depth = services::optional_double(location_visual_depth);
        request.position_in_location = services::optional_int(position_in_location);
        request.condition = services::optional_trimmed(condition);
        request.acquired_date = services::optional_trimmed(acquired_date);
        request.acquired_where = services::optional_trimmed(acquired_where);
        request.borrowed_from = services::optional_trimmed(borrowed_from);
        request.lent_to = services::optional_trimmed(lent_to);
        request.copy_notes = services::optional_trimmed(copy_notes);
        return request;
    };

    auto run_lookup = [&] {
        if (services::optional_trimmed(isbn).value_or("").empty()) {
            status = "Bitte zuerst eine ISBN eingeben.";
            return;
        }

        try {
            if (const auto existing = import_service.find_edition_by_isbn(isbn); existing.has_value()) {
                edition_exists = true;
                existing_edition_work_id = existing->work_id;
                title = existing->title;
                subtitle = optional_text(existing->subtitle);
                authors = join(existing->authors);
                publisher = optional_text(existing->publisher);
                published_date = existing->publication_year.has_value() ? std::to_string(*existing->publication_year) : "";
                language = existing->language_code;
                page_count = optional_number(existing->page_count);
                isbn_13 = existing->isbn;
                canonical_title = existing->work_title;
                existing_edition_summary = existing->title + " / " + existing->work_title;
                work_options = {"Edition existiert: " + existing->work_title};
                work_ids = {existing->work_id};
                selected_work = 0;
                status = "Edition existiert bereits. Beim Speichern wird nur ein neues Exemplar angelegt.";
                tab_index = 5;
                return;
            }

            edition_exists = false;
            existing_edition_work_id.clear();
            existing_edition_summary.clear();

            status = "Suche bei Google Books...";
            const auto result = lookup::lookup_google_books_by_isbn(isbn, google_books_api_key);

            google_volume_id = result.google_volume_id;
            title = optional_text(result.title);
            subtitle = optional_text(result.subtitle);
            authors = join(result.authors);
            publisher = optional_text(result.publisher);
            published_date = optional_text(result.published_date);
            language = optional_text(result.language);
            isbn_10 = optional_text(result.isbn_10);
            isbn_13 = optional_text(result.isbn_13);
            page_count = optional_number(result.page_count);
            categories = join(result.categories);
            maturity_rating = optional_text(result.maturity_rating);
            cover_url = optional_text(result.thumbnail_url);
            info_link = optional_text(result.info_link);
            canonical_link = optional_text(result.canonical_link);
            description = optional_text(result.description);
            canonical_title = title;

            reload_work_suggestions();
            reload_series_options();
            status = "Treffer geladen. Werkvorschläge wurden über Autoren vorgefiltert.";
            tab_index = 1;
        } catch (const std::exception& error) {
            status = std::string{"Lookup fehlgeschlagen: "} + error.what();
        }
    };
    lookup_action = run_lookup;

    auto save_import = [&] {
        try {
            const auto result = import_service.save_import(make_request());
            const auto saved_status = "Gespeichert. Work: " + result.work_id + " / Copy: " + result.copy_id + ". Maske wurde geleert.";
            clear_form();
            status = saved_status;
        } catch (const std::exception& error) {
            status = std::string{"Speichern fehlgeschlagen: "} + error.what();
        }
    };

    auto search_button = Button("Lookup", run_lookup);
    auto save_button = Button("Speichern", save_import);
    auto quit_button = Button("Beenden", [&] {
        result = Result::Quit;
        exit();
    });
    auto next_button = Button("Weiter", [&] {
        if (tab_index + 1 < static_cast<int>(tabs.size())) {
            ++tab_index;
        }
    });
    auto previous_button = Button("Zurück", [&] {
        if (tab_index == 0) {
            result = Result::BackToMainMenu;
            exit();
            return;
        }
        else if (tab_index > 0) {
            --tab_index;
        }
    });

    auto tab_menu = Menu(&tabs, &tab_index);
    auto isbn_tab = Container::Vertical({isbn_input, search_button});
    auto edition_tab = Container::Vertical({
        title_input,
        subtitle_input,
        publisher_input,
        published_date_input,
        language_input,
        edition_name_input,
        edition_format_input,
        page_count_input,
        edition_notes_input,
        edition_contributors_input,
        categories_input,
        description_input,
    });
    auto work_tab = Container::Vertical({
        work_radio,
        authors_input,
        canonical_title_input,
        original_title_input,
        original_language_code_input,
        work_notes_input,
    });
    auto series_tab = Container::Vertical({
        series_radio,
        new_series_name_input,
        series_original_title_input,
        series_description_input,
        series_notes_input,
        series_season_input,
        series_position_input,
        series_position_label_input,
        work_series_notes_input,
    });
    auto classification_tab = Container::Vertical({
        parent_genre_input,
        genre_input,
        genre_description_input,
        genre_notes_input,
        reading_status_radio,
        reading_started_date_input,
        reading_finished_date_input,
        rating_input,
        reading_notes_input,
    });
    auto copy_tab = Container::Vertical({
        location_input,
        location_description_input,
        location_notes_input,
        location_visual_x_input,
        location_visual_y_input,
        location_visual_z_input,
        location_visual_width_input,
        location_visual_height_input,
        location_visual_depth_input,
        position_input,
        condition_input,
        acquired_date_input,
        acquired_where_input,
        borrowed_from_input,
        lent_to_input,
        copy_notes_input,
    });
    auto review_tab = Container::Vertical({});

    auto tab_container = Container::Tab({
        isbn_tab,
        edition_tab,
        work_tab,
        series_tab,
        classification_tab,
        copy_tab,
        review_tab,
    }, &tab_index);

    auto form = Container::Vertical({
        tab_menu,
        tab_container,
        Container::Horizontal({previous_button, next_button, save_button, quit_button}),
    });

    auto renderer = Renderer(form, [&] {
        Element content;
        if (tab_index == 0) {
            content = vbox({
                text("1. ISBN") | bold,
                separator(),
                labeled_input("ISBN", isbn_input),
                hbox({search_button->Render(), text("  "), help_line("Prüft zuerst die lokale DB, dann Google Books.")}),
            });
        } else if (tab_index == 1) {
            content = vbox({
                text("2. Edition") | bold,
                separator(),
                edition_exists ? text("Edition existiert lokal: " + existing_edition_summary) | bold : text("Neue Edition aus API-Daten"),
                labeled_input("Titel", title_input),
                labeled_input("Untertitel", subtitle_input),
                labeled_input("Verlag", publisher_input),
                labeled_input("Erschienen", published_date_input),
                labeled_input("Sprache", language_input),
                labeled_input("Edition", edition_name_input),
                labeled_input("Format", edition_format_input),
                labeled_input("Seiten", page_count_input),
                labeled_input("Editionsnotizen", edition_notes_input),
                labeled_input("Mitwirkende", edition_contributors_input),
                help_line("Mitwirkende: role: Name|Sortname|Geburtsjahr|Todesjahr|Notizen; translator: Max Beispiel"),
                labeled_input("Kategorien", categories_input),
                labeled_input("Beschreibung", description_input),
            });
        } else if (tab_index == 2) {
            content = vbox({
                text("3. Werk") | bold,
                separator(),
                help_line("Vorschläge basieren auf vorhandenen Werken mit gleichem Autor."),
                work_radio->Render() | border,
                labeled_input("Autoren", authors_input),
                labeled_input("Kanonischer Titel", canonical_title_input),
                labeled_input("Originaltitel", original_title_input),
                labeled_input("Originalsprache", original_language_code_input),
                labeled_input("Werknotizen", work_notes_input),
            });
        } else if (tab_index == 3) {
            content = vbox({
                text("4. Serie") | bold,
                separator(),
                series_radio->Render() | border,
                labeled_input("Neue Serie", new_series_name_input),
                labeled_input("Originaltitel", series_original_title_input),
                labeled_input("Beschreibung", series_description_input),
                labeled_input("Seriennotizen", series_notes_input),
                labeled_input("Staffel", series_season_input),
                labeled_input("Position", series_position_input),
                labeled_input("Positionslabel", series_position_label_input),
                labeled_input("Zuordnungsnotiz", work_series_notes_input),
            });
        } else if (tab_index == 4) {
            content = vbox({
                text("5. Einordnung") | bold,
                separator(),
                labeled_input("Elterngenre", parent_genre_input),
                labeled_input("Genre", genre_input),
                labeled_input("Genre-Beschreibung", genre_description_input),
                labeled_input("Genre-Notizen", genre_notes_input),
                text("Lesestatus") | bold,
                reading_status_radio->Render() | border,
                labeled_input("Gestartet", reading_started_date_input),
                labeled_input("Beendet", reading_finished_date_input),
                labeled_input("Rating", rating_input),
                labeled_input("Lesenotizen", reading_notes_input),
            });
        } else if (tab_index == 5) {
            content = vbox({
                text("6. Exemplar") | bold,
                separator(),
                help_line("Standortpfad trennt Ebenen mit /, z. B. Wohnzimmer / Regal 1 / Fach A."),
                labeled_input("Standort", location_input),
                labeled_input("Standortbeschreibung", location_description_input),
                labeled_input("Standortnotizen", location_notes_input),
                hbox({
                    text("Visuell") | size(WIDTH, EQUAL, 20),
                    location_visual_x_input->Render() | flex,
                    text(" "),
                    location_visual_y_input->Render() | flex,
                    text(" "),
                    location_visual_z_input->Render() | flex,
                }),
                hbox({
                    text("Maße") | size(WIDTH, EQUAL, 20),
                    location_visual_width_input->Render() | flex,
                    text(" "),
                    location_visual_height_input->Render() | flex,
                    text(" "),
                    location_visual_depth_input->Render() | flex,
                }),
                labeled_input("Position", position_input),
                labeled_input("Zustand", condition_input),
                labeled_input("Gekauft am", acquired_date_input),
                labeled_input("Gekauft bei", acquired_where_input),
                labeled_input("Geliehen von", borrowed_from_input),
                labeled_input("Verliehen an", lent_to_input),
                labeled_input("Notizen", copy_notes_input),
            });
        } else {
            const auto chosen_work = selected_work >= 0 && selected_work < static_cast<int>(work_options.size()) ? work_options[selected_work] : "-";
            const auto chosen_series = selected_series >= 0 && selected_series < static_cast<int>(series_options.size()) ? series_options[selected_series] : "-";
            content = vbox({
                text("7. Review & Save") | bold,
                separator(),
                readonly_field("ISBN", isbn_13.empty() ? isbn : isbn_13),
                readonly_field("Titel", title),
                readonly_field("Autoren", authors),
                readonly_field("Editions-Mitwirkende", edition_contributors),
                readonly_field("Werk", chosen_work),
                readonly_field("Serie", chosen_series),
                readonly_field("Genre", parent_genre.empty() ? genre : parent_genre + " > " + genre),
                readonly_field("Lesestatus", selected_reading_status > 0 ? reading_status_options[selected_reading_status] : ""),
                readonly_field("Rating", rating),
                readonly_field("Standort", location_path),
                readonly_field("Zustand", condition),
                readonly_field("Geliehen von", borrowed_from),
                readonly_field("Verliehen an", lent_to),
                separator(),
                save_button->Render(),
            });
        }

        const auto api_fields = vbox({
            text("API/IDs") | bold,
            separator(),
            readonly_field("Google Volume ID", google_volume_id),
            readonly_field("ISBN-10", isbn_10),
            readonly_field("ISBN-13", isbn_13),
            readonly_field("Altersfreigabe", maturity_rating),
            readonly_field("Cover", cover_url),
            readonly_field("Info-Link", info_link),
            readonly_field("Canonical-Link", canonical_link),
        });

        return vbox({
            text("Buchhaltung - Buch hinzufügen") | bold | center,
            separator(),
            hbox({
                tab_menu->Render() | size(WIDTH, EQUAL, 18),
                separator(),
                content | flex,
                separator(),
                api_fields | size(WIDTH, GREATER_THAN, 34),
            }) | flex,
            separator(),
            hbox({
                previous_button->Render(),
                text(" "),
                next_button->Render(),
                text(" "),
                save_button->Render(),
                text(" "),
                quit_button->Render(),
            }),
            separator(),
            text(status),
        }) | border;
    });

    screen.Loop(renderer);
    return result;
}

} // namespace buch::tui::add_book
