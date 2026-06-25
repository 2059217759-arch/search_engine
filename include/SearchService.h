#pragma once

#include <cppjieba/Jieba.hpp>

#include <string>
#include <vector>
#include <map>
#include <set>
#include <fstream>
#include <mutex>

class SearchService {
public:
    SearchService();
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

    //加载文档，返回上面的结构体
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
};
