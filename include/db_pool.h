#pragma once
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <string>
#include "database.h"  // your existing Database class

class DBConnectionPool {
public:
    DBConnectionPool(const std::string& conninfo, size_t pool_size);

    // get a DB connection (blocks if all are busy)
    Database* acquire();

    // return a connection to the pool
    void release(Database* db);

    bool is_connected() const { return connected_; }

private:
    std::vector<std::unique_ptr<Database>> conns_;
    std::vector<bool> in_use_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    bool connected_ = false;
};
