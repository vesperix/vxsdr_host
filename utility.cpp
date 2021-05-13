#include <chrono>
#include <sstream>
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

std::string packet_type_to_name(const uint8_t number) {
  switch(number) {
    case PACKET_TYPES::TX_SIGNAL_DATA:
      return "TX_SIGNAL_DATA";
    case PACKET_TYPES::RX_SIGNAL_DATA:
      return "RX_SIGNAL_DATA";
    case PACKET_TYPES::DEVICE_CMD:
      return "DEVICE_CMD";
    case PACKET_TYPES::TX_RADIO_CMD:
      return "TX_RADIO_CMD";
    case PACKET_TYPES::RX_RADIO_CMD:
      return "RX_RADIO_CMD";
    case PACKET_TYPES::DEVICE_CMD_ERR:
      return "DEVICE_CMD_ERR";
    case PACKET_TYPES::TX_RADIO_CMD_ERR:
      return "TX_RADIO_CMD_ERR";
    case PACKET_TYPES::RX_RADIO_CMD_ERR:
      return "RX_RADIO_CMD_ERR";
    case PACKET_TYPES::DEVICE_CMD_RSP:
      return "DEVICE_CMD_RSP";
    case PACKET_TYPES::TX_RADIO_CMD_RSP:
      return "TX_RADIO_CMD_RSP";
    case PACKET_TYPES::RX_RADIO_CMD_RSP:
      return "RX_RADIO_CMD_RSP";
    case PACKET_TYPES::TX_SIGNAL_DATA_ACK:
      return "TX_SIGNAL_DATA_ACK";
    case PACKET_TYPES::RX_SIGNAL_DATA_ACK:
      return "RX_SIGNAL_DATA_ACK";
    case PACKET_TYPES::DEVICE_CMD_ACK:
      return "DEVICE_CMD_ACK";
    case PACKET_TYPES::TX_RADIO_CMD_ACK:
      return "TX_RADIO_CMD_ACK";
    case PACKET_TYPES::RX_RADIO_CMD_ACK:
      return "RX_RADIO_CMD_ACK";
  }
  return "UNDEFINED";
}