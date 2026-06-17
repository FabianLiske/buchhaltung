#include "services/ImportWorkflowService.hpp"

#include "lookup/GoogleBooksLookup.hpp"
#include "services/Isbn.hpp"
#include "services/LibraryBrowseService.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace buch::services {
namespace {

using Json = nlohmann::json;

struct Session {
    std::string id;
    std::string state;
    std::optional<std::string> isbn;
    Json payload;
    Json history;
};

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
        } else if (sqlite3_bind_null(statement_, index) != SQLITE_OK) {
            throw std::runtime_error(std::string{"Could not bind null: "} + sqlite3_errmsg(handle_));
        }
    }

    void bind_optional_int(int index, const std::optional<int>& value) {
        if (value.has_value()) {
            if (sqlite3_bind_int(statement_, index, *value) != SQLITE_OK) {
                throw std::runtime_error(std::string{"Could not bind int: "} + sqlite3_errmsg(handle_));
            }
        } else if (sqlite3_bind_null(statement_, index) != SQLITE_OK) {
            throw std::runtime_error(std::string{"Could not bind null: "} + sqlite3_errmsg(handle_));
        }
    }

    void bind_optional_double(int index, const std::optional<double>& value) {
        if (value.has_value()) {
            if (sqlite3_bind_double(statement_, index, *value) != SQLITE_OK) {
                throw std::runtime_error(std::string{"Could not bind double: "} + sqlite3_errmsg(handle_));
            }
        } else if (sqlite3_bind_null(statement_, index) != SQLITE_OK) {
            throw std::runtime_error(std::string{"Could not bind null: "} + sqlite3_errmsg(handle_));
        }
    }

    void bind_int(int index, int value) {
        if (sqlite3_bind_int(statement_, index, value) != SQLITE_OK) {
            throw std::runtime_error(std::string{"Could not bind int: "} + sqlite3_errmsg(handle_));
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
        const auto* value = sqlite3_column_text(statement_, column);
        return value == nullptr ? "" : reinterpret_cast<const char*>(value);
    }

    std::optional<std::string> optional_text(int column) const {
        if (sqlite3_column_type(statement_, column) == SQLITE_NULL) {
            return std::nullopt;
        }
        return text(column);
    }

private:
    sqlite3* handle_{nullptr};
    sqlite3_stmt* statement_{nullptr};
};

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

std::string trim_copy(std::string value) {
    const auto is_not_space = [](unsigned char character) {
        return !std::isspace(character);
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), is_not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), is_not_space).base(), value.end());
    return value;
}

std::optional<std::string> optional_string(const Json& object, std::string_view key) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || iterator->is_null()) {
        return std::nullopt;
    }
    if (!iterator->is_string()) {
        throw std::runtime_error("Field '" + std::string{key} + "' must be a string.");
    }
    const auto value = trim_copy(iterator->get<std::string>());
    return value.empty() ? std::nullopt : std::optional<std::string>{value};
}

std::string required_string(const Json& object, std::string_view key) {
    const auto value = optional_string(object, key);
    if (!value.has_value()) {
        throw std::runtime_error("Field '" + std::string{key} + "' is required.");
    }
    return *value;
}

std::optional<int> optional_int(const Json& object, std::string_view key) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || iterator->is_null()) {
        return std::nullopt;
    }
    if (!iterator->is_number_integer()) {
        throw std::runtime_error("Field '" + std::string{key} + "' must be an integer.");
    }
    return iterator->get<int>();
}

std::optional<double> optional_double(const Json& object, std::string_view key) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || iterator->is_null()) {
        return std::nullopt;
    }
    if (!iterator->is_number()) {
        throw std::runtime_error("Field '" + std::string{key} + "' must be a number.");
    }
    return iterator->get<double>();
}

std::vector<std::string> string_array(const Json& object, std::string_view key) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || iterator->is_null()) {
        return {};
    }
    if (!iterator->is_array()) {
        throw std::runtime_error("Field '" + std::string{key} + "' must be an array.");
    }

    std::vector<std::string> result;
    for (const auto& value : *iterator) {
        if (!value.is_string()) {
            throw std::runtime_error("Field '" + std::string{key} + "' must contain only strings.");
        }
        const auto trimmed = trim_copy(value.get<std::string>());
        if (!trimmed.empty()) {
            result.push_back(trimmed);
        }
    }
    return result;
}

