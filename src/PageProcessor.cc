#include "PageProcessor.h"
#include "DirectoryScanner.h"

#include <tinyxml2.h>
#include <utfcpp/utf8.h>

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <set>
#include <bitset>

using namespace std;
using namespace tinyxml2;

// ===================== HTML helpers =====================
//作用是从输入字符串 text 中去掉 HTML 标签，只保留标签外的文字内容
static string strip_html_tags(const string& text) {
    string result;
    result.reserve(text.size());
    bool in_tag = false;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '<')
            in_tag = true;
        else if (text[i] == '>')
            in_tag = false;
        else if (!in_tag)
            result += text[i];
    }
    return result;
}

//HTML 实体转义字符还原成普通字符
static string decode_html_entities(const string& text) {
    string result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '&') {
            size_t semi = text.find(';', i);
            if (semi == string::npos) { result += text[i]; continue; }
            string entity = text.substr(i, semi - i + 1);
            if (entity == "&nbsp;")      { result += ' '; i = semi; }
            else if (entity == "&lt;")   { result += '<'; i = semi; }
            else if (entity == "&gt;")   { result += '>'; i = semi; }
            else if (entity == "&amp;")  { result += '&'; i = semi; }
            else if (entity == "&quot;") { result += '"'; i = semi; }
            else if (entity == "&apos;") { result += '\''; i = semi; }
            else if (entity == "&#8211;"){ result += '-'; i = semi; }
            else if (entity == "&#8212;"){ result += '-'; i = semi; }
            else if (entity == "&#8230;"){ result += '.'; i = semi; }
            else if (!entity.empty() && entity[1] == '#') {
                // numeric entity like &#21313; — skip for tokenizer to ignore
                i = semi;
            }
            else { result += text[i]; }
        } else {
            result += text[i];
        }
    }
    return result;
}

static string clean_html(const string& raw) {
    return strip_html_tags(decode_html_entities(raw));
}

// ===================== PageProcessor =====================

PageProcessor::PageProcessor()
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

void PageProcessor::process(const string& dir)
{
    extract_documents(dir);
    deduplicate_documents();
    build_pages_and_offsets("data/pages.dat", "data/offsets.dat");
    build_inverted_index("data/inverted_index.dat");
}

// ===================== Extract =====================

void PageProcessor::extract_documents(const string& dir)
{
    auto files = DirectoryScanner::scan(dir);
    int next_id = 1;

    for (const auto& file : files) {
        XMLDocument doc;
        if (doc.LoadFile(file.c_str()) != XML_SUCCESS) continue;

        //在文档中找第一个名字叫rss的节点
        XMLElement* channel = doc.FirstChildElement("rss");
        if (!channel) continue;
        //继续向下找channel节点
        channel = channel->FirstChildElement("channel");
        if (!channel) continue;

        //开始逐个遍历item
        for (XMLElement* item = channel->FirstChildElement("item");
             item; item = item->NextSiblingElement("item")) {

            Document d;
            d.id = next_id++;

            // title
            auto* title_el = item->FirstChildElement("title");
            if (title_el && title_el->GetText())
                d.title = title_el->GetText();

            // link
            auto* link_el = item->FirstChildElement("link");
            if (link_el && link_el->GetText())
                d.link = link_el->GetText();

            // content: prefer <content>, then <content:encoded>, then <description>
            // 降级处理
            string raw_content;
            auto* content_el = item->FirstChildElement("content");
            if (content_el && content_el->GetText())
                raw_content = content_el->GetText();

            if (raw_content.empty()) {
                auto* encoded_el = item->FirstChildElement("content:encoded");
                if (encoded_el && encoded_el->GetText())
                    raw_content = encoded_el->GetText();
            }
            if (raw_content.empty()) {
                auto* desc_el = item->FirstChildElement("description");
                if (desc_el && desc_el->GetText())
                    raw_content = desc_el->GetText();
            }

            //到这里还是空的，就跳过该文档
            if (raw_content.empty()) continue;

            d.content = clean_html(raw_content);
            if (d.content.empty()) continue;

            documents_.push_back(move(d));
        }
    }
}

// ===================== Dedup =====================
//计算x和y的海明距离，二进制不同的个数
static int hamming_distance(uint64_t x, uint64_t y)
{
    int dist = 0;
    uint64_t z = x ^ y;//异或
    while (z) {
        z &= (z - 1);//0000 1000和0000 0111 & 清除最低位1
        ++dist;
    }
    return dist;
}

