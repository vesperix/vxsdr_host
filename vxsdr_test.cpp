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

#include "logging.hpp"
#include "errors.hpp"

#include "vxsdr.hpp"
#include "host_radio_options.hpp"

extern "C" void sig_int_handler(int /* sig_num */) {
  LOG_FATAL("terminated by SIGINT (ctrl-C)");
  exit(EXIT_USER_TERMINATED);
}

int main(int argc, char *argv[])
{
  INIT_LOGGER();
  try {
    LOG_INFO("started");
    signal(SIGINT, &sig_int_handler);
    boost::program_options::options_description desc("Command line options");
    boost::program_options::variables_map vm;

    add_common_options(desc);
    add_network_options(desc);
    add_tx_1ch_options(desc);
    add_rx_1ch_options(desc);

    init_vm(desc, vm, argc, argv);

    boost::asio::ip::address_v4 local_ip = boost::asio::ip::address_v4::from_string(vm["local_address"].as<std::string>());
    boost::asio::ip::address_v4 destination_ip = boost::asio::ip::address_v4::from_string(vm["destination_address"].as<std::string>());
    boost::asio::ip::udp::endpoint local_send_endpoint(local_ip, vm["local_send_port"].as<unsigned>());
    LOG_INFO(fmt::format("using local send IP {:s} and port {:d}", local_send_endpoint.address().to_string(), local_send_endpoint.port()));
    boost::asio::ip::udp::endpoint local_receive_endpoint(local_ip, vm["local_receive_port"].as<unsigned>());
    LOG_INFO(fmt::format("using local receive IP {:s} and port {:d}", local_receive_endpoint.address().to_string(), local_receive_endpoint.port()));
    boost::asio::ip::udp::endpoint destination_endpoint(destination_ip, vm["destination_port"].as<unsigned>());
    LOG_INFO(fmt::format("using destination IP {:s} and port {:d}", destination_endpoint.address().to_string(), destination_endpoint.port()));

    auto radio = std::make_unique<vxsdr>(local_send_endpoint, local_receive_endpoint, destination_endpoint);

    LOG_INFO(fmt::format("using frequency {:e}", vm["freq"].as<double>()));
    LOG_INFO(fmt::format("using rate      {:e}", vm["rate"].as<double>()));
    LOG_INFO(fmt::format("using tx_gain   {:e}", vm["tx_gain"].as<double>()));
    LOG_INFO(fmt::format("using rx_gain   {:e}", vm["rx_gain"].as<double>()));

    if(vm["duration"].as<double>() <= 0.0) {
      LOG_FATAL("duration must be positive");
      return EXIT_CMD_LINE_ERROR;
    }

    // let radio complete initialization
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    set_common_options(vm, radio);
    set_rx_1ch_options(vm, radio);
    set_tx_1ch_options(vm, radio);

    size_t n_samp = std::llround(vm["duration"].as<double>() * vm["rate"].as<double>());

    LOG_INFO(fmt::format("preparing {:.3f} seconds ({:lld} samples) of data", vm["duration"].as<double>(), n_samp));

    std::vector<std::complex<int16_t> > data(n_samp);

    for(size_t i = 0; i < n_samp; i++) {
      data[i] = std::complex<int16_t>(10000,0);
    }

    LOG_INFO("sending data...");

    radio->tx_start_now_for_n_samples(n_samp);
    radio->put_tx_data(data, n_samp);
    radio->tx_stop_now();

    LOG_INFO("send complete");

    std::string filename = vm["prefix"].as<std::string>();
    filename.append(make_filename_time(std::chrono::system_clock::now(), vm["freq"].as<double>(), vm["rate"].as<double>()));
    filename.append(vm["suffix"].as<std::string>());

    LOG_INFO(fmt::format("receiving data to file {:s}", filename));

    radio->rx_start_now_for_n_samples(n_samp);
    radio->get_rx_data(data, n_samp);
    radio->rx_stop_now();

    LOG_INFO("receive complete");

    size_t n_written = write_cplx_16(filename, data);
    LOG_INFO(fmt::format("{:d} samples written", n_written));
  }
  catch (std::exception& e) {
    LOG_FATAL(fmt::format("exception caught: {:s}", e.what()));
    return EXIT_INTERNAL_ERROR;
  }

  LOG_INFO("finished");
  return EXIT_SUCCESS;
}