Json optional_json(const std::optional<std::string>& value) {
    return value.has_value() ? Json(*value) : Json(nullptr);
}

Json optional_json(const std::optional<int>& value) {
    return value.has_value() ? Json(*value) : Json(nullptr);
}

Json work_json(const WorkDetails& work) {
    return {
        {"id", work.id},
        {"canonical_title", work.canonical_title},
        {"original_title", optional_json(work.original_title)},
        {"original_language_code", optional_json(work.original_language_code)},
        {"first_published_year", optional_json(work.first_published_year)},
        {"description", optional_json(work.description)},
        {"age_rating", optional_json(work.age_rating)},
        {"notes", optional_json(work.notes)},
        {"authors", work.authors},
    };
}

Json edition_json(const EditionDetails& edition) {
    return {
        {"isbn", edition.isbn},
        {"title", edition.title},
        {"subtitle", optional_json(edition.subtitle)},
        {"language_code", edition.language_code},
        {"publisher", optional_json(edition.publisher)},
        {"publication_year", optional_json(edition.publication_year)},
        {"edition_name", optional_json(edition.edition_name)},
        {"format", optional_json(edition.format)},
        {"page_count", optional_json(edition.page_count)},
        {"cover_url", optional_json(edition.cover_url)},
        {"notes", optional_json(edition.notes)},
    };
}

Json lookup_json(const lookup::BookLookupResult& result) {
    return {
        {"google_volume_id", result.google_volume_id},
        {"title", optional_json(result.title)},
        {"subtitle", optional_json(result.subtitle)},
        {"authors", result.authors},
        {"publisher", optional_json(result.publisher)},
        {"published_date", optional_json(result.published_date)},
        {"language", optional_json(result.language)},
        {"isbn_10", optional_json(result.isbn_10)},
        {"isbn_13", optional_json(result.isbn_13)},
        {"page_count", optional_json(result.page_count)},
        {"categories", result.categories},
        {"maturity_rating", optional_json(result.maturity_rating)},
        {"description", optional_json(result.description)},
        {"thumbnail_url", optional_json(result.thumbnail_url)},
        {"info_link", optional_json(result.info_link)},
        {"canonical_link", optional_json(result.canonical_link)},
    };
}

std::optional<int> publication_year(const std::optional<std::string>& published_date) {
    if (!published_date.has_value() || published_date->size() < 4) {
        return std::nullopt;
    }
    try {
        return std::stoi(published_date->substr(0, 4));
    } catch (...) {
        return std::nullopt;
    }
}

Json new_copy_draft() {
    return {
        {"location_path", nullptr},
        {"position_in_location", nullptr},
        {"condition", nullptr},
        {"acquired_date", nullptr},
        {"acquired_where", nullptr},
        {"borrowed_from", nullptr},
        {"lent_to", nullptr},
        {"notes", nullptr},
    };
}

Json new_series_draft() {
    return {
        {"mode", "none"},
        {"existing_series_id", nullptr},
        {"name", nullptr},
        {"original_title", nullptr},
        {"description", nullptr},
        {"notes", nullptr},
        {"season", nullptr},
        {"position", nullptr},
        {"position_label", nullptr},
        {"work_series_notes", nullptr},
    };
}

Json series_options_json(const std::vector<SeriesOption>& options) {
    Json result = Json::array();
    for (const auto& option : options) {
        result.push_back({
            {"id", option.id},
            {"name", option.name},
        });
    }
    return result;
}

Json allowed_actions(const std::string& state, bool has_history) {
    Json actions = Json::array();
    if (state == "awaiting_isbn" || state == "lookup_failed") {
        actions.push_back("submit_isbn");
    } else if (state == "select_work") {
        actions.push_back("select_existing_work");
        actions.push_back("create_new_work");
    } else if (state == "edit_work") {
        actions.push_back("update_work");
    } else if (state == "edit_series") {
        actions.push_back("update_series");
    } else if (state == "edit_edition") {
        actions.push_back("update_edition");
    } else if (state == "edit_copy") {
        actions.push_back("update_copy");
    } else if (state == "review") {
        actions.push_back("commit");
    }

    if (has_history && state != "completed") {
        actions.push_back("back");
    }
    if (state != "completed") {
        actions.push_back("cancel");
    }
    return actions;
}

