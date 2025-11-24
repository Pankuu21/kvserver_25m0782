#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <atomic>
#include <mutex>
#include <fstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <ctime>

const std::string HOST = "127.0.0.1";
const int PORT = 8080;
std::atomic<bool> stop_flag{false};

struct Metrics {
    std::atomic<long long> total_requests{0};
    std::atomic<long long> successful_requests{0};
    std::atomic<long long> failed_requests{0};
    std::atomic<long long> total_latency_us{0};
    std::atomic<int> cache_hits{0};
    std::atomic<int> cache_misses{0};
    std::atomic<int> get_requests{0};
    std::vector<long long> latencies_us;
    std::mutex latency_mutex;
    
    void add_result(long long latency_us, bool success, bool is_cache_hit, bool is_get) {
        total_requests++;
        if (success) {
            successful_requests++;
            total_latency_us += latency_us;
            if (is_get) {
                get_requests++;
                if (is_cache_hit) cache_hits++;
                else cache_misses++;
            }
            std::lock_guard<std::mutex> lock(latency_mutex);
            latencies_us.push_back(latency_us);
        } else {
            failed_requests++;
        }
    }
};

// Persistent connection class
class PersistentConnection {
public:
    PersistentConnection() : fd_(-1) {}
    
    ~PersistentConnection() {
        close_connection();
    }
    
    bool connect() {
        if (fd_ >= 0) return true;  // already connected
        
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
        
        struct timeval timeout = {5, 0};
        setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        
        // Enable TCP keepalive
        int keepalive = 1;
        setsockopt(fd_, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(PORT);
        inet_pton(AF_INET, HOST.c_str(), &addr.sin_addr);
        
        if (::connect(fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
            close(fd_);
            fd_ = -1;
            return false;
        }
        
        return true;
    }
    
    void close_connection() {
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
    }
    
    bool send_request(const std::string& method, const std::string& path, 
                     const std::string& body, std::string& response) {
        if (fd_ < 0 && !connect()) {
            return false;
        }
        
        // Build HTTP request with keep-alive
        std::string req = method + " " + path + " HTTP/1.1\r\n";
        req += "Host: " + HOST + "\r\n";
        req += "Connection: keep-alive\r\n";
        if (!body.empty()) {
            req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        }
        req += "\r\n" + body;
        
        // Send request
        ssize_t sent = send(fd_, req.data(), req.size(), MSG_NOSIGNAL);
        if (sent < 0) {
            close_connection();
            return false;
        }
        
        // Receive response
        response.clear();
        char buf[8192];
        size_t content_length = 0;
        bool headers_complete = false;
        size_t header_end_pos = 0;
        
        while (true) {
            ssize_t n = recv(fd_, buf, sizeof(buf), 0);
            if (n <= 0) {
                close_connection();
                return false;
            }
            
            response.append(buf, n);
            
            // Parse headers to get Content-Length
            if (!headers_complete) {
                header_end_pos = response.find("\r\n\r\n");
                if (header_end_pos != std::string::npos) {
                    headers_complete = true;
                    
                    // Extract Content-Length
                    size_t cl_pos = response.find("Content-Length:");
                    if (cl_pos != std::string::npos) {
                        size_t cl_start = cl_pos + 15;
                        size_t cl_end = response.find("\r\n", cl_start);
                        std::string cl_str = response.substr(cl_start, cl_end - cl_start);
                        content_length = std::stoull(cl_str);
                    }
                }
            }
            
            // Check if we have received the complete response
            if (headers_complete) {
                size_t body_start = header_end_pos + 4;
                size_t body_received = response.size() - body_start;
                if (body_received >= content_length) {
                    break;
                }
            }
        }
        
        return true;
    }
    
private:
    int fd_;
};

bool http_request_persistent(PersistentConnection& conn, const std::string& method, 
                             const std::string& path, const std::string& body, 
                             long long* latency_us, bool* is_cache_hit) {
    auto t0 = std::chrono::high_resolution_clock::now();
    
    std::string response;
    bool success = conn.send_request(method, path, body, response);
    
    *latency_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - t0).count();
    
    if (!success) return false;
    
    if (is_cache_hit) {
        *is_cache_hit = response.find("X-Cache-Status: HIT") != std::string::npos;
    }
    
    return response.find("200 OK") != std::string::npos;
}

class ZipfianGenerator {
public:
    ZipfianGenerator(int n, double alpha = 1.5) : n(n), alpha(alpha) {
        gen.seed(std::random_device{}());
        
        double sum = 0.0;
        for (int i = 1; i <= n; ++i) {
            sum += 1.0 / std::pow(i, alpha);
        }
        
        double cumulative = 0.0;
        for (int i = 1; i <= n; ++i) {
            cumulative += (1.0 / std::pow(i, alpha)) / sum;
            cdf.push_back(cumulative);
        }
    }
    
