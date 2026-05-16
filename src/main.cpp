#include "db/Database.hpp"
#include "db/Migrations.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

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
        << '\n'
        << "Commands:\n"
        << "  init    Datenbank anlegen und Migrationen ausführen\n";
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
    std::optional<std::string_view> command;
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
            args.command = arg;
            continue;
        }

        throw std::runtime_error("Unexpected argument: " + std::string{arg});
    }

    return args;
}

int run_init(const std::filesystem::path& database_path) {
    ensure_parent_directory(database_path);

    const buch::db::Database database{database_path};
    buch::db::apply_migrations(database);

    std::cout << "Datenbank ist bereit: " << database_path << '\n';
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

        const auto database_path = args.database_path.value_or(default_database_path());

        if (args.command == "init") {
            return run_init(database_path);
        }

        std::cerr << "Unbekannter Befehl: " << *args.command << '\n';
        std::cerr << "Nutze 'buch --help' für Hilfe.\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Fehler: " << error.what() << '\n';
        return 1;
    }
}
