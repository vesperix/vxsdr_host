#ifndef LOGGING_HPP
#define LOGGING_HPP

#include <boost/log/trivial.hpp>
#include <spdlog/spdlog.h>

#define INIT_LOGGER()
#define LOGGER(x)            BOOST_LOG_TRIVIAL(x)

#endif // LOGGING_HPP