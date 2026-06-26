#include "QueryCache.h"
#include "Logger.h"

#include <algorithm>
#include <sstream>
#include <cctype>
#include <mutex>

using namespace std;

QueryCache::QueryCache(size_t maxSize, int ttlSeconds)
    : maxSize_(maxSize > 0 ? maxSize : 1),
      ttl_(ttlSeconds > 0 ? ttlSeconds : 0) {}

// ===================== 查询规范化 =====================

string QueryCache::normalizeKey(const string& query) {
    // 去除首尾空白
    size_t start = 0;
    while (start < query.size() && isspace(static_cast<unsigned char>(query[start])))
        ++start;
    size_t end = query.size();
    while (end > start && isspace(static_cast<unsigned char>(query[end - 1])))
        --end;

    string key(query, start, end - start);
    // 转小写
    transform(key.begin(), key.end(), key.begin(),
              [](unsigned char c) { return tolower(c); });
    return key;
}

// ===================== LRU 操作 =====================

void QueryCache::touch(ListIter it) {
    lru_.splice(lru_.begin(), lru_, it);
}

void QueryCache::evictLocked() {
    int evicted = 0;
    while (lru_.size() > maxSize_) {
        map_.erase(lru_.back().key);
        lru_.pop_back();
        ++evicted;
    }
    if (evicted > 0)
        LOG_TRACE("QueryCache evicted {} entries (size {}/{})", evicted, lru_.size(), maxSize_);
}

// ===================== 公开接口 =====================

bool QueryCache::get(const string& query, string& value) {
    string key = normalizeKey(query);
    if (key.empty()) return false;

    shared_lock lock(mutex_);

    auto it = map_.find(key);
    if (it == map_.end()) {
        ++misses_;
        LOG_DEBUG("QueryCache miss: \"{}\"", key);
        return false;
    }

    // 检查 TTL
    if (ttl_.count() > 0) {
        auto age = chrono::steady_clock::now() - it->second->timestamp;
        if (age > ttl_) {
            ++misses_;
            LOG_TRACE("QueryCache TTL expired: \"{}\" (age={}s)", key,
                      chrono::duration_cast<chrono::seconds>(age).count());
            return false;
        }
    }

    value = it->second->value;

    // LRU: 提升到队首
    lock.unlock();
    {
        unique_lock writeLock(mutex_);
        auto it2 = map_.find(key);
        if (it2 != map_.end())
            touch(it2->second);
    }

    ++hits_;
    LOG_DEBUG("QueryCache hit: \"{}\"", key);
    return true;
}

void QueryCache::put(const string& query, const string& value) {
    string key = normalizeKey(query);
    if (key.empty()) return;

    unique_lock lock(mutex_);

    auto it = map_.find(key);
    if (it != map_.end()) {
        // 更新已有条目
        it->second->value     = value;
        it->second->timestamp = chrono::steady_clock::now();
        touch(it->second);
        LOG_TRACE("QueryCache update: \"{}\"", key);
        return;
    }

    // 新条目
    if (lru_.size() >= maxSize_)
        evictLocked();

    Entry entry;
    entry.key       = key;
    entry.value     = value;
    entry.timestamp = chrono::steady_clock::now();

    lru_.push_front(move(entry));
    map_[key] = lru_.begin();
    LOG_TRACE("QueryCache insert: \"{}\" (size {}/{})", key, lru_.size(), maxSize_);
}

// ===================== 管理 =====================

void QueryCache::clear() {
    unique_lock lock(mutex_);
    lru_.clear();
    map_.clear();
    hits_   = 0;
    misses_ = 0;
    LOG_TRACE("QueryCache cleared");
}

void QueryCache::setMaxSize(size_t n) {
    if (n == 0) return;
    unique_lock lock(mutex_);
    maxSize_ = n;
    evictLocked();
}

void QueryCache::setTtl(int seconds) {
    unique_lock lock(mutex_);
    ttl_ = chrono::seconds(seconds > 0 ? seconds : 0);
}

size_t QueryCache::size() const {
    shared_lock lock(mutex_);
    return lru_.size();
}

string QueryCache::stats() const {
    shared_lock lock(mutex_);
    uint64_t total = hits_ + misses_;
    double rate = total > 0 ? 100.0 * hits_ / total : 0.0;

    ostringstream oss;
    oss << "hits: " << hits_
        << ", misses: " << misses_
        << ", rate: " << rate << "%"
        << ", size: " << lru_.size()
        << "/" << maxSize_;
    return oss.str();
}
