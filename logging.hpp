#ifndef LOGGING_HPP
#define LOGGING_HPP

// disable exceptions in logging
#define SPDLOG_NO_EXCEPTIONS
// use the compiled library
#ifndef SPDLOG_COMPILED_LIB
#define SPDLOG_COMPILED_LIB
#endif

#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#endif

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#define LOG_INIT(x)             log_init(x)

#define LOG_TRACE(x)            SPDLOG_LOGGER_TRACE(spdlog::get("multi"), x)
#define LOG_DEBUG(x)            SPDLOG_LOGGER_DEBUG(spdlog::get("multi"), x)
#define LOG_INFO(x)             SPDLOG_LOGGER_INFO(spdlog::get("multi"), x)
#define LOG_WARN(x)             SPDLOG_LOGGER_WARN(spdlog::get("multi"), x)
#define LOG_ERROR(x)            SPDLOG_LOGGER_ERROR(spdlog::get("multi"), x)
#define LOG_FATAL(x)            SPDLOG_LOGGER_CRITICAL(spdlog::get("multi"), x)

#define LOG_CLOSE()             log_close();

// what level to use for packet logs
#define LOG_PACKET(x)           SPDLOG_LOGGER_INFO(spdlog::get("multi"), x)

void log_init(const std::string &logfilename);
void log_close();
void log_set_level(const int level);

#endif // LOGGING_HPP