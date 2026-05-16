#include "Migrations.hpp"

#include <sqlite3.h>
#include <stdexcept>
#include <string>

namespace buch::db {
namespace {

constexpr int initial_schema_version = 1;

constexpr std::string_view migration_table_sql = R"sql(
CREATE TABLE IF NOT EXISTS schema_migrations (
    version INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
)sql";

constexpr std::string_view initial_schema_sql = R"sql(
CREATE TABLE works (
    id TEXT PRIMARY KEY,
    canonical_title TEXT NOT NULL,
    original_title TEXT,
    original_language_code TEXT,
    first_published_year INTEGER,
    description TEXT,
    age_rating TEXT,
    notes TEXT
);

CREATE TABLE editions (
    isbn TEXT PRIMARY KEY,
    work_id TEXT NOT NULL REFERENCES works(id),
    title TEXT NOT NULL,
    subtitle TEXT,
    language_code TEXT NOT NULL,
    publisher TEXT,
    publication_year INTEGER,
    edition_name TEXT,
    format TEXT,
    page_count INTEGER,
    cover_url TEXT,
    notes TEXT
);

CREATE TABLE locations (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    type TEXT NOT NULL,
    parent_location_id TEXT REFERENCES locations(id),
    sort_order INTEGER,
    visual_x REAL,
    visual_y REAL,
    visual_z REAL,
    visual_width REAL,
    visual_height REAL,
    visual_depth REAL,
    description TEXT,
    notes TEXT
);

CREATE TABLE copies (
    id TEXT PRIMARY KEY,
    edition_isbn TEXT NOT NULL REFERENCES editions(isbn),
    location_id TEXT REFERENCES locations(id),
    position_in_location INTEGER,
    acquired_date TEXT,
    acquired_where TEXT,
    condition TEXT,
    borrowed_from TEXT,
    lent_to TEXT,
    notes TEXT
);

CREATE TABLE series (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    original_title TEXT,
    description TEXT,
    notes TEXT
);

CREATE TABLE work_series (
    work_id TEXT NOT NULL REFERENCES works(id),
    series_id TEXT NOT NULL REFERENCES series(id),
    season INTEGER,
    position REAL,
    position_label TEXT,
    notes TEXT,
    PRIMARY KEY (work_id, series_id)
);

CREATE TABLE contributors (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    sort_name TEXT,
    birth_year INTEGER,
    death_year INTEGER,
    notes TEXT
);

CREATE TABLE work_contributors (
    work_id TEXT NOT NULL REFERENCES works(id),
    contributor_id TEXT NOT NULL REFERENCES contributors(id),
    role TEXT NOT NULL,
    contributor_order INTEGER,
    PRIMARY KEY (work_id, contributor_id, role)
);

CREATE TABLE edition_contributors (
    edition_isbn TEXT NOT NULL REFERENCES editions(isbn),
    contributor_id TEXT NOT NULL REFERENCES contributors(id),
    role TEXT NOT NULL,
    contributor_order INTEGER,
    PRIMARY KEY (edition_isbn, contributor_id, role)
);

CREATE TABLE genres (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    parent_genre_id TEXT REFERENCES genres(id),
    description TEXT,
    notes TEXT
);

CREATE TABLE work_genres (
    work_id TEXT NOT NULL REFERENCES works(id),
    genre_id TEXT NOT NULL REFERENCES genres(id),
    primary_genre INTEGER,
    PRIMARY KEY (work_id, genre_id)
);

CREATE TABLE work_reading_status (
    work_id TEXT NOT NULL REFERENCES works(id),
    language_code TEXT NOT NULL,
    status TEXT NOT NULL,
    started_date TEXT,
    finished_date TEXT,
    rating INTEGER,
    notes TEXT,
    PRIMARY KEY (work_id, language_code)
);

CREATE INDEX idx_editions_work_id ON editions(work_id);
CREATE INDEX idx_copies_edition_isbn ON copies(edition_isbn);
CREATE INDEX idx_copies_location_id ON copies(location_id);
CREATE INDEX idx_locations_parent_location_id ON locations(parent_location_id);
CREATE INDEX idx_work_series_series_id ON work_series(series_id);
CREATE INDEX idx_work_contributors_contributor_id ON work_contributors(contributor_id);
CREATE INDEX idx_edition_contributors_contributor_id ON edition_contributors(contributor_id);
CREATE INDEX idx_genres_parent_genre_id ON genres(parent_genre_id);
CREATE INDEX idx_work_genres_genre_id ON work_genres(genre_id);
)sql";

bool has_migration(const Database& database, int version) {
    sqlite3_stmt* statement = nullptr;
    constexpr std::string_view sql = "SELECT 1 FROM schema_migrations WHERE version = ?;";

    if (sqlite3_prepare_v2(database.handle(), sql.data(), static_cast<int>(sql.size()), &statement, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string{"Could not prepare migration lookup: "} + sqlite3_errmsg(database.handle()));
    }

    if (sqlite3_bind_int(statement, 1, version) != SQLITE_OK) {
        sqlite3_finalize(statement);
        throw std::runtime_error(std::string{"Could not bind migration lookup: "} + sqlite3_errmsg(database.handle()));
    }

    const int result = sqlite3_step(statement);
    sqlite3_finalize(statement);

    if (result == SQLITE_ROW) {
        return true;
    }

    if (result == SQLITE_DONE) {
        return false;
    }

    throw std::runtime_error(std::string{"Could not run migration lookup: "} + sqlite3_errmsg(database.handle()));
}

} // namespace

void apply_migrations(const Database& database) {
    database.execute(migration_table_sql);

    if (has_migration(database, initial_schema_version)) {
        return;
    }

    try {
        database.execute("BEGIN;");
        database.execute(initial_schema_sql);
        database.execute("INSERT INTO schema_migrations (version, name) VALUES (1, 'initial schema');");
        database.execute("COMMIT;");
    } catch (...) {
        database.execute("ROLLBACK;");
        throw;
    }
}

} // namespace buch::db
