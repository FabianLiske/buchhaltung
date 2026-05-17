#include "BookImportService.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace buch::services {
namespace {

class Statement {
public:
    Statement(sqlite3* handle, std::string_view sql)
        : handle_{handle} {
        if (sqlite3_prepare_v2(handle_, sql.data(), static_cast<int>(sql.size()), &statement_, nullptr) != SQLITE_OK) {
            throw std::runtime_error(std::string{"Could not prepare SQL: "} + sqlite3_errmsg(handle_));
        }
    }

    ~Statement() {
        sqlite3_finalize(statement_);
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind(int index, const std::string& value) {
        if (sqlite3_bind_text(statement_, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK) {
            throw std::runtime_error(std::string{"Could not bind text: "} + sqlite3_errmsg(handle_));
        }
    }

    void bind_optional(int index, const std::optional<std::string>& value) {
        if (value.has_value()) {
            bind(index, *value);
            return;
        }
        bind_null(index);
    }

    void bind_optional_int(int index, const std::optional<int>& value) {
        if (value.has_value()) {
            bind_int(index, *value);
            return;
        }
        bind_null(index);
    }

    void bind_optional_double(int index, const std::optional<double>& value) {
        if (value.has_value()) {
            if (sqlite3_bind_double(statement_, index, *value) != SQLITE_OK) {
                throw std::runtime_error(std::string{"Could not bind double: "} + sqlite3_errmsg(handle_));
            }
            return;
        }
        bind_null(index);
    }

    void bind_int(int index, int value) {
        if (sqlite3_bind_int(statement_, index, value) != SQLITE_OK) {
            throw std::runtime_error(std::string{"Could not bind int: "} + sqlite3_errmsg(handle_));
        }
    }

    void bind_null(int index) {
        if (sqlite3_bind_null(statement_, index) != SQLITE_OK) {
            throw std::runtime_error(std::string{"Could not bind null: "} + sqlite3_errmsg(handle_));
        }
    }

    bool step_row() {
        const int result = sqlite3_step(statement_);
        if (result == SQLITE_ROW) {
            return true;
        }
        if (result == SQLITE_DONE) {
            return false;
        }
        throw std::runtime_error(std::string{"Could not step SQL: "} + sqlite3_errmsg(handle_));
    }

    void execute_done() {
        if (sqlite3_step(statement_) != SQLITE_DONE) {
            throw std::runtime_error(std::string{"Could not execute SQL: "} + sqlite3_errmsg(handle_));
        }
    }

    std::string text(int column) const {
        const unsigned char* value = sqlite3_column_text(statement_, column);
        return value == nullptr ? "" : reinterpret_cast<const char*>(value);
    }

    std::optional<std::string> optional_text(int column) const {
        if (sqlite3_column_type(statement_, column) == SQLITE_NULL) {
            return std::nullopt;
        }
        return text(column);
    }

    std::optional<int> optional_int_column(int column) const {
        if (sqlite3_column_type(statement_, column) == SQLITE_NULL) {
            return std::nullopt;
        }
        return sqlite3_column_int(statement_, column);
    }

private:
    sqlite3* handle_{nullptr};
    sqlite3_stmt* statement_{nullptr};
};

std::string trim_copy(std::string value) {
    const auto is_not_space = [](unsigned char character) {
        return !std::isspace(character);
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), is_not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), is_not_space).base(), value.end());
    return value;
}

std::string normalize_isbn(std::string isbn) {
    std::string normalized;
    for (const char character : isbn) {
        if (std::isdigit(static_cast<unsigned char>(character)) || character == 'X' || character == 'x') {
            normalized.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
        }
    }
    return normalized;
}

std::string random_uuid() {
    std::random_device device;
    std::mt19937_64 generator{device()};
    std::uniform_int_distribution<int> byte_distribution{0, 255};

    std::array<unsigned char, 16> bytes{};
    for (auto& byte : bytes) {
        byte = static_cast<unsigned char>(byte_distribution(generator));
    }

    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) {
            output << '-';
        }
        output << std::setw(2) << static_cast<int>(bytes[index]);
    }
    return output.str();
}

