#include "services/LibraryBrowseService.hpp"

#include <sqlite3.h>

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

} // namespace

LibraryBrowseService::LibraryBrowseService(db::Database& database)
    : database_{database} {    
    }

std::vector<WorkListItem> LibraryBrowseService::list_works(const std::string& search_query) const {
    std::vector<WorkListItem> result;

    const bool has_search = !search_query.empty();

    Statement statement{database_.handle(), R"sql(
        SELECT
            w.id,
            w.canonical_title,
            w.first_published_year,
            COUNT(DISTINCT e.isbn) AS edition_count,
            COUNT(DISTINCT c.id) AS copy_count
        FROM works w
        LEFT JOIN editions e ON e.work_id = w.id
        LEFT JOIN copies c ON c.edition_isbn = e.isbn
        WHERE
            ? = ''
            OR lower(w.canonical_title) LIKE '%' || lower(?) || '%'
            OR EXISTS (
                SELECT 1
                FROM work_contributors wc
                JOIN contributors contributor ON contributor.id = wc.contributor_id
                WHERE wc.work_id = w.id
                    AND wc.role = 'author'
                    AND lower(contributor.name) LIKE '%' || lower(?) || '%'
            )
        GROUP BY w.id
        ORDER BY lower(w.canonical_title)
        LIMIT 200;    
    )sql"};

    statement.bind(1, has_search ? search_query : "");
    statement.bind(2, search_query);
    statement.bind(3, search_query);

    while (statement.step_row()) {
        WorkListItem item;
        item.id = statement.text(0);
        item.title = statement.text(1);
        item.first_published_year = statement.optional_int_column(2);
        item.edition_count = statement.int_column(3);
        item.copy_count = statement.int_column(4);
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
            l.name,
            c.position_in_location,
            c.condition,
            c.acquired_date,
            c.acquired_where,
            c.borrowed_from,
            c.lent_to,
            c.notes
        FROM copies c
        LEFT JOIN locations l ON l.id = c.location_id
        WHERE c.edition_isbn = ?
        ORDER BY l.name, c.position_in_location, c.id;
    )sql"};

    statement.bind(1, edition_isbn);

    while (statement.step_row()) {
        CopyListItem item;
        item.id = statement.text(0);
        item.location_path = statement.optional_text(1);
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

} // namespace buch::services