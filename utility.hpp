#ifndef UTILITY_HPP
#define UTILITY_HPP

#include <chrono>
#include <sstream>

#include <boost/format.hpp>

std::string format_time(const std::chrono::time_point<std::chrono::system_clock> t, const int digits = 6, const bool use_utc = false);
std::string packet_type_to_name(const uint8_t number);

#endif // UTILITY_HPP