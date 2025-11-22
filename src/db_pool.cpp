#include "db_pool.h"
#include <iostream>

DBConnectionPool::DBConnectionPool(const std::string& conninfo, size_t pool_size) {
    conns_.reserve(pool_size);
    in_use_.assign(pool_size, false);

    for (size_t i = 0; i < pool_size; ++i) {
        auto db = std::make_unique<Database>(conninfo);
        if (!db->connect()) {
            std::cerr << "DB pool: failed to connect connection " << i << "\n";
            connected_ = false;
            return;
        }
        conns_.push_back(std::move(db));
    }
    connected_ = true;
}

Database* DBConnectionPool::acquire() {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [&]{
        for (bool used : in_use_) if (!used) return true;
        return false;
    });

    for (size_t i = 0; i < conns_.size(); ++i) {
        if (!in_use_[i]) {
            in_use_[i] = true;
            return conns_[i].get();
        }
    }
    return nullptr; // should not happen
}

void DBConnectionPool::release(Database* db) {
    std::unique_lock<std::mutex> lock(mtx_);
    for (size_t i = 0; i < conns_.size(); ++i) {
        if (conns_[i].get() == db) {
            in_use_[i] = false;
            cv_.notify_one();
            return;
        }
    }
}
