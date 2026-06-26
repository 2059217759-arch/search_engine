#pragma once

#include <cppjieba/Jieba.hpp>
#include "QueryCache.h"
#include "HotTracker.h"
#include "DocCache.h"

#include <string>
#include <vector>
#include <map>
#include <set>
#include <fstream>
#include <mutex>

class DenseRetriever;//前向声明

class SearchService {
public:
    SearchService();
    ~SearchService();

    bool init(const std::string& pagesFile,
              const std::string& offsetsFile,
              const std::string& indexFile);

    // 返回 JSON 格式的搜索结果
    std::string search(const std::string& query, int topK = 20);

    int getTotalDocs() const { return totalDocs_; }

    // 查询缓存统计
    void setCacheMaxSize(size_t n);
    void setCacheTtl(int seconds);
    std::string cacheStats() const;

    // 热门文档 Top-K（返回 JSON）
    std::string hotPages(int k = 10);

    // 热门文档统计
    std::string hotStats() const;

    // 记录用户点击（前端点击标题时上报）
    void recordClick(int docId);

private:
    DocMeta loadDocument(int docId);

    // 计算查询向量的 TF-IDF 权重（已归一化）
    std::map<std::string, double> computeQueryVector(
        const std::vector<std::string>& keywords);

    // 余弦相似度
    double cosineSimilarity(
        const std::map<std::string, double>& queryVec,
        const std::map<std::string, double>& docWeights);

    // 生成动态摘要：在 content 中定位最早出现的关键词，提取上下文窗口
    std::string generateAbstract(const std::string& content,
                                 const std::vector<std::string>& keywords,
                                 int maxLen = 300);

    // JSON 字符串转义
    static std::string jsonEscape(const std::string& s);

    cppjieba::Jieba tokenizer_;
    std::set<std::string> stopWords_;

    // keyword → [(docId, normalized_weight), ...] 存倒排索引
    std::map<std::string, std::vector<std::pair<int, double>>> invertedIndex_;

    // docId → (byte_offset, byte_size) 存偏移
    std::map<int, std::pair<long, long>> offsets_;

    int totalDocs_;
    std::ifstream pagesStream_;
    std::mutex pagesMutex_;

    // ---- 查询缓存（L1） ----
    QueryCache queryCache_{1000, 300};  // 1000 条, 5 min TTL

    // ---- 文档内容缓存（L3） ----
    DocCache docCache_{500};  // 500 篇文档内容

    // ---- 热门文档追踪 ----
    HotTracker hotTracker_;

    // ---- Dense retrieval ----
    DenseRetriever* dense_ = nullptr;
    bool denseAvailable_ = false;
    float alpha_ = 0.5f;                     // BM25/TF-IDF 与 dense 的融合权重
    std::string embedServiceUrl_ = "http://localhost:8765/embed";

    // 调用 Python embedding 微服务获取 query 向量
    std::vector<float> fetchQueryEmbedding(const std::string& query);
};
