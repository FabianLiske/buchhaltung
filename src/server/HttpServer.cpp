#include "server/HttpServer.hpp"

#include "lookup/GoogleBooksLookup.hpp"
#include "services/BookImportService.hpp"
#include "services/LibraryBrowseService.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace buch::server {
namespace {

using Json = nlohmann::json;

constexpr auto json_content_type = "application/json; charset=utf-8";

template <typename T>
Json optional_to_json(const std::optional<T>& value) {
    if (value.has_value()) {
        return *value;
    }
    return nullptr;
}

std::optional<std::string> optional_string_from_json(const Json& object, std::string_view key) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || iterator->is_null()) {
        return std::nullopt;
    }
    if (!iterator->is_string()) {
        throw std::runtime_error("Field '" + std::string{key} + "' must be a string.");
    }
    const auto value = iterator->get<std::string>();
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

std::string string_from_json(const Json& object, std::string_view key, std::string fallback = {}) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || iterator->is_null()) {
        return fallback;
    }
    if (!iterator->is_string()) {
        throw std::runtime_error("Field '" + std::string{key} + "' must be a string.");
    }
    return iterator->get<std::string>();
}

std::optional<int> optional_int_from_json(const Json& object, std::string_view key) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || iterator->is_null()) {
        return std::nullopt;
    }
    if (!iterator->is_number_integer()) {
        throw std::runtime_error("Field '" + std::string{key} + "' must be an integer.");
    }
    return iterator->get<int>();
}

std::optional<double> optional_double_from_json(const Json& object, std::string_view key) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || iterator->is_null()) {
        return std::nullopt;
    }
    if (!iterator->is_number()) {
        throw std::runtime_error("Field '" + std::string{key} + "' must be a number.");
    }
    return iterator->get<double>();
}

std::vector<std::string> string_vector_from_json(const Json& object, std::string_view key) {
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
        result.push_back(value.get<std::string>());
    }
    return result;
}

Json parse_body(const httplib::Request& request) {
    if (request.body.empty()) {
        return Json::object();
    }
    return Json::parse(request.body);
}

void send_json(httplib::Response& response, const Json& body, int status = 200) {
    response.status = status;
    response.set_content(body.dump(), json_content_type);
}

void send_no_content(httplib::Response& response) {
    response.status = 204;
}

void send_error(httplib::Response& response, int status, std::string message) {
    send_json(response, Json{{"error", std::move(message)}}, status);
}

template <typename Handler>
auto route(Handler handler) {
    return [handler = std::move(handler)](const httplib::Request& request, httplib::Response& response) {
        try {
            handler(request, response);
        } catch (const nlohmann::json::exception& error) {
            send_error(response, 400, std::string{"Invalid JSON: "} + error.what());
        } catch (const std::invalid_argument& error) {
            send_error(response, 400, error.what());
        } catch (const std::runtime_error& error) {
            send_error(response, 400, error.what());
        } catch (const std::exception& error) {
            send_error(response, 500, error.what());
        }
    };
}

Json to_json(const services::ExistingEdition& edition) {
    return {
        {"isbn", edition.isbn},
        {"title", edition.title},
        {"subtitle", optional_to_json(edition.subtitle)},
        {"language_code", edition.language_code},
        {"publisher", optional_to_json(edition.publisher)},
        {"publication_year", optional_to_json(edition.publication_year)},
        {"page_count", optional_to_json(edition.page_count)},
        {"work_id", edition.work_id},
        {"work_title", edition.work_title},
        {"authors", edition.authors},
    };
}

Json to_json(const services::WorkSuggestion& suggestion) {
    return {
        {"id", suggestion.id},
        {"title", suggestion.title},
        {"authors", suggestion.authors},
    };
}

Json to_json(const services::SeriesOption& series) {
    return {
        {"id", series.id},
        {"name", series.name},
    };
}

Json to_json(const services::ImportResult& result) {
    return {
        {"work_id", result.work_id},
        {"edition_isbn", result.edition_isbn},
        {"copy_id", result.copy_id},
        {"created_work", result.created_work},
        {"created_edition", result.created_edition},
    };
}

Json to_json(const services::WorkListItem& work) {
    return {
        {"id", work.id},
        {"title", work.title},
        {"authors", work.authors},
        {"first_published_year", optional_to_json(work.first_published_year)},
        {"series", work.series},
        {"reading_status", work.reading_status},
        {"edition_count", work.edition_count},
        {"copy_count", work.copy_count},
    };
}

