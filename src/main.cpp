#include "db/Database.hpp"
#include "db/Migrations.hpp"
#include "lookup/GoogleBooksLookup.hpp"
#include "tui/MainTui.hpp"
#include "tui/add_book/AddBookTui.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view version = "0.1.0";

void print_help() {
    std::cout
        << "buch " << version << '\n'
        << '\n'
        << "Usage:\n"
        << "  buch --help\n"
        << "  buch --version\n"
        << "  buch [--db <path>] init\n"
        << "  buch [--db <path>] reset\n"
        << "  buch lookup-isbn [isbn]\n"
        << "  buch [--db <path>] tui\n"
        << '\n'
        << "Commands:\n"
        << "  init          Datenbank anlegen und Migrationen ausführen\n"
        << "  reset         Datenbank zurücksetzen\n"
        << "  lookup-isbn   Buchdaten über Google Books abrufen und anzeigen\n"
        << "  tui           Interaktive Buchhaltungs-Oberfläche starten\n";
}

std::filesystem::path default_database_path() {
    if (const char* xdg_data_home = std::getenv("XDG_DATA_HOME"); xdg_data_home != nullptr && std::string_view{xdg_data_home}.size() > 0) {
        return std::filesystem::path{xdg_data_home} / "buchhaltung" / "buchhaltung.sqlite";
    }

    if (const char* home = std::getenv("HOME"); home != nullptr && std::string_view{home}.size() > 0) {
        return std::filesystem::path{home} / ".local" / "share" / "buchhaltung" / "buchhaltung.sqlite";
    }

    throw std::runtime_error("Neither XDG_DATA_HOME nor HOME is set; pass --db <path> explicitly.");
}

void ensure_parent_directory(const std::filesystem::path& database_path) {
    if (database_path == ":memory:") {
        return;
    }

    const auto parent = database_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

struct Args {
    std::optional<std::filesystem::path> database_path;
    std::optional<std::string> command;
    std::vector<std::string> command_args;
};

Args parse_args(int argc, char** argv) {
    Args args;

    for (int index = 1; index < argc; ++index) {
        const std::string_view arg{argv[index]};

        if (arg == "--db") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--db requires a path.");
            }

            args.database_path = std::filesystem::path{argv[++index]};
            continue;
        }

        if (arg.starts_with("--db=")) {
            args.database_path = std::filesystem::path{std::string{arg.substr(5)}};
            continue;
        }

        if (!args.command.has_value()) {
            args.command = std::string{arg};
            continue;
        }

        args.command_args.emplace_back(arg);
    }

    return args;
}

std::string trim(std::string value) {
    const auto is_not_space = [](unsigned char character) {
        return !std::isspace(character);
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), is_not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), is_not_space).base(), value.end());
    return value;
}

std::optional<std::string> read_env_file_value(std::string_view key) {
    std::ifstream env_file{".env"};
    if (!env_file) {
        return std::nullopt;
    }

    std::string line;
    while (std::getline(env_file, line)) {
        line = trim(line);
        if (line.empty() || line.starts_with('#')) {
            continue;
        }

        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        const auto name = trim(line.substr(0, separator));
        auto value = trim(line.substr(separator + 1));
        if (name != key) {
            continue;
        }

        if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }

        return value;
    }

    return std::nullopt;
}

std::string google_books_api_key() {
    if (const char* key = std::getenv("GOOGLE_BOOKS_KEY"); key != nullptr && std::string_view{key}.size() > 0) {
        return key;
    }

    if (const auto key = read_env_file_value("GOOGLE_BOOKS_KEY"); key.has_value() && !key->empty()) {
        return *key;
    }

    throw std::runtime_error("GOOGLE_BOOKS_KEY is not set. Export it or add it to .env.");
}

std::string isbn_from_args_or_prompt(const std::vector<std::string>& command_args) {
    if (command_args.size() > 1) {
        throw std::runtime_error("lookup-isbn accepts at most one ISBN.");
    }

    if (!command_args.empty()) {
        return command_args.front();
    }

    std::cout << "ISBN: ";
    std::string isbn;
    std::getline(std::cin, isbn);
    isbn = trim(isbn);
    if (isbn.empty()) {
        throw std::runtime_error("ISBN must not be empty.");
    }

    return isbn;
}

