#pragma once

#include "db/Database.hpp"

#include <string>

namespace buch::server {

struct ServerConfig {
    std::string host{"127.0.0.1"};
    int port{8080};
    std::string google_books_api_key;
};

void run_http_server(db::Database& database, const ServerConfig& config);

} // namespace buch::server
