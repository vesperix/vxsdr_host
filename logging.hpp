#ifndef LOGGING_HPP
#define LOGGING_HPP

// disable exceptions in logging
#define SPDLOG_NO_EXCEPTIONS
// use the compiled library
#ifndef SPDLOG_COMPILED_LIB
#define SPDLOG_COMPILED_LIB
#endif

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#define INIT_LOGGER(x)          log_init(x)

#define LOG_TRACE(x)            spdlog::get("multi")->trace(x)
#define LOG_DEBUG(x)            spdlog::get("multi")->debug(x)
#define LOG_INFO(x)             spdlog::get("multi")->info(x)
#define LOG_WARN(x)             spdlog::get("multi")->warn(x)
#define LOG_ERROR(x)            spdlog::get("multi")->error(x)
#define LOG_FATAL(x)            spdlog::get("multi")->critical(x)

void log_init(const std::string &logfilename);
void log_set_level(const int level);

#endif // LOGGING_HPP