Json session_json(const Session& session) {
    Json result = session.payload;
    result["id"] = session.id;
    result["state"] = session.state;
    result["isbn"] = session.isbn.has_value() ? Json(*session.isbn) : Json(nullptr);
    result["allowed_actions"] = allowed_actions(session.state, !session.history.empty());
    result["history_depth"] = session.history.size();
    return result;
}

Session load_session(sqlite3* handle, const std::string& id) {
    Statement statement{handle, R"sql(
SELECT id, state, isbn, payload_json, history_json
FROM import_sessions
WHERE id = ?
LIMIT 1;
)sql"};
    statement.bind(1, id);
    if (!statement.step_row()) {
        throw std::runtime_error("Import session not found.");
    }

    return Session{
        .id = statement.text(0),
        .state = statement.text(1),
        .isbn = statement.optional_text(2),
        .payload = Json::parse(statement.text(3)),
        .history = Json::parse(statement.text(4)),
    };
}

void save_session(sqlite3* handle, const Session& session, bool completed = false) {
    Statement statement{handle, R"sql(
UPDATE import_sessions
SET
    state = ?,
    isbn = ?,
    payload_json = ?,
    history_json = ?,
    updated_at = CURRENT_TIMESTAMP,
    completed_at = CASE WHEN ? THEN CURRENT_TIMESTAMP ELSE completed_at END
WHERE id = ?;
)sql"};
    statement.bind(1, session.state);
    statement.bind_optional(2, session.isbn);
    statement.bind(3, session.payload.dump());
    statement.bind(4, session.history.dump());
    statement.bind_int(5, completed ? 1 : 0);
    statement.bind(6, session.id);
    statement.execute_done();
}

void push_history(Session& session) {
    session.history.push_back({
        {"state", session.state},
        {"isbn", session.isbn.has_value() ? Json(*session.isbn) : Json(nullptr)},
        {"payload", session.payload},
    });
}

std::vector<std::string> equivalent_isbns(const std::string& isbn) {
    std::vector<std::string> values{isbn};
    const auto equivalent = isbn.size() == 10 ? isbn_10_to_13(isbn) : isbn_13_to_10(isbn);
    if (equivalent.has_value()) {
        values.push_back(*equivalent);
    }
    return values;
}

std::optional<std::string> existing_edition_isbn(sqlite3* handle, const std::vector<std::string>& isbns) {
    for (const auto& isbn : isbns) {
        Statement statement{handle, "SELECT isbn FROM editions WHERE isbn = ? LIMIT 1;"};
        statement.bind(1, isbn);
        if (statement.step_row()) {
            return statement.text(0);
        }
    }
    return std::nullopt;
}

Json candidates_for_authors(LibraryBrowseService& library, const std::vector<std::string>& authors) {
    std::map<std::string, WorkListItem> candidates;
    for (const auto& author : authors) {
        for (const auto& work : library.list_works(WorkFilters{
                 .text = "",
                 .author = author,
                 .series = "",
                 .reading_status = "",
             })) {
            candidates.try_emplace(work.id, work);
        }
    }

    Json result = Json::array();
    for (const auto& [_, work] : candidates) {
        result.push_back({
            {"id", work.id},
            {"title", work.title},
            {"authors", work.authors},
            {"first_published_year", optional_json(work.first_published_year)},
            {"edition_count", work.edition_count},
            {"copy_count", work.copy_count},
        });
    }
    return result;
}

std::string preferred_isbn(const std::string& entered, const lookup::BookLookupResult& result) {
    if (result.isbn_13.has_value()) {
        const auto normalized = normalize_isbn(*result.isbn_13);
        if (is_valid_isbn(normalized)) {
            return normalized;
        }
    }
    if (entered.size() == 10) {
        if (const auto converted = isbn_10_to_13(entered); converted.has_value()) {
            return *converted;
        }
    }
    return entered;
}

