#include "LookupTui.hpp"

#include "lookup/GoogleBooksLookup.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace buch::tui {
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
        text(std::move(label)) | size(WIDTH, EQUAL, 18),
        input->Render() | flex,
    });
}

Element readonly_field(std::string label, const std::string& value) {
    return hbox({
        text(std::move(label)) | size(WIDTH, EQUAL, 18),
        text(value.empty() ? "-" : value) | flex,
    });
}

} // namespace

int run_lookup_tui(const std::string& google_books_api_key) {
    auto screen = ScreenInteractive::TerminalOutput();

    std::string isbn;
    std::string status = "ISBN eingeben und Suchen drücken.";

    std::string google_volume_id;
    std::string title;
    std::string subtitle;
    std::string authors;
    std::string publisher;
    std::string published_date;
    std::string language;
    std::string isbn_10;
    std::string isbn_13;
    std::string page_count;
    std::string categories;
    std::string maturity_rating;
    std::string cover_url;
    std::string info_link;
    std::string canonical_link;
    std::string description;

    std::string location;
    std::string condition;
    std::string notes;

    auto isbn_input = Input(&isbn, "978...");
    auto title_input = Input(&title, "Titel");
    auto subtitle_input = Input(&subtitle, "Untertitel");
    auto authors_input = Input(&authors, "Autor");
    auto publisher_input = Input(&publisher, "Verlag");
    auto published_date_input = Input(&published_date, "YYYY-MM-DD");
    auto language_input = Input(&language, "de");
    auto page_count_input = Input(&page_count, "Seiten");
    auto categories_input = Input(&categories, "Genres/Kategorien");
    auto location_input = Input(&location, "Standort");
    auto condition_input = Input(&condition, "Zustand");
    auto notes_input = Input(&notes, "Notizen");

    auto run_lookup = [&] {
        if (isbn.empty()) {
            status = "Bitte zuerst eine ISBN eingeben.";
            return;
        }

        status = "Suche bei Google Books...";

        try {
            const auto result = buch::lookup::lookup_google_books_by_isbn(isbn, google_books_api_key);

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

            status = "Treffer geladen. Felder können jetzt manuell angepasst werden.";
        } catch (const std::exception& error) {
            status = std::string{"Lookup fehlgeschlagen: "} + error.what();
        }
    };

    auto search_button = Button("Suchen", run_lookup);
    auto quit_button = Button("Beenden", screen.ExitLoopClosure());

    auto form = Container::Vertical({
        isbn_input,
        search_button,
        title_input,
        subtitle_input,
        authors_input,
        publisher_input,
        published_date_input,
        language_input,
        page_count_input,
        categories_input,
        location_input,
        condition_input,
        notes_input,
        quit_button,
    });

    auto renderer = Renderer(form, [&] {
        const auto editable_fields = vbox({
            text("Lookup") | bold,
            separator(),
            labeled_input("ISBN", isbn_input),
            hbox({
                search_button->Render(),
                text("  "),
                quit_button->Render(),
            }),
            separator(),
            text("Bearbeitbare Felder") | bold,
            labeled_input("Titel", title_input),
            labeled_input("Untertitel", subtitle_input),
            labeled_input("Autor", authors_input),
            labeled_input("Verlag", publisher_input),
            labeled_input("Erschienen", published_date_input),
            labeled_input("Sprache", language_input),
            labeled_input("Seiten", page_count_input),
            labeled_input("Kategorien", categories_input),
            separator(),
            text("Manuelle Ergänzungen") | bold,
            labeled_input("Standort", location_input),
            labeled_input("Zustand", condition_input),
            labeled_input("Notizen", notes_input),
        }) | flex;

        const auto api_fields = vbox({
            text("API-Rohfelder") | bold,
            separator(),
            readonly_field("Google Volume ID", google_volume_id),
            readonly_field("ISBN-10", isbn_10),
            readonly_field("ISBN-13", isbn_13),
            readonly_field("Altersfreigabe", maturity_rating),
            readonly_field("Cover", cover_url),
            readonly_field("Info-Link", info_link),
            readonly_field("Canonical-Link", canonical_link),
            separator(),
            text("Beschreibung/Snippet") | bold,
            paragraph(description.empty() ? "-" : description),
        }) | flex;

        return vbox({
            text("Buchhaltung - ISBN Import") | bold | center,
            separator(),
            hbox({
                editable_fields | flex,
                separator(),
                api_fields | flex,
            }) | flex,
            separator(),
            text(status),
        }) | border;
    });

    screen.Loop(renderer);
    return 0;
}

} // namespace buch::tui
