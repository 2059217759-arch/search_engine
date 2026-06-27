#include "KeywordRecommender.h"
#include "Logger.h"

#include <utfcpp/utf8.h>

#include <fstream>
#include <sstream>
#include <algorithm>
#include <queue>
#include <set>

using namespace std;

// ===================== 初始化 =====================
// 构建了trie树，在内存
bool KeywordRecommender::init(const string& cnDictFile, const string& enDictFile)
{
    allWords_.clear();

    auto loadDict = [this](const string& filepath) {
        ifstream ifs(filepath);
        if (!ifs) return false;
        string line;
        while (getline(ifs, line)) {
            if (line.empty()) continue;
            if (line.back() == '\r')
                line.pop_back();
            auto pos = line.find('\t');
            if (pos == string::npos) continue;//npos，没找到，在末尾
            string word = line.substr(0, pos);//提取单词
            uint64_t freq = stoull(line.substr(pos + 1));//提取频率
            insert(word, freq);//调用插入函数
            allWords_.emplace_back(move(word), freq);
        }
        return true;
    };

    if (!loadDict(cnDictFile)) {
        LOG_ERROR("KeywordRecommender: failed to load CN dict: {}", cnDictFile);
        return false;
    }
    if (!loadDict(enDictFile)) {
        LOG_ERROR("KeywordRecommender: failed to load EN dict: {}", enDictFile);
        return false;
    }

    initialized_ = true;
    LOG_INFO("KeywordRecommender initialized: {} words in trie", allWords_.size());
    return true;
}

// ===================== 二进制序列化 =====================

bool KeywordRecommender::saveBinary(const string& filepath) const
{
    ofstream ofs(filepath, ios::binary);
    if (!ofs) {
        LOG_ERROR("KeywordRecommender::saveBinary: failed to open {}", filepath);
        return false;
    }

    auto write = [&ofs](const void* data, size_t size) {
        ofs.write(reinterpret_cast<const char*>(data), size);
    };

    // Header: magic + version
    const char magic[4] = {'T', 'R', 'I', 'E'};
    uint32_t version = 1;
    write(magic, 4);
    write(&version, 4);

    // allWords_ section
    uint64_t wordCount = allWords_.size();
    write(&wordCount, 8);
    for (const auto& [word, freq] : allWords_) {
        uint32_t len = static_cast<uint32_t>(word.size());
        write(&len, 4);
        write(word.data(), len);
        write(&freq, 8);
    }

    // Trie section — preorder DFS
    function<void(const TrieNode*)> writeNode = [&](const TrieNode* node) {
        uint8_t isEnd = node->isEnd ? 1 : 0;
        write(&isEnd, 1);
        write(&node->freq, 8);
        uint32_t numChildren = static_cast<uint32_t>(node->children.size());
        write(&numChildren, 4);
        for (const auto& [key, child] : node->children) {
            uint16_t keyLen = static_cast<uint16_t>(key.size());
            write(&keyLen, 2);
            write(key.data(), keyLen);
            writeNode(child.get());
        }
    };

    writeNode(&root_);

    LOG_INFO("KeywordRecommender::saveBinary: saved {} words to {}", allWords_.size(), filepath);
    return true;
}

