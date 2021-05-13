#include <iostream>
#include <fstream>
#include <csignal>
#include <complex>
#include <string>
#include <chrono>
#include <ctime>
#include <limits>
#include <cstdlib>
#include <thread>

#include <boost/version.hpp>
#include <boost/program_options.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/format.hpp>

#include "logging.hpp"
#include "errors.hpp"

#include "vxsdr.hpp"
#include "host_radio_options.hpp"

extern "C" void sig_int_handler(int sig_num) {
  LOGGER(fatal) << "terminated by SIGINT (ctrl-C)";
  exit(EXIT_USER_TERMINATED);
}

int main(int argc, char *argv[])
{
  INIT_LOGGER();
  try {
    LOGGER(info) << argv[0] << " started";
    signal(SIGINT, &sig_int_handler);
    boost::program_options::options_description desc("Command line options");
    boost::program_options::variables_map vm;

    add_common_options(desc);
    add_network_options(desc);
    add_rx_1ch_options(desc);

    init_vm(desc, vm, argc, argv);

    boost::asio::ip::address_v4 local_ip = boost::asio::ip::address_v4::from_string(vm["local_address"].as<std::string>());
    boost::asio::ip::address_v4 destination_ip = boost::asio::ip::address_v4::from_string(vm["destination_address"].as<std::string>());
    boost::asio::ip::udp::endpoint local_send_endpoint(local_ip, vm["local_send_port"].as<unsigned>());
    LOGGER(info) << "using local send IP " << local_send_endpoint.address().to_string() << " and port " << local_send_endpoint.port();
    boost::asio::ip::udp::endpoint local_receive_endpoint(local_ip, vm["local_receive_port"].as<unsigned>());
    LOGGER(info) << "using local receive IP " << local_receive_endpoint.address().to_string() << " and port " << local_receive_endpoint.port();
    boost::asio::ip::udp::endpoint destination_endpoint(destination_ip, vm["destination_port"].as<unsigned>());
    LOGGER(info) << "using destination IP " << destination_endpoint.address().to_string() << " and port " << destination_endpoint.port();
    auto radio = std::shared_ptr<vxsdr>(new vxsdr(local_send_endpoint, local_receive_endpoint, destination_endpoint));

    set_common_options(vm, radio);
    set_rx_1ch_options(vm, radio);
    set_tx_1ch_options(vm, radio);
  }
  catch (std::exception& e) {
    LOGGER(fatal) << "exception caught: " << e.what();
    return EXIT_INTERNAL_ERROR;
  }

  LOGGER(info) << argv[0] << " finished";
  return EXIT_SUCCESS;
}