std::string first_non_empty(std::initializer_list<std::optional<std::string>> values, std::string fallback) {
    for (const auto& value : values) {
        if (value.has_value() && !trim_copy(*value).empty()) {
            return trim_copy(*value);
        }
    }
    return fallback;
}

std::string upsert_contributor(sqlite3* handle, const ContributorInput& contributor) {
    Statement statement{handle, "SELECT id FROM contributors WHERE lower(name) = lower(?) LIMIT 1;"};
    statement.bind(1, contributor.name);
    if (statement.step_row()) {
        const auto id = statement.text(0);
        Statement update{handle, R"sql(
UPDATE contributors
SET
    sort_name = COALESCE(?, sort_name),
    birth_year = COALESCE(?, birth_year),
    death_year = COALESCE(?, death_year),
    notes = COALESCE(?, notes)
WHERE id = ?;
)sql"};
        update.bind_optional(1, contributor.sort_name);
        update.bind_optional_int(2, contributor.birth_year);
        update.bind_optional_int(3, contributor.death_year);
        update.bind_optional(4, contributor.notes);
        update.bind(5, id);
        update.execute_done();
        return id;
    }

    const auto id = random_uuid();
    Statement insert{handle, R"sql(
INSERT INTO contributors (id, name, sort_name, birth_year, death_year, notes)
VALUES (?, ?, ?, ?, ?, ?);
)sql"};
    insert.bind(1, id);
    insert.bind(2, contributor.name);
    insert.bind_optional(3, contributor.sort_name.has_value() ? contributor.sort_name : std::optional<std::string>{contributor.name});
    insert.bind_optional_int(4, contributor.birth_year);
    insert.bind_optional_int(5, contributor.death_year);
    insert.bind_optional(6, contributor.notes);
    insert.execute_done();
    return id;
}

std::string contributor_id_by_name(sqlite3* handle, const std::string& name) {
    return upsert_contributor(handle, ContributorInput{
        .role = "author",
        .name = name,
        .sort_name = name,
        .birth_year = std::nullopt,
        .death_year = std::nullopt,
        .notes = std::nullopt,
    });
}

std::optional<std::string> find_genre_id(sqlite3* handle, const std::string& name, const std::optional<std::string>& parent_id) {
    const std::string sql = parent_id.has_value()
        ? "SELECT id FROM genres WHERE lower(name) = lower(?) AND parent_genre_id = ? LIMIT 1;"
        : "SELECT id FROM genres WHERE lower(name) = lower(?) AND parent_genre_id IS NULL LIMIT 1;";
    Statement statement{handle, sql};
    statement.bind(1, name);
    if (parent_id.has_value()) {
        statement.bind(2, *parent_id);
    }
    if (statement.step_row()) {
        return statement.text(0);
    }
    return std::nullopt;
}

std::string upsert_genre(
    sqlite3* handle,
    const std::string& name,
    const std::optional<std::string>& parent_id,
    const std::optional<std::string>& description = std::nullopt,
    const std::optional<std::string>& notes = std::nullopt) {
    if (const auto existing = find_genre_id(handle, name, parent_id); existing.has_value()) {
        Statement update{handle, R"sql(
UPDATE genres
SET
    description = COALESCE(?, description),
    notes = COALESCE(?, notes)
WHERE id = ?;
)sql"};
        update.bind_optional(1, description);
        update.bind_optional(2, notes);
        update.bind(3, *existing);
        update.execute_done();
        return *existing;
    }

    const auto id = random_uuid();
    Statement insert{handle, "INSERT INTO genres (id, name, parent_genre_id, description, notes) VALUES (?, ?, ?, ?, ?);"};
    insert.bind(1, id);
    insert.bind(2, name);
    insert.bind_optional(3, parent_id);
    insert.bind_optional(4, description);
    insert.bind_optional(5, notes);
    insert.execute_done();
    return id;
}

std::string location_type_for_depth(std::size_t depth) {
    if (depth == 0) {
        return "Zimmer";
    }
    if (depth == 1) {
        return "Regal";
    }
    if (depth == 2) {
        return "Fach";
    }
    return "Reihe";
}

