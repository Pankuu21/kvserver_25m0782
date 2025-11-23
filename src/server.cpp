#include "server.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <cstring>

HTTPServer::HTTPServer(int port, size_t num_threads, size_t cache_capacity,
                       const std::string &db_conn_string, size_t db_pool_size)
    : listen_port_(port), listen_fd_(-1)
{
    thread_pool_ = std::make_unique<ThreadPool>(num_threads);
    cache_       = std::make_unique<LRUCache>(cache_capacity);
    db_pool_     = std::make_unique<DBConnectionPool>(db_conn_string, db_pool_size);
}

HTTPServer::~HTTPServer()
{
    stop();
}

void HTTPServer::start()
{
    if (!db_pool_->is_connected()) {
        std::cerr << "Failed to connect to database pool\n";
        return;
    }

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
    {
        std::cerr << "Failed to create socket\n";
        return;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(listen_port_);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_fd_, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        std::cerr << "Failed to bind\n";
        close(listen_fd_);
        return;
    }

    if (listen(listen_fd_, 128) < 0)
    {
        std::cerr << "Failed to listen\n";
        close(listen_fd_);
        return;
    }

    running_ = true;
    std::cout << "Server started on port " << listen_port_ << std::endl;

    accept_loop();
}

void HTTPServer::accept_loop()
{
    while (running_)
    {
        int client_fd = accept(listen_fd_, nullptr, nullptr);
        if (client_fd < 0)
        {
            if (running_)
                std::cerr << "Accept failed\n";
            continue;
        }

        thread_pool_->enqueue([this, client_fd]()
                              { handle_client(client_fd); });
    }
}

void HTTPServer::handle_client(int client_fd)
{
    struct timeval timeout = {30, 0}; // 30 second timeout for keep-alive
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    bool keep_alive = true;
    
    while (keep_alive && running_)
    {
        std::string request;
        char buffer[8192];
        ssize_t bytes;
        bool headers_complete = false;
        size_t content_length = 0;
        size_t header_end_pos = 0;

        // Read request headers
        while ((bytes = recv(client_fd, buffer, sizeof(buffer), 0)) > 0)
        {
            request.append(buffer, bytes);
            
            // Check if headers are complete
            header_end_pos = request.find("\r\n\r\n");
            if (header_end_pos != std::string::npos) {
                headers_complete = true;
                
                // Extract Content-Length if present
                size_t cl_pos = request.find("Content-Length:");
                if (cl_pos != std::string::npos && cl_pos < header_end_pos) {
                    size_t cl_start = cl_pos + 15;
                    size_t cl_end = request.find("\r\n", cl_start);
                    std::string cl_str = request.substr(cl_start, cl_end - cl_start);
                    // Trim whitespace
                    cl_str.erase(0, cl_str.find_first_not_of(" \t"));
                    cl_str.erase(cl_str.find_last_not_of(" \t") + 1);
                    content_length = std::stoull(cl_str);
                }
                break;
            }
        }

        if (request.empty() || !headers_complete)
        {
            break; // Connection closed or error
        }
        
        // Read body if Content-Length specified
        if (content_length > 0) {
            size_t body_start = header_end_pos + 4;
            size_t body_received = request.size() - body_start;
            
            while (body_received < content_length) {
                bytes = recv(client_fd, buffer, 
                           std::min(sizeof(buffer), content_length - body_received), 0);
                if (bytes <= 0) {
                    keep_alive = false;
                    break;
                }
                request.append(buffer, bytes);
                body_received += bytes;
            }
        }

        // Parse request
        std::istringstream stream(request);
        std::string method, path, version;
        stream >> method >> path >> version;

        // Check for Connection header
        size_t conn_pos = request.find("Connection:");
        if (conn_pos != std::string::npos) {
            size_t conn_start = conn_pos + 11;
            size_t conn_end = request.find("\r\n", conn_start);
            std::string conn_val = request.substr(conn_start, conn_end - conn_start);
            // Trim and check
            conn_val.erase(0, conn_val.find_first_not_of(" \t"));
            conn_val.erase(conn_val.find_last_not_of(" \t") + 1);
            if (conn_val == "close") {
                keep_alive = false;
            }
        }

        std::string body;
        size_t body_pos = request.find("\r\n\r\n");
        if (body_pos != std::string::npos)
        {
            body = request.substr(body_pos + 4);
        }

        std::string key;
        if (path.rfind("/kv/", 0) == 0)
        {
            key = path.substr(4);
        }

        std::string response_body, status = "HTTP/1.1 200 OK", headers;

        // -------------------------- PUT --------------------------
        if (method == "PUT" && !key.empty())
        {
            Database* conn = db_pool_->acquire();
            if (!conn) {
                status = "HTTP/1.1 500 Internal Server Error";
                response_body = "DB_UNAVAILABLE";
            } else {
                conn->put(key, body);
                db_pool_->release(conn);
                cache_->put(key, body);
                response_body = "OK";
            }
        }

        // -------------------------- GET --------------------------
        else if (method == "GET" && !key.empty())
        {
            auto cached = cache_->get(key);

            if (cached)
            {
                std::string value = *cached;
                std::string prefix = "VALUE:";
                std::string suffix = ":END";
                response_body = prefix + value + suffix;
                headers += "X-Cache-Status: HIT\r\n";
            }
            else
            {
                Database* conn = db_pool_->acquire();
                if (!conn) {
                    status = "HTTP/1.1 500 Internal Server Error";
                    response_body = "DB_UNAVAILABLE";
                    headers += "X-Cache-Status: MISS\r\n";
                } else {
                    auto db_value = conn->get(key);
                    db_pool_->release(conn);

                    if (db_value)
                    {
                        response_body = "DB_VALUE:" + *db_value;
                        cache_->put(key, *db_value);
                        headers += "X-Cache-Status: MISS\r\n";
                    }
                    else
                    {
                        response_body = "NOT_FOUND";
                        status = "HTTP/1.1 404 Not Found";
                        headers += "X-Cache-Status: MISS\r\n";
                    }
                }
            }
        }

        // -------------------------- DELETE --------------------------
        else if (method == "DELETE" && !key.empty())
        {
            Database* conn = db_pool_->acquire();
            if (!conn) {
                status = "HTTP/1.1 500 Internal Server Error";
                response_body = "DB_UNAVAILABLE";
            } else {
                conn->remove(key);
                db_pool_->release(conn);
                cache_->remove(key);
                response_body = "OK";
            }
        }

        // -------------------------- BAD REQUEST --------------------------
        else
        {
            response_body = "BAD_REQUEST";
            status = "HTTP/1.1 400 Bad Request";
        }

        // -------------------------- SEND RESPONSE --------------------------
        std::string connection_header = keep_alive ? "keep-alive" : "close";
        std::string response = status + "\r\n" + 
                             headers +
                             "Connection: " + connection_header + "\r\n" +
                             "Content-Length: " + std::to_string(response_body.size()) + "\r\n" +
                             "\r\n" + response_body;

        ssize_t sent = send(client_fd, response.c_str(), response.size(), MSG_NOSIGNAL);
        if (sent < 0) {
            break; // Connection error
        }
        
        if (!keep_alive) {
            break;
        }
    }
    
    close(client_fd);
}

void HTTPServer::stop()
{
    running_ = false;
    if (listen_fd_ >= 0)
    {
        close(listen_fd_);
    }
}