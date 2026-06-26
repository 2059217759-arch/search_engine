#pragma once

#include <string>
#include <list>
#include <unordered_map>
#include <shared_mutex>
#include <chrono>

// ===================== 线程安全 LRU + TTL 查询缓存 =====================
//
// 用法：
//   QueryCache cache(1000, 300);  // 最多 1000 条，TTL 5 分钟
//   std::string result;
//   if (cache.get("比特币", result)) { /* 命中 */ }
//   cache.put("比特币", jsonStr);
//   cache.stats() → "hits: 42, misses: 10, rate: 80.8%, size: 52"
//
// 线程安全：get() 共享锁，put() 独占锁，读写不互斥。

class QueryCache {
public:
    explicit QueryCache(size_t maxSize = 1000, int ttlSeconds = 300);

    // 查找缓存，命中时填充 value 并返回 true
    // 若条目已过期则自动淘汰，返回 false
    bool get(const std::string& query, std::string& value);

    // 写入缓存（若已存在则更新值+时间戳，提升至 LRU 队首）
    void put(const std::string& query, const std::string& value);

    // —— 管理 ——
    void clear();
    void setMaxSize(size_t n);
    void setTtl(int seconds);

    // —— 统计 ——
    size_t size() const;
    size_t maxSize() const { return maxSize_; }
    int    ttlSeconds() const { return static_cast<int>(ttl_.count()); }
    std::string stats() const;   // 单行摘要

private:
    struct Entry {
        std::string key;
        std::string value;
        std::chrono::steady_clock::time_point timestamp;
    };

    using ListIter = std::list<Entry>::iterator;
    // 将 entry 移到队首（最近使用）
    void touch(ListIter it);
    // 淘汰队尾直到 size <= maxSize_（调用方需持有写锁）
    void evictLocked();

    // 规范化查询字符串作为缓存 key：去除首尾空白 + 转小写
    static std::string normalizeKey(const std::string& query);

    size_t maxSize_;
    std::chrono::seconds ttl_;

    std::list<Entry> lru_;                        // 队首 = 最近使用
    std::unordered_map<std::string, ListIter> map_;

    mutable std::shared_mutex mutex_;             // 读写锁

    // 统计
    mutable uint64_t hits_   = 0;
    mutable uint64_t misses_ = 0;
};
