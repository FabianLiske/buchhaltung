#include "services/LibraryBrowseService.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
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
            throw std::runtime_error(std::string{"Could not bind to text:" } + sqlite3_errmsg(handle_));
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

    int int_column(int column) const {
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

std::vector<std::string> split_path(const std::string& value) {
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
        if (character == '/') {
            push_item();
        } else {
            item.push_back(character);
        }
    }
    push_item();

    return result;
}

std::optional<std::string> empty_to_null(std::optional<std::string> value) {
    if (!value.has_value()) {
        return std::nullopt;
    }
    value = trim_copy(*value);
    if (value->empty()) {
        return std::nullopt;
    }
    return value;
}

std::string contributor_id_by_name(sqlite3* handle, const std::string& name) {
    Statement lookup{handle, "SELECT id FROM contributors WHERE lower(name) = lower(?) LIMIT 1;"};
    lookup.bind(1, name);
    if (lookup.step_row()) {
        return lookup.text(0);
    }

    const auto id = random_uuid();
    Statement insert{handle, "INSERT INTO contributors (id, name, sort_name) VALUES (?, ?, ?);"};
    insert.bind(1, id);
    insert.bind(2, name);
    insert.bind(3, name);
    insert.execute_done();
    return id;
}

void replace_authors(sqlite3* handle, const std::string& work_id, const std::vector<std::string>& authors) {
    Statement remove{handle, "DELETE FROM work_contributors WHERE work_id = ? AND role = 'author';"};
    remove.bind(1, work_id);
    remove.execute_done();

    int order = 0;
    for (const auto& raw_author : authors) {
        const auto author = trim_copy(raw_author);
        if (author.empty()) {
            continue;
        }

        const auto contributor_id = contributor_id_by_name(handle, author);
        Statement insert{handle, R"sql(
INSERT INTO work_contributors (work_id, contributor_id, role, contributor_order)
VALUES (?, ?, 'author', ?)
ON CONFLICT(work_id, contributor_id, role) DO UPDATE SET contributor_order = excluded.contributor_order;
)sql"};
        insert.bind(1, work_id);
        insert.bind(2, contributor_id);
        insert.bind_int(3, order++);
        insert.execute_done();
    }
}

std::vector<std::string> authors_for_work(sqlite3* handle, const std::string& work_id) {
    std::vector<std::string> authors;

    Statement statement{handle, R"sql(
        SELECT c.name
        FROM contributors c
        JOIN work_contributors wc ON wc.contributor_id = c.id
        WHERE wc.work_id = ? AND wc.role = 'author'
        ORDER BY wc.contributor_order, c.name;
    )sql"};

    statement.bind(1, work_id);

    while (statement.step_row()) {
        authors.push_back(statement.text(0));
    }

    return authors;
}

std::string text_for_work(sqlite3* handle, const std::string& work_id, std::string_view sql) {
    Statement statement{handle, sql};
    statement.bind(1, work_id);
    if (statement.step_row()) {
        return statement.text(0);
    }
    return "";
}

