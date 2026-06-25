#include "KeywordProcessor.h"
#include "PageProcessor.h"
#include <iostream>

int main()
{
    // Phase 1.1: Keyword recommendation
    KeywordProcessor kwProcessor;
    kwProcessor.process("corpus/CN", "corpus/EN");
    std::cout << "Phase 1.1 done: cn_dict.dat, cn_index.dat, en_dict.dat, en_index.dat"
              << std::endl;

    // Phase 1.2: Web search
    PageProcessor pageProcessor;
    pageProcessor.process("corpus/webpages");
    std::cout << "Phase 1.2 done: pages.dat, offsets.dat, inverted_index.dat"
              << std::endl;

    return 0;
}