void process_isbn(
    Session& session,
    const std::string& raw_isbn,
    db::Database& database,
    const std::string& api_key,
    bool add_history
) {
    const auto isbn = normalize_isbn(raw_isbn);
    if (!is_valid_isbn(isbn)) {
        throw std::runtime_error("ISBN has an invalid length, character, or check digit.");
    }

    if (add_history) {
        push_history(session);
    }

    session.isbn = isbn;
    session.payload = {
        {"entered_isbn", raw_isbn},
        {"normalized_isbn", isbn},
        {"existing_edition", false},
        {"lookup", nullptr},
        {"lookup_error", nullptr},
        {"work_candidates", Json::array()},
        {"selected_work_mode", nullptr},
        {"selected_work_id", nullptr},
        {"work", nullptr},
        {"series_options", Json::array()},
        {"series", new_series_draft()},
        {"edition", nullptr},
        {"copy", new_copy_draft()},
        {"result", nullptr},
    };

    LibraryBrowseService library{database};
    session.payload["series_options"] = series_options_json(library.list_series());
    if (const auto existing_isbn = existing_edition_isbn(database.handle(), equivalent_isbns(isbn)); existing_isbn.has_value()) {
        const auto edition = library.get_edition(*existing_isbn);
        Statement work_lookup{database.handle(), "SELECT work_id FROM editions WHERE isbn = ? LIMIT 1;"};
        work_lookup.bind(1, *existing_isbn);
        work_lookup.step_row();
        const auto work = library.get_work(work_lookup.text(0));

        session.payload["existing_edition"] = true;
        session.payload["selected_work_mode"] = "existing";
        session.payload["selected_work_id"] = work->id;
        session.payload["work"] = work_json(*work);
        session.payload["edition"] = edition_json(*edition);
        session.state = "edit_copy";
        return;
    }

    if (api_key.empty()) {
        session.state = "lookup_failed";
        session.payload["lookup_error"] = "GOOGLE_BOOKS_KEY is not configured.";
        return;
    }

    try {
        const auto lookup_result = lookup::lookup_google_books_by_isbn(isbn, api_key);
        session.payload["lookup"] = lookup_json(lookup_result);
        session.payload["work_candidates"] = candidates_for_authors(library, lookup_result.authors);
        session.payload["work"] = {
            {"canonical_title", optional_json(lookup_result.title)},
            {"original_title", nullptr},
            {"original_language_code", nullptr},
            {"first_published_year", optional_json(publication_year(lookup_result.published_date))},
            {"description", optional_json(lookup_result.description)},
            {"age_rating", optional_json(lookup_result.maturity_rating)},
            {"notes", nullptr},
            {"authors", lookup_result.authors},
        };
        session.payload["edition"] = {
            {"isbn", preferred_isbn(isbn, lookup_result)},
            {"title", optional_json(lookup_result.title)},
            {"subtitle", optional_json(lookup_result.subtitle)},
            {"language_code", lookup_result.language.value_or("und")},
            {"publisher", optional_json(lookup_result.publisher)},
            {"publication_year", optional_json(publication_year(lookup_result.published_date))},
            {"edition_name", nullptr},
            {"format", nullptr},
            {"page_count", optional_json(lookup_result.page_count)},
            {"cover_url", optional_json(lookup_result.thumbnail_url)},
            {"notes", nullptr},
        };
        session.state = "select_work";
    } catch (const std::exception& error) {
        session.state = "lookup_failed";
        session.payload["lookup_error"] = error.what();
    }
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

std::optional<std::string> upsert_location_path(sqlite3* handle, const std::optional<std::string>& path) {
    if (!path.has_value()) {
        return std::nullopt;
    }

    std::vector<std::string> parts;
    std::string part;
    for (const char character : *path) {
        if (character == '/') {
            const auto trimmed = trim_copy(part);
            if (!trimmed.empty()) {
                parts.push_back(trimmed);
            }
            part.clear();
        } else {
            part.push_back(character);
        }
    }
    const auto trimmed = trim_copy(part);
    if (!trimmed.empty()) {
        parts.push_back(trimmed);
    }

    std::optional<std::string> parent_id;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (const auto existing = find_location_id(handle, parts[index], parent_id); existing.has_value()) {
            parent_id = existing;
            continue;
        }

        const auto id = random_uuid();
        const std::string type = index == 0 ? "Zimmer" : index == 1 ? "Regal" : index == 2 ? "Fach" : "Reihe";
        Statement insert{handle, "INSERT INTO locations (id, name, type, parent_location_id, sort_order) VALUES (?, ?, ?, ?, ?);"};
        insert.bind(1, id);
        insert.bind(2, parts[index]);
        insert.bind(3, type);
        insert.bind_optional(4, parent_id);
        insert.bind_int(5, static_cast<int>(index));
        insert.execute_done();
        parent_id = id;
    }
    return parent_id;
}

