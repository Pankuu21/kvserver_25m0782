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
{if (!db_pool_->is_connected()) {
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
    struct timeval timeout = {5, 0};
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    std::string request;
    char buffer[4096];
    ssize_t bytes;

    while ((bytes = recv(client_fd, buffer, sizeof(buffer), 0)) > 0)
    {
        request.append(buffer, bytes);
        if (request.find("\r\n\r\n") != std::string::npos)
            break;
    }

    if (request.empty())
    {
        close(client_fd);
        return;
    }

    std::istringstream stream(request);
    std::string method, path, version;
    stream >> method >> path >> version;

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
    std::string response = status + "\r\n" + headers +
                           "Content-Length: " + std::to_string(response_body.size()) + "\r\n" +
                           "Connection: close\r\n\r\n" + response_body;

    send(client_fd, response.c_str(), response.size(), 0);
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