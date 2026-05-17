#pragma once

#include "db/Database.hpp"

#include <optional>
#include <string>
#include <vector>

namespace buch::services {

struct ExistingEdition {
    std::string isbn;
    std::string title;
    std::optional<std::string> subtitle;
    std::string language_code;
    std::optional<std::string> publisher;
    std::optional<int> publication_year;
    std::optional<int> page_count;
    std::string work_id;
    std::string work_title;
    std::vector<std::string> authors;
};

struct WorkSuggestion {
    std::string id;
    std::string title;
    std::vector<std::string> authors;
};

struct SeriesOption {
    std::string id;
    std::string name;
};

struct ContributorInput {
    std::string role;
    std::string name;
    std::optional<std::string> sort_name;
    std::optional<int> birth_year;
    std::optional<int> death_year;
    std::optional<std::string> notes;
};

struct ImportRequest {
    std::string isbn;
    std::string title;
    std::optional<std::string> subtitle;
    std::vector<std::string> authors;
    std::vector<ContributorInput> edition_contributors;
    std::optional<std::string> publisher;
    std::optional<std::string> published_date;
    std::string language_code;
    std::optional<std::string> edition_name;
    std::optional<std::string> format;
    std::optional<int> page_count;
    std::optional<std::string> cover_url;
    std::optional<std::string> edition_notes;
    std::optional<std::string> description;
    std::optional<std::string> age_rating;

    std::optional<std::string> existing_work_id;
    std::optional<std::string> canonical_title;
    std::optional<std::string> original_title;
    std::optional<std::string> original_language_code;
    std::optional<std::string> work_notes;

    std::optional<std::string> existing_series_id;
    std::optional<std::string> new_series_name;
    std::optional<std::string> series_original_title;
    std::optional<std::string> series_description;
    std::optional<std::string> series_notes;
    std::optional<int> series_season;
    std::optional<double> series_position;
    std::optional<std::string> series_position_label;
    std::optional<std::string> work_series_notes;

    std::optional<std::string> parent_genre_name;
    std::optional<std::string> genre_name;
    std::optional<std::string> genre_description;
    std::optional<std::string> genre_notes;

    std::optional<std::string> reading_status;
    std::optional<std::string> reading_started_date;
    std::optional<std::string> reading_finished_date;
    std::optional<int> rating;
    std::optional<std::string> reading_notes;

    std::optional<std::string> location_path;
    std::optional<std::string> location_description;
    std::optional<std::string> location_notes;
    std::optional<double> location_visual_x;
    std::optional<double> location_visual_y;
    std::optional<double> location_visual_z;
    std::optional<double> location_visual_width;
    std::optional<double> location_visual_height;
    std::optional<double> location_visual_depth;
    std::optional<int> position_in_location;
    std::optional<std::string> condition;
    std::optional<std::string> acquired_date;
    std::optional<std::string> acquired_where;
    std::optional<std::string> borrowed_from;
    std::optional<std::string> lent_to;
    std::optional<std::string> copy_notes;
};

struct ImportResult {
    std::string work_id;
    std::string edition_isbn;
    std::string copy_id;
    bool created_work{false};
    bool created_edition{false};
};

class BookImportService {
public:
    explicit BookImportService(db::Database& database);

    std::optional<ExistingEdition> find_edition_by_isbn(const std::string& isbn) const;
    std::vector<WorkSuggestion> suggest_works_by_authors(const std::vector<std::string>& authors) const;
    std::vector<SeriesOption> list_series() const;
    ImportResult save_import(const ImportRequest& request) const;

private:
    db::Database& database_;
};

std::vector<std::string> split_list(const std::string& value);
std::vector<ContributorInput> parse_contributors(const std::string& value, const std::string& default_role);
std::optional<std::string> optional_trimmed(std::string value);
std::optional<int> optional_int(std::string value);
std::optional<double> optional_double(std::string value);
int publication_year_from_date(const std::optional<std::string>& published_date);

} // namespace buch::services
