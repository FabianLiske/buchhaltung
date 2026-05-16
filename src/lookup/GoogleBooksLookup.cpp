#include "GoogleBooksLookup.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace buch::lookup {
namespace {

using Json = nlohmann::json;

class CurlGlobal {
public:
    CurlGlobal() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            throw std::runtime_error("Could not initialize libcurl.");
        }
    }

    ~CurlGlobal() {
        curl_global_cleanup();
    }

    CurlGlobal(const CurlGlobal&) = delete;
    CurlGlobal& operator=(const CurlGlobal&) = delete;
};

size_t write_response(char* contents, size_t size, size_t nmemb, void* user_data) {
    const auto byte_count = size * nmemb;
    auto* response = static_cast<std::string*>(user_data);
    response->append(contents, byte_count);
    return byte_count;
}

std::string escape(CURL* handle, const std::string& value) {
    char* escaped = curl_easy_escape(handle, value.c_str(), static_cast<int>(value.size()));
    if (escaped == nullptr) {
        throw std::runtime_error("Could not URL-encode value.");
    }

    std::string result{escaped};
    curl_free(escaped);
    return result;
}

std::string fetch_google_books_response(const std::string& isbn, const std::string& api_key) {
    const CurlGlobal curl_global;

    using CurlHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
    CurlHandle handle{curl_easy_init(), curl_easy_cleanup};
    if (!handle) {
        throw std::runtime_error("Could not create libcurl handle.");
    }

    const std::string url = "https://www.googleapis.com/books/v1/volumes?q=isbn:"
        + escape(handle.get(), isbn)
        + "&key="
        + escape(handle.get(), api_key);

    std::string response_body;

    curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, "buchhaltung/0.1.0");
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &response_body);

    const CURLcode result = curl_easy_perform(handle.get());
    if (result != CURLE_OK) {
        const std::string message = curl_easy_strerror(result);
        throw std::runtime_error("Google Books request failed: " + message);
    }

    long response_code = 0;
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &response_code);

    if (response_code < 200 || response_code >= 300) {
        throw std::runtime_error("Google Books returned HTTP " + std::to_string(response_code) + ": " + response_body);
    }

    return response_body;
}

std::optional<std::string> optional_string(const Json& object, std::string_view key) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_string()) {
        return std::nullopt;
    }

    return iterator->get<std::string>();
}

std::vector<std::string> optional_string_array(const Json& object, std::string_view key) {
    std::vector<std::string> values;
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_array()) {
        return values;
    }

    for (const auto& value : *iterator) {
        if (value.is_string()) {
            values.push_back(value.get<std::string>());
        }
    }

    return values;
}

std::optional<int> optional_positive_int(const Json& object, std::string_view key) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_number_integer()) {
        return std::nullopt;
    }

    const int value = iterator->get<int>();
    if (value <= 0) {
        return std::nullopt;
    }

    return value;
}

BookLookupResult parse_google_books_response(const std::string& response_body) {
    const Json response = Json::parse(response_body);

    if (response.contains("error")) {
        const auto message = response["error"].value("message", "unknown Google Books error");
        throw std::runtime_error("Google Books error: " + message);
    }

    const int total_items = response.value("totalItems", 0);
    if (total_items == 0 || !response.contains("items") || !response["items"].is_array() || response["items"].empty()) {
        throw std::runtime_error("No Google Books result found for this ISBN.");
    }

    const Json& item = response["items"].at(0);
    const Json volume_info = item.value("volumeInfo", Json::object());

    BookLookupResult result;
    result.google_volume_id = item.value("id", "");
    result.title = optional_string(volume_info, "title");
    result.subtitle = optional_string(volume_info, "subtitle");
    result.authors = optional_string_array(volume_info, "authors");
    result.publisher = optional_string(volume_info, "publisher");
    result.published_date = optional_string(volume_info, "publishedDate");
    result.language = optional_string(volume_info, "language");
    result.page_count = optional_positive_int(volume_info, "pageCount");
    result.categories = optional_string_array(volume_info, "categories");
    result.maturity_rating = optional_string(volume_info, "maturityRating");
    result.description = optional_string(volume_info, "description");
    result.info_link = optional_string(volume_info, "infoLink");
    result.canonical_link = optional_string(volume_info, "canonicalVolumeLink");

    if (!result.description.has_value() && item.contains("searchInfo") && item["searchInfo"].is_object()) {
        result.description = optional_string(item["searchInfo"], "textSnippet");
    }

    if (volume_info.contains("imageLinks") && volume_info["imageLinks"].is_object()) {
        result.thumbnail_url = optional_string(volume_info["imageLinks"], "thumbnail");
        if (!result.thumbnail_url.has_value()) {
            result.thumbnail_url = optional_string(volume_info["imageLinks"], "smallThumbnail");
        }
    }

    const auto identifiers = volume_info.find("industryIdentifiers");
    if (identifiers != volume_info.end() && identifiers->is_array()) {
        for (const auto& identifier : *identifiers) {
            if (!identifier.is_object()) {
                continue;
            }

            const auto type = optional_string(identifier, "type");
            const auto value = optional_string(identifier, "identifier");
            if (!type.has_value() || !value.has_value()) {
                continue;
            }

            if (*type == "ISBN_10") {
                result.isbn_10 = value;
            } else if (*type == "ISBN_13") {
                result.isbn_13 = value;
            }
        }
    }

    return result;
}

} // namespace

BookLookupResult lookup_google_books_by_isbn(const std::string& isbn, const std::string& api_key) {
    if (isbn.empty()) {
        throw std::runtime_error("ISBN must not be empty.");
    }

    if (api_key.empty()) {
        throw std::runtime_error("GOOGLE_BOOKS_KEY is empty.");
    }

    return parse_google_books_response(fetch_google_books_response(isbn, api_key));
}

} // namespace buch::lookup