void print_optional_field(std::string_view label, const std::optional<std::string>& value) {
    std::cout << label << ": " << (value.has_value() && !value->empty() ? *value : "-") << '\n';
}

void print_optional_field(std::string_view label, const std::optional<int>& value) {
    if (value.has_value()) {
        std::cout << label << ": " << *value << '\n';
    } else {
        std::cout << label << ": -\n";
    }
}

void print_list_field(std::string_view label, const std::vector<std::string>& values) {
    std::cout << label << ": ";
    if (values.empty()) {
        std::cout << "-\n";
        return;
    }

    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            std::cout << ", ";
        }
        std::cout << values[index];
    }
    std::cout << '\n';
}

int run_init(const std::filesystem::path& database_path) {
    ensure_parent_directory(database_path);

    const buch::db::Database database{database_path};
    buch::db::apply_migrations(database);

    std::cout << "Datenbank ist bereit: " << database_path << '\n';
    return 0;
}

int run_reset(const std::filesystem::path& database_path) {
    ensure_parent_directory(database_path);

    if (database_path != ":memory:" && std::filesystem::exists(database_path)) {
        std::filesystem::remove(database_path);
    }

    const buch::db::Database database{database_path};
    buch::db::apply_migrations(database);

    std::cout << "Datenbank wurde zurückgesetzt: " << database_path << "\n";
    return 0;
}

int run_lookup_isbn(const std::vector<std::string>& command_args) {
    const auto isbn = isbn_from_args_or_prompt(command_args);
    const auto api_key = google_books_api_key();
    const auto result = buch::lookup::lookup_google_books_by_isbn(isbn, api_key);

    std::cout << "Google Books Treffer\n";
    std::cout << "--------------------\n";
    std::cout << "Google Volume ID: " << (result.google_volume_id.empty() ? "-" : result.google_volume_id) << '\n';
    print_optional_field("Titel", result.title);
    print_optional_field("Untertitel", result.subtitle);
    print_list_field("Autoren", result.authors);
    print_optional_field("Verlag", result.publisher);
    print_optional_field("Erscheinungsdatum", result.published_date);
    print_optional_field("Sprache", result.language);
    print_optional_field("ISBN-10", result.isbn_10);
    print_optional_field("ISBN-13", result.isbn_13);
    print_optional_field("Seiten", result.page_count);
    print_list_field("Kategorien", result.categories);
    print_optional_field("Altersfreigabe", result.maturity_rating);
    print_optional_field("Cover", result.thumbnail_url);
    print_optional_field("Info-Link", result.info_link);
    print_optional_field("Canonical-Link", result.canonical_link);
    print_optional_field("Beschreibung/Snippet", result.description);

    return 0;
}

int run_tui(const std::vector<std::string>& command_args, const std::filesystem::path& database_path) {
    if (!command_args.empty()) {
        throw std::runtime_error("tui does not accept arguments yet.");
    }

    ensure_parent_directory(database_path);
    const auto action = buch::tui::run_main_tui();
    if (action == buch::tui::MainMenuAction::AddBook) {
        return buch::tui::add_book::run(google_books_api_key(), database_path);
    }

    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc > 1) {
            const std::string_view first_arg{argv[1]};

            if (first_arg == "--help" || first_arg == "-h") {
                print_help();
                return 0;
            }

            if (first_arg == "--version" || first_arg == "-v") {
                std::cout << "buch " << version << '\n';
                return 0;
            }
        }

        const Args args = parse_args(argc, argv);
        if (!args.command.has_value()) {
            print_help();
            return 0;
        }

        if (args.command == "init") {
            const auto database_path = args.database_path.value_or(default_database_path());
            return run_init(database_path);
        }

        if (args.command == "reset") {
            const auto database_path = args.database_path.value_or(default_database_path());
            return run_reset(database_path);
        }

        if (args.command == "lookup-isbn") {
            return run_lookup_isbn(args.command_args);
        }

        if (args.command == "tui") {
            const auto database_path = args.database_path.value_or(default_database_path());
            return run_tui(args.command_args, database_path);
        }

        std::cerr << "Unbekannter Befehl: " << *args.command << '\n';
        std::cerr << "Nutze 'buch --help' für Hilfe.\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Fehler: " << error.what() << '\n';
        return 1;
    }
}