std::string upsert_contributor(sqlite3* handle, const std::string& name) {
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

void insert_authors(sqlite3* handle, const std::string& work_id, const std::vector<std::string>& authors) {
    int order = 0;
    for (const auto& author : authors) {
        const auto contributor_id = upsert_contributor(handle, author);
        Statement insert{handle, R"sql(
INSERT INTO work_contributors (work_id, contributor_id, role, contributor_order)
VALUES (?, ?, 'author', ?);
)sql"};
        insert.bind(1, work_id);
        insert.bind(2, contributor_id);
        insert.bind_int(3, order++);
        insert.execute_done();
    }
}

std::string insert_work(sqlite3* handle, const Json& work) {
    const auto id = random_uuid();
    Statement insert{handle, R"sql(
INSERT INTO works (
    id, canonical_title, original_title, original_language_code,
    first_published_year, description, age_rating, notes
) VALUES (?, ?, ?, ?, ?, ?, ?, ?);
)sql"};
    insert.bind(1, id);
    insert.bind(2, required_string(work, "canonical_title"));
    insert.bind_optional(3, optional_string(work, "original_title"));
    insert.bind_optional(4, optional_string(work, "original_language_code"));
    insert.bind_optional_int(5, optional_int(work, "first_published_year"));
    insert.bind_optional(6, optional_string(work, "description"));
    insert.bind_optional(7, optional_string(work, "age_rating"));
    insert.bind_optional(8, optional_string(work, "notes"));
    insert.execute_done();
    insert_authors(handle, id, string_array(work, "authors"));
    return id;
}

void insert_series_assignment(sqlite3* handle, const std::string& work_id, const Json& series) {
    const auto mode = required_string(series, "mode");
    if (mode == "none") {
        return;
    }

    std::string series_id;
    if (mode == "existing") {
        series_id = required_string(series, "existing_series_id");
        Statement lookup{handle, "SELECT 1 FROM series WHERE id = ? LIMIT 1;"};
        lookup.bind(1, series_id);
        if (!lookup.step_row()) {
            throw std::runtime_error("Selected series not found.");
        }
    } else if (mode == "new") {
        series_id = random_uuid();
        Statement insert{handle, R"sql(
INSERT INTO series (id, name, original_title, description, notes)
VALUES (?, ?, ?, ?, ?);
)sql"};
        insert.bind(1, series_id);
        insert.bind(2, required_string(series, "name"));
        insert.bind_optional(3, optional_string(series, "original_title"));
        insert.bind_optional(4, optional_string(series, "description"));
        insert.bind_optional(5, optional_string(series, "notes"));
        insert.execute_done();
    } else {
        throw std::runtime_error("Series mode must be 'none', 'existing', or 'new'.");
    }

    Statement link{handle, R"sql(
INSERT INTO work_series (
    work_id, series_id, season, position, position_label, notes
) VALUES (?, ?, ?, ?, ?, ?)
ON CONFLICT(work_id, series_id) DO UPDATE SET
    season = excluded.season,
    position = excluded.position,
    position_label = excluded.position_label,
    notes = excluded.notes;
)sql"};
    link.bind(1, work_id);
    link.bind(2, series_id);
    link.bind_optional_int(3, optional_int(series, "season"));
    link.bind_optional_double(4, optional_double(series, "position"));
    link.bind_optional(5, optional_string(series, "position_label"));
    link.bind_optional(6, optional_string(series, "work_series_notes"));
    link.execute_done();
}

std::string insert_edition(sqlite3* handle, const std::string& work_id, const Json& edition) {
    const auto isbn = normalize_isbn(required_string(edition, "isbn"));
    if (!is_valid_isbn(isbn)) {
        throw std::runtime_error("Edition ISBN is invalid.");
    }

    Statement insert{handle, R"sql(
INSERT INTO editions (
    isbn, work_id, title, subtitle, language_code, publisher,
    publication_year, edition_name, format, page_count, cover_url, notes
) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
)sql"};
    insert.bind(1, isbn);
    insert.bind(2, work_id);
    insert.bind(3, required_string(edition, "title"));
    insert.bind_optional(4, optional_string(edition, "subtitle"));
    insert.bind(5, optional_string(edition, "language_code").value_or("und"));
    insert.bind_optional(6, optional_string(edition, "publisher"));
    insert.bind_optional_int(7, optional_int(edition, "publication_year"));
    insert.bind_optional(8, optional_string(edition, "edition_name"));
    insert.bind_optional(9, optional_string(edition, "format"));
    insert.bind_optional_int(10, optional_int(edition, "page_count"));
    insert.bind_optional(11, optional_string(edition, "cover_url"));
    insert.bind_optional(12, optional_string(edition, "notes"));
    insert.execute_done();
    return isbn;
}

std::string insert_copy(sqlite3* handle, const std::string& edition_isbn, const Json& copy) {
    const auto id = random_uuid();
    const auto location_id = upsert_location_path(handle, optional_string(copy, "location_path"));
    Statement insert{handle, R"sql(
INSERT INTO copies (
    id, edition_isbn, location_id, position_in_location, acquired_date,
    acquired_where, condition, borrowed_from, lent_to, notes
) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
)sql"};
    insert.bind(1, id);
    insert.bind(2, edition_isbn);
    insert.bind_optional(3, location_id);
    insert.bind_optional_int(4, optional_int(copy, "position_in_location"));
    insert.bind_optional(5, optional_string(copy, "acquired_date"));
    insert.bind_optional(6, optional_string(copy, "acquired_where"));
    insert.bind_optional(7, optional_string(copy, "condition"));
    insert.bind_optional(8, optional_string(copy, "borrowed_from"));
    insert.bind_optional(9, optional_string(copy, "lent_to"));
    insert.bind_optional(10, optional_string(copy, "notes"));
    insert.execute_done();
    return id;
}

void require_state(const Session& session, std::string_view expected) {
    if (session.state != expected) {
        throw std::runtime_error("Action is not allowed while session is in state '" + session.state + "'.");
    }
}

} // namespace

