#include "services/Isbn.hpp"

#include <cctype>

namespace buch::services {

std::string normalize_isbn(const std::string& value) {
    std::string result;
    for (const char character : value) {
        if (character == '-' || character == '_' || std::isspace(static_cast<unsigned char>(character))) {
            continue;
        }
        if (character == 'x') {
            result.push_back('X');
        } else {
            result.push_back(character);
        }
    }
    return result;
}

bool is_valid_isbn(const std::string& normalized) {
    if (normalized.size() == 10) {
        int sum = 0;
        for (std::size_t index = 0; index < normalized.size(); ++index) {
            int value = 0;
            if (index == 9 && normalized[index] == 'X') {
                value = 10;
            } else if (std::isdigit(static_cast<unsigned char>(normalized[index]))) {
                value = normalized[index] - '0';
            } else {
                return false;
            }
            sum += static_cast<int>(10 - index) * value;
        }
        return sum % 11 == 0;
    }

    if (normalized.size() == 13) {
        int sum = 0;
        for (std::size_t index = 0; index < normalized.size(); ++index) {
            if (!std::isdigit(static_cast<unsigned char>(normalized[index]))) {
                return false;
            }
            const int value = normalized[index] - '0';
            sum += value * (index % 2 == 0 ? 1 : 3);
        }
        return sum % 10 == 0;
    }

    return false;
}

std::optional<std::string> isbn_10_to_13(const std::string& isbn_10) {
    if (isbn_10.size() != 10 || !is_valid_isbn(isbn_10)) {
        return std::nullopt;
    }

    std::string result = "978" + isbn_10.substr(0, 9);
    int sum = 0;
    for (std::size_t index = 0; index < result.size(); ++index) {
        sum += (result[index] - '0') * (index % 2 == 0 ? 1 : 3);
    }
    result.push_back(static_cast<char>('0' + ((10 - (sum % 10)) % 10)));
    return result;
}

std::optional<std::string> isbn_13_to_10(const std::string& isbn_13) {
    if (isbn_13.size() != 13 || !isbn_13.starts_with("978") || !is_valid_isbn(isbn_13)) {
        return std::nullopt;
    }

    std::string result = isbn_13.substr(3, 9);
    int sum = 0;
    for (std::size_t index = 0; index < result.size(); ++index) {
        sum += static_cast<int>(10 - index) * (result[index] - '0');
    }
    const int check = (11 - (sum % 11)) % 11;
    result.push_back(check == 10 ? 'X' : static_cast<char>('0' + check));
    return result;
}

} // namespace buch::services
