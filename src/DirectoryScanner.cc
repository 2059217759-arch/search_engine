#include "DirectoryScanner.h"
#include <dirent.h>
using namespace std;

vector<string> DirectoryScanner::scan(const string& dir)
{
    vector<string> files;//存文件名
    DIR* dp = opendir(dir.c_str());//打开目录
    if (!dp) return files;

    struct dirent* entry;//目录结构体
    while ((entry = readdir(dp)) != nullptr) {
        string name = entry->d_name;
        if (name == "." || name == "..") continue;
        files.push_back(dir + "/" + name);
    }
    closedir(dp);
    return files;
}
