#pragma once

#include <optional>
#include <string>
#include <vector>

namespace buch::lookup {

struct BookLookupResult {
    std::string google_volume_id;
    std::optional<std::string> title;
    std::optional<std::string> subtitle;
    std::vector<std::string> authors;
    std::optional<std::string> publisher;
    std::optional<std::string> published_date;
    std::optional<std::string> language;
    std::optional<std::string> isbn_10;
    std::optional<std::string> isbn_13;
    std::optional<int> page_count;
    std::vector<std::string> categories;
    std::optional<std::string> maturity_rating;
    std::optional<std::string> description;
    std::optional<std::string> thumbnail_url;
    std::optional<std::string> info_link;
    std::optional<std::string> canonical_link;
};

BookLookupResult lookup_google_books_by_isbn(const std::string& isbn, const std::string& api_key);

} // namespace buch::lookup
