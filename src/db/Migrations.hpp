#pragma once

#include "Database.hpp"

namespace buch::db {

void apply_migrations(const Database& database);

} // namespace buch::db
