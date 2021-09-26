#ifndef LOGGING_HPP
#define LOGGING_HPP

//#include <boost/log/trivial.hpp>
#include <spdlog/spdlog.h>

#define INIT_LOGGER()
//#define LOGGER(x)            BOOST_LOG_TRIVIAL(x)

#define LOG_DEBUG(x)            spdlog::debug(x)
#define LOG_INFO(x)             spdlog::info(x)
#define LOG_WARN(x)             spdlog::warn(x)
#define LOG_ERROR(x)            spdlog::error(x)
#define LOG_FATAL(x)            spdlog::critical(x)

#endif // LOGGING_HPP