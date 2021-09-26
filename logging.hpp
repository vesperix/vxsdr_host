#ifndef LOGGING_HPP
#define LOGGING_HPP

#include <boost/log/trivial.hpp>
#include <spdlog/spdlog.h>

#define INIT_LOGGER()
#define LOGGER(x)            BOOST_LOG_TRIVIAL(x)

#define LOG_DEBUG            spdlog::debug
#define LOG_INFO             spdlog::info
#define LOG_ERROR            spdlog::error
#define LOG_FATAL            spdlog::critical

#endif // LOGGING_HPP