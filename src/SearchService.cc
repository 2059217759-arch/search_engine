#include "SearchService.h"
#include "DenseRetriever.h"
#include "Logger.h"

#include <tinyxml2.h>
#include <utfcpp/utf8.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>
#include <unordered_map>
#include <iostream>

using namespace std;
using namespace tinyxml2;
// ===================== 网页搜索处理逻辑 =====================


// ===================== helpers =====================

static vector<string> split(const string& s, char delim) {
    vector<string> result;
    istringstream iss(s);
    string item;
    while (getline(iss, item, delim))
        result.push_back(item);
    return result;
}

// ===================== SearchService =====================

SearchService::SearchService() : totalDocs_(0)
{
    // load stopwords
    ifstream cns("stopwords/cn_stopwords.txt");
    if (cns) {
        string line;
        while (getline(cns, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) stopWords_.insert(line);
        }
    }
    ifstream ens("stopwords/en_stopwords.txt");
    if (ens) {
        string line;
        while (getline(ens, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) stopWords_.insert(line);
        }
    }
}

SearchService::~SearchService()
{
    delete dense_;
}

bool SearchService::init(const string& pagesFile,
                         const string& offsetsFile,
                         const string& indexFile)
{
    // 加载偏移库
    ifstream ofs(offsetsFile);
    if (!ofs) {
        LOG_ERROR("Failed to open offsets file: {}", offsetsFile);
        return false;
    }
    string line;
    while (getline(ofs, line)) {
        auto parts = split(line, '\t');
        if (parts.size() < 3) continue;
        int id = stoi(parts[0]);
        long off = stol(parts[1]);
        long sz = stol(parts[2]);
        offsets_[id] = {off, sz};
    }
    //这里获取了总文档数，后面用
    totalDocs_ = offsets_.size();
    LOG_INFO("Loaded {} document offsets from {}", totalDocs_, offsetsFile);

    // 加载倒排索引
    ifstream ifs(indexFile);
    if (!ifs) {
        LOG_ERROR("Failed to open inverted index file: {}", indexFile);
        return false;
    }
    while (getline(ifs, line)) {
        auto parts = split(line, '\t');
        if (parts.size() < 2) continue;
        const string& keyword = parts[0];
        for (size_t i = 1; i + 1 < parts.size(); i += 2) {
            int docId = stoi(parts[i]);
            double weight = stod(parts[i + 1]);
            invertedIndex_[keyword].emplace_back(docId, weight);
        }
    }
    LOG_INFO("Loaded {} terms into inverted index", invertedIndex_.size());

    // 打开网页库供后续按 id 读取
    pagesStream_.open(pagesFile);

    // try to load dense retriever (non-fatal)
    dense_ = new DenseRetriever();
    if (dense_->load("data/doc_embeddings.dat")) {
        denseAvailable_ = true;
    } else {
        LOG_WARN("Dense retriever not available, will use TF-IDF only");
    }

    LOG_INFO("SearchService init complete: {} docs, {} terms, dense={}",
             totalDocs_, invertedIndex_.size(), denseAvailable_ ? "yes" : "no");
    return pagesStream_.good();//返回打开状态
}

// ===================== 核心搜索 =====================