    int next() {
        double u = dist(gen);
        for (int i = 0; i < n; ++i) {
            if (u <= cdf[i]) return i;
        }
        return n - 1;
    }
    
private:
    int n;
    double alpha;
    std::vector<double> cdf;
    std::mt19937 gen;
    std::uniform_real_distribution<> dist{0.0, 1.0};
};

void worker_put(int thread_id, int keys_per_thread, int duration_sec, int total_keys, Metrics& m) {
    PersistentConnection conn;
    if (!conn.connect()) {
        std::cerr << "Thread " << thread_id << ": Failed to connect\n";
        return;
    }
    
    auto start = std::chrono::steady_clock::now();
    int idx = 0;
    
    while (!stop_flag) {
        if (duration_sec > 0 && std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count() >= duration_sec) break;
        if (duration_sec == 0 && idx >= keys_per_thread) break;
        
        std::string key = "key_" + std::to_string((thread_id * keys_per_thread + idx) % total_keys);
        std::string val = "VALUE_START_" + std::string(4096, 'A') + "_END";

        long long lat;
        bool ok = http_request_persistent(conn, "PUT", "/kv/" + key, val, &lat, nullptr);
        m.add_result(lat, ok, false, false);
        idx++;
    }
}

void worker_get_all(int thread_id, int keys_per_thread, int duration_sec, int total_keys, Metrics& m) {
    PersistentConnection conn;
    if (!conn.connect()) {
        std::cerr << "Thread " << thread_id << ": Failed to connect\n";
        return;
    }
    
    auto start = std::chrono::steady_clock::now();
    int idx = 0;
    
    while (!stop_flag) {
        if (duration_sec > 0 && std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count() >= duration_sec) break;
        if (duration_sec == 0 && idx >= keys_per_thread) break;
        
        std::string key = "key_" + std::to_string((thread_id * keys_per_thread + (idx % keys_per_thread)) % total_keys);
        
        long long lat;
        bool hit = false;
        bool ok = http_request_persistent(conn, "GET", "/kv/" + key, "", &lat, &hit);
        m.add_result(lat, ok, hit, true);
        idx++;
    }
}

void worker_get_popular(int thread_id, int keys_per_thread, int duration_sec, int total_keys, Metrics& m) {
    PersistentConnection conn;
    if (!conn.connect()) {
        std::cerr << "Thread " << thread_id << ": Failed to connect\n";
        return;
    }
    
    ZipfianGenerator zipf(total_keys, 1.5);
    auto start = std::chrono::steady_clock::now();
    int idx = 0;
    
    while (!stop_flag) {
        if (duration_sec > 0 && std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count() >= duration_sec) break;
        if (duration_sec == 0 && idx >= keys_per_thread) break;
        
        std::string key = "key_" + std::to_string(zipf.next());
        
        long long lat;
        bool hit = false;
        bool ok = http_request_persistent(conn, "GET", "/kv/" + key, "", &lat, &hit);
        m.add_result(lat, ok, hit, true);
        idx++;
    }
}

void worker_mixed(int thread_id, int keys_per_thread, int duration_sec, int total_keys, Metrics& m) {
    PersistentConnection conn;
    if (!conn.connect()) {
        std::cerr << "Thread " << thread_id << ": Failed to connect\n";
        return;
    }
    
    std::mt19937 gen(std::random_device{}());
    std::uniform_real_distribution<> dist(0.0, 1.0);
    auto start = std::chrono::steady_clock::now();
    int idx = 0;
    
    while (!stop_flag) {
        if (duration_sec > 0 && std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count() >= duration_sec) break;
        if (duration_sec == 0 && idx >= keys_per_thread) break;
        
        std::string key = "key_" + std::to_string((thread_id * keys_per_thread + (idx % keys_per_thread)) % total_keys);
        long long lat;
        
        if (dist(gen) < 0.1) {
            bool ok = http_request_persistent(conn, "PUT", "/kv/" + key, "value_" + std::to_string(idx), &lat, nullptr);
            m.add_result(lat, ok, false, false);
        } else {
            bool hit = false;
            bool ok = http_request_persistent(conn, "GET", "/kv/" + key, "", &lat, &hit);
            m.add_result(lat, ok, hit, true);
        }
        idx++;
    }
}

void run_benchmark(const std::string& workload,
                   int num_keys,
                   int num_threads,
                   int duration_sec,
                   int server_threads,
                   int cache_capacity,
                   int db_pool_size,
                   std::ofstream& csv) {
    Metrics m;
    stop_flag = false;
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> threads;
    int keys_per_thread = num_keys / num_threads;
    
    for (int t = 0; t < num_threads; ++t) {
        if (workload == "put_all") 
            threads.emplace_back(worker_put, t, keys_per_thread, duration_sec, num_keys, std::ref(m));
        else if (workload == "get_all") 
            threads.emplace_back(worker_get_all, t, keys_per_thread, duration_sec, num_keys, std::ref(m));
        else if (workload == "get_popular") 
            threads.emplace_back(worker_get_popular, t, keys_per_thread, duration_sec, num_keys, std::ref(m));
        else if (workload == "mixed") 
            threads.emplace_back(worker_mixed, t, keys_per_thread, duration_sec, num_keys, std::ref(m));
    }
    
    for (auto& th : threads) th.join();
    
    double elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start).count() / 1000000.0;
    