Json to_json(const services::EditionListItem& edition) {
    return {
        {"isbn", edition.isbn},
        {"title", edition.title},
        {"subtitle", optional_to_json(edition.subtitle)},
        {"language_code", edition.language_code},
        {"publisher", optional_to_json(edition.publisher)},
        {"publication_year", optional_to_json(edition.publication_year)},
        {"edition_name", optional_to_json(edition.edition_name)},
        {"format", optional_to_json(edition.format)},
        {"page_count", optional_to_json(edition.page_count)},
        {"copy_count", edition.copy_count},
    };
}

Json to_json(const services::CopyListItem& copy) {
    return {
        {"id", copy.id},
        {"location_path", optional_to_json(copy.location_path)},
        {"position_in_location", optional_to_json(copy.position_in_location)},
        {"condition", optional_to_json(copy.condition)},
        {"acquired_date", optional_to_json(copy.acquired_date)},
        {"acquired_where", optional_to_json(copy.acquired_where)},
        {"borrowed_from", optional_to_json(copy.borrowed_from)},
        {"lent_to", optional_to_json(copy.lent_to)},
        {"notes", optional_to_json(copy.notes)},
    };
}

Json to_json(const services::WorkDetails& work) {
    return {
        {"id", work.id},
        {"canonical_title", work.canonical_title},
        {"original_title", optional_to_json(work.original_title)},
        {"original_language_code", optional_to_json(work.original_language_code)},
        {"first_published_year", optional_to_json(work.first_published_year)},
        {"description", optional_to_json(work.description)},
        {"age_rating", optional_to_json(work.age_rating)},
        {"notes", optional_to_json(work.notes)},
        {"authors", work.authors},
        {"series", work.series},
        {"genres", work.genres},
        {"reading_status", work.reading_status},
    };
}

Json to_json(const services::EditionDetails& edition) {
    return {
        {"isbn", edition.isbn},
        {"title", edition.title},
        {"subtitle", optional_to_json(edition.subtitle)},
        {"language_code", edition.language_code},
        {"publisher", optional_to_json(edition.publisher)},
        {"publication_year", optional_to_json(edition.publication_year)},
        {"edition_name", optional_to_json(edition.edition_name)},
        {"format", optional_to_json(edition.format)},
        {"page_count", optional_to_json(edition.page_count)},
        {"cover_url", optional_to_json(edition.cover_url)},
        {"notes", optional_to_json(edition.notes)},
    };
}

Json to_json(const services::CopyDetails& copy) {
    return {
        {"id", copy.id},
        {"edition_isbn", copy.edition_isbn},
        {"location_path", optional_to_json(copy.location_path)},
        {"position_in_location", optional_to_json(copy.position_in_location)},
        {"condition", optional_to_json(copy.condition)},
        {"acquired_date", optional_to_json(copy.acquired_date)},
        {"acquired_where", optional_to_json(copy.acquired_where)},
        {"borrowed_from", optional_to_json(copy.borrowed_from)},
        {"lent_to", optional_to_json(copy.lent_to)},
        {"notes", optional_to_json(copy.notes)},
    };
}

Json to_json(const lookup::BookLookupResult& lookup) {
    return {
        {"google_volume_id", lookup.google_volume_id},
        {"title", optional_to_json(lookup.title)},
        {"subtitle", optional_to_json(lookup.subtitle)},
        {"authors", lookup.authors},
        {"publisher", optional_to_json(lookup.publisher)},
        {"published_date", optional_to_json(lookup.published_date)},
        {"language", optional_to_json(lookup.language)},
        {"isbn_10", optional_to_json(lookup.isbn_10)},
        {"isbn_13", optional_to_json(lookup.isbn_13)},
        {"page_count", optional_to_json(lookup.page_count)},
        {"categories", lookup.categories},
        {"maturity_rating", optional_to_json(lookup.maturity_rating)},
        {"description", optional_to_json(lookup.description)},
        {"thumbnail_url", optional_to_json(lookup.thumbnail_url)},
        {"info_link", optional_to_json(lookup.info_link)},
        {"canonical_link", optional_to_json(lookup.canonical_link)},
    };
}

services::ContributorInput contributor_from_json(const Json& json) {
    if (!json.is_object()) {
        throw std::runtime_error("Contributor entries must be objects.");
    }

    return services::ContributorInput{
        .role = string_from_json(json, "role", "contributor"),
        .name = string_from_json(json, "name"),
        .sort_name = optional_string_from_json(json, "sort_name"),
        .birth_year = optional_int_from_json(json, "birth_year"),
        .death_year = optional_int_from_json(json, "death_year"),
        .notes = optional_string_from_json(json, "notes"),
    };
}

