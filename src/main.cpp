#include "db/Database.hpp"
#include "db/Migrations.hpp"
#include "server/HttpServer.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
        << "  buch [server] [--db <path>] [--host <host>] [--port <port>]\n"
        << '\n'
        << "Environment:\n"
        << "  BUCH_DB_PATH       Default database path\n"
        << "  BUCH_HOST          Default listen host, defaults to 127.0.0.1\n"
        << "  BUCH_PORT          Default listen port, defaults to 8080\n"
        << "  GOOGLE_BOOKS_KEY   Optional Google Books API key\n";
}

std::filesystem::path default_database_path() {
    if (const char* database_path = std::getenv("BUCH_DB_PATH"); database_path != nullptr && std::string_view{database_path}.size() > 0) {
        return std::filesystem::path{database_path};
    }

    if (const char* xdg_data_home = std::getenv("XDG_DATA_HOME"); xdg_data_home != nullptr && std::string_view{xdg_data_home}.size() > 0) {
        return std::filesystem::path{xdg_data_home} / "buchhaltung" / "buchhaltung.sqlite";
    }

    if (const char* home = std::getenv("HOME"); home != nullptr && std::string_view{home}.size() > 0) {
        return std::filesystem::path{home} / ".local" / "share" / "buchhaltung" / "buchhaltung.sqlite";
    }

    return std::filesystem::path{"/data/buchhaltung.sqlite"};
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

std::string trim(std::string value) {
    const auto is_not_space = [](unsigned char character) {
        return !std::isspace(character);
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), is_not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), is_not_space).base(), value.end());
    return value;
}

std::optional<std::string> read_env_file_value_from(const std::filesystem::path& path, std::string_view key) {
    std::ifstream env_file{path};
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

std::optional<std::string> read_env_file_value(std::string_view key) {
    const std::vector<std::filesystem::path> env_paths{
        std::filesystem::current_path() / ".env",
        std::filesystem::path{BUCH_SOURCE_DIR} / ".env",
    };

    for (const auto& env_path : env_paths) {
        if (const auto value = read_env_file_value_from(env_path, key); value.has_value()) {
            return value;
        }
    }

    return std::nullopt;
}

std::string environment_or_file(std::string_view key) {
    const std::string key_string{key};
    if (const char* value = std::getenv(key_string.c_str()); value != nullptr && std::string_view{value}.size() > 0) {
        return value;
    }

    return read_env_file_value(key).value_or("");
}

int parse_port(std::string_view value) {
    try {
        const int port = std::stoi(std::string{value});
        if (port <= 0 || port > 65535) {
            throw std::runtime_error("Port must be between 1 and 65535.");
        }
        return port;
    } catch (const std::invalid_argument&) {
        throw std::runtime_error("Port must be a number.");
    } catch (const std::out_of_range&) {
        throw std::runtime_error("Port is out of range.");
    }
}

struct Args {
    std::filesystem::path database_path{default_database_path()};
    std::string host{"127.0.0.1"};
    int port{8080};
    bool help{false};
    bool version{false};
};

std::string next_value(int& index, int argc, char** argv, std::string_view option) {
    if (index + 1 >= argc) {
        throw std::runtime_error(std::string{option} + " requires a value.");
    }
    return argv[++index];
}

Args parse_args(int argc, char** argv) {
    Args args;

    if (const char* host = std::getenv("BUCH_HOST"); host != nullptr && std::string_view{host}.size() > 0) {
        args.host = host;
    }

    if (const char* port = std::getenv("BUCH_PORT"); port != nullptr && std::string_view{port}.size() > 0) {
        args.port = parse_port(port);
    }

    for (int index = 1; index < argc; ++index) {
        const std::string_view arg{argv[index]};

        if (arg == "--help" || arg == "-h") {
            args.help = true;
            continue;
        }

        if (arg == "--version" || arg == "-v") {
            args.version = true;
            continue;
        }

        if (arg == "server") {
            continue;
        }

        if (arg == "--db") {
            args.database_path = std::filesystem::path{next_value(index, argc, argv, arg)};
            continue;
        }

        if (arg.starts_with("--db=")) {
            args.database_path = std::filesystem::path{std::string{arg.substr(5)}};
            continue;
        }

        if (arg == "--host") {
            args.host = next_value(index, argc, argv, arg);
            continue;
        }

        if (arg.starts_with("--host=")) {
            args.host = std::string{arg.substr(7)};
            continue;
        }

        if (arg == "--port") {
            args.port = parse_port(next_value(index, argc, argv, arg));
            continue;
        }

        if (arg.starts_with("--port=")) {
            args.port = parse_port(arg.substr(7));
            continue;
        }

        throw std::runtime_error("Unknown argument: " + std::string{arg});
    }

    return args;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto args = parse_args(argc, argv);

        if (args.help) {
            print_help();
            return 0;
        }

        if (args.version) {
            std::cout << "buch " << version << '\n';
            return 0;
        }

        ensure_parent_directory(args.database_path);

        buch::db::Database database{args.database_path};
        buch::db::apply_migrations(database);

        buch::server::run_http_server(database, buch::server::ServerConfig{
            .host = args.host,
            .port = args.port,
            .google_books_api_key = environment_or_file("GOOGLE_BOOKS_KEY"),
        });

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Fehler: " << error.what() << '\n';
        return 1;
    }
}