std::optional<std::string> find_location_id(sqlite3* handle, const std::string& name, const std::optional<std::string>& parent_id) {
    const std::string sql = parent_id.has_value()
        ? "SELECT id FROM locations WHERE lower(name) = lower(?) AND parent_location_id = ? LIMIT 1;"
        : "SELECT id FROM locations WHERE lower(name) = lower(?) AND parent_location_id IS NULL LIMIT 1;";
    Statement statement{handle, sql};
    statement.bind(1, name);
    if (parent_id.has_value()) {
        statement.bind(2, *parent_id);
    }
    if (statement.step_row()) {
        return statement.text(0);
    }
    return std::nullopt;
}

std::optional<std::string> upsert_location_path(sqlite3* handle, const std::optional<std::string>& location_path) {
    if (!location_path.has_value()) {
        return std::nullopt;
    }

    const auto parts = split_list(*location_path);
    if (parts.empty()) {
        return std::nullopt;
    }

    std::optional<std::string> parent_id;
    std::optional<std::string> current_id;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (const auto existing = find_location_id(handle, parts[index], parent_id); existing.has_value()) {
            current_id = existing;
            parent_id = existing;
            continue;
        }

        const auto id = random_uuid();
        Statement insert{handle, "INSERT INTO locations (id, name, type, parent_location_id, sort_order) VALUES (?, ?, ?, ?, ?);"};
        insert.bind(1, id);
        insert.bind(2, parts[index]);
        insert.bind(3, location_type_for_depth(index));
        insert.bind_optional(4, parent_id);
        insert.bind_int(5, static_cast<int>(index));
        insert.execute_done();

        current_id = id;
        parent_id = id;
    }

    return current_id;
}

void update_location_details(sqlite3* handle, const std::optional<std::string>& location_id, const ImportRequest& request) {
    if (!location_id.has_value()) {
        return;
    }

    Statement update{handle, R"sql(
UPDATE locations
SET
    visual_x = COALESCE(?, visual_x),
    visual_y = COALESCE(?, visual_y),
    visual_z = COALESCE(?, visual_z),
    visual_width = COALESCE(?, visual_width),
    visual_height = COALESCE(?, visual_height),
    visual_depth = COALESCE(?, visual_depth),
    description = COALESCE(?, description),
    notes = COALESCE(?, notes)
WHERE id = ?;
)sql"};
    update.bind_optional_double(1, request.location_visual_x);
    update.bind_optional_double(2, request.location_visual_y);
    update.bind_optional_double(3, request.location_visual_z);
    update.bind_optional_double(4, request.location_visual_width);
    update.bind_optional_double(5, request.location_visual_height);
    update.bind_optional_double(6, request.location_visual_depth);
    update.bind_optional(7, request.location_description);
    update.bind_optional(8, request.location_notes);
    update.bind(9, *location_id);
    update.execute_done();
}

std::string upsert_series(sqlite3* handle, const ImportRequest& request) {
    if (request.existing_series_id.has_value()) {
        return *request.existing_series_id;
    }

    if (!request.new_series_name.has_value()) {
        return "";
    }

    Statement lookup{handle, "SELECT id FROM series WHERE lower(name) = lower(?) LIMIT 1;"};
    lookup.bind(1, *request.new_series_name);
    if (lookup.step_row()) {
        const auto id = lookup.text(0);
        Statement update{handle, R"sql(
UPDATE series
SET
    original_title = COALESCE(?, original_title),
    description = COALESCE(?, description),
    notes = COALESCE(?, notes)
WHERE id = ?;
)sql"};
        update.bind_optional(1, request.series_original_title);
        update.bind_optional(2, request.series_description);
        update.bind_optional(3, request.series_notes);
        update.bind(4, id);
        update.execute_done();
        return id;
    }

    const auto id = random_uuid();
    Statement insert{handle, "INSERT INTO series (id, name, original_title, description, notes) VALUES (?, ?, ?, ?, ?);"};
    insert.bind(1, id);
    insert.bind(2, *request.new_series_name);
    insert.bind_optional(3, request.series_original_title);
    insert.bind_optional(4, request.series_description);
    insert.bind_optional(5, request.series_notes);
    insert.execute_done();
    return id;
}

