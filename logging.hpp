#ifndef LOGGING_HPP
#define LOGGING_HPP

#include <string>

#ifndef VXSDR_DISABLE_LOGGING

// disable exceptions in logging
#define SPDLOG_NO_EXCEPTIONS
// use the compiled library
#ifndef SPDLOG_COMPILED_LIB
#define SPDLOG_COMPILED_LIB
#endif

#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#endif

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#define LOG_INIT(x)     log_init(x)

#define LOG_TRACE(...)  SPDLOG_LOGGER_TRACE(spdlog::get("multi"), __VA_ARGS__)
#define LOG_DEBUG(...)  SPDLOG_LOGGER_DEBUG(spdlog::get("multi"), __VA_ARGS__)
#define LOG_INFO(...)   SPDLOG_LOGGER_INFO(spdlog::get("multi"), __VA_ARGS__)
#define LOG_WARN(...)   SPDLOG_LOGGER_WARN(spdlog::get("multi"), __VA_ARGS__)
#define LOG_ERROR(...)  SPDLOG_LOGGER_ERROR(spdlog::get("multi"), __VA_ARGS__)
#define LOG_FATAL(...)  SPDLOG_LOGGER_CRITICAL(spdlog::get("multi"), __VA_ARGS__)

#define LOG_CLOSE()     log_close();

// what level to use for packet logs
#define LOG_PACKET(...) SPDLOG_LOGGER_INFO(spdlog::get("multi"), __VA_ARGS__)

#else

// need fmtlib if not using spdlog's
#include <fmt/core.h>

#define LOG_INIT(x)     {}

#define LOG_TRACE(...)  {}
#define LOG_DEBUG(...)  {}
#define LOG_INFO(...)   {}
#define LOG_WARN(...)   {}
#define LOG_ERROR(...)  {}
#define LOG_FATAL(...)  {}

#define LOG_CLOSE()     {}

#define LOG_PACKET(...) {}

#endif  // VXSDR_DISABLE_LOGGING

void log_init(const std::string& logfilename);
void log_close();
void log_set_level(const int level);

#endif  // LOGGING_HPP