cmake_minimum_required(VERSION 3.28)

file(REAL_PATH "${CMAKE_SOURCE_DIR}" SOURCE_DIR_REAL)
file(REAL_PATH "${CMAKE_BINARY_DIR}" BINARY_DIR_REAL)
string(FIND "${BINARY_DIR_REAL}/" "${SOURCE_DIR_REAL}/" BUILD_INSIDE_SOURCE_TREE)
if(BUILD_INSIDE_SOURCE_TREE EQUAL 0)
    message(FATAL_ERROR
        "Build-Verzeichnis liegt innerhalb des Source-Trees. "
        "Bitte lokal ausserhalb des Repos bauen, z.B.: "
        "cmake --preset local"
    )
endif()

project(
    buchhaltung
    VERSION 0.1.0
    DESCRIPTION "Backend-Service zur Verwaltung einer Büchersammlung"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib
    GIT_TAG v0.19.0
)
FetchContent_MakeAvailable(cpp_httplib)

find_package(SQLite3 REQUIRED)
find_package(CURL REQUIRED)

if(TARGET SQLite3::SQLite3)
    set(BUCH_SQLITE_TARGET SQLite3::SQLite3)
elseif(TARGET SQLite::SQLite3)
    set(BUCH_SQLITE_TARGET SQLite::SQLite3)
else()
    add_library(SQLite::SQLite3 UNKNOWN IMPORTED)
    set_target_properties(SQLite::SQLite3 PROPERTIES
        IMPORTED_LOCATION "${SQLite3_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SQLite3_INCLUDE_DIRS}"
    )
    set(BUCH_SQLITE_TARGET SQLite::SQLite3)
endif()

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include
)

add_executable(buch
    src/db/Database.cpp
    src/db/Migrations.cpp
    src/lookup/GoogleBooksLookup.cpp
    src/server/HttpServer.cpp
    src/services/LibraryBrowseService.cpp
    src/main.cpp
)

target_compile_features(buch PRIVATE cxx_std_23)
target_include_directories(buch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(buch PRIVATE
    BUCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(buch PRIVATE
    CURL::libcurl
    ${BUCH_SQLITE_TARGET}
    httplib::httplib
    nlohmann_json
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(buch PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