string SearchService::search(const string& query, int topK)
{
    if (totalDocs_ == 0) return "[]";

    // 0. 查询缓存（L1）
    {
        string cached;
        if (queryCache_.get(query, cached)) {
            LOG_DEBUG("L1 cache hit for query: \"{}\"", query);
            return cached;
        }
    }

    // 1. 分词 + 过滤停用词
    vector<string> rawWords;
    tokenizer_.Cut(query, rawWords);//依旧mix方式

    vector<string> keywords;
    for (const auto& w : rawWords) {
        if (w.empty()) continue;
        if (w.find_first_not_of(" \t\n\r\f\v") == string::npos) continue;
        // ASCII 小写归一化，与离线索引保持一致
        string kw = w;
        for (char& c : kw)
            if (static_cast<unsigned char>(c) < 0x80)
                c = std::tolower(static_cast<unsigned char>(c));
        if (stopWords_.count(kw)) continue;
        keywords.push_back(kw);
    }

    if (keywords.empty()) {
        LOG_DEBUG("Empty keywords for query: \"{}\"", query);
        return "[]";
    }
    LOG_DEBUG("Query \"{}\": {} keywords after tokenization", query, keywords.size());

    // 2. 计算查询 TF-IDF 基准向量
    // queryVec是map<string,double> 关键字-权重
    auto queryVec = computeQueryVector(keywords);
    // queryVec 中的 key 已经是去重后的有效关键词

    // 3. 求交集：包含所有查询关键词的文档
    vector<int> candidateDocs;
    {
        // 取第一个有倒排列表的关键词作为初始候选集
        unordered_set<int> intersectSet; //int是id

        bool first = true;
        //遍历quary的每个关键字，忽略权重
        for (const auto& [term, _] : queryVec) {
            auto it = invertedIndex_.find(term);
            if (it == invertedIndex_.end()) {
                // 有查询词不在倒排索引中 → 无结果
                // 但不直接返回空，因为 dense 检索可以兜底
                continue;
            }

            //first标记处理第一个关键字，把他的倒排列表全部加入容器
            if (first) {
                for (const auto& [docId, _] : it->second)
                    intersectSet.insert(docId);
                first = false;
            }
            //后续的关键字，先遍历该关键字的倒排索引，遍历里面的每个文档id
            //查是否在容器里，求了交集
            else {
                unordered_set<int> tmp;
                for (const auto& [docId, _] : it->second) {
                    if (intersectSet.count(docId))
                        tmp.insert(docId);
                }
                intersectSet = move(tmp);//更新
            }
            if (intersectSet.empty()) break;
        }
        //放到交集里
        candidateDocs.assign(intersectSet.begin(), intersectSet.end());
    }

    // 4. 余弦相似度排序
    vector<pair<int, double>> scored;  // 文档的打分(docId, cosine)
    unordered_map<int, double> tfidfScores; // 存下来供融合用
    for (int docId : candidateDocs) {
        //docWeight是当前的文档的关键字向量
        //如<苹果，0.999> <公司，0.88>
        map<string, double> docWeights;
        for (const auto& [term, _] : queryVec) {
            const auto& posting = invertedIndex_[term];
            for (const auto& [id, w] : posting) {
                if (id == docId) {
                    docWeights[term] = w;
                    break;
                }
            }
        }
        double cos = cosineSimilarity(queryVec, docWeights);//计算
        scored.emplace_back(docId, cos);
        tfidfScores[docId] = cos;
    }

    // 4.5 Dense retrieval
    unordered_map<int, float> denseScores;
    if (denseAvailable_) {

        ////这个函数发送了请求给python微服务，返回得到向量化的查询语句
        auto qEmb = fetchQueryEmbedding(query);
        if (!qEmb.empty()) {

            // 拿着这个向量去匹配本地的向量索引
            auto denseResults = dense_->search(qEmb, topK * 3);
            LOG_DEBUG("Dense retrieval returned {} candidates for query \"{}\"",
                      denseResults.size(), query);
            for (const auto& [docId, score] : denseResults)
                denseScores[docId] = score;
        } else {
            LOG_DEBUG("Dense embedding unavailable for query \"{}\", fell back to sparse only", query);
        }
    }

    //  -----------混合检索----------
    // 4.6 分数融合: TF-IDF cosine + Dense inner product
    if (denseAvailable_ && !denseScores.empty()) {
        // 收集所有候选（TF-IDF 交集 + Dense top-K）
        unordered_set<int> allCandidates;
        for (const auto& [docId, _] : tfidfScores) allCandidates.insert(docId);
        for (const auto& [docId, _] : denseScores)  allCandidates.insert(docId);

        // 找到 TF-IDF 最大分用于归一化（虽然 cosine 本身在 [0,1]，但作为参考）
        double maxTfidf = 0.0;
        for (const auto& [_, s] : tfidfScores) maxTfidf = max(maxTfidf, s);

        //融合过程
        vector<pair<int, double>> fused;
        for (int docId : allCandidates) {

            //在TF-IDF稀疏集合里，赋值
            double tfidfScore = 0.0;
            auto itTf = tfidfScores.find(docId);
            if (itTf != tfidfScores.end())
                tfidfScore = itTf->second;  // cosine 已在 [0,1]

            double denseScore = 0.0;
            auto itDense = denseScores.find(docId);
            if (itDense != denseScores.end())
                // [-1,1] → [0,1]，统一分数为0-1之间
                denseScore = (itDense->second + 1.0) / 2.0;  

            double finalScore = alpha_ * tfidfScore + (1.0 - alpha_) * denseScore;
            fused.emplace_back(docId, finalScore);
        }

        sort(fused.begin(), fused.end(),
             [](const auto& a, const auto& b) { return a.second > b.second; });

        scored = std::move(fused);
    }
    // 否则 scored 保持纯 TF-IDF 排序

    sort(scored.begin(), scored.end(),
         [](const auto& a, const auto& b) { return a.second > b.second; });

    if ((int)scored.size() > topK)
        scored.resize(topK);

    LOG_DEBUG("Search \"{}\": {} sparse + {} dense -> {} final (mode: {})",
              query, candidateDocs.size(), denseScores.size(), scored.size(),
              (denseAvailable_ && !denseScores.empty()) ? "hybrid" : "sparse");

    // 5. 构建 JSON 结果
    ostringstream json;
    json << "[";
    for (size_t i = 0; i < scored.size(); ++i) {
        int docId = scored[i].first;
        double finalScore = scored[i].second;
        auto doc = loadDocument(docId);
        string abstract = generateAbstract(doc.content, keywords, 300);

        // 查找稀疏检索得分 (TF-IDF cosine, 0~1)
        double sparseScore = 0.0;
        auto itTf = tfidfScores.find(docId);
        if (itTf != tfidfScores.end())
            sparseScore = itTf->second;

        // 查找稠密检索得分 (归一化到 0~1)
        double denseScore = 0.0;
        auto itDense = denseScores.find(docId);
        if (itDense != denseScores.end())
            denseScore = (itDense->second + 1.0) / 2.0;

        if (i > 0) json << ",";
        json << "\n  {"
             << "\"id\":" << doc.id << ","
             << "\"title\":\"" << jsonEscape(doc.title) << "\","
             << "\"link\":\"" << jsonEscape(doc.link) << "\","
             << "\"abstract\":\"" << jsonEscape(abstract) << "\","
             << "\"sparseScore\":" << sparseScore << ","
             << "\"denseScore\":" << denseScore << ","
             << "\"finalScore\":" << finalScore
             << "}";
    }
    if (!scored.empty()) json << "\n";
    json << "]";

    string result = json.str();

    // 存入 L1 缓存
    queryCache_.put(query, result);

    return result;
}

