#include <chrono>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <cmath>

#include <boost/format.hpp>

#include "vxsdr_header.hpp"
#include "logging.hpp"
#include "errors.hpp"

std::string format_time(const std::chrono::time_point<std::chrono::system_clock> t, const int digits, const bool use_utc, const std::string fmt) {
  std::stringstream output;
  time_t n_seconds = std::chrono::system_clock::to_time_t(t);
  if(use_utc) {
    output << std::put_time(gmtime(&n_seconds), fmt.c_str());
  }
  else {
    output << std::put_time(localtime(&n_seconds), fmt.c_str());
  }
  int32_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t - std::chrono::system_clock::from_time_t(n_seconds)).count();
  switch(digits) {
    case 0:
      break;
    case 1:
      output << boost::format(".%01d") % std::lround(1e-8 * ns);
      break;
    case 2:
      output << boost::format(".%02d") % std::lround(1e-7 * ns);
      break;
    case 3:
      output << boost::format(".%03d") % std::lround(1e-6 * ns);
      break;
    case 4:
      output << boost::format(".%04d") % std::lround(1e-5 * ns);
      break;
    case 5:
      output << boost::format(".%05d") % std::lround(1e-4 * ns);
      break;
    case 6:
    default:
      output << boost::format(".%06d") % std::lround(1e-3 * ns);
      break;
  }
  return output.str();
}

std::string make_filename_time(const std::chrono::time_point<std::chrono::system_clock> t, const double freq, const double rate)
{
  std::stringstream s;
  s << boost::format("%.3f-mhz-%.1f-ksps-") % (freq / 1e6) %  (rate / 1e3);
  s << format_time(t, 0, true, "%Y-%m-%d-%H.%M.%S-utc");
  return s.str();
}

std::string make_filename_no_time(const double freq, const double rate)
{
  std::stringstream s;
  s << boost::format("%.3f-mhz-%.1f-ksps") % (freq / 1e6) %  (rate / 1e3);
  return s.str();
}

size_t read_cplx_16(const std::string &name, std::vector<std::complex<int16_t> > &data)
{
  std::ifstream infile(name, std::ios::binary);

  if(infile.is_open()) {
    infile.seekg(0, infile.end);
    size_t num_elem = infile.tellg() / sizeof(std::complex<int16_t>);
    infile.seekg(0, infile.beg);
    
    if(num_elem > data.size())
      data.resize(num_elem);

    if(infile.good())
      infile.read((char *)&data.front(), sizeof(std::complex<int16_t>) * num_elem);
    infile.close();
    
    for(size_t i = num_elem; i < data.size(); i++)
      data[i] = std::complex<int16_t>(0, 0);
    return num_elem;
  }
  else
    return 0;
}

size_t write_cplx_16(const std::string &name, std::vector<std::complex<int16_t> > &data)
{
  std::fstream outfile(name, std::ios::binary);

  if(outfile.is_open()) {
    outfile.write((char *)&data.front(), sizeof(std::complex<int16_t>) * data.size());
    outfile.seekg(0, outfile.end);
    size_t num_elem = outfile.tellg() / sizeof(std::complex<int16_t>);
    outfile.close();
    return num_elem;
  }
  else
    return 0;
}
