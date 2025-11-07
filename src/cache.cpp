#include "cache.h"

LRUCache::LRUCache(size_t capacity) : max_capacity_(capacity) {}

std::optional<std::string> LRUCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = index_.find(key);
    if (it == index_.end()) {
        return std::nullopt;
    }
    
    items_.splice(items_.begin(), items_, it->second);
    return it->second->second;
}

void LRUCache::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = index_.find(key);
    if (it != index_.end()) {
        it->second->second = value;
        items_.splice(items_.begin(), items_, it->second);
        return;
    }
    
    if (items_.size() >= max_capacity_) {
        auto last_key = items_.rbegin()->first;
        items_.pop_back();
        index_.erase(last_key);
    }

    items_.emplace_front(key, value);
    index_[key] = items_.begin();
}

void LRUCache::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = index_.find(key);
    if (it != index_.end()) {
        items_.erase(it->second);
        index_.erase(it);
    }
}

size_t LRUCache::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return index_.size();
}
