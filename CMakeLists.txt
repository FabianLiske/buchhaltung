cmake_minimum_required(VERSION 3.28)

project(
    buchhaltung
    VERSION 0.1.0
    DESCRIPTION "Lokale Terminal-Anwendung zur Verwaltung einer Büchersammlung"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v6.1.9
)
FetchContent_MakeAvailable(ftxui)

find_package(SQLite3 REQUIRED)
find_package(CURL REQUIRED)

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include
)

add_executable(buch
    src/db/Database.cpp
    src/db/Migrations.cpp
    src/lookup/GoogleBooksLookup.cpp
    src/services/BookImportService.cpp
    src/tui/add_book/AddBookTui.cpp
    src/main.cpp
)

target_compile_features(buch PRIVATE cxx_std_23)
target_include_directories(buch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(buch PRIVATE
    CURL::libcurl
    SQLite3::SQLite3
    ftxui::component
    ftxui::dom
    ftxui::screen
    nlohmann_json
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(buch PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
