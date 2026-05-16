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

add_executable(buch
    src/main.cpp
)

target_compile_features(buch PRIVATE cxx_std_23)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(buch PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