    long long total   = m.total_requests.load();
    long long success = m.successful_requests.load();
    long long gets    = m.get_requests.load();
    double avg_lat    = (success > 0) ? (double)m.total_latency_us.load() / success / 1000.0 : 0.0;
    double throughput = (elapsed > 0) ? (double)success / elapsed : 0.0;
    double hit_rate   = (gets > 0) ? 100.0 * m.cache_hits.load() / gets : 0.0;
    
    std::sort(m.latencies_us.begin(), m.latencies_us.end());
    
    
    std::cout << "Requests: " << success << "/" << total << " (GETs: " << gets << ")\n";
    std::cout << "Throughput: " << throughput << " ops/sec\n";
    std::cout << "Avg latency: " << avg_lat << " ms\n";
    std::cout << "Hit rate: " << hit_rate << "% (" << m.cache_hits.load() << "/" << gets << ")\n";

    std::time_t now = std::time(nullptr);
    csv << now << ","
        << num_threads << ","
        << workload << ","
        << num_keys << ","
        << duration_sec << ","
        << success << ","
        << gets << ","
        << throughput << ","
        << avg_lat << ","
        
        << hit_rate << ","
        << server_threads << ","
        << cache_capacity << ","
        << db_pool_size
        << "\n";
}

int main(int argc, char* argv[]) {
    int num_keys      = 1000;
    int num_threads   = 4;
    int duration_sec  = 0;
    std::string workload = "get_all";
    int server_threads  = 0;
    int cache_capacity  = 0;
    int db_pool_size    = 0;
    
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) break;
        std::string arg = argv[i];
        if (arg == "--keys") num_keys = std::stoi(argv[i + 1]);
        else if (arg == "--threads") num_threads = std::stoi(argv[i + 1]);
        else if (arg == "--duration") duration_sec = std::stoi(argv[i + 1]);
        else if (arg == "--workload") workload = argv[i + 1];
        else if (arg == "--server-threads") server_threads = std::stoi(argv[i + 1]);
        else if (arg == "--cache-size")     cache_capacity = std::stoi(argv[i + 1]);
        else if (arg == "--db-pool")        db_pool_size   = std::stoi(argv[i + 1]);
    }
    
    std::ofstream csv("results.csv", std::ios::app);
    
    if (csv.tellp() == 0) {
        csv << "timestamp,threads,workload,num_keys,duration,requests,get_requests,"
               "throughput,avg_latency_ms,hit_rate,"
               "server_threads,cache_capacity,db_pool_size\n";
    }
    
    run_benchmark(workload, num_keys, num_threads, duration_sec,
                  server_threads, cache_capacity, db_pool_size, csv);
    
    csv.close();
    return 0;
}