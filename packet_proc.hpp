#include <vector>

#include <boost/format.hpp>

#include "logging.hpp"
#include "vxsdr_header.hpp"

#ifndef PACKET_PROC_HPP
#define PACKET_PROC_HPP

std::string packet_type_to_name(const uint8_t number);
std::string command_to_name(const uint8_t cmd);
void log_packet_info(cmd_or_rsp_packet &packet);

#endif // PACKET_PROC_HPP