#include "DocCache.h"
#include "Logger.h"

#include <mutex>

using namespace std;

DocCache::DocCache(size_t maxSize)
    : maxSize_(maxSize > 0 ? maxSize : 1) {}

// ===================== LRU 内部操作 =====================

void DocCache::touch(ListIter it) {
    lru_.splice(lru_.begin(), lru_, it);
}

void DocCache::evictLocked() {
    int evicted = 0;
    while (lru_.size() > maxSize_) {
        map_.erase(lru_.back().first);
        lru_.pop_back();
        ++evicted;
    }
    if (evicted > 0)
        LOG_TRACE("DocCache evicted {} entries (size {}/{})", evicted, lru_.size(), maxSize_);
}

// ===================== 公开接口 =====================

bool DocCache::get(int docId, DocMeta& meta) {
    shared_lock lock(mutex_);

    auto it = map_.find(docId);
    if (it == map_.end()) {
        ++misses_;
        LOG_TRACE("DocCache miss: docId={}", docId);
        return false;
    }

    meta = it->second->second;  // 拷贝 DocMeta

    // LRU: 提升到队首
    lock.unlock();
    {
        unique_lock writeLock(mutex_);
        auto it2 = map_.find(docId);
        if (it2 != map_.end())
            touch(it2->second);
    }

    ++hits_;
    LOG_TRACE("DocCache hit: docId={} (rate={:.1f}%)", docId, hitRate());
    return true;
}

void DocCache::put(int docId, const DocMeta& meta) {
    unique_lock lock(mutex_);

    auto it = map_.find(docId);
    if (it != map_.end()) {
        it->second->second = meta;  // 更新内容
        touch(it->second);
        LOG_TRACE("DocCache update: docId={}", docId);
        return;
    }

    if (lru_.size() >= maxSize_)
        evictLocked();

    lru_.push_front({docId, meta});
    map_[docId] = lru_.begin();
    LOG_TRACE("DocCache insert: docId={} (size {}/{})", docId, lru_.size(), maxSize_);
}

// ===================== 管理 =====================

void DocCache::clear() {
    unique_lock lock(mutex_);
    lru_.clear();
    map_.clear();
    hits_   = 0;
    misses_ = 0;
    LOG_TRACE("DocCache cleared");
}

void DocCache::setMaxSize(size_t n) {
    if (n == 0) return;
    unique_lock lock(mutex_);
    maxSize_ = n;
    evictLocked();
}

size_t DocCache::size() const {
    shared_lock lock(mutex_);
    return lru_.size();
}

double DocCache::hitRate() const {
    uint64_t total = hits_ + misses_;
    return total > 0 ? 100.0 * hits_ / total : 0.0;
}