// ===================== 查询向量 =====================

map<string, double> SearchService::computeQueryVector(
    const vector<string>& keywords)
{
    // TF: 词频
    unordered_map<string, int> tf;
    for (const auto& w : keywords)
        ++tf[w];

    int totalTerms = keywords.size();

    // TF-IDF
    unordered_map<string, double> weights;
    double sumSq = 0.0;
    for (const auto& [word, count] : tf) {
        auto it = invertedIndex_.find(word);
        int df = (it != invertedIndex_.end()) ? (int)it->second.size() : 0;
        double tfVal = (double)count / totalTerms;
        double idf = log2((double)totalDocs_ / (df + 1));
        double w = tfVal * idf;
        weights[word] = w;
        sumSq += w * w;
    }

    // 归一化
    double norm = sqrt(sumSq);
    map<string, double> result;
    if (norm > 0) {
        for (const auto& [word, w] : weights)
            result[word] = w / norm;
    }
    return result;
}

// ===================== 余弦相似度 =====================

double SearchService::cosineSimilarity(
    const map<string, double>& queryVec,
    const map<string, double>& docWeights)
{
    // 两个向量在构建时都已 L2 归一化（||q||=1, ||d||=1），
    // 因此余弦相似度 = 点积，无需再算范数。
    double dot = 0.0;
    for (const auto& [term, qw] : queryVec) {
        auto it = docWeights.find(term);
        if (it != docWeights.end())
            dot += qw * it->second;
    }
    return dot;
}

// ===================== Dense: libcurl query embedding =====================
// 发送http请求给python微服务
static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb,
                                 string* out) {
    size_t total = size * nmemb;
    out->append(static_cast<char*>(contents), total);
    return total;
}

