#ifndef LOGGING_HPP
#define LOGGING_HPP

#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>  // support for loading levels from the environment variable
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#define INIT_LOGGER()           my_log_init()

#define LOG_DEBUG(x)            spdlog::get("multi")->debug(x)
#define LOG_INFO(x)             spdlog::get("multi")->info(x)
#define LOG_WARN(x)             spdlog::get("multi")->warn(x)
#define LOG_ERROR(x)            spdlog::get("multi")->error(x)
#define LOG_FATAL(x)            spdlog::get("multi")->critical(x)

void my_log_init();

#endif // LOGGING_HPP