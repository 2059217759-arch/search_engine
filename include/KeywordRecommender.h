#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>

class KeywordRecommender {
public:
    KeywordRecommender() = default;

    bool init(const std::string& cnDictFile, const std::string& enDictFile);

    // 返回 JSON 格式的推荐结果
    // type: "prefix" 表示前缀匹配, "correction" 表示纠错建议
    std::string suggest(const std::string& query, int topK = 5);

private:
    struct TrieNode {
        //这里用到了智能指针管理子节点
        std::map<std::string, std::unique_ptr<TrieNode>> children;
        bool isEnd = false;
        uint64_t freq = 0;
    };

    // 向 Trie 中插入一个词及其词频
    void insert(const std::string& word, uint64_t freq);

    // 在 Trie 中沿着 prefixwan 走到对应节点，收集子树下所有完整词
    // 返回按词频降序排序的结果
    std::vector<std::pair<std::string, uint64_t>>
    collectPrefix(const std::string& prefix, int maxResults);

    // 编辑距离纠错：扫描全词典，返回与 query 编辑距离最小的词
    std::vector<std::pair<std::string, uint64_t>>
    collectCorrections(const std::string& query, int maxResults);

    // UTF-8 字符串按字符拆分
    static std::vector<std::string> splitUTF8(const std::string& s);

    // 字符级编辑距离（Levenshtein）
    static int editDistance(const std::vector<std::string>& a,
                            const std::vector<std::string>& b);

    // DFS 遍历 Trie 子树，收集所有完整词
    void dfsCollect(TrieNode* node, const std::string& basePrefix,
                    std::vector<std::pair<std::string, uint64_t>>& results);

    // JSON 字符串转义
    static std::string jsonEscape(const std::string& s);

    TrieNode root_;
    std::vector<std::pair<std::string, uint64_t>> allWords_;
    bool initialized_ = false;
};