std::vector<services::ContributorInput> contributors_from_json(const Json& object, std::string_view key) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || iterator->is_null()) {
        return {};
    }
    if (!iterator->is_array()) {
        throw std::runtime_error("Field '" + std::string{key} + "' must be an array.");
    }

    std::vector<services::ContributorInput> result;
    for (const auto& value : *iterator) {
        result.push_back(contributor_from_json(value));
    }
    return result;
}

services::ImportRequest import_request_from_json(const Json& json) {
    if (!json.is_object()) {
        throw std::runtime_error("Import request must be an object.");
    }

    services::ImportRequest request;
    request.isbn = string_from_json(json, "isbn");
    request.title = string_from_json(json, "title");
    request.subtitle = optional_string_from_json(json, "subtitle");
    request.authors = string_vector_from_json(json, "authors");
    request.edition_contributors = contributors_from_json(json, "edition_contributors");
    request.publisher = optional_string_from_json(json, "publisher");
    request.published_date = optional_string_from_json(json, "published_date");
    request.language_code = string_from_json(json, "language_code", "und");
    request.edition_name = optional_string_from_json(json, "edition_name");
    request.format = optional_string_from_json(json, "format");
    request.page_count = optional_int_from_json(json, "page_count");
    request.cover_url = optional_string_from_json(json, "cover_url");
    request.edition_notes = optional_string_from_json(json, "edition_notes");
    request.description = optional_string_from_json(json, "description");
    request.age_rating = optional_string_from_json(json, "age_rating");
    request.existing_work_id = optional_string_from_json(json, "existing_work_id");
    request.canonical_title = optional_string_from_json(json, "canonical_title");
    request.original_title = optional_string_from_json(json, "original_title");
    request.original_language_code = optional_string_from_json(json, "original_language_code");
    request.work_notes = optional_string_from_json(json, "work_notes");
    request.existing_series_id = optional_string_from_json(json, "existing_series_id");
    request.new_series_name = optional_string_from_json(json, "new_series_name");
    request.series_original_title = optional_string_from_json(json, "series_original_title");
    request.series_description = optional_string_from_json(json, "series_description");
    request.series_notes = optional_string_from_json(json, "series_notes");
    request.series_season = optional_int_from_json(json, "series_season");
    request.series_position = optional_double_from_json(json, "series_position");
    request.series_position_label = optional_string_from_json(json, "series_position_label");
    request.work_series_notes = optional_string_from_json(json, "work_series_notes");
    request.parent_genre_name = optional_string_from_json(json, "parent_genre_name");
    request.genre_name = optional_string_from_json(json, "genre_name");
    request.genre_description = optional_string_from_json(json, "genre_description");
    request.genre_notes = optional_string_from_json(json, "genre_notes");
    request.reading_status = optional_string_from_json(json, "reading_status");
    request.reading_started_date = optional_string_from_json(json, "reading_started_date");
    request.reading_finished_date = optional_string_from_json(json, "reading_finished_date");
    request.rating = optional_int_from_json(json, "rating");
    request.reading_notes = optional_string_from_json(json, "reading_notes");
    request.location_path = optional_string_from_json(json, "location_path");
    request.location_description = optional_string_from_json(json, "location_description");
    request.location_notes = optional_string_from_json(json, "location_notes");
    request.location_visual_x = optional_double_from_json(json, "location_visual_x");
    request.location_visual_y = optional_double_from_json(json, "location_visual_y");
    request.location_visual_z = optional_double_from_json(json, "location_visual_z");
    request.location_visual_width = optional_double_from_json(json, "location_visual_width");
    request.location_visual_height = optional_double_from_json(json, "location_visual_height");
    request.location_visual_depth = optional_double_from_json(json, "location_visual_depth");
    request.position_in_location = optional_int_from_json(json, "position_in_location");
    request.condition = optional_string_from_json(json, "condition");
    request.acquired_date = optional_string_from_json(json, "acquired_date");
    request.acquired_where = optional_string_from_json(json, "acquired_where");
    request.borrowed_from = optional_string_from_json(json, "borrowed_from");
    request.lent_to = optional_string_from_json(json, "lent_to");
    request.copy_notes = optional_string_from_json(json, "copy_notes");
    return request;
}

services::WorkUpdate work_update_from_json(const Json& json, std::string id) {
    if (!json.is_object()) {
        throw std::runtime_error("Work update must be an object.");
    }

    return services::WorkUpdate{
        .id = std::move(id),
        .canonical_title = string_from_json(json, "canonical_title"),
        .original_title = optional_string_from_json(json, "original_title"),
        .original_language_code = optional_string_from_json(json, "original_language_code"),
        .first_published_year = optional_int_from_json(json, "first_published_year"),
        .description = optional_string_from_json(json, "description"),
        .age_rating = optional_string_from_json(json, "age_rating"),
        .notes = optional_string_from_json(json, "notes"),
        .authors = string_vector_from_json(json, "authors"),
    };
}

