#include "database.h"
#include <iostream>

Database::Database(const std::string& conn_string)
    : conninfo_(conn_string), conn_handle_(nullptr) {}

Database::~Database() {
    if (conn_handle_) {
        PQfinish(conn_handle_);
    }
}

bool Database::connect() {
    conn_handle_ = PQconnectdb(conninfo_.c_str());
    if (PQstatus(conn_handle_) != CONNECTION_OK) {
        std::cerr << "DB connection failed: " << PQerrorMessage(conn_handle_) << "\n";
        return false;
    }
    
    const char* sql = "CREATE TABLE IF NOT EXISTS kv_store (key VARCHAR(255) PRIMARY KEY, value TEXT)";
    return execute(sql);
}

bool Database::execute(const std::string& query) {
    std::lock_guard<std::mutex> lock(mutex_);
    PGresult* res = PQexec(conn_handle_, query.c_str());
    if (!res) return false;
    
    bool ok = (PQresultStatus(res) == PGRES_COMMAND_OK || PQresultStatus(res) == PGRES_TUPLES_OK);
    PQclear(res);
    return ok;
}

bool Database::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* params[2] = {key.c_str(), value.c_str()};
    PGresult* res = PQexecParams(conn_handle_,
        "INSERT INTO kv_store (key, value) VALUES ($1, $2) ON CONFLICT (key) DO UPDATE SET value = $2",
        2, NULL, params, NULL, NULL, 0);
    if (!res) return false;
    bool ok = (PQresultStatus(res) == PGRES_COMMAND_OK);
    PQclear(res);
    return ok;
}

std::optional<std::string> Database::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* params[1] = {key.c_str()};
    PGresult* res = PQexecParams(conn_handle_,
        "SELECT value FROM kv_store WHERE key = $1",
        1, NULL, params, NULL, NULL, 0);
    if (!res) return std::nullopt;
    
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }
    
    std::string value = PQgetvalue(res, 0, 0);
    PQclear(res);
    return value;
}

bool Database::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* params[1] = {key.c_str()};
    PGresult* res = PQexecParams(conn_handle_,
        "DELETE FROM kv_store WHERE key = $1",
        1, NULL, params, NULL, NULL, 0);
    if (!res) return false;
    bool ok = (PQresultStatus(res) == PGRES_COMMAND_OK);
    PQclear(res);
    return ok;
}