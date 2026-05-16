#pragma once

#include <filesystem>
#include <sqlite3.h>
#include <string_view>

namespace buch::db {

class Database {
public:
    explicit Database(const std::filesystem::path& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    Database(Database&&) = delete;
    Database& operator=(Database&&) = delete;

    sqlite3* handle() const;
    void execute(std::string_view sql) const;

private:
    sqlite3* handle_{nullptr};
};

} // namespace buch::db
