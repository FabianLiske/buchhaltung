#include <iostream>
#include <string_view>

namespace {

constexpr std::string_view version = "0.1.0";

void print_help() {
    std::cout
        << "Hello World!\n"
        << "buch " << version << '\n'
        << '\n'
        << "Usage:\n"
        << "  buch --help\n"
        << "  buch --version\n"
        << '\n'
        << "Dieses Binary ist der Startpunkt für die lokale Büchersammlungs-App.\n";
}

}

int main(int argc, char** argv) {
    if (argc > 1) {
        const std::string_view arg{argv[1]};

        if (arg == "--help" || arg == "-h") {
            print_help();
            return 0;
        }

        if (arg == "--version" || arg == "-v") {
            std::cout << "buch " << version << '\n';
            return 0;
        }

        std::cerr << "Unbekanntes Argument: " << arg << '\n';
        std::cerr << "Nutze 'buch --help' für Hilfe.\n";
        return 1;
    }

    print_help();
    return 0;
}
