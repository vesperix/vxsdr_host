#ifndef UTILITY_HPP
#define UTILITY_HPP

#include <chrono>
#include <sstream>

std::string format_time(const std::chrono::time_point<std::chrono::system_clock> t,
                        const int digits       = 6,
                        const bool use_utc     = true,
                        const std::string& fmt = "%Y-%m-%d %H:%M:%S");
std::string make_filename_no_time(const double freq, const double rate);
std::string make_filename_time(const std::chrono::time_point<std::chrono::system_clock> t, const double freq, const double rate);

size_t read_cplx_16(const std::string& name, std::vector<std::complex<int16_t> >& data);
size_t write_cplx_16(const std::string& name, std::vector<std::complex<int16_t> >& data);

#endif  // UTILITY_HPP