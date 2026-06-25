#pragma once

#include <cppjieba/Jieba.hpp>
#include <simhash/Simhasher.hpp>
#include <string>
#include <vector>
#include <set>
#include <map>

class PageProcessor {
public:
    PageProcessor();
    void process(const std::string& dir);

private:
    void extract_documents(const std::string& dir);
    void deduplicate_documents();
    void build_pages_and_offsets(const std::string& pages,
                                 const std::string& offsets);
    void build_inverted_index(const std::string& filename);

    struct Document {
        int id;
        std::string link;
        std::string title;
        std::string content;
    };

    cppjieba::Jieba tokenizer_;
    simhash::Simhasher hasher_;
    std::set<std::string> stopWords_;
    std::vector<Document> documents_;//所有文档
    std::map<std::string, std::map<int, double>> invertedIndex_;
};