ImportWorkflowService::ImportWorkflowService(db::Database& database, std::string google_books_api_key)
    : database_{database},
      google_books_api_key_{std::move(google_books_api_key)} {
}

Json ImportWorkflowService::create_session(const std::string& isbn) const {
    const auto normalized = normalize_isbn(isbn);
    if (!is_valid_isbn(normalized)) {
        throw std::runtime_error("ISBN has an invalid length, character, or check digit.");
    }

    Session session{
        .id = random_uuid(),
        .state = "awaiting_isbn",
        .isbn = std::nullopt,
        .payload = Json::object(),
        .history = Json::array(),
    };

    Statement insert{database_.handle(), R"sql(
INSERT INTO import_sessions (id, state, isbn, payload_json, history_json)
VALUES (?, ?, ?, ?, ?);
)sql"};
    insert.bind(1, session.id);
    insert.bind(2, session.state);
    insert.bind_optional(3, session.isbn);
    insert.bind(4, session.payload.dump());
    insert.bind(5, session.history.dump());
    insert.execute_done();

    process_isbn(session, isbn, database_, google_books_api_key_, true);
    save_session(database_.handle(), session);
    return session_json(session);
}

Json ImportWorkflowService::get_session(const std::string& session_id) const {
    return session_json(load_session(database_.handle(), session_id));
}

Json ImportWorkflowService::submit_isbn(const std::string& session_id, const std::string& isbn) const {
    auto session = load_session(database_.handle(), session_id);
    if (session.state != "awaiting_isbn" && session.state != "lookup_failed") {
        throw std::runtime_error("ISBN can only be submitted from awaiting_isbn or lookup_failed.");
    }
    process_isbn(session, isbn, database_, google_books_api_key_, true);
    save_session(database_.handle(), session);
    return session_json(session);
}

Json ImportWorkflowService::select_work(const std::string& session_id, const Json& selection) const {
    auto session = load_session(database_.handle(), session_id);
    require_state(session, "select_work");
    const auto mode = required_string(selection, "mode");
    push_history(session);

    if (mode == "existing") {
        const auto work_id = required_string(selection, "work_id");
        LibraryBrowseService library{database_};
        const auto work = library.get_work(work_id);
        if (!work.has_value()) {
            throw std::runtime_error("Selected work not found.");
        }
        session.payload["selected_work_mode"] = "existing";
        session.payload["selected_work_id"] = work_id;
        session.payload["work"] = work_json(*work);
        session.state = "edit_series";
    } else if (mode == "new") {
        session.payload["selected_work_mode"] = "new";
        session.payload["selected_work_id"] = nullptr;
        session.state = "edit_work";
    } else {
        throw std::runtime_error("Field 'mode' must be 'existing' or 'new'.");
    }

    save_session(database_.handle(), session);
    return session_json(session);
}

