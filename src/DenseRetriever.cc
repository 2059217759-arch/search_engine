#include "DenseRetriever.h"
#include "Logger.h"

#include <fstream>
#include <queue>
#include <algorithm>
#include <cstring>

using namespace std;

static constexpr uint32_t EMB_MAGIC = 0x44454D42;  // "DEMB"

bool DenseRetriever::load(const string& path)
{
    ifstream ifs(path, ios::binary);
    if (!ifs) {
        LOG_WARN("DenseRetriever: embedding file not found ({}), dense retrieval disabled", path);
        return false;
    }

    //取前 4 个字节作为 magic 数。这是一个常见的文件格式校验机制。如果读取到的魔数
    //与预期的 EMB_MAGIC 不匹配，说明这不是一个合法的向量索引文件，直接返回 false
    uint32_t magic;
    int32_t numDocs;//接着读取 4 个字节，表示文件中包含多少个文档向量
    ifs.read(reinterpret_cast<char*>(&magic), 4);
    if (magic != EMB_MAGIC) {
        LOG_WARN("DenseRetriever: invalid magic number in {}, dense retrieval disabled", path);
        return false;
    }
    ifs.read(reinterpret_cast<char*>(&numDocs), 4);

    //获取每个向量维度，就是每个文档维度
    ifs.read(reinterpret_cast<char*>(&dim_), 4);

    docIds_.clear();
    docIds_.reserve(numDocs);//提前分配内存空间
    embeddings_.clear();
    embeddings_.reserve(numDocs);

    for (int i = 0; i < numDocs; ++i) {
        int32_t docId;
        ifs.read(reinterpret_cast<char*>(&docId), 4);
        docIds_.push_back(docId);

        vector<float> vec(dim_);
        ifs.read(reinterpret_cast<char*>(vec.data()), dim_ * sizeof(float));
        embeddings_.push_back(move(vec));//移动语义避免深拷贝
    }

    if (ifs.good()) {
        LOG_INFO("DenseRetriever: loaded {} doc embeddings ({} dims) from {}",
                 numDocs, dim_, path);
        return true;
    }
    LOG_ERROR("DenseRetriever: read error while loading {}", path);
    return false;
}

vector<pair<int, float>> DenseRetriever::search(
    const vector<float>& queryVec, int topK) const
{
    using ScoredDoc = pair<float, int>;  // (score, docId) — min-heap by score

    //用堆排序
    priority_queue<ScoredDoc, vector<ScoredDoc>, greater<ScoredDoc>> heap;

    for (size_t i = 0; i < embeddings_.size(); ++i) {
        float dot = 0.0f;
        const float* emb = embeddings_[i].data();
        for (int d = 0; d < dim_; ++d)
            dot += queryVec[d] * emb[d];

        if ((int)heap.size() < topK) {
            heap.emplace(dot, docIds_[i]);
        } else if (dot > heap.top().first) {
            heap.pop();
            heap.emplace(dot, docIds_[i]);
        }
    }

    vector<pair<int, float>> results;
    results.reserve(heap.size());
    while (!heap.empty()) {
        results.emplace_back(heap.top().second, heap.top().first);
        heap.pop();
    }
    reverse(results.begin(), results.end());//反转，因为是小根堆
    return results;
}