std::optional<std::string> location_path_for_id(sqlite3* handle, const std::optional<std::string>& location_id) {
    if (!location_id.has_value()) {
        return std::nullopt;
    }

    Statement statement{handle, R"sql(
WITH RECURSIVE location_tree(id, name, parent_location_id, depth) AS (
    SELECT id, name, parent_location_id, 0
    FROM locations
    WHERE id = ?
    UNION ALL
    SELECT parent.id, parent.name, parent.parent_location_id, location_tree.depth + 1
    FROM locations parent
    JOIN location_tree ON location_tree.parent_location_id = parent.id
)
SELECT group_concat(name, ' / ')
FROM (
    SELECT name
    FROM location_tree
    ORDER BY depth DESC
);
)sql"};
    statement.bind(1, *location_id);
    if (statement.step_row()) {
        return empty_to_null(statement.optional_text(0));
    }
    return std::nullopt;
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

std::optional<std::string> upsert_location_path(sqlite3* handle, const std::optional<std::string>& location_path) {
    const auto path = empty_to_null(location_path);
    if (!path.has_value()) {
        return std::nullopt;
    }

    const auto parts = split_path(*path);
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

} // namespace

LibraryBrowseService::LibraryBrowseService(db::Database& database)
    : database_{database} {    
    }

std::vector<WorkListItem> LibraryBrowseService::list_works(const WorkFilters& filters) const {
    std::vector<WorkListItem> result;

    const auto text_filter = trim_copy(filters.text);
    const auto author_filter = trim_copy(filters.author);
    const auto series_filter = trim_copy(filters.series);
    const auto status_filter = trim_copy(filters.reading_status);

    Statement statement{database_.handle(), R"sql(
        SELECT
            w.id,
            w.canonical_title,
            w.first_published_year,
            COUNT(DISTINCT e.isbn) AS edition_count,
            COUNT(DISTINCT c.id) AS copy_count,
            COALESCE((
                SELECT group_concat(name, ', ')
                FROM (
                    SELECT DISTINCT s.name AS name
                    FROM series s
                    JOIN work_series ws ON ws.series_id = s.id
                    WHERE ws.work_id = w.id
                    ORDER BY lower(s.name)
                )
            ), '') AS series_names,
            COALESCE((
                SELECT group_concat(status, ', ')
                FROM (
                    SELECT DISTINCT wrs.status AS status
                    FROM work_reading_status wrs
                    WHERE wrs.work_id = w.id
                    ORDER BY lower(wrs.status)
                )
            ), '') AS reading_statuses
        FROM works w
        LEFT JOIN editions e ON e.work_id = w.id
        LEFT JOIN copies c ON c.edition_isbn = e.isbn
        WHERE
            (? = ''
                OR lower(w.canonical_title) LIKE '%' || lower(?) || '%'
                OR lower(COALESCE(w.original_title, '')) LIKE '%' || lower(?) || '%'
                OR lower(COALESCE(e.title, '')) LIKE '%' || lower(?) || '%'
                OR lower(COALESCE(e.isbn, '')) LIKE '%' || lower(?) || '%'
                OR EXISTS (
                    SELECT 1
                    FROM work_contributors wc
                    JOIN contributors contributor ON contributor.id = wc.contributor_id
                    WHERE wc.work_id = w.id
                        AND wc.role = 'author'
                        AND lower(contributor.name) LIKE '%' || lower(?) || '%'
                ))
            AND (? = ''
                OR EXISTS (
                    SELECT 1
                    FROM work_contributors wc
                    JOIN contributors contributor ON contributor.id = wc.contributor_id
                    WHERE wc.work_id = w.id
                        AND wc.role = 'author'
                        AND lower(contributor.name) LIKE '%' || lower(?) || '%'
                ))
            AND (? = ''
                OR EXISTS (
                    SELECT 1
                    FROM work_series ws
                    JOIN series s ON s.id = ws.series_id
                    WHERE ws.work_id = w.id
                        AND lower(s.name) LIKE '%' || lower(?) || '%'
                ))
            AND (? = ''
                OR EXISTS (
                    SELECT 1
                    FROM work_reading_status wrs
                    WHERE wrs.work_id = w.id
                        AND lower(wrs.status) LIKE '%' || lower(?) || '%'
                ))
        GROUP BY w.id
        ORDER BY lower(w.canonical_title)
        LIMIT 200;    
    )sql"};

    statement.bind(1, text_filter);
    statement.bind(2, text_filter);
    statement.bind(3, text_filter);
    statement.bind(4, text_filter);
    statement.bind(5, text_filter);
    statement.bind(6, text_filter);
    statement.bind(7, author_filter);
    statement.bind(8, author_filter);
    statement.bind(9, series_filter);
    statement.bind(10, series_filter);
    statement.bind(11, status_filter);
    statement.bind(12, status_filter);

    while (statement.step_row()) {
        WorkListItem item;
        item.id = statement.text(0);
        item.title = statement.text(1);
        item.first_published_year = statement.optional_int_column(2);
        item.edition_count = statement.int_column(3);
        item.copy_count = statement.int_column(4);
        item.series = statement.text(5);
        item.reading_status = statement.text(6);
        item.authors = authors_for_work(database_.handle(), item.id);
        result.push_back(std::move(item));
    }

    return result;
}

std::vector<EditionListItem> LibraryBrowseService::list_editions_for_work(const std::string& work_id) const {
    std::vector<EditionListItem> result;

    Statement statement{database_.handle(), R"sql(
        SELECT
            e.isbn,
            e.title,
            e.subtitle,
            e.language_code,
            e.publisher,
            e.publication_year,
            e.edition_name,
            e.format,
            e.page_count,
            COUNT(c.id) as copy_count
        FROM editions e
        LEFT JOIN copies c ON c.edition_isbn = e.isbn
        WHERE e.work_id = ?
        GROUP BY e.isbn
        ORDER BY e.publication_year, lower(e.title), e.isbn;    
    )sql"};

    statement.bind(1, work_id);

    while (statement.step_row()) {
        EditionListItem item;
        item.isbn = statement.text(0);
        item.title = statement.text(1);
        item.subtitle = statement.optional_text(2);
        item.language_code = statement.text(3);
        item.publisher = statement.optional_text(4);
        item.publication_year = statement.optional_int_column(5);
        item.edition_name = statement.optional_text(6);
        item.format = statement.optional_text(7);
        item.page_count = statement.optional_int_column(8);
        item.copy_count = statement.int_column(9);
        result.push_back(std::move(item));
    }

    return result;
}

std::vector<CopyListItem> LibraryBrowseService::list_copies_for_edition(const std::string& edition_isbn) const {
    std::vector<CopyListItem> result;

    Statement statement{database_.handle(), R"sql(
        SELECT
            c.id,
            c.location_id,
            c.position_in_location,
            c.condition,
            c.acquired_date,
            c.acquired_where,
            c.borrowed_from,
            c.lent_to,
            c.notes
        FROM copies c
        WHERE c.edition_isbn = ?
        ORDER BY c.position_in_location, c.id;
    )sql"};

    statement.bind(1, edition_isbn);

    while (statement.step_row()) {
        CopyListItem item;
        item.id = statement.text(0);
        item.location_path = location_path_for_id(database_.handle(), statement.optional_text(1));
        item.position_in_location = statement.optional_int_column(2);
        item.condition = statement.optional_text(3);
        item.acquired_date = statement.optional_text(4);
        item.acquired_where = statement.optional_text(5);
        item.borrowed_from = statement.optional_text(6);
        item.lent_to = statement.optional_text(7);
        item.notes = statement.optional_text(8);
        result.push_back(std::move(item));
    }

    return result;
}

std::optional<WorkDetails> LibraryBrowseService::get_work(const std::string& work_id) const {
    Statement statement{database_.handle(), R"sql(
SELECT
    id,
    canonical_title,
    original_title,
    original_language_code,
    first_published_year,
    description,
    age_rating,
    notes
FROM works
WHERE id = ?
LIMIT 1;
)sql"};
    statement.bind(1, work_id);
    if (!statement.step_row()) {
        return std::nullopt;
    }

    WorkDetails details;
    details.id = statement.text(0);
    details.canonical_title = statement.text(1);
    details.original_title = statement.optional_text(2);
    details.original_language_code = statement.optional_text(3);
    details.first_published_year = statement.optional_int_column(4);
    details.description = statement.optional_text(5);
    details.age_rating = statement.optional_text(6);
    details.notes = statement.optional_text(7);
    details.authors = authors_for_work(database_.handle(), details.id);
    details.series = text_for_work(database_.handle(), details.id, R"sql(
SELECT COALESCE(group_concat(label, ', '), '')
FROM (
    SELECT
        s.name
        || COALESCE(' #' || ws.position_label, '')
        || CASE
            WHEN ws.position_label IS NULL AND ws.position IS NOT NULL THEN ' #' || ws.position
            ELSE ''
        END AS label
    FROM work_series ws
    JOIN series s ON s.id = ws.series_id
    WHERE ws.work_id = ?
    ORDER BY lower(s.name), ws.season, ws.position
);
)sql");
    details.genres = text_for_work(database_.handle(), details.id, R"sql(
SELECT COALESCE(group_concat(label, ', '), '')
FROM (
    SELECT
        CASE
            WHEN parent.name IS NULL THEN g.name
            ELSE parent.name || ' > ' || g.name
        END AS label
    FROM work_genres wg
    JOIN genres g ON g.id = wg.genre_id
    LEFT JOIN genres parent ON parent.id = g.parent_genre_id
    WHERE wg.work_id = ?
    ORDER BY lower(label)
);
)sql");
    details.reading_status = text_for_work(database_.handle(), details.id, R"sql(
SELECT COALESCE(group_concat(label, ', '), '')
FROM (
    SELECT
        language_code || ': ' || status
        || COALESCE(' (' || rating || '/5)', '') AS label
    FROM work_reading_status
    WHERE work_id = ?
    ORDER BY lower(language_code)
);
)sql");
    return details;
}

std::optional<EditionDetails> LibraryBrowseService::get_edition(const std::string& isbn) const {
    Statement statement{database_.handle(), R"sql(
SELECT
    isbn,
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
FROM editions
WHERE isbn = ?
LIMIT 1;
)sql"};
    statement.bind(1, isbn);
    if (!statement.step_row()) {
        return std::nullopt;
    }

    EditionDetails details;
    details.isbn = statement.text(0);
    details.title = statement.text(1);
    details.subtitle = statement.optional_text(2);
    details.language_code = statement.text(3);
    details.publisher = statement.optional_text(4);
    details.publication_year = statement.optional_int_column(5);
    details.edition_name = statement.optional_text(6);
    details.format = statement.optional_text(7);
    details.page_count = statement.optional_int_column(8);
    details.cover_url = statement.optional_text(9);
    details.notes = statement.optional_text(10);
    return details;
}

std::optional<CopyDetails> LibraryBrowseService::get_copy(const std::string& copy_id) const {
    Statement statement{database_.handle(), R"sql(
SELECT
    id,
    edition_isbn,
    location_id,
    position_in_location,
    condition,
    acquired_date,
    acquired_where,
    borrowed_from,
    lent_to,
    notes
FROM copies
WHERE id = ?
LIMIT 1;
)sql"};
    statement.bind(1, copy_id);
    if (!statement.step_row()) {
        return std::nullopt;
    }

    CopyDetails details;
    details.id = statement.text(0);
    details.edition_isbn = statement.text(1);
    details.location_path = location_path_for_id(database_.handle(), statement.optional_text(2));
    details.position_in_location = statement.optional_int_column(3);
    details.condition = statement.optional_text(4);
    details.acquired_date = statement.optional_text(5);
    details.acquired_where = statement.optional_text(6);
    details.borrowed_from = statement.optional_text(7);
    details.lent_to = statement.optional_text(8);
    details.notes = statement.optional_text(9);
    return details;
}

void LibraryBrowseService::update_work(const WorkUpdate& update) const {
    if (trim_copy(update.id).empty()) {
        throw std::runtime_error("Werk-ID fehlt.");
    }
    if (trim_copy(update.canonical_title).empty()) {
        throw std::runtime_error("Werk-Titel fehlt.");
    }

    try {
        database_.execute("BEGIN;");
        Statement statement{database_.handle(), R"sql(
UPDATE works
SET
    canonical_title = ?,
    original_title = ?,
    original_language_code = ?,
    first_published_year = ?,
    description = ?,
    age_rating = ?,
    notes = ?
WHERE id = ?;
)sql"};
        statement.bind(1, trim_copy(update.canonical_title));
        statement.bind_optional(2, empty_to_null(update.original_title));
        statement.bind_optional(3, empty_to_null(update.original_language_code));
        statement.bind_optional_int(4, update.first_published_year);
        statement.bind_optional(5, empty_to_null(update.description));
        statement.bind_optional(6, empty_to_null(update.age_rating));
        statement.bind_optional(7, empty_to_null(update.notes));
        statement.bind(8, update.id);
        statement.execute_done();
        replace_authors(database_.handle(), update.id, update.authors);
        database_.execute("COMMIT;");
    } catch (...) {
        database_.execute("ROLLBACK;");
        throw;
    }
}

void LibraryBrowseService::update_edition(const EditionUpdate& update) const {
    if (trim_copy(update.isbn).empty()) {
        throw std::runtime_error("ISBN fehlt.");
    }
    if (trim_copy(update.title).empty()) {
        throw std::runtime_error("Editionstitel fehlt.");
    }
    if (trim_copy(update.language_code).empty()) {
        throw std::runtime_error("Sprache fehlt.");
    }

    Statement statement{database_.handle(), R"sql(
UPDATE editions
SET
    title = ?,
    subtitle = ?,
    language_code = ?,
    publisher = ?,
    publication_year = ?,
    edition_name = ?,
    format = ?,
    page_count = ?,
    cover_url = ?,
    notes = ?
WHERE isbn = ?;
)sql"};
    statement.bind(1, trim_copy(update.title));
    statement.bind_optional(2, empty_to_null(update.subtitle));
    statement.bind(3, trim_copy(update.language_code));
    statement.bind_optional(4, empty_to_null(update.publisher));
    statement.bind_optional_int(5, update.publication_year);
    statement.bind_optional(6, empty_to_null(update.edition_name));
    statement.bind_optional(7, empty_to_null(update.format));
    statement.bind_optional_int(8, update.page_count);
    statement.bind_optional(9, empty_to_null(update.cover_url));
    statement.bind_optional(10, empty_to_null(update.notes));
    statement.bind(11, update.isbn);
    statement.execute_done();
}

void LibraryBrowseService::update_copy(const CopyUpdate& update) const {
    if (trim_copy(update.id).empty()) {
        throw std::runtime_error("Exemplar-ID fehlt.");
    }

    try {
        database_.execute("BEGIN;");
        const auto location_id = upsert_location_path(database_.handle(), update.location_path);
        Statement statement{database_.handle(), R"sql(
UPDATE copies
SET
    location_id = ?,
    position_in_location = ?,
    condition = ?,
    acquired_date = ?,
    acquired_where = ?,
    borrowed_from = ?,
    lent_to = ?,
    notes = ?
WHERE id = ?;
)sql"};
        statement.bind_optional(1, location_id);
        statement.bind_optional_int(2, update.position_in_location);
        statement.bind_optional(3, empty_to_null(update.condition));
        statement.bind_optional(4, empty_to_null(update.acquired_date));
        statement.bind_optional(5, empty_to_null(update.acquired_where));
        statement.bind_optional(6, empty_to_null(update.borrowed_from));
        statement.bind_optional(7, empty_to_null(update.lent_to));
        statement.bind_optional(8, empty_to_null(update.notes));
        statement.bind(9, update.id);
        statement.execute_done();
        database_.execute("COMMIT;");
    } catch (...) {
        database_.execute("ROLLBACK;");
        throw;
    }
}

void LibraryBrowseService::delete_work(const std::string& work_id) const {
    if (trim_copy(work_id).empty()) {
        throw std::runtime_error("Werk-ID fehlt.");
    }

    try {
        database_.execute("BEGIN;");

        Statement delete_copies{database_.handle(), R"sql(
DELETE FROM copies
WHERE edition_isbn IN (
    SELECT isbn
    FROM editions
    WHERE work_id = ?
);
)sql"};
        delete_copies.bind(1, work_id);
        delete_copies.execute_done();

        Statement delete_edition_contributors{database_.handle(), R"sql(
DELETE FROM edition_contributors
WHERE edition_isbn IN (
    SELECT isbn
    FROM editions
    WHERE work_id = ?
);
)sql"};
        delete_edition_contributors.bind(1, work_id);
        delete_edition_contributors.execute_done();

        Statement delete_editions{database_.handle(), "DELETE FROM editions WHERE work_id = ?;"};
        delete_editions.bind(1, work_id);
        delete_editions.execute_done();

        Statement delete_authors{database_.handle(), "DELETE FROM work_contributors WHERE work_id = ?;"};
        delete_authors.bind(1, work_id);
        delete_authors.execute_done();

        Statement delete_series{database_.handle(), "DELETE FROM work_series WHERE work_id = ?;"};
        delete_series.bind(1, work_id);
        delete_series.execute_done();

        Statement delete_genres{database_.handle(), "DELETE FROM work_genres WHERE work_id = ?;"};
        delete_genres.bind(1, work_id);
        delete_genres.execute_done();

        Statement delete_status{database_.handle(), "DELETE FROM work_reading_status WHERE work_id = ?;"};
        delete_status.bind(1, work_id);
        delete_status.execute_done();

        Statement delete_work_statement{database_.handle(), "DELETE FROM works WHERE id = ?;"};
        delete_work_statement.bind(1, work_id);
        delete_work_statement.execute_done();

        database_.execute("COMMIT;");
    } catch (...) {
        database_.execute("ROLLBACK;");
        throw;
    }
}

void LibraryBrowseService::delete_edition(const std::string& isbn) const {
    if (trim_copy(isbn).empty()) {
        throw std::runtime_error("ISBN fehlt.");
    }

    try {
        database_.execute("BEGIN;");

        Statement delete_copies{database_.handle(), "DELETE FROM copies WHERE edition_isbn = ?;"};
        delete_copies.bind(1, isbn);
        delete_copies.execute_done();

        Statement delete_contributors{database_.handle(), "DELETE FROM edition_contributors WHERE edition_isbn = ?;"};
        delete_contributors.bind(1, isbn);
        delete_contributors.execute_done();

        Statement delete_edition_statement{database_.handle(), "DELETE FROM editions WHERE isbn = ?;"};
        delete_edition_statement.bind(1, isbn);
        delete_edition_statement.execute_done();

        database_.execute("COMMIT;");
    } catch (...) {
        database_.execute("ROLLBACK;");
        throw;
    }
}

void LibraryBrowseService::delete_copy(const std::string& copy_id) const {
    if (trim_copy(copy_id).empty()) {
        throw std::runtime_error("Exemplar-ID fehlt.");
    }

    Statement statement{database_.handle(), "DELETE FROM copies WHERE id = ?;"};
    statement.bind(1, copy_id);
    statement.execute_done();
}

} // namespace buch::services