void PageProcessor::deduplicate_documents()
{
    size_t n = documents_.size();//document_是所有文档的vector
    vector<uint64_t> hashes(n);//存每个文档的哈希值
    vector<bool> keep(n, true);

    // compute simhash for each document
    for (size_t i = 0; i < n; ++i) {
        //动态计算特征维度，5~200
        int topN = max(5, min(200, (int)documents_[i].content.size() / 120));
        //苹姐标题和正文
        string text = documents_[i].title + " " + documents_[i].content;
        hasher_.make(text, topN, hashes[i]);
    }

    // pairwise dedup: O(n²) 两两去重
    //文档去重，复杂度n方，贪心策略，由于是离线流程，可以不优化
    for (size_t i = 0; i < n; ++i) {
        if (!keep[i]) continue;
        for (size_t j = i + 1; j < n; ++j) {
            if (!keep[j]) continue;
            if (hamming_distance(hashes[i], hashes[j]) <= 3)
                keep[j] = false;
        }
    }

    // rebuild document list, re-assign sequential IDs
    vector<Document> deduped;
    int new_id = 1;
    for (size_t i = 0; i < n; ++i) {
        if (keep[i]) {
            documents_[i].id = new_id++;//重赋值id
            deduped.push_back(move(documents_[i]));
        }
    }
    documents_ = move(deduped);
}

// ===================== Build pages & offsets =====================
//网页库和网页偏移库
void PageProcessor::build_pages_and_offsets(const string& pages_file,
                                            const string& offsets_file)
{
    ofstream pfs(pages_file);
    ofstream ofs(offsets_file);

    for (const auto& doc : documents_) {
        ostringstream oss;//字符串输出流对象
        oss << "<doc>\n"
            << "  <id>" << doc.id << "</id>\n"
            << "  <link>" << doc.link << "</link>\n"
            << "  <title>" << doc.title << "</title>\n"
            << "  <content>" << doc.content << "</content>\n"
            << "</doc>\n";
        string xml = oss.str();
        streamoff offset = pfs.tellp();//把pfs的绝对位置写入offset
        pfs << xml;//把xml写入后，pfs流的位置会改变
        ofs << doc.id << '\t' << offset << '\t' << xml.size() << '\n';
    }
}

// ===================== Inverted index =====================
//索引结构是---<关键字>||<文档 id><关键字在文档中的权重>||<文档id><关键字在文档中的权重>||
void PageProcessor::build_inverted_index(const string& filename)
{
    size_t N = documents_.size();           // 文档总数
    unordered_map<string, int> df;          // 包含某个词语的文档个数
   
    // tf值，某个单词string在某个id文档下的数量int
    unordered_map<int, unordered_map<string, int>> doc_tf;

    // set of all unique words
    set<string> all_words;

    // first pass: tokenize each doc, collect TF and DF
    for (const auto& doc : documents_) {
        int id = doc.id;
        string text = doc.title + " " + doc.content;

        vector<string> words;
        tokenizer_.Cut(text, words);//用mix方式对文档分词

        set<string> words_in_doc;  // for DF counting，该文档的关键字set
        int total = 0;
        for (const auto& w : words) {
            // skip invalid tokens
            if (w.empty()) continue;
            if (w.find_first_not_of(" \t\n\r\f\v") == string::npos) continue;
            if (stopWords_.count(w)) continue;

            ++doc_tf[id][w];//该文档下这个关键字的频次数量加一
            ++total;//该文档总关键字数量加一
            words_in_doc.insert(w);//把这个关键字存到set里面
            all_words.insert(w);//存到所有文档的总关键字set
        }

        // store total word count as a special entry
        doc_tf[id][""] = total;

        // update DF
        for (const auto& w : words_in_doc)
            ++df[w];
    }

    // second pass: compute TF-IDF weights, normalize per doc
    // weight权重，int文档id，string关键字，double权重值
    unordered_map<int, unordered_map<string, double>> weights;

    for (const auto& doc : documents_) {
        int id = doc.id;
        int total = doc_tf[id][""];
        if (total == 0) continue;

        double sum_sq = 0.0;
        for (const auto& [word, count] : doc_tf[id]) {
            if (word.empty()) continue;  // skip sentinel
            double tf = (double)count / total;
            double idf = log2((double)N / (df[word] + 1));
            double w = tf * idf;
            weights[id][word] = w;
            sum_sq += w * w;
        }

        // normalize归一化处理w
        double norm = sqrt(sum_sq);
        if (norm > 0) {
            for (auto& [word, w] : weights[id])
                w /= norm;
        }
    }

    // build inverted index: word -> [(doc_id, weight), ...]
    invertedIndex_.clear();
    for (const auto& word : all_words) {
        for (const auto& doc : documents_) {
            auto it = weights[doc.id].find(word);
            if (it != weights[doc.id].end())
                invertedIndex_[word][doc.id] = it->second;
        }
    }

    // write to file: word \t doc_id \t weight \t doc_id \t weight ...
    ofstream ofs(filename);
    for (const auto& [word, posting] : invertedIndex_) {
        ofs << word;
        for (const auto& [doc_id, weight] : posting)
            ofs << '\t' << doc_id << '\t' << weight;
        ofs << '\n';
    }
}
