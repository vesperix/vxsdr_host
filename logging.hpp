#ifndef LOGGING_HPP
#define LOGGING_HPP

#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>

#define APPLICATION_LOGGER_NAME     "applog"
#define MAKE_LOG_FILE
#define TIME_IN_LOG_FILE_NAME

// disable exceptions in logging
#define SPDLOG_NO_EXCEPTIONS
#define FMT_EXCEPTIONS              (0)
// force header only
#define SPDLOG_HEADER_ONLY
#define FMT_HEADER_ONLY

#define SPDLOG_ACTIVE_LEVEL         (1)  # 0 = trace, 1 = debug, 2 = info, 3 = warn, 4 = error 5 = critical

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>

#define LOG_INIT(x)     log_init(x)

#define LOG_TRACE(...)  SPDLOG_LOGGER_TRACE(spdlog::get(APPLICATION_LOGGER_NAME), __VA_ARGS__)
#define LOG_DEBUG(...)  SPDLOG_LOGGER_DEBUG(spdlog::get(APPLICATION_LOGGER_NAME), __VA_ARGS__)
#define LOG_INFO(...)   SPDLOG_LOGGER_INFO(spdlog::get(APPLICATION_LOGGER_NAME), __VA_ARGS__)
#define LOG_WARN(...)   SPDLOG_LOGGER_WARN(spdlog::get(APPLICATION_LOGGER_NAME), __VA_ARGS__)
#define LOG_ERROR(...)  SPDLOG_LOGGER_ERROR(spdlog::get(APPLICATION_LOGGER_NAME), __VA_ARGS__)
#define LOG_FATAL(...)  SPDLOG_LOGGER_CRITICAL(spdlog::get(APPLICATION_LOGGER_NAME), __VA_ARGS__)

void log_init(const std::string& logfilename);
void log_set_level(const int level);

#endif  // LOGGING_HPP