bool KeywordRecommender::loadBinary(const string& filepath)
{
    ifstream ifs(filepath, ios::binary);
    if (!ifs) {
        LOG_WARN("KeywordRecommender::loadBinary: file not found: {}", filepath);
        return false;
    }

    auto read = [&ifs](void* data, size_t size) {
        ifs.read(reinterpret_cast<char*>(data), size);
    };
    auto checkEof = [&ifs]() -> bool {
        return ifs.eof() || ifs.fail();
    };

    // Header: magic + version
    char magic[4];
    uint32_t version;
    read(magic, 4);
    if (checkEof() || strncmp(magic, "TRIE", 4) != 0) {
        LOG_ERROR("KeywordRecommender::loadBinary: invalid magic in {}", filepath);
        return false;
    }
    read(&version, 4);
    if (checkEof() || version != 1) {
        LOG_ERROR("KeywordRecommender::loadBinary: unsupported version {} in {}", version, filepath);
        return false;
    }

    // allWords_ section
    allWords_.clear();
    uint64_t wordCount;
    read(&wordCount, 8);
    if (checkEof()) return false;
    allWords_.reserve(wordCount);
    for (uint64_t i = 0; i < wordCount; ++i) {
        uint32_t len;
        read(&len, 4);
        if (checkEof()) return false;
        string word(len, '\0');
        read(&word[0], len);
        uint64_t freq;
        read(&freq, 8);
        if (checkEof()) return false;
        allWords_.emplace_back(move(word), freq);
    }

    // Trie section — recursive reconstruction
    root_ = TrieNode();  // reset root

    function<void(TrieNode*)> readNode = [&](TrieNode* node) {
        uint8_t isEnd;
        read(&isEnd, 1);
        read(&node->freq, 8);
        node->isEnd = (isEnd != 0);

        uint32_t numChildren;
        read(&numChildren, 4);
        for (uint32_t i = 0; i < numChildren; ++i) {
            uint16_t keyLen;
            read(&keyLen, 2);
            if (checkEof()) return;
            string key(keyLen, '\0');
            read(&key[0], keyLen);
            auto child = make_unique<TrieNode>();
            TrieNode* childPtr = child.get();
            node->children[move(key)] = move(child);
            readNode(childPtr);
        }
    };

    readNode(&root_);
    if (checkEof()) {
        LOG_ERROR("KeywordRecommender::loadBinary: unexpected EOF in {}", filepath);
        return false;
    }

    initialized_ = true;
    LOG_INFO("KeywordRecommender::loadBinary: loaded {} words from {}", allWords_.size(), filepath);
    return true;
}

// ===================== Trie 构建 =====================
// 针对一个单词，从左向右遍历，拆成一条树枝
void KeywordRecommender::insert(const string& word, uint64_t freq)
{
    TrieNode* node = &root_;
    const char* curr = word.c_str();
    const char* end = word.c_str() + word.size();

    while (curr != end) {
        auto start = curr;
        //next能自动适配中英文
        utf8::next(curr, end);
        string ch(start, curr);//提取start和curr之间的字节，即一个字母或汉字
        auto& child = node->children[ch];//children是一个map

        //子节点不存在，就新建节点，child是智能指针
        if (!child) child = make_unique<TrieNode>();
        node = child.get();//获取裸指针，这里实际就是下移
    }
    node->isEnd = true;
    node->freq = freq;
}

// ===================== 前缀匹配 =====================

vector<pair<string, uint64_t>>
KeywordRecommender::collectPrefix(const string& prefix, int maxResults)
{
    // 走到前缀对应的 Trie 节点
    TrieNode* node = &root_;
    const char* curr = prefix.c_str();
    const char* end = prefix.c_str() + prefix.size();

    while (curr != end) {//遍历字母/单词
        auto start = curr;
        utf8::next(curr, end);
        string ch(start, curr);
        auto it = node->children.find(ch);
        if (it == node->children.end())
            return {};  // 前缀不存在
        node = it->second.get();
    }

    // DFS 收集该节点下所有完整词
    vector<pair<string, uint64_t>> results;
    dfsCollect(node, prefix, results);

    // 按词频降序
    sort(results.begin(), results.end(),
         [](const auto& a, const auto& b) { return a.second > b.second; });

    if ((int)results.size() > maxResults)
        results.resize(maxResults);

    return results;
}

//dfs收集节点下所有子节点的完整词
void KeywordRecommender::dfsCollect(
    TrieNode* node, const string& basePrefix,
    vector<pair<string, uint64_t>>& results)
{
    // 基于显式栈的dfs
    // 节点
    struct Frame {
        TrieNode* node;
        string prefix;
        //迭代器，指向子节点集合
        typename map<string, unique_ptr<TrieNode>>::const_iterator it;
        bool visited;
    };
    
    vector<Frame> stack;
    stack.push_back({node, basePrefix, node->children.cbegin(), false});

    while (!stack.empty()) {
        auto& f = stack.back();//f又更新为了下一个子节点，实现深入

        if (!f.visited) {
            f.visited = true;
            if (f.node->isEnd) //如果是叶节点了，存入结果集
                results.emplace_back(f.prefix, f.node->freq);
        }
        
        //检查当前节点的迭代器是不是已经到末尾了
        if (f.it == f.node->children.cend()) {
            stack.pop_back();
            continue;
        }
        
        //取出当前迭代器指向的子节点，包括字符、智能指针
        //把这子节点压栈
        const auto& [ch, child] = *f.it;
        ++f.it;
        stack.push_back({child.get(), f.prefix + ch, child->children.cbegin(), false});
    }
}

// ===================== 编辑距离纠错 =====================