void insert_work_series(sqlite3* handle, const std::string& work_id, const std::string& series_id, const ImportRequest& request) {
    if (series_id.empty()) {
        return;
    }

    Statement insert{handle, R"sql(
INSERT INTO work_series (work_id, series_id, season, position, position_label, notes)
VALUES (?, ?, ?, ?, ?, ?)
ON CONFLICT(work_id, series_id) DO UPDATE SET
    season = excluded.season,
    position = excluded.position,
    position_label = excluded.position_label,
    notes = excluded.notes;
)sql"};
    insert.bind(1, work_id);
    insert.bind(2, series_id);
    insert.bind_optional_int(3, request.series_season);
    insert.bind_optional_double(4, request.series_position);
    insert.bind_optional(5, request.series_position_label);
    insert.bind_optional(6, request.work_series_notes);
    insert.execute_done();
}

void upsert_reading_status(sqlite3* handle, const std::string& work_id, const ImportRequest& request) {
    if (!request.reading_status.has_value()
        && !request.reading_started_date.has_value()
        && !request.reading_finished_date.has_value()
        && !request.rating.has_value()
        && !request.reading_notes.has_value()) {
        return;
    }

    const auto status = request.reading_status.value_or("ungelesen");
    const auto language = request.language_code.empty() ? "und" : request.language_code;
    Statement insert{handle, R"sql(
INSERT INTO work_reading_status (work_id, language_code, status, started_date, finished_date, rating, notes)
VALUES (?, ?, ?, ?, ?, ?, ?)
ON CONFLICT(work_id, language_code) DO UPDATE SET
    status = excluded.status,
    started_date = excluded.started_date,
    finished_date = excluded.finished_date,
    rating = excluded.rating,
    notes = excluded.notes;
)sql"};
    insert.bind(1, work_id);
    insert.bind(2, language);
    insert.bind(3, status);
    insert.bind_optional(4, request.reading_started_date);
    insert.bind_optional(5, request.reading_finished_date);
    insert.bind_optional_int(6, request.rating);
    insert.bind_optional(7, request.reading_notes);
    insert.execute_done();
}

void insert_copy(sqlite3* handle, const std::string& copy_id, const std::string& isbn, const ImportRequest& request, const std::optional<std::string>& location_id) {
    Statement insert{handle, R"sql(
INSERT INTO copies (
    id,
    edition_isbn,
    location_id,
    position_in_location,
    acquired_date,
    acquired_where,
    condition,
    borrowed_from,
    lent_to,
    notes
) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
)sql"};
    insert.bind(1, copy_id);
    insert.bind(2, isbn);
    insert.bind_optional(3, location_id);
    insert.bind_optional_int(4, request.position_in_location);
    insert.bind_optional(5, request.acquired_date);
    insert.bind_optional(6, request.acquired_where);
    insert.bind_optional(7, request.condition);
    insert.bind_optional(8, request.borrowed_from);
    insert.bind_optional(9, request.lent_to);
    insert.bind_optional(10, request.copy_notes);
    insert.execute_done();
}

void link_genre(sqlite3* handle, const std::string& work_id, const ImportRequest& request) {
    if (!request.genre_name.has_value()) {
        return;
    }

    std::optional<std::string> parent_id;
    if (request.parent_genre_name.has_value()) {
        parent_id = upsert_genre(handle, *request.parent_genre_name, std::nullopt);
    }
    const auto genre_id = upsert_genre(handle, *request.genre_name, parent_id, request.genre_description, request.genre_notes);

    Statement insert{handle, R"sql(
INSERT INTO work_genres (work_id, genre_id, primary_genre)
VALUES (?, ?, 1)
ON CONFLICT(work_id, genre_id) DO UPDATE SET primary_genre = 1;
)sql"};
    insert.bind(1, work_id);
    insert.bind(2, genre_id);
    insert.execute_done();
}

