#ifndef LOGGING_HPP
#define LOGGING_HPP

#include <string>

#define APPLICATION_LOGGER_NAME     "applog"

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

#define LOG_TRACE(...)  SPDLOG_LOGGER_TRACE(spdlog::get(APPLICATION_LOGGER_NAME), __VA_ARGS__)
#define LOG_DEBUG(...)  SPDLOG_LOGGER_DEBUG(spdlog::get(APPLICATION_LOGGER_NAME), __VA_ARGS__)
#define LOG_INFO(...)   SPDLOG_LOGGER_INFO(spdlog::get(APPLICATION_LOGGER_NAME), __VA_ARGS__)
#define LOG_WARN(...)   SPDLOG_LOGGER_WARN(spdlog::get(APPLICATION_LOGGER_NAME), __VA_ARGS__)
#define LOG_ERROR(...)  SPDLOG_LOGGER_ERROR(spdlog::get(APPLICATION_LOGGER_NAME), __VA_ARGS__)
#define LOG_FATAL(...)  SPDLOG_LOGGER_CRITICAL(spdlog::get(APPLICATION_LOGGER_NAME), __VA_ARGS__)

void log_init(const std::string& logfilename);
void log_set_level(const int level);

#endif  // LOGGING_HPP