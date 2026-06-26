#include "KeywordProcessor.h"
#include "PageProcessor.h"
#include "Logger.h"

#include <iostream>

int main()
{
    LOG_INFO("========== Offline indexing pipeline start ==========");

    // Phase 1.1: Keyword recommendation
    LOG_INFO("Phase 1.1: Building keyword dictionaries...");
    KeywordProcessor kwProcessor;
    kwProcessor.process("corpus/CN", "corpus/EN");
    LOG_INFO("Phase 1.1 done: cn_dict.dat, cn_index.dat, en_dict.dat, en_index.dat");

    // Phase 1.2: Web search
    LOG_INFO("Phase 1.2: Building page index and inverted index...");
    PageProcessor pageProcessor;
    pageProcessor.process("corpus/webpages");
    LOG_INFO("Phase 1.2 done: pages.dat, offsets.dat, inverted_index.dat");

    LOG_INFO("========== Offline indexing pipeline done ==========");
    return 0;
}
