#include "server.h"
#include <iostream>
#include <csignal>

HTTPServer* g_server = nullptr;

void signal_handler(int signal) {
    if (g_server) {
        std::cout << "\nShutting down..." << std::endl;
        g_server->stop();
        exit(0);
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <port> [num_threads] [cache_capacity]" << std::endl;
        return 1;
    }

    int listen_port = atoi(argv[1]);
    size_t workers_count = (argc > 2) ? static_cast<size_t>(atoi(argv[2])) : 4;
    size_t cache_limit = (argc > 3) ? static_cast<size_t>(atoi(argv[3])) : 100;


    std::string db_conn = "host=localhost port=5432 dbname=kv_db user=postgres password=password";
    
    std::signal(SIGINT, signal_handler);
    
    HTTPServer server(listen_port, workers_count, cache_limit, db_conn);
    g_server = &server;
    
    std::cout << "Starting KV Server..." << std::endl;
    std::cout << "Port: " << listen_port << std::endl;
    std::cout << "Threads: " << workers_count << std::endl;
    std::cout << "Cache Capacity: " << cache_limit << std::endl;
    
    server.start();
    
    return 0;
}
