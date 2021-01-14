#include <vector>

#include <boost/format.hpp>

#include "logging.hpp"
#include "vxsdr_header.hpp"

#ifndef PACKET_PROC_HPP
#define PACKET_PROC_HPP

void log_packet_info(generic_cmd_or_rsp_packet &packet);

#endif // PACKET_PROC_HPP