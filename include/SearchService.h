#pragma once

#include <cppjieba/Jieba.hpp>

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

private:
    struct DocMeta {
        int id;
        std::string link;
        std::string title;
        std::string content;
    };

    DocMeta loadDocument(int docId);

    // 计算查询向量的 TF-IDF 权重（已归一化）
    std::map<std::string, double> computeQueryVector(
        const std::vector<std::string>& keywords);

    // 余弦相似度
    double cosineSimilarity(
        const std::map<std::string, double>& queryVec,
        const std::map<std::string, double>& docWeights);

    // 生成静态摘要
    std::string generateAbstract(const std::string& content, int maxLen = 100);

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

    // ---- Dense retrieval ----
    DenseRetriever* dense_ = nullptr;
    bool denseAvailable_ = false;
    float alpha_ = 0.5f;                     // BM25/TF-IDF 与 dense 的融合权重
    std::string embedServiceUrl_ = "http://localhost:8765/embed";

    // 调用 Python embedding 微服务获取 query 向量
    std::vector<float> fetchQueryEmbedding(const std::string& query);
};
