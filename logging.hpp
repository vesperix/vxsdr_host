#ifndef LOGGING_HPP
#define LOGGING_HPP

#include <spdlog/spdlog.h>

#define INIT_LOGGER()           my_log_init()

#define LOG_DEBUG(x)            spdlog::debug(x)
#define LOG_INFO(x)             spdlog::info(x)
#define LOG_WARN(x)             spdlog::warn(x)
#define LOG_ERROR(x)            spdlog::error(x)
#define LOG_FATAL(x)            spdlog::critical(x)

void my_log_init();

#endif // LOGGING_HPP