services::EditionUpdate edition_update_from_json(const Json& json, std::string isbn) {
    if (!json.is_object()) {
        throw std::runtime_error("Edition update must be an object.");
    }

    return services::EditionUpdate{
        .isbn = std::move(isbn),
        .title = string_from_json(json, "title"),
        .subtitle = optional_string_from_json(json, "subtitle"),
        .language_code = string_from_json(json, "language_code", "und"),
        .publisher = optional_string_from_json(json, "publisher"),
        .publication_year = optional_int_from_json(json, "publication_year"),
        .edition_name = optional_string_from_json(json, "edition_name"),
        .format = optional_string_from_json(json, "format"),
        .page_count = optional_int_from_json(json, "page_count"),
        .cover_url = optional_string_from_json(json, "cover_url"),
        .notes = optional_string_from_json(json, "notes"),
    };
}

services::CopyUpdate copy_update_from_json(const Json& json, std::string id) {
    if (!json.is_object()) {
        throw std::runtime_error("Copy update must be an object.");
    }

    return services::CopyUpdate{
        .id = std::move(id),
        .location_path = optional_string_from_json(json, "location_path"),
        .position_in_location = optional_int_from_json(json, "position_in_location"),
        .condition = optional_string_from_json(json, "condition"),
        .acquired_date = optional_string_from_json(json, "acquired_date"),
        .acquired_where = optional_string_from_json(json, "acquired_where"),
        .borrowed_from = optional_string_from_json(json, "borrowed_from"),
        .lent_to = optional_string_from_json(json, "lent_to"),
        .notes = optional_string_from_json(json, "notes"),
    };
}

template <typename T>
Json to_array_json(const std::vector<T>& values) {
    Json result = Json::array();
    for (const auto& value : values) {
        result.push_back(to_json(value));
    }
    return result;
}

std::string capture(const httplib::Request& request, std::size_t index) {
    if (index >= request.matches.size()) {
        return {};
    }
    return request.matches[index].str();
}

std::string query_param(const httplib::Request& request, const char* name) {
    if (!request.has_param(name)) {
        return {};
    }
    return request.get_param_value(name);
}

void configure_cors(httplib::Server& http) {
    http.set_pre_routing_handler([](const httplib::Request&, httplib::Response& response) {
        response.set_header("Access-Control-Allow-Origin", "*");
        response.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        response.set_header("Access-Control-Allow-Headers", "Content-Type");
        return httplib::Server::HandlerResponse::Unhandled;
    });

    http.Options(R"(.*)", [](const httplib::Request&, httplib::Response& response) {
        response.status = 204;
    });
}

} // namespace

