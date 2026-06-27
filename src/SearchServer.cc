#include "SearchServer.h"
#include "Logger.h"

#include <wfrest/CodeUtil.h>

#include <iostream>
#include <csignal>

using namespace std;
using namespace wfrest;

SearchServer::SearchServer() = default;

int SearchServer::run(int port)
{
    // 加载搜索离线数据
    if (!searchService_.init("data/pages.dat",
                             "data/offsets.dat",
                             "data/inverted_index.dat")) {
        LOG_ERROR("Failed to load search index files");
        return 1;
    }
    LOG_INFO("Index loaded: {} documents", searchService_.getTotalDocs());

    // 加载关键字推荐数据（优先加载序列化的 Trie，fallback 到 dict 文件）
    if (!recommender_.loadBinary("data/trie.dat")) {
        LOG_WARN("trie.dat not found, falling back to dict files...");
        if (!recommender_.init("data/cn_dict.dat", "data/en_dict.dat")) {
            LOG_ERROR("Failed to load dict files for recommender");
            return 1;
        }
    }
    LOG_INFO("Recommender Trie loaded");

    HttpServer server;

    // GET /search?q=xxx
    server.GET("/search", [this](const HttpReq* req, HttpResp* resp) {
        string q = CodeUtil::url_decode(req->query("q"));
        string result = q.empty() ? "[]" : searchService_.search(q);
        resp->String(result);
        resp->add_header("Access-Control-Allow-Origin", "*");
        resp->add_header("Content-Type", "application/json; charset=utf-8");
    });

    // GET /suggest?q=xxx
    server.GET("/suggest", [this](const HttpReq* req, HttpResp* resp) {
        //对q进行url解码
        string q = CodeUtil::url_decode(req->query("q"));
        string result = recommender_.suggest(q);
        resp->String(result);
        resp->add_header("Access-Control-Allow-Origin", "*");
        resp->add_header("Content-Type", "application/json; charset=utf-8");
    });

    // GET /health  健康检查，检查后端服务器状态怎么样
    server.GET("/health", [this](const HttpReq*, HttpResp* resp) {
        resp->String("{\"status\":\"ok\",\"docs\":" +
                     to_string(searchService_.getTotalDocs()) + "}");
        resp->add_header("Content-Type", "application/json; charset=utf-8");
    });

    // GET /hot?k=10  热门文档 Top-K ，每次刷新时会发请求
    server.GET("/hot", [this](const HttpReq* req, HttpResp* resp) {
        string kStr = req->query("k");
        int k = kStr.empty() ? 10 : stoi(kStr);
        string result = searchService_.hotPages(k);
        resp->String(result);
        resp->add_header("Access-Control-Allow-Origin", "*");
        resp->add_header("Content-Type", "application/json; charset=utf-8");
    });

    // POST /click?id=xxx  用户点击上报，为了热度排行
    server.POST("/click", [this](const HttpReq* req, HttpResp* resp) {
        string idStr = req->query("id");
        if (!idStr.empty()) {
            searchService_.recordClick(stoi(idStr));
        }
        resp->String("{\"ok\":true}");
        resp->add_header("Access-Control-Allow-Origin", "*");
        resp->add_header("Content-Type", "application/json; charset=utf-8");
    });

    // 首页
    server.GET("/", [](const HttpReq*, HttpResp* resp) {
        resp->File("static/index.html");
    });

    // 静态文件（CSS / JS 等）
    server.Static("/", "static/");

    if (server.start(port) == 0) {
        LOG_INFO("Server listening on http://localhost:{}", port);
        LOG_INFO("Press Ctrl+C to stop");

        sigset_t mask; // 声明信号集合
        sigemptyset(&mask); // 清空
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGTERM); // 这两放进去
        if (pthread_sigmask(SIG_BLOCK, &mask, nullptr) != 0) {
            LOG_ERROR("Failed to block SIGINT/SIGTERM");
            server.stop();
            return 1;
        }

        int sig = 0;
        // 同步阻塞函数，传统的 signal() 或 sigaction() 会在中断当前代码流的情况下异步执行回调，
        // 容易引发多线程或复杂状态下的死锁、数据竞争等问题
        sigwait(&mask, &sig);

        server.stop();
        return 0;
    }

    LOG_ERROR("Failed to start server on port {}", port);
    return 1;
}

int main()
{
    SearchServer server;
    return server.run(8080);
}