Json ImportWorkflowService::update_work_draft(const std::string& session_id, const Json& work) const {
    auto session = load_session(database_.handle(), session_id);
    require_state(session, "edit_work");
    required_string(work, "canonical_title");
    string_array(work, "authors");
    session.payload["work"] = work;
    push_history(session);
    session.state = "edit_series";
    save_session(database_.handle(), session);
    return session_json(session);
}

Json ImportWorkflowService::update_series_draft(const std::string& session_id, const Json& series) const {
    auto session = load_session(database_.handle(), session_id);
    require_state(session, "edit_series");

    const auto mode = required_string(series, "mode");
    if (mode == "existing") {
        required_string(series, "existing_series_id");
    } else if (mode == "new") {
        required_string(series, "name");
    } else if (mode != "none") {
        throw std::runtime_error("Series mode must be 'none', 'existing', or 'new'.");
    }
    optional_int(series, "season");
    optional_double(series, "position");

    session.payload["series"] = series;
    push_history(session);
    session.state = "edit_edition";
    save_session(database_.handle(), session);
    return session_json(session);
}

Json ImportWorkflowService::update_edition_draft(const std::string& session_id, const Json& edition) const {
    auto session = load_session(database_.handle(), session_id);
    require_state(session, "edit_edition");
    const auto isbn = normalize_isbn(required_string(edition, "isbn"));
    if (!is_valid_isbn(isbn)) {
        throw std::runtime_error("Edition ISBN is invalid.");
    }
    required_string(edition, "title");
    session.payload["edition"] = edition;
    session.payload["edition"]["isbn"] = isbn;
    push_history(session);
    session.state = "edit_copy";
    save_session(database_.handle(), session);
    return session_json(session);
}

Json ImportWorkflowService::update_copy_draft(const std::string& session_id, const Json& copy) const {
    auto session = load_session(database_.handle(), session_id);
    require_state(session, "edit_copy");
    optional_int(copy, "position_in_location");
    session.payload["copy"] = copy;
    push_history(session);
    session.state = "review";
    save_session(database_.handle(), session);
    return session_json(session);
}

Json ImportWorkflowService::back(const std::string& session_id) const {
    auto session = load_session(database_.handle(), session_id);
    if (session.state == "completed") {
        throw std::runtime_error("Completed sessions cannot go back.");
    }
    if (session.history.empty()) {
        throw std::runtime_error("Session has no previous state.");
    }

    const auto snapshot = session.history.back();
    session.history.erase(session.history.end() - 1);
    session.state = snapshot.at("state").get<std::string>();
    session.isbn = snapshot.at("isbn").is_null()
        ? std::nullopt
        : std::optional<std::string>{snapshot.at("isbn").get<std::string>()};
    session.payload = snapshot.at("payload");
    save_session(database_.handle(), session);
    return session_json(session);
}

Json ImportWorkflowService::commit(const std::string& session_id) const {
    auto session = load_session(database_.handle(), session_id);
    require_state(session, "review");

    try {
        database_.execute("BEGIN;");

        std::string work_id;
        std::string edition_isbn;
        if (session.payload.value("existing_edition", false)) {
            work_id = required_string(session.payload, "selected_work_id");
            edition_isbn = required_string(session.payload.at("edition"), "isbn");
        } else {
            const auto mode = required_string(session.payload, "selected_work_mode");
            if (mode == "existing") {
                work_id = required_string(session.payload, "selected_work_id");
            } else if (mode == "new") {
                work_id = insert_work(database_.handle(), session.payload.at("work"));
            } else {
                throw std::runtime_error("No work selection exists.");
            }
            insert_series_assignment(database_.handle(), work_id, session.payload.at("series"));
            edition_isbn = insert_edition(database_.handle(), work_id, session.payload.at("edition"));
        }

        const auto copy_id = insert_copy(database_.handle(), edition_isbn, session.payload.at("copy"));
        session.payload["result"] = {
            {"work_id", work_id},
            {"edition_isbn", edition_isbn},
            {"copy_id", copy_id},
        };
        session.state = "completed";
        save_session(database_.handle(), session, true);
        database_.execute("COMMIT;");
    } catch (...) {
        database_.execute("ROLLBACK;");
        throw;
    }

    return session_json(session);
}

void ImportWorkflowService::remove_session(const std::string& session_id) const {
    Statement statement{database_.handle(), "DELETE FROM import_sessions WHERE id = ?;"};
    statement.bind(1, session_id);
    statement.execute_done();
}

} // namespace buch::services
