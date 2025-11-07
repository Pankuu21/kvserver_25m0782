#include <iostream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

class SimpleClient {
public:
    SimpleClient(const std::string& host, int port)
        : host_(host), port_(port) {}

    bool put(const std::string& key, const std::string& value) {
        std::string resp;
        return perform_http("PUT", "/kv/" + key, value, resp);
    }

    std::string get(const std::string& key) {
        std::string resp;
        if (perform_http("GET", "/kv/" + key, "", resp)) {
            return resp;
        }
        return std::string();
    }

    bool delete_key(const std::string& key) {
        std::string resp;
        return perform_http("DELETE", "/kv/" + key, "", resp);
    }

private:
    std::string host_;
    int port_;

    bool send_request(const std::string& method,
                      const std::string& path,
                      const std::string& body) {
        std::string response_buf;
        return perform_http(method, path, body, response_buf);
    }

    std::string send_request_get(const std::string& method,
                                 const std::string& path) {
        std::string response_buf;
        if (perform_http(method, path, "", response_buf)) {
            return response_buf;
        }
        return {};
    }

    bool perform_http(const std::string& method,
                      const std::string& path,
                      const std::string& body,
                      std::string& out_response) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);

        if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "Failed to connect to server" << std::endl;
            close(fd);
            return false;
        }

        
        std::string start_line = method + " " + path + " HTTP/1.1\r\n";
        std::string host_hdr = "Host: " + host_ + "\r\n";
        std::string cl_hdr;
        if (!body.empty()) cl_hdr = "Content-Length: " + std::to_string(body.size()) + "\r\n";
        std::string conn_hdr = "Connection: close\r\n\r\n";

        std::string request = start_line + host_hdr + cl_hdr + conn_hdr + body;

        size_t total = request.size();
        size_t sent = 0;
        while (sent < total) {
            ssize_t n = send(fd, request.data() + sent, total - sent, 0);
            if (n < 0) {
                std::cerr << "Failed to send request" << std::endl;
                close(fd);
                return false;
            }
            sent += static_cast<size_t>(n);
        }

    shutdown(fd, SHUT_WR);

        out_response.clear();
        char buffer[4096];
        ssize_t r = 0;
        while ((r = recv(fd, buffer, sizeof(buffer), 0)) > 0) {
            out_response.append(buffer, static_cast<size_t>(r));
        }

        if (!out_response.empty()) std::cout << out_response << std::endl;

        close(fd);
        return true;
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <command> [args...]" << std::endl;
        std::cerr << "Commands:" << std::endl;
        std::cerr << "  put <key> <value>    - Store a value" << std::endl;
        std::cerr << "  get <key>            - Retrieve a value" << std::endl;
        std::cerr << "  delete <key>         - Delete a value" << std::endl;
        return 1;
    }

    SimpleClient client("localhost", 8080);

    std::string command = argv[1];

    if (command == "put" && argc == 4) {
        std::string key = argv[2];
        std::string value = argv[3];
        std::cout << "Sending PUT request: key=" << key << ", value=" << value << std::endl;
        client.put(key, value);

    } else if (command == "get" && argc == 3) {
        std::string key = argv[2];
        std::cout << "Sending GET request: key=" << key << std::endl;
        client.get(key);

    } else if (command == "delete" && argc == 3) {
        std::string key = argv[2];
        std::cout << "Sending DELETE request: key=" << key << std::endl;
        client.delete_key(key);

    } else {
        std::cerr << "Invalid command or arguments" << std::endl;
        return 1;
    }

    return 0;
}
