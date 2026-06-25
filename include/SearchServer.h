#pragma once

#include "SearchService.h"
#include "KeywordRecommender.h"

#include <wfrest/HttpServer.h>

class SearchServer {
public:
    SearchServer();
    int run(int port = 8080);

private:
    SearchService searchService_;
    KeywordRecommender recommender_;
};