void link_authors(sqlite3* handle, const std::string& work_id, const std::vector<std::string>& authors) {
    int order = 0;
    for (const auto& raw_author : authors) {
        const auto author = trim_copy(raw_author);
        if (author.empty()) {
            continue;
        }

        const auto contributor_id = contributor_id_by_name(handle, author);
        Statement link{handle, R"sql(
INSERT INTO work_contributors (work_id, contributor_id, role, contributor_order)
VALUES (?, ?, 'author', ?)
ON CONFLICT(work_id, contributor_id, role) DO UPDATE SET contributor_order = excluded.contributor_order;
)sql"};
        link.bind(1, work_id);
        link.bind(2, contributor_id);
        link.bind_int(3, order++);
        link.execute_done();
    }
}

void link_edition_contributors(sqlite3* handle, const std::string& isbn, const std::vector<ContributorInput>& contributors) {
    int order = 0;
    for (const auto& contributor : contributors) {
        if (trim_copy(contributor.name).empty() || trim_copy(contributor.role).empty()) {
            continue;
        }

        const auto contributor_id = upsert_contributor(handle, contributor);
        Statement link{handle, R"sql(
INSERT INTO edition_contributors (edition_isbn, contributor_id, role, contributor_order)
VALUES (?, ?, ?, ?)
ON CONFLICT(edition_isbn, contributor_id, role) DO UPDATE SET contributor_order = excluded.contributor_order;
)sql"};
        link.bind(1, isbn);
        link.bind(2, contributor_id);
        link.bind(3, contributor.role);
        link.bind_int(4, order++);
        link.execute_done();
    }
}

} // namespace

BookImportService::BookImportService(db::Database& database)
    : database_{database} {
}

std::optional<ExistingEdition> BookImportService::find_edition_by_isbn(const std::string& isbn) const {
    Statement statement{database_.handle(), R"sql(
SELECT
    e.isbn,
    e.title,
    e.subtitle,
    e.language_code,
    e.publisher,
    e.publication_year,
    e.page_count,
    w.id,
    w.canonical_title
FROM editions e
JOIN works w ON w.id = e.work_id
WHERE e.isbn = ?
LIMIT 1;
)sql"};
    statement.bind(1, normalize_isbn(isbn));

    if (!statement.step_row()) {
        return std::nullopt;
    }

    ExistingEdition edition;
    edition.isbn = statement.text(0);
    edition.title = statement.text(1);
    edition.subtitle = statement.optional_text(2);
    edition.language_code = statement.text(3);
    edition.publisher = statement.optional_text(4);
    edition.publication_year = statement.optional_int_column(5);
    edition.page_count = statement.optional_int_column(6);
    edition.work_id = statement.text(7);
    edition.work_title = statement.text(8);

    Statement authors{database_.handle(), R"sql(
SELECT c.name
FROM contributors c
JOIN work_contributors wc ON wc.contributor_id = c.id
WHERE wc.work_id = ? AND wc.role = 'author'
ORDER BY wc.contributor_order, c.name;
)sql"};
    authors.bind(1, edition.work_id);
    while (authors.step_row()) {
        edition.authors.push_back(authors.text(0));
    }

    return edition;
}

std::vector<WorkSuggestion> BookImportService::suggest_works_by_authors(const std::vector<std::string>& authors) const {
    std::map<std::string, WorkSuggestion> suggestions;

    for (const auto& raw_author : authors) {
        const auto author = trim_copy(raw_author);
        if (author.empty()) {
            continue;
        }

        Statement statement{database_.handle(), R"sql(
SELECT DISTINCT w.id, w.canonical_title
FROM works w
JOIN work_contributors wc ON wc.work_id = w.id
JOIN contributors c ON c.id = wc.contributor_id
WHERE wc.role = 'author' AND lower(c.name) = lower(?)
ORDER BY w.canonical_title
LIMIT 20;
)sql"};
        statement.bind(1, author);
        while (statement.step_row()) {
            const auto work_id = statement.text(0);
            suggestions.try_emplace(work_id, WorkSuggestion{work_id, statement.text(1), {}});
        }
    }

    for (auto& [work_id, suggestion] : suggestions) {
        Statement author_statement{database_.handle(), R"sql(
SELECT c.name
FROM contributors c
JOIN work_contributors wc ON wc.contributor_id = c.id
WHERE wc.work_id = ? AND wc.role = 'author'
ORDER BY wc.contributor_order, c.name;
)sql"};
        author_statement.bind(1, work_id);
        while (author_statement.step_row()) {
            suggestion.authors.push_back(author_statement.text(0));
        }
    }

    std::vector<WorkSuggestion> result;
    for (auto& [_, suggestion] : suggestions) {
        result.push_back(std::move(suggestion));
    }
    return result;
}

