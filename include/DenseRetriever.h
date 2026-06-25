#pragma once

#include <vector>
#include <string>
#include <utility>
#include <cstdint>

class DenseRetriever {
public:
    DenseRetriever() = default;

    //load("data/doc_embeddings.dat")
    bool load(const std::string& path);

    // 计算查询向量（Query Vector）与内存中所有文档向量之间的相似度（点积），
    // 并返回相似度最高的 Top-K 个文档。
    std::vector<std::pair<int, float>> search(
        const std::vector<float>& queryVec, int topK = 100) const;

    bool isLoaded() const { return !embeddings_.empty(); }
    int dim() const { return dim_; }

private:
    int dim_ = 0;
    std::vector<int> docIds_;
    std::vector<std::vector<float>> embeddings_;//文档向量
};
