#include "server.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <cstring>

HTTPServer::HTTPServer(int port, size_t num_threads, size_t cache_capacity,
                       const std::string& db_conn_string)
    : listen_port_(port), listen_fd_(-1) {
    
    thread_pool_ = std::make_unique<ThreadPool>(num_threads);
    cache_ = std::make_unique<LRUCache>(cache_capacity);
    db_ = std::make_unique<Database>(db_conn_string);
}

HTTPServer::~HTTPServer() {
    stop();
}

void HTTPServer::start() {
    if (!db_->connect()) {
        std::cerr << "Failed to connect to database" << std::endl;
        return;
    }
    
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return;
    }
    
    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(listen_port_);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind" << std::endl;
        close(listen_fd_);
        return;
    }

    if (listen(listen_fd_, 128) < 0) {
        std::cerr << "Failed to listen" << std::endl;
        close(listen_fd_);
        return;
    }
    
    running_ = true;
    std::cout << "Server started on port " << listen_port_ << std::endl;
    
    accept_loop();
}

void HTTPServer::accept_loop() {
    while (running_) {
    int client_fd = accept(listen_fd_, nullptr, nullptr);
        if (client_fd < 0) {
            if (running_) {
                std::cerr << "Accept failed" << std::endl;
            }
            continue;
        }
        
        thread_pool_->enqueue([this, client_fd]() {
            handle_client(client_fd);
        });
    }
}

void HTTPServer::parse_http_request(const std::string& request,
                                    std::string& method,
                                    std::string& path,
                                    std::string& body) {
    std::istringstream stream(request);
    std::string http_version;
    
    stream >> method >> path >> http_version;
    
    size_t body_pos = request.find("\r\n\r\n");
    if (body_pos != std::string::npos) {
        body = request.substr(body_pos + 4);
    }
}

void HTTPServer::handle_client(int client_fd) {
    std::string request;
    char buffer[4096];
    ssize_t bytes = 0;
    while ((bytes = recv(client_fd, buffer, sizeof(buffer), 0)) > 0) {
        request.append(buffer, static_cast<size_t>(bytes));
        
    }

    if (request.empty()) {
        close(client_fd);
        return;
    }
    
    std::string method, path, body;
    parse_http_request(request, method, path, body);
    
    std::string key;
    if (path.rfind("/kv/", 0) == 0) {
        key = path.substr(4);
    }
    
    std::string response_body;
    std::string status_line;
    
    if (method == "PUT" && !key.empty()) {
        
        cache_->put(key, body);
        db_->put(key, body);
        
        response_body = "OK";
        status_line = "HTTP/1.1 200 OK";
        std::cout << "PUT: " << key << std::endl;
        
    } else if (method == "GET" && !key.empty()) {
        
        auto cached = cache_->get(key);
        if (cached) {
            response_body = *cached;
            status_line = "HTTP/1.1 200 OK";
            std::cout << "CACHE HIT: " << key << std::endl;
        } else {
            auto db_value = db_->get(key);
            if (db_value) {
                response_body = *db_value;
                cache_->put(key, *db_value);
                status_line = "HTTP/1.1 200 OK";
                std::cout << "CACHE MISS (DB HIT): " << key << std::endl;
            } else {
                response_body = "NOT_FOUND";
                status_line = "HTTP/1.1 404 Not Found";
                std::cout << "CACHE MISS (DB MISS): " << key << std::endl;
            }
        }
        
    } else if (method == "DELETE" && !key.empty()) {
        
        db_->remove(key);
        cache_->remove(key);
        
        response_body = "OK";
        status_line = "HTTP/1.1 200 OK";
        std::cout << "DELETE: " << key << std::endl;
        
    } else {
        response_body = "BAD_REQUEST";
        status_line = "HTTP/1.1 400 Bad Request";
    }
    
    std::string response = status_line + "\r\n" +
                          "Content-Length: " + std::to_string(response_body.size()) + "\r\n" +
                          "Connection: close\r\n" +
                          "\r\n" +
                          response_body;
    
    send(client_fd, response.c_str(), response.size(), 0);
    close(client_fd);
}


void HTTPServer::stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
        close(listen_fd_);
    }
}
