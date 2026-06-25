#pragma once

#include <string>
#include <vector>
using namespace std;

class DirectoryScanner {
public:
    static vector<string> scan(const string& dir);

private:
    DirectoryScanner() = delete;
};
