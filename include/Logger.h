#pragma once

#define SPDLOG_HEADER_ONLY
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/daily_file_sink.h>

#include <memory>
#include <string>

// ============================================================================
// 全局 Logger — 双 sink 输出
//   - 控制台: 彩色，开发时实时查看，默认 INFO 级别
//   - 文件:   logs/search.log，按天轮转保留 7 天，TRACE 级别记录所有细节
//
// 使用方式:
//   LOG_INFO("Server started on port {}", port);
//   LOG_ERROR("Failed to load index: {}", filename);
//   LOG_DEBUG("Cache hit for query: {}", query);
// ============================================================================

inline std::shared_ptr<spdlog::logger> getLogger() {
    static auto instance = [] {
        // 控制台 sink — 彩色输出，INFO 以上级别
        auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console->set_level(spdlog::level::info);

        // 文件 sink — 按天轮转，保留 7 天，TRACE 以上全部记录
        auto file = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
            "logs/search.log",   // 基础文件名
            0,                    // 0 点轮转
            0);                   // 0 分轮转
        file->set_level(spdlog::level::trace);

        // 拼接成多 sink logger
        spdlog::sinks_init_list sinks{console, file};
        auto logger = std::make_shared<spdlog::logger>("search", sinks);
        logger->set_level(spdlog::level::trace);

        // 设置格式: [2026-06-26 21:30:05.123] [info] [SearchServer.cc:89] message
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
        logger->flush_on(spdlog::level::warn);  // warn 及以上立即刷盘

        spdlog::register_logger(logger);
        return logger;
    }();
    return instance;
}

// ===================== 便捷宏 =====================

#define LOG_TRACE(...) getLogger()->trace(__VA_ARGS__)
#define LOG_DEBUG(...) getLogger()->debug(__VA_ARGS__)
#define LOG_INFO(...)  getLogger()->info(__VA_ARGS__)
#define LOG_WARN(...)  getLogger()->warn(__VA_ARGS__)
#define LOG_ERROR(...) getLogger()->error(__VA_ARGS__)