vector<float> SearchService::fetchQueryEmbedding(const string& query) {
    CURL* curl = curl_easy_init();
    if (!curl) return {};

    nlohmann::json reqBody = {{"query", query}};
    string reqStr = reqBody.dump();
    string response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, embedServiceUrl_.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, reqStr.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)reqStr.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        LOG_WARN("Embedding service request failed: {}", curl_easy_strerror(res));
        return {};
    }

    try {
        auto j = nlohmann::json::parse(response);
        auto emb = j.at("embedding").get<vector<float>>();
        LOG_TRACE("Embedding received: {} dims", emb.size());
        return emb;
    } catch (...) {
        LOG_WARN("Failed to parse embedding response for query \"{}\"", query);
        return {};
    }
}

// ===================== 加载文档 =====================

DocMeta SearchService::loadDocument(int docId)
{
    // L3: 文档内容缓存
    DocMeta meta;
    if (docCache_.get(docId, meta))
        return meta;

    meta.id = docId;

    auto offIt = offsets_.find(docId);
    if (offIt == offsets_.end()) return meta;

    long offset = offIt->second.first;
    long size = offIt->second.second;

    string xml;
    {
        lock_guard<mutex> lock(pagesMutex_);
        xml.resize(size);
        pagesStream_.seekg(offset);
        pagesStream_.read(&xml[0], size);
    }

    XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != XML_SUCCESS)
        return meta;

    auto* root = doc.FirstChildElement("doc");
    if (!root) return meta;

    auto* idEl = root->FirstChildElement("id");
    if (idEl && idEl->GetText()) meta.id = stoi(idEl->GetText());

    auto* linkEl = root->FirstChildElement("link");
    if (linkEl && linkEl->GetText()) meta.link = linkEl->GetText();

    auto* titleEl = root->FirstChildElement("title");
    if (titleEl && titleEl->GetText()) meta.title = titleEl->GetText();

    auto* contentEl = root->FirstChildElement("content");
    if (contentEl && contentEl->GetText()) meta.content = contentEl->GetText();

    // 存入文档缓存（只存有效文档）
    if (!meta.link.empty() || !meta.title.empty())
        docCache_.put(docId, meta);

    return meta;
}

// ===================== 摘要 =====================

