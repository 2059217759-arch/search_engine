#include "SearchServer.h"

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
        cerr << "Failed to load search index files" << endl;
        return 1;
    }
    cout << "Index loaded: " << searchService_.getTotalDocs() << " documents" << endl;

    // 加载关键字推荐数据（Trie）
    if (!recommender_.init("data/cn_dict.dat", "data/en_dict.dat")) {
        cerr << "Failed to load dict files for recommender" << endl;
        return 1;
    }
    cout << "Recommender Trie loaded" << endl;

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

    // 首页
    server.GET("/", [](const HttpReq*, HttpResp* resp) {
        resp->File("static/index.html");
    });

    // 静态文件（CSS / JS 等）
    server.Static("/", "static/");

    if (server.start(port) == 0) {
        cout << "Server listening on http://localhost:" << port << endl;
        cout << "Press Ctrl+C to stop" << endl;

        sigset_t mask; // 声明信号集合
        sigemptyset(&mask); // 清空
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGTERM); // 这两放进去
        if (pthread_sigmask(SIG_BLOCK, &mask, nullptr) != 0) {
            cerr << "Failed to block SIGINT/SIGTERM" << endl;
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

    cerr << "Failed to start server on port " << port << endl;
    return 1;
}

int main()
{
    SearchServer server;
    return server.run(8080);
}
