#pragma once

#include <cppjieba/Jieba.hpp>
#include <string>
#include <set>
#include <unordered_map>

class KeywordProcessor {
public:
    //ch中文，en英文
    KeywordProcessor();
    void process(const std::string& chDir, const std::string& enDir);

private:
    void create_cn_dict(const std::string& dir, const std::string& outfile);
    void build_cn_index(const std::string& dict, const std::string& index);

    void create_en_dict(const std::string& dir, const std::string& outfile);
    void build_en_index(const std::string& dict, const std::string& index);

    void load_stopwords(const std::string& file, std::set<std::string>& stopwords);

    cppjieba::Jieba tokenizer_;
    std::set<std::string> en_stopwords_;
    std::set<std::string> cn_stopwords_;
};