// UTF-8 安全：判断当前字节是否为多字节字符的后续字节
static inline bool isUtf8Continuation(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

// 在 UTF-8 文本中执行大小写不敏感搜索
// ASCII 字母忽略大小写；非 ASCII 字节（UTF-8 多字节部分）须精确匹配
static size_t findKeyword(const string& text, const string& keyword) {
    if (keyword.empty()) return string::npos;

    auto it = search(text.begin(), text.end(), keyword.begin(), keyword.end(),
        [](char a, char b) {
            unsigned char ua = static_cast<unsigned char>(a);
            unsigned char ub = static_cast<unsigned char>(b);
            // ASCII 范围：忽略大小写
            if (ua < 0x80 && ub < 0x80)
                return tolower(ua) == tolower(ub);
            // 非 ASCII（UTF-8 多字节）：精确匹配
            return a == b;
        });
    if (it == text.end()) return string::npos;
    return distance(text.begin(), it);
}

string SearchService::generateAbstract(const string& content,
                                        const vector<string>& keywords,
                                        int maxLen)
{
    if ((int)content.size() <= maxLen)
        return content;

    // ── 1. 找到所有关键词中最早出现的位置 ──
    size_t firstMatch = string::npos;
    for (const auto& kw : keywords) {
        size_t pos = findKeyword(content, kw);
        if (pos != string::npos && pos < firstMatch)
            firstMatch = pos;
    }

    // ── 2. 无匹配 → 回退静态摘要（取文档开头） ──
    if (firstMatch == string::npos) {
        int cut = maxLen;
        while (cut > 0 && isUtf8Continuation((unsigned char)content[cut]))
            --cut;
        if (cut == 0) return "";

        string s = content.substr(0, cut);
        static const string delims[] = {"。", "，", "！", "？", ",", ".", "!", "?", " "};
        size_t best = string::npos;
        for (const auto& d : delims) {
            auto pos = s.rfind(d);
            if (pos != string::npos && (best == string::npos || pos > best))
                best = pos;
        }
        if (best != string::npos && (int)best > cut * 2 / 3)
            s = s.substr(0, best);
        return s + "...";
    }

    // ── 3. 以关键词为中心提取上下文窗口 ──
    // 窗口起点：关键词前移 1/3 窗口，让关键词大约在靠前 1/3 处
    size_t prefix = maxLen / 3;
    size_t windowStart = (firstMatch > prefix) ? firstMatch - prefix : 0;

    // 对齐 UTF-8 字符边界（不能切在多字节字符中间）
    while (windowStart > 0 && isUtf8Continuation((unsigned char)content[windowStart]))
        --windowStart;

    size_t windowEnd = windowStart + maxLen;
    if (windowEnd < content.size()) {
        // 延伸至完整的 UTF-8 字符末尾
        while (windowEnd < content.size() && isUtf8Continuation((unsigned char)content[windowEnd]))
            ++windowEnd;
    } else {
        windowEnd = content.size();
    }

    string snippet = content.substr(windowStart, windowEnd - windowStart);

    // ── 4. 微调：尝试在句子边界截断，使摘要更自然 ──
    static const string sentDelims[] = {"。", "！", "？", "\n", ". ", "! ", "? "};
    static const int sentDelimLen[] = {3, 3, 3, 1, 2, 2, 2};  // UTF-8 字节长度
    // 去掉开头可能的不完整句子：找到第一个句子分隔符，从其后开始
    if (windowStart > 0) {
        size_t firstDelim = string::npos;
        size_t delimBytes = 0;
        for (int di = 0; di < 7; ++di) {
            auto pos = snippet.find(sentDelims[di]);
            if (pos != string::npos && pos < firstDelim) {
                firstDelim = pos;
                delimBytes = sentDelimLen[di];
            }
        }
        // 只在分隔符不太远时裁剪（< 窗口的 1/4），避免截掉太多
        if (firstDelim != string::npos && firstDelim < snippet.size() / 4) {
            snippet = snippet.substr(firstDelim + delimBytes);
        }
    }
    // 去掉末尾的不完整句子
    if (windowEnd < content.size()) {
        size_t lastDelim = string::npos;
        for (int di = 0; di < 7; ++di) {
            auto pos = snippet.rfind(sentDelims[di]);
            if (pos != string::npos && (lastDelim == string::npos || pos > lastDelim))
                lastDelim = pos;
        }
        if (lastDelim != string::npos && lastDelim > snippet.size() * 2 / 3) {
            snippet = snippet.substr(0, lastDelim + 1);
        }
    }

    if (snippet.empty()) return "";

    // ── 5. 添加省略号 ──
    // 重新确定实际窗口在原文中的位置
    if (windowStart > 0) snippet = "..." + snippet;
    if (windowEnd < content.size()) snippet = snippet + "...";

    return snippet;
}

// ===================== JSON 转义 =====================

string SearchService::jsonEscape(const string& s)
{
    string result;
    result.reserve(s.size());
    for (char ch : s) {
        switch (ch) {
        case '"':  result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n";  break;
        case '\r': result += "\\r";  break;
        case '\t': result += "\\t";  break;
        default:   result += ch;
        }
    }
    return result;
}

// ===================== 缓存管理 =====================

void SearchService::setCacheMaxSize(size_t n) {
    queryCache_.setMaxSize(n);
}

void SearchService::setCacheTtl(int seconds) {
    queryCache_.setTtl(seconds);
}

string SearchService::cacheStats() const {
    return queryCache_.stats();
}

// ===================== 热门文档 =====================

string SearchService::hotPages(int k) {
    auto top = hotTracker_.topK(k);
    if (top.empty()) return "[]";

    ostringstream json;
    json << "[";
    for (size_t i = 0; i < top.size(); ++i) {
        int docId = top[i].first;
        uint64_t clickCount = top[i].second;
        auto doc = loadDocument(docId);

        if (i > 0) json << ",";
        json << "\n  {"
             << "\"rank\":" << (i + 1) << ","
             << "\"id\":" << docId << ","
             << "\"title\":\"" << jsonEscape(doc.title) << "\","
             << "\"link\":\"" << jsonEscape(doc.link) << "\","
             << "\"clickCount\":" << clickCount
             << "}";
    }
    if (!top.empty()) json << "\n";
    json << "]";
    return json.str();
}

string SearchService::hotStats() const {
    ostringstream oss;
    oss << "{\"uniqueDocs\":" << hotTracker_.uniqueDocs()
        << ",\"totalClicks\":" << hotTracker_.totalClicks()
        << "}";
    return oss.str();
}

void SearchService::recordClick(int docId) {
    hotTracker_.recordClick(docId);
}
