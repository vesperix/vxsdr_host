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
    auto radio = std::make_shared<vxsdr>(local_send_endpoint, local_receive_endpoint, destination_endpoint);

    set_common_options(vm, radio);
    set_rx_1ch_options(vm, radio);
    set_tx_1ch_options(vm, radio);

    if(vm["duration"].as<double>() <= 0.0) {
      LOGGER(fatal) << "Duration must be positive";
      return EXIT_CMD_LINE_ERROR;
    }

    size_t n_samp = std::llround(vm["duration"].as<double>() * vm["rate"].as<double>());

    LOGGER(info) << boost::format("Acquiring data for %.3f seconds (%lld samples)") % vm["duration"].as<double>() % n_samp;

    std::vector<std::complex<int16_t> > data(n_samp);

    std::string filename = vm["prefix"].as<std::string>();
    filename.append(make_filename_time(std::chrono::system_clock::now(), vm["freq"].as<double>(), vm["rate"].as<double>()));
    filename.append(vm["suffix"].as<std::string>());

    LOGGER(info) << "Saving data to file " << filename;

    LOGGER(info) << "Starting...";

    radio->rx_start_now_for_n_samples(n_samp);
    radio->get_rx_data(data, n_samp);
    radio->rx_stop_now();

    LOGGER(info) << "Acquisition complete";

    size_t n_written = write_cplx_16(filename, data);
    LOGGER(info) << n_written << "samples written";

  }
  catch (std::exception& e) {
    LOGGER(fatal) << "exception caught: " << e.what();
    return EXIT_INTERNAL_ERROR;
  }

  LOGGER(info) << argv[0] << " finished";
  return EXIT_SUCCESS;
}