std::vector<SeriesOption> BookImportService::list_series() const {
    std::vector<SeriesOption> result;
    Statement statement{database_.handle(), "SELECT id, name FROM series ORDER BY lower(name);"};
    while (statement.step_row()) {
        result.push_back({statement.text(0), statement.text(1)});
    }
    return result;
}

ImportResult BookImportService::save_import(const ImportRequest& request) const {
    if (normalize_isbn(request.isbn).empty()) {
        throw std::runtime_error("ISBN fehlt.");
    }
    if (trim_copy(request.title).empty() && !request.existing_work_id.has_value()) {
        throw std::runtime_error("Titel fehlt.");
    }
    if (trim_copy(request.language_code).empty()) {
        throw std::runtime_error("Sprache fehlt.");
    }

    const auto isbn = normalize_isbn(request.isbn);
    const auto existing_edition = find_edition_by_isbn(isbn);

    ImportResult result;
    result.edition_isbn = isbn;
    result.copy_id = random_uuid();

    try {
        database_.execute("BEGIN;");

        std::string work_id;
        bool created_work = false;
        bool created_edition = false;

        if (existing_edition.has_value()) {
            work_id = existing_edition->work_id;
        } else if (request.existing_work_id.has_value()) {
            work_id = *request.existing_work_id;
        } else {
            work_id = random_uuid();
            created_work = true;

            const auto canonical_title = first_non_empty({request.canonical_title, std::optional<std::string>{request.title}}, "Unbenannt");
            Statement insert_work{database_.handle(), R"sql(
INSERT INTO works (
    id,
    canonical_title,
    original_title,
    original_language_code,
    first_published_year,
    description,
    age_rating,
    notes
) VALUES (?, ?, ?, ?, ?, ?, ?, ?);
)sql"};
            insert_work.bind(1, work_id);
            insert_work.bind(2, canonical_title);
            insert_work.bind_optional(3, request.original_title);
            insert_work.bind_optional(4, request.original_language_code);
            insert_work.bind_optional_int(5, publication_year_from_date(request.published_date) == 0 ? std::nullopt : std::optional<int>{publication_year_from_date(request.published_date)});
            insert_work.bind_optional(6, request.description);
            insert_work.bind_optional(7, request.age_rating);
            insert_work.bind_optional(8, request.work_notes);
            insert_work.execute_done();
        }

        if (!existing_edition.has_value()) {
            created_edition = true;
            Statement insert_edition{database_.handle(), R"sql(
INSERT INTO editions (
    isbn,
    work_id,
    title,
    subtitle,
    language_code,
    publisher,
    publication_year,
    edition_name,
    format,
    page_count,
    cover_url,
    notes
) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
)sql"};
            insert_edition.bind(1, isbn);
            insert_edition.bind(2, work_id);
            insert_edition.bind(3, request.title);
            insert_edition.bind_optional(4, request.subtitle);
            insert_edition.bind(5, request.language_code);
            insert_edition.bind_optional(6, request.publisher);
            const int publication_year = publication_year_from_date(request.published_date);
            insert_edition.bind_optional_int(7, publication_year == 0 ? std::nullopt : std::optional<int>{publication_year});
            insert_edition.bind_optional(8, request.edition_name);
            insert_edition.bind_optional(9, request.format);
            insert_edition.bind_optional_int(10, request.page_count);
            insert_edition.bind_optional(11, request.cover_url);
            insert_edition.bind_optional(12, request.edition_notes);
            insert_edition.execute_done();

            link_authors(database_.handle(), work_id, request.authors);
        }

        link_edition_contributors(database_.handle(), isbn, request.edition_contributors);

        const auto series_id = upsert_series(database_.handle(), request);
        insert_work_series(database_.handle(), work_id, series_id, request);
        link_genre(database_.handle(), work_id, request);
        upsert_reading_status(database_.handle(), work_id, request);

        const auto location_id = upsert_location_path(database_.handle(), request.location_path);
        update_location_details(database_.handle(), location_id, request);
        insert_copy(database_.handle(), result.copy_id, isbn, request, location_id);

        database_.execute("COMMIT;");

        result.work_id = work_id;
        result.created_work = created_work;
        result.created_edition = created_edition;
        return result;
    } catch (...) {
        database_.execute("ROLLBACK;");
        throw;
    }
}

