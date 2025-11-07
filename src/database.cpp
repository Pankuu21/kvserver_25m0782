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
        std::cerr << "Database connection failed: " << PQerrorMessage(conn_handle_) << std::endl;
        return false;
    }
    
    const char* create_table = 
        "CREATE TABLE IF NOT EXISTS kv_store ("
        "key VARCHAR(255) PRIMARY KEY, "
        "value TEXT NOT NULL"
        ");";
    
    return execute(create_table);
}

bool Database::execute(const std::string& query) {
    std::lock_guard<std::mutex> lock(mutex_);
    PGresult* res = PQexec(conn_handle_, query.c_str());
    if (res == nullptr) {
        std::cerr << "PQexec returned null result" << std::endl;
        return false;
    }

    ExecStatusType status = PQresultStatus(res);
    bool success = (status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK);

    if (!success) {
        std::cerr << "Query failed: " << PQerrorMessage(conn_handle_) << std::endl;
    }

    PQclear(res);
    return success;
}

std::string Database::escape_sql(const std::string& str) {
    std::string result;
    for (char c : str) {
        if (c == '\'') result += "''";
        else result += c;
    }
    return result;
}

bool Database::put(const std::string& key, const std::string& value) {
    std::string query = "INSERT INTO kv_store (key, value) VALUES ('" + 
                       escape_sql(key) + "', '" + escape_sql(value) + "') " +
                       "ON CONFLICT (key) DO UPDATE SET value = '" + escape_sql(value) + "';";
    return execute(query);
}

std::optional<std::string> Database::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string query = "SELECT value FROM kv_store WHERE key = '" + escape_sql(key) + "';";
    PGresult* res = PQexec(conn_handle_, query.c_str());
    if (res == nullptr) {
        std::cerr << "PQexec returned null result for get()" << std::endl;
        return std::nullopt;
    }

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        PQclear(res);
        return std::nullopt;
    }

    if (PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }

    std::string value = PQgetvalue(res, 0, 0);
    PQclear(res);
    return value;
}

bool Database::remove(const std::string& key) {
    std::string query = "DELETE FROM kv_store WHERE key = '" + escape_sql(key) + "';";
    return execute(query);
}
