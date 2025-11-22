#pragma once
#include <memory>
#include <atomic>
#include "threadpool.h"
#include "cache.h"
#include "database.h"
#include "db_pool.h"


class HTTPServer {
public:
    HTTPServer(int port, size_t num_threads, size_t cache_capacity,
           const std::string& db_conn_string, size_t db_pool_size = 16);
;
    ~HTTPServer();
    
    void start();
    void stop();
    
private:
    int listen_port_;
    int listen_fd_;
    std::atomic<bool> running_{false};
    
    std::unique_ptr<ThreadPool> thread_pool_;
    std::unique_ptr<LRUCache> cache_;
    std::unique_ptr<DBConnectionPool> db_pool_;

    
    void accept_loop();
    void handle_client(int client_fd);
    
    void parse_http_request(const std::string& request, 
                           std::string& method, 
                           std::string& path,
                           std::string& body);
};
