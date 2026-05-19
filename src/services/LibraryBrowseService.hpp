#pragma once

#include "db/Database.hpp"

#include <optional>
#include <string>
#include <vector>

namespace buch::services {

struct WorkListItem {
    std::string id;
    std::string title;
    std::vector<std::string> authors;
    std::optional<int> first_published_year;
    int edition_count{0};
    int copy_count{0};
};

struct EditionListItem {
    std::string isbn;
    std::string title;
    std::optional<std::string> subtitle;
    std::string language_code;
    std::optional<std::string> publisher;
    std::optional<int> publication_year;
    std::optional<std::string> edition_name;
    std::optional<std::string> format;
    std::optional<int> page_count;
    int copy_count{0};
};

struct CopyListItem {
    std::string id;
    std::optional<std::string> location_path;
    std::optional<int> position_in_location;
    std::optional<std::string> condition;
    std::optional<std::string> acquired_date;
    std::optional<std::string> acquired_where;
    std::optional<std::string> borrowed_from;
    std::optional<std::string> lent_to;
    std::optional<std::string> notes;
};

class LibraryBrowseService {
public:
    explicit LibraryBrowseService(db::Database& database);

    std::vector<WorkListItem> list_works(const std::string& search_query) const;
    std::vector<EditionListItem> list_editions_for_work(const std::string& work_id) const;
    std::vector<CopyListItem> list_copies_for_edition(const std::string& edition_isbn) const;

private:
    db::Database& database_;
};

} // namespace buch::services