#ifndef LOGGING_HPP
#define LOGGING_HPP

#include <boost/log/trivial.hpp>

#define INIT_LOGGER()
#define LOGGER(x)            BOOST_LOG_TRIVIAL(x)

#endif // LOGGING_HPP