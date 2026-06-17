#pragma once

#include "db/Database.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace buch::services {

class ImportWorkflowService {
public:
    ImportWorkflowService(db::Database& database, std::string google_books_api_key);

    nlohmann::json create_session(const std::string& isbn) const;
    nlohmann::json get_session(const std::string& session_id) const;
    nlohmann::json submit_isbn(const std::string& session_id, const std::string& isbn) const;
    nlohmann::json select_work(const std::string& session_id, const nlohmann::json& selection) const;
    nlohmann::json update_work_draft(const std::string& session_id, const nlohmann::json& work) const;
    nlohmann::json update_series_draft(const std::string& session_id, const nlohmann::json& series) const;
    nlohmann::json update_edition_draft(const std::string& session_id, const nlohmann::json& edition) const;
    nlohmann::json update_copy_draft(const std::string& session_id, const nlohmann::json& copy) const;
    nlohmann::json back(const std::string& session_id) const;
    nlohmann::json commit(const std::string& session_id) const;
    void remove_session(const std::string& session_id) const;

private:
    db::Database& database_;
    std::string google_books_api_key_;
};

} // namespace buch::services