vector<pair<string, uint64_t>>
KeywordRecommender::collectCorrections(const string& query, int maxResults)
{
    auto queryChars = splitUTF8(query);
    int qLen = queryChars.size();
    int maxDist = qLen <= 2 ? 1 : (qLen <= 5 ? 2 : 3);

    // 优先队列: (编辑距离, -词频, 词)  — 编辑距离小优先，词频高优先
    using Candidate = tuple<int, int64_t, string>;
    auto cmp = [](const Candidate& a, const Candidate& b) {
        if (get<0>(a) != get<0>(b)) return get<0>(a) > get<0>(b);
        if (get<1>(a) != get<1>(b)) return get<1>(a) < get<1>(b);
        return get<2>(a) > get<2>(b);
    };
    priority_queue<Candidate, vector<Candidate>, decltype(cmp)> pq(cmp);

    for (const auto& [word, freq] : allWords_) {
        int lenDiff = abs((int)splitUTF8(word).size() - (int)queryChars.size());
        if (lenDiff > maxDist + 1) continue;

        auto wordChars = splitUTF8(word);
        int dist = editDistance(queryChars, wordChars);
        if (dist <= maxDist) {
            pq.emplace(dist, -(int64_t)freq, word);
            if ((int)pq.size() > maxResults * 3) pq.pop(); // 保留多一些候选用来去重
        }
    }

    vector<pair<string, uint64_t>> results;
    while (!pq.empty()) {
        auto [dist, negFreq, word] = pq.top();
        pq.pop();
        results.emplace_back(move(word), -negFreq);
    }
    reverse(results.begin(), results.end());

    if ((int)results.size() > maxResults)
        results.resize(maxResults);

    return results;
}

// ===================== UTF-8 工具 =====================

vector<string> KeywordRecommender::splitUTF8(const string& s)
{
    vector<string> chars;
    const char* curr = s.c_str();
    const char* end = s.c_str() + s.size();
    while (curr != end) {
        auto start = curr;
        utf8::next(curr, end);
        chars.emplace_back(start, curr);
    }
    return chars;
}

int KeywordRecommender::editDistance(
    const vector<string>& a, const vector<string>& b)
{
    int n = a.size(), m = b.size();
    // 只用两行，空间 O(min(n,m))
    if (n < m) return editDistance(b, a);

    vector<int> prev(m + 1), curr(m + 1);
    for (int j = 0; j <= m; ++j) prev[j] = j;

    for (int i = 1; i <= n; ++i) {
        curr[0] = i;
        int rowMin = curr[0];
        for (int j = 1; j <= m; ++j) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            curr[j] = min({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost});
            rowMin = min(rowMin, curr[j]);
        }
        // 如果当前行最小值已经超过合理阈值，提前终止
        // 这里用 n 作为宽松上限，实际调用时会用 maxDist 过滤
        if (rowMin > n) break;
        swap(prev, curr);
    }
    return prev[m];
}

// ===================== 对外接口 =====================

string KeywordRecommender::suggest(const string& query, int topK)
{
    if (!initialized_ || query.empty()) return "[]";

    // 1. Trie 前缀匹配
    auto prefixResults = collectPrefix(query, topK);

    // 2. 前缀结果不足 → 编辑距离兜底
    vector<pair<string, uint64_t>> corrections;
    if ((int)prefixResults.size() < topK) {
        corrections = collectCorrections(query, topK - prefixResults.size());
    }

    // 3. 构建 JSON
    ostringstream json;
    json << "[";
    int idx = 0;

    auto append = [&](const string& word, uint64_t freq, const string& type) {
        if (idx > 0) json << ",";
        json << "\n  {"
             << "\"word\":\"" << jsonEscape(word) << "\","
             << "\"freq\":" << freq << ","
             << "\"type\":\"" << type << "\""
             << "}";
        ++idx;
    };

    for (const auto& [word, freq] : prefixResults)
        append(word, freq, "prefix");

    // 纠错结果跳过已在前缀结果中出现的词
    set<string> seen;
    for (const auto& [word, _] : prefixResults)
        seen.insert(word);

    for (const auto& [word, freq] : corrections) {
        if (seen.count(word)) continue;
        append(word, freq, "correction");
    }

    if (idx > 0) json << "\n";
    json << "]";

    return json.str();
}

// ===================== JSON 转义 =====================

string KeywordRecommender::jsonEscape(const string& s)
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
