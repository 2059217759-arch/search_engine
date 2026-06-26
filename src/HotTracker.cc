#include "HotTracker.h"
#include "Logger.h"

#include <queue>
#include <algorithm>
#include <mutex>

using namespace std;

void HotTracker::recordClick(int docId) {
    unique_lock lock(mutex_);
    ++counts_[docId];
    ++totalClicks_;
    LOG_TRACE("HotTracker click: docId={} (count={})", docId, counts_[docId]);
}

vector<pair<int, uint64_t>> HotTracker::topK(int k) const {
    if (k <= 0) return {};

    shared_lock lock(mutex_);

    // ── 小根堆：O(N log K) 按点击次数提取 Top-K ──
    // pair: (clickCount, docId)，按 clickCount 升序的小根堆
    using HeapElem = pair<uint64_t, int>;
    priority_queue<HeapElem, vector<HeapElem>, greater<HeapElem>> heap;

    for (const auto& [docId, clickCount] : counts_) {
        if (clickCount == 0) continue;
        if ((int)heap.size() < k) {
            heap.emplace(clickCount, docId);
        } else if (clickCount > heap.top().first) {
            heap.pop();
            heap.emplace(clickCount, docId);
        }
    }

    // 从堆中取出，逆序排列（降序）
    vector<pair<int, uint64_t>> result;
    result.reserve(heap.size());
    while (!heap.empty()) {
        result.emplace_back(heap.top().second, heap.top().first);
        heap.pop();
    }
    reverse(result.begin(), result.end());
    return result;
}

size_t HotTracker::uniqueDocs() const {
    shared_lock lock(mutex_);
    return counts_.size();
}

uint64_t HotTracker::totalClicks() const {
    shared_lock lock(mutex_);
    return totalClicks_;
}

void HotTracker::clear() {
    unique_lock lock(mutex_);
    counts_.clear();
    totalClicks_ = 0;
}
