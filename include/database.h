#pragma once
#include <string>
#include <optional>
#include <libpq-fe.h>
#include <mutex>

class Database {
public:
    explicit Database(const std::string& conn_string);
    ~Database();
    
    bool connect();
    bool put(const std::string& key, const std::string& value);
    std::optional<std::string> get(const std::string& key);
    bool remove(const std::string& key);
    
private:
    std::string conninfo_;
    PGconn* conn_handle_;
    std::mutex mutex_;
    
    bool execute(const std::string& query);
    std::string escape_sql(const std::string& str);
};
