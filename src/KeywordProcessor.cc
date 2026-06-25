#include "KeywordProcessor.h"
#include "DirectoryScanner.h"

#include <utfcpp/utf8.h>

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <map>
#include <vector>
#include <set>
using namespace std;

KeywordProcessor::KeywordProcessor()
{
    load_stopwords("stopwords/en_stopwords.txt", en_stopwords_);
    load_stopwords("stopwords/cn_stopwords.txt", cn_stopwords_);
}

//把停用词放到set里
void KeywordProcessor::load_stopwords(const string& file,
                                      set<string>& stopwords)
{
    ifstream ifs(file);
    if (!ifs) return;
    string line;
    while (getline(ifs, line)) {
        // trim trailing \r
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            stopwords.insert(line);
    }
}

void KeywordProcessor::process(const std::string& chDir,
                               const std::string& enDir)
{
    //chdir为中文目录CN
    create_cn_dict(chDir, "data/cn_dict.dat");
    build_cn_index("data/cn_dict.dat", "data/cn_index.dat");

    create_en_dict(enDir, "data/en_dict.dat");
    build_en_index("data/en_dict.dat", "data/en_index.dat");
}

// ===================== English =====================

void KeywordProcessor::create_en_dict(const string& dir,
                                      const string& outfile)
{
    //中文目录下所有文件名
    auto files = DirectoryScanner::scan(dir);
    unordered_map<string, int> freq;

    for (const auto& file : files) {
        ifstream ifs(file);
        if (!ifs) continue;
        string line;
        while (getline(ifs, line)) {
            
            //遍历一行，文档清洗，全部转化为小写，非字符的东东转化为空格
            for (auto& ch : line) {
                if (isalpha(static_cast<unsigned char>(ch)))
                    ch = tolower(static_cast<unsigned char>(ch));
                else
                    ch = ' ';
            }
            istringstream iss(line);
            string token;
            //默认以空格分隔，提取单词
            while (iss >> token) {
                //如果在停用词set中，忽略
                if (en_stopwords_.count(token)) continue;
                ++freq[token];
            }
        }
    }

    // sort by freq descending
    vector<pair<string, int>> items(freq.begin(), freq.end());
    sort(items.begin(), items.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    ofstream ofs(outfile);
    for (const auto& [word, count] : items)
        ofs << word << '\t' << count << '\n';
}

void KeywordProcessor::build_en_index(const string& dictfile,
                                      const string& indexfile)
{
    ifstream ifs(dictfile);
    if (!ifs) return;

    map<char, set<string>> index;

    string line;
    while (getline(ifs, line)) {
        auto pos = line.find('\t');
        //没有的话跳过
        if (pos == string::npos) continue;
        string word = line.substr(0, pos);

        // insert word under each unique character it contains
        set<char> seen;//去重容器，利用set去重特性
        for (char ch : word) {
            //seen.insert插入失败代表不是字母不是第一次出现
            //first是指向被插入元素，second是bool
            if (seen.insert(ch).second)
                index[ch].insert(word);
        }
    }

    ofstream ofs(indexfile);
    for (const auto& [ch, words] : index) {
        ofs << ch;
        for (const auto& w : words)
            ofs << '\t' << w;
        ofs << '\n';
    }
}

// ===================== Chinese =====================

// Returns true if a codepoint represents a CJK character, letter, or digit
//合法性检查，cjk中日韩统一表意文字
static inline bool is_valid_codepoint(char32_t cp) {
    // ASCII letters and digits
    if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z')) return true;
    if (cp >= '0' && cp <= '9') return true;
    // CJK Unified Ideographs cjk统一汉字基本区
    if (cp >= 0x4E00 && cp <= 0x9FFF) return true;
    // CJK Unified Ideographs Extension A 扩展A区包含了生僻字
    if (cp >= 0x3400 && cp <= 0x4DBF) return true;
    // CJK Compatibility Ideographs 兼容汉字区
    if (cp >= 0xF900 && cp <= 0xFAFF) return true;
    return false;
}

// Check if a token is worth keeping: at least one CJK/letter/digit char
static bool is_valid_cn_token(const string& token) {
    if (token.empty()) return false;
    const char* curr = token.c_str();
    const char* end = token.c_str() + token.size();
    while (curr != end) {
        //遍历token，只要里面有一个合法的字符就行，合法就是上面的定义
        char32_t cp = utf8::unchecked::next(curr);
        if (is_valid_codepoint(cp)) return true;
    }
    return false;
}

void KeywordProcessor::create_cn_dict(const string& dir,
                                      const string& outfile)
{
    auto files = DirectoryScanner::scan(dir);
    unordered_map<string, int> freq;

    for (const auto& file : files) {
        ifstream ifs(file);
        if (!ifs) continue;
        stringstream buffer;
        buffer << ifs.rdbuf();//整体倾倒
        string text = buffer.str();

        //mix方式进行分词
        vector<string> words;
        tokenizer_.Cut(text, words);

        for (const auto& w : words) {
            if (!is_valid_cn_token(w)) continue;//跳过无效词
            if (cn_stopwords_.count(w)) continue;//跳过停用词
            ++freq[w];
        }
    }

    vector<pair<std::string, int>> items(freq.begin(), freq.end());
    sort(items.begin(), items.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    ofstream ofs(outfile);
    for (const auto& [word, count] : items)
        ofs << word << '\t' << count << '\n';
}

void KeywordProcessor::build_cn_index(const std::string& dictfile,
                                      const std::string& indexfile)
{
    ifstream ifs(dictfile);
    if (!ifs) return;

    // key: UTF-8 character string, value: set of words containing it
    map<string, set<std::string>> index;

    string line;
    while (std::getline(ifs, line)) {
        auto pos = line.find('\t');
        if (pos == string::npos) continue;
        string word = line.substr(0, pos);//和上面一样，拿行首的词语

        const char* curr = word.c_str();
        const char* end = word.c_str() + word.size();

        set<string> seen;//和上面一样，用set去重，然后形成索引
        while (curr != end) {
            auto start = curr;
            //next函数会将迭代器 curr 向前移动一个完整的 UTF-8 字符的长度
            utf8::next(curr, end);
            string ch(start, curr);
            if (seen.insert(ch).second)
                index[ch].insert(word);
        }
    }

    ofstream ofs(indexfile);
    for (const auto& [ch, words] : index) {
        ofs << ch;
        for (const auto& w : words)
            ofs << '\t' << w;
        ofs << '\n';
    }
}
