#pragma once
#include <string>
#include <unordered_map>
#include <list>
#include <mutex>
#include <optional>

class LRUCache {
public:
    explicit LRUCache(size_t capacity);
    
    std::optional<std::string> get(const std::string& key);
    void put(const std::string& key, const std::string& value);
    void remove(const std::string& key);
    size_t size() const;
    
private:
    size_t max_capacity_;
    std::list<std::pair<std::string, std::string>> items_;
    std::unordered_map<std::string, std::list<std::pair<std::string, std::string>>::iterator> index_;
    mutable std::mutex mutex_;
};