std::vector<std::string> split_list(const std::string& value) {
    std::vector<std::string> result;
    std::string item;

    const auto push_item = [&] {
        item = trim_copy(item);
        if (!item.empty()) {
            result.push_back(item);
        }
        item.clear();
    };

    for (const char character : value) {
        if (character == ',' || character == '/' || character == '>' || character == ';') {
            push_item();
        } else {
            item.push_back(character);
        }
    }
    push_item();

    return result;
}

std::optional<std::string> optional_trimmed(std::string value) {
    value = trim_copy(std::move(value));
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

std::optional<int> optional_int(std::string value) {
    value = trim_copy(std::move(value));
    if (value.empty()) {
        return std::nullopt;
    }

    int result = 0;
    const auto parse_result = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parse_result.ec != std::errc{} || parse_result.ptr != value.data() + value.size()) {
        throw std::runtime_error("Keine gültige Ganzzahl: " + value);
    }
    return result;
}

std::optional<double> optional_double(std::string value) {
    value = trim_copy(std::move(value));
    if (value.empty()) {
        return std::nullopt;
    }

    char* end = nullptr;
    const double result = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0') {
        throw std::runtime_error("Keine gültige Dezimalzahl: " + value);
    }
    return result;
}

std::vector<ContributorInput> parse_contributors(const std::string& value, const std::string& default_role) {
    std::vector<ContributorInput> contributors;
    std::string entry;

    const auto flush_entry = [&] {
        entry = trim_copy(entry);
        if (entry.empty()) {
            return;
        }

        auto role = default_role;
        auto payload = entry;
        if (const auto role_separator = entry.find(':'); role_separator != std::string::npos) {
            role = trim_copy(entry.substr(0, role_separator));
            payload = trim_copy(entry.substr(role_separator + 1));
        }

        std::vector<std::string> parts;
        std::string part;
        for (const char character : payload) {
            if (character == '|') {
                parts.push_back(trim_copy(part));
                part.clear();
            } else {
                part.push_back(character);
            }
        }
        parts.push_back(trim_copy(part));

        if (parts.empty() || parts[0].empty()) {
            return;
        }

        ContributorInput contributor;
        contributor.role = role.empty() ? default_role : role;
        contributor.name = parts[0];
        if (parts.size() > 1) {
            contributor.sort_name = optional_trimmed(parts[1]);
        }
        if (parts.size() > 2) {
            contributor.birth_year = optional_int(parts[2]);
        }
        if (parts.size() > 3) {
            contributor.death_year = optional_int(parts[3]);
        }
        if (parts.size() > 4) {
            contributor.notes = optional_trimmed(parts[4]);
        }
        contributors.push_back(std::move(contributor));
    };

    for (const char character : value) {
        if (character == ';') {
            flush_entry();
            entry.clear();
        } else {
            entry.push_back(character);
        }
    }
    flush_entry();

    return contributors;
}

int publication_year_from_date(const std::optional<std::string>& published_date) {
    if (!published_date.has_value() || published_date->size() < 4) {
        return 0;
    }

    const auto year_text = published_date->substr(0, 4);
    int year = 0;
    const auto result = std::from_chars(year_text.data(), year_text.data() + year_text.size(), year);
    if (result.ec != std::errc{}) {
        return 0;
    }
    return year;
}

} // namespace buch::services
