#pragma once

// ===================== 文档内容缓存（L3） =====================
//
// 缓存 loadDocument() 的结果，避免频繁的磁盘 seekg + read + XML 解析。
// 热门文档会在不同查询的结果中反复出现，缓存收益显著。
//
// 线程安全：shared_mutex 读写锁。

#include <string>
#include <unordered_map>
#include <list>
#include <shared_mutex>
#include <cstddef>

struct DocMeta {
    int id = 0;
    std::string link;
    std::string title;
    std::string content;
};

class DocCache {
public:
    explicit DocCache(size_t maxSize = 500);

    // 查找缓存，命中时填充 meta 并返回 true
    bool get(int docId, DocMeta& meta);

    // 写入缓存
    void put(int docId, const DocMeta& meta);

    // 管理
    void clear();
    void setMaxSize(size_t n);

    // 统计
    size_t size() const;
    size_t maxSize() const { return maxSize_; }
    uint64_t hits()   const { return hits_; }
    uint64_t misses() const { return misses_; }
    double hitRate()  const;

private:
    using ListIter = std::list<std::pair<int, DocMeta>>::iterator;

    void touch(ListIter it);
    void evictLocked();

    size_t maxSize_;
        
    std::list<std::pair<int, DocMeta>> lru_;           // 队首 = 最近使用
    std::unordered_map<int, ListIter> map_;

    mutable std::shared_mutex mutex_;

    mutable uint64_t hits_   = 0;
    mutable uint64_t misses_ = 0;
};
