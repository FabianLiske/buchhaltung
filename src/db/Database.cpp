#include "Database.hpp"

#include <stdexcept>
#include <string>

namespace buch::db {

Database::Database(const std::filesystem::path& path) {
    const auto database_path = path.string();
    if (sqlite3_open(database_path.c_str(), &handle_) != SQLITE_OK) {
        const std::string message = handle_ != nullptr ? sqlite3_errmsg(handle_) : "unknown sqlite error";
        sqlite3_close(handle_);
        handle_ = nullptr;
        throw std::runtime_error("Could not open database: " + message);
    }

    execute("PRAGMA foreign_keys = ON;");
}

Database::~Database() {
    if (handle_ != nullptr) {
        sqlite3_close(handle_);
    }
}

sqlite3* Database::handle() const {
    return handle_;
}

void Database::execute(std::string_view sql) const {
    char* error_message = nullptr;
    const std::string statement{sql};

    if (sqlite3_exec(handle_, statement.c_str(), nullptr, nullptr, &error_message) != SQLITE_OK) {
        std::string message = "sqlite statement failed";
        if (error_message != nullptr) {
            message += ": ";
            message += error_message;
            sqlite3_free(error_message);
        }
        throw std::runtime_error(message);
    }
}

} // namespace buch::db
