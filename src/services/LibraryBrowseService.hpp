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
    std::string series;
    std::string reading_status;
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

struct WorkFilters {
    std::string text;
    std::string author;
    std::string series;
    std::string reading_status;
};

struct WorkDetails {
    std::string id;
    std::string canonical_title;
    std::optional<std::string> original_title;
    std::optional<std::string> original_language_code;
    std::optional<int> first_published_year;
    std::optional<std::string> description;
    std::optional<std::string> age_rating;
    std::optional<std::string> notes;
    std::vector<std::string> authors;
    std::string series;
    std::string genres;
    std::string reading_status;
};

struct EditionDetails {
    std::string isbn;
    std::string title;
    std::optional<std::string> subtitle;
    std::string language_code;
    std::optional<std::string> publisher;
    std::optional<int> publication_year;
    std::optional<std::string> edition_name;
    std::optional<std::string> format;
    std::optional<int> page_count;
    std::optional<std::string> cover_url;
    std::optional<std::string> notes;
};

struct CopyDetails {
    std::string id;
    std::string edition_isbn;
    std::optional<std::string> location_path;
    std::optional<int> position_in_location;
    std::optional<std::string> condition;
    std::optional<std::string> acquired_date;
    std::optional<std::string> acquired_where;
    std::optional<std::string> borrowed_from;
    std::optional<std::string> lent_to;
    std::optional<std::string> notes;
};

struct WorkUpdate {
    std::string id;
    std::string canonical_title;
    std::optional<std::string> original_title;
    std::optional<std::string> original_language_code;
    std::optional<int> first_published_year;
    std::optional<std::string> description;
    std::optional<std::string> age_rating;
    std::optional<std::string> notes;
    std::vector<std::string> authors;
};

struct EditionUpdate {
    std::string isbn;
    std::string title;
    std::optional<std::string> subtitle;
    std::string language_code;
    std::optional<std::string> publisher;
    std::optional<int> publication_year;
    std::optional<std::string> edition_name;
    std::optional<std::string> format;
    std::optional<int> page_count;
    std::optional<std::string> cover_url;
    std::optional<std::string> notes;
};

struct CopyUpdate {
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

    std::vector<WorkListItem> list_works(const WorkFilters& filters) const;
    std::vector<EditionListItem> list_editions_for_work(const std::string& work_id) const;
    std::vector<CopyListItem> list_copies_for_edition(const std::string& edition_isbn) const;
    std::optional<WorkDetails> get_work(const std::string& work_id) const;
    std::optional<EditionDetails> get_edition(const std::string& isbn) const;
    std::optional<CopyDetails> get_copy(const std::string& copy_id) const;
    void update_work(const WorkUpdate& update) const;
    void update_edition(const EditionUpdate& update) const;
    void update_copy(const CopyUpdate& update) const;
    void delete_work(const std::string& work_id) const;
    void delete_edition(const std::string& isbn) const;
    void delete_copy(const std::string& copy_id) const;

private:
    db::Database& database_;
};

} // namespace buch::services
