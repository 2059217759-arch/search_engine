#pragma once

#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <cstdint>

// ===================== 热门文档追踪器 =====================
//
// 记录每个文档被用户点击的次数（前端上报），并支持查询 Top-K。
//
// 数据结构选择：
//   - unordered_map<docId, clickCount>：O(1) 增量更新
//   - min-heap：O(N log K) 提取 Top-K
//
// 线程安全：shared_mutex 读写锁。

class HotTracker {
public:
    HotTracker() = default;

    // 记录一次用户点击（前端上报）
    void recordClick(int docId);

    // 获取 Top-K 热门文档，按点击次数降序
    // pair: (docId, clickCount)
    std::vector<std::pair<int, uint64_t>> topK(int k = 10) const;

    // 统计
    size_t uniqueDocs() const;      // 被点击过的去重文档数
    uint64_t totalClicks() const;   // 总点击次数

    // 重置
    void clear();

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<int, uint64_t> counts_;  // docId → 点击次数
    uint64_t totalClicks_ = 0;
};