void run_http_server(db::Database& database, const ServerConfig& config) {
    services::BookImportService import_service{database};
    services::LibraryBrowseService browse_service{database};

    httplib::Server http;
    http.new_task_queue = [] {
        return new httplib::ThreadPool(1);
    };
    configure_cors(http);

    http.Get("/healthz", route([](const httplib::Request&, httplib::Response& response) {
        send_json(response, Json{{"status", "ok"}});
    }));

    http.Get("/api/works", route([&](const httplib::Request& request, httplib::Response& response) {
        const auto works = browse_service.list_works(services::WorkFilters{
            .text = query_param(request, "text"),
            .author = query_param(request, "author"),
            .series = query_param(request, "series"),
            .reading_status = query_param(request, "reading_status"),
        });
        send_json(response, Json{{"works", to_array_json(works)}});
    }));

    http.Get(R"(/api/works/([^/]+))", route([&](const httplib::Request& request, httplib::Response& response) {
        const auto work = browse_service.get_work(capture(request, 1));
        if (!work.has_value()) {
            send_error(response, 404, "Work not found.");
            return;
        }
        send_json(response, to_json(*work));
    }));

    http.Get(R"(/api/works/([^/]+)/editions)", route([&](const httplib::Request& request, httplib::Response& response) {
        const auto editions = browse_service.list_editions_for_work(capture(request, 1));
        send_json(response, Json{{"editions", to_array_json(editions)}});
    }));

    http.Put(R"(/api/works/([^/]+))", route([&](const httplib::Request& request, httplib::Response& response) {
        const auto update = work_update_from_json(parse_body(request), capture(request, 1));
        browse_service.update_work(update);
        const auto updated = browse_service.get_work(update.id);
        if (!updated.has_value()) {
            send_error(response, 404, "Work not found.");
            return;
        }
        send_json(response, to_json(*updated));
    }));

    http.Delete(R"(/api/works/([^/]+))", route([&](const httplib::Request& request, httplib::Response& response) {
        browse_service.delete_work(capture(request, 1));
        send_no_content(response);
    }));

    http.Get(R"(/api/editions/([^/]+))", route([&](const httplib::Request& request, httplib::Response& response) {
        const auto edition = browse_service.get_edition(capture(request, 1));
        if (!edition.has_value()) {
            send_error(response, 404, "Edition not found.");
            return;
        }
        send_json(response, to_json(*edition));
    }));

    http.Get(R"(/api/editions/([^/]+)/copies)", route([&](const httplib::Request& request, httplib::Response& response) {
        const auto copies = browse_service.list_copies_for_edition(capture(request, 1));
        send_json(response, Json{{"copies", to_array_json(copies)}});
    }));

    http.Put(R"(/api/editions/([^/]+))", route([&](const httplib::Request& request, httplib::Response& response) {
        const auto update = edition_update_from_json(parse_body(request), capture(request, 1));
        browse_service.update_edition(update);
        const auto updated = browse_service.get_edition(update.isbn);
        if (!updated.has_value()) {
            send_error(response, 404, "Edition not found.");
            return;
        }
        send_json(response, to_json(*updated));
    }));

    http.Delete(R"(/api/editions/([^/]+))", route([&](const httplib::Request& request, httplib::Response& response) {
        browse_service.delete_edition(capture(request, 1));
        send_no_content(response);
    }));

    http.Get(R"(/api/copies/([^/]+))", route([&](const httplib::Request& request, httplib::Response& response) {
        const auto copy = browse_service.get_copy(capture(request, 1));
        if (!copy.has_value()) {
            send_error(response, 404, "Copy not found.");
            return;
        }
        send_json(response, to_json(*copy));
    }));

    http.Put(R"(/api/copies/([^/]+))", route([&](const httplib::Request& request, httplib::Response& response) {
        const auto update = copy_update_from_json(parse_body(request), capture(request, 1));
        browse_service.update_copy(update);
        const auto updated = browse_service.get_copy(update.id);
        if (!updated.has_value()) {
            send_error(response, 404, "Copy not found.");
            return;
        }
        send_json(response, to_json(*updated));
    }));

    http.Delete(R"(/api/copies/([^/]+))", route([&](const httplib::Request& request, httplib::Response& response) {
        browse_service.delete_copy(capture(request, 1));
        send_no_content(response);
    }));

    http.Get("/api/series", route([&](const httplib::Request&, httplib::Response& response) {
        send_json(response, Json{{"series", to_array_json(import_service.list_series())}});
    }));

    http.Get(R"(/api/editions/by-isbn/([^/]+))", route([&](const httplib::Request& request, httplib::Response& response) {
        const auto edition = import_service.find_edition_by_isbn(capture(request, 1));
        if (!edition.has_value()) {
            send_error(response, 404, "Edition not found.");
            return;
        }
        send_json(response, to_json(*edition));
    }));

    http.Get("/api/work-suggestions", route([&](const httplib::Request& request, httplib::Response& response) {
        const auto suggestions = import_service.suggest_works_by_authors(services::split_list(query_param(request, "authors")));
        send_json(response, Json{{"suggestions", to_array_json(suggestions)}});
    }));

    http.Get(R"(/api/lookup/isbn/([^/]+))", route([&](const httplib::Request& request, httplib::Response& response) {
        if (config.google_books_api_key.empty()) {
            send_error(response, 503, "GOOGLE_BOOKS_KEY is not configured.");
            return;
        }
        send_json(response, to_json(lookup::lookup_google_books_by_isbn(capture(request, 1), config.google_books_api_key)));
    }));

    http.Post("/api/imports", route([&](const httplib::Request& request, httplib::Response& response) {
        const auto import_request = import_request_from_json(parse_body(request));
        send_json(response, to_json(import_service.save_import(import_request)), 201);
    }));

    http.set_error_handler([](const httplib::Request&, httplib::Response& response) {
        if (response.status == 404) {
            send_error(response, 404, "Route not found.");
        }
    });

    std::cout << "buch backend listening on " << config.host << ':' << config.port << std::endl;
    if (!http.listen(config.host, config.port)) {
        throw std::runtime_error("Could not bind HTTP server to " + config.host + ':' + std::to_string(config.port));
    }
}

} // namespace buch::server
