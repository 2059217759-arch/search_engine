#include "SearchService.h"

#include <tinyxml2.h>
#include <utfcpp/utf8.h>

#include <sstream>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <unordered_map>

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

bool SearchService::init(const string& pagesFile,
                         const string& offsetsFile,
                         const string& indexFile)
{
    // 加载偏移库
    ifstream ofs(offsetsFile);
    if (!ofs) return false;
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

    // 加载倒排索引
    ifstream ifs(indexFile);
    if (!ifs) return false;
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

    // 打开网页库供后续按 id 读取
    pagesStream_.open(pagesFile);
    return pagesStream_.good();//返回打开状态
}

// ===================== 核心搜索 =====================

string SearchService::search(const string& query, int topK)
{
    if (totalDocs_ == 0) return "[]";

    // 1. 分词 + 过滤停用词
    vector<string> rawWords;
    tokenizer_.Cut(query, rawWords);//依旧mix方式

    vector<string> keywords;
    for (const auto& w : rawWords) {
        if (w.empty()) continue;
        if (w.find_first_not_of(" \t\n\r\f\v") == string::npos) continue;
        if (stopWords_.count(w)) continue; //w在停用词的集合里
        keywords.push_back(w);
    }

    if (keywords.empty()) return "[]";

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
                return "[]";
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
            if (intersectSet.empty()) return "[]";
        }
        //放到交集里
        candidateDocs.assign(intersectSet.begin(), intersectSet.end());
    }

    // 4. 余弦相似度排序
    vector<pair<int, double>> scored;  // 文档的打分(docId, cosine)
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
    }

    sort(scored.begin(), scored.end(),
         [](const auto& a, const auto& b) { return a.second > b.second; });

    if ((int)scored.size() > topK)
        scored.resize(topK);

    // 5. 构建 JSON 结果
    ostringstream json;
    json << "[";
    for (size_t i = 0; i < scored.size(); ++i) {
        int docId = scored[i].first;
        auto doc = loadDocument(docId);
        string abstract = generateAbstract(doc.content, 300);

        if (i > 0) json << ",";
        json << "\n  {"
             << "\"id\":" << doc.id << ","
             << "\"title\":\"" << jsonEscape(doc.title) << "\","
             << "\"link\":\"" << jsonEscape(doc.link) << "\","
             << "\"abstract\":\"" << jsonEscape(abstract) << "\""
             << "}";
    }
    if (!scored.empty()) json << "\n";
    json << "]";

    return json.str();
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
    double dot = 0.0, normQ = 0.0, normD = 0.0;

    for (const auto& [term, qw] : queryVec) {
        normQ += qw * qw;
        auto it = docWeights.find(term);
        if (it != docWeights.end()) {
            double dw = it->second;
            dot += qw * dw;
            normD += dw * dw;
        }
        // 文档中该词权重为 0 → 不影响 dot 但影响 normQ
    }

    double denom = sqrt(normQ) * sqrt(normD);
    return (denom > 0) ? dot / denom : 0.0;
}

// ===================== 加载文档 =====================

SearchService::DocMeta SearchService::loadDocument(int docId)
{
    DocMeta meta;
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

    return meta;
}

// ===================== 摘要 =====================

string SearchService::generateAbstract(const string& content, int maxLen)
{
    if ((int)content.size() <= maxLen)
        return content;

    // 取前 maxLen 字节，回退到合法的 UTF-8 字符边界
    // UTF-8 编码中，英文字符占 1 个字节，而中文等字符通常占 3 个字节
    // 多字节字符的后续字节（如中文的第二、三个字节）：必须且只能以 10xxxxxx 开头
    // while里面的意思是如果是中文词的后续字节，回退
    int cut = maxLen;
    while (cut > 0 && ((unsigned char)content[cut] & 0xC0) == 0x80)
        --cut;

    if (cut == 0)
        return "";

    string s = content.substr(0, cut);

    // 只在 ASCII 标点及完整 CJK 标点处截断（避免匹配多字节字符的内部字节）
    static const string delims[] = {"。", "，", "！", "？", ",", ".", "!", "?", " "};
    size_t best = string::npos;
    // 遍历所有标点，找到最末尾的标点best
    for (const auto& d : delims) {
        auto pos = s.rfind(d);
        if (pos != string::npos && (best == string::npos || pos > best))
            best = pos;
    }
    // 阈值处理，如果截断太多了也不好，回退到前面的cut
    if (best != string::npos && (int)best > cut * 2 / 3)
        s = s.substr(0, best);

    return s + "...";
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
