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
  INIT_LOGGER(argv[0]);
  try {
    LOG_INFO(fmt::format("{:s} started", argv[0]));
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

    LOG_INFO(fmt::format("using frequency {:.5e}", vm["freq"].as<double>()));
    LOG_INFO(fmt::format("using rate      {:.5e}", vm["rate"].as<double>()));
    LOG_INFO(fmt::format("using tx_gain   {:.1f}", vm["tx_gain"].as<double>()));
    LOG_INFO(fmt::format("using rx_gain   {:.1f}", vm["rx_gain"].as<double>()));

    if(vm["duration"].as<double>() <= 0.0) {
      LOG_FATAL("duration must be positive");
      return EXIT_CMD_LINE_ERROR;
    }

    // let radio complete initialization
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    set_common_options(vm, radio);
    set_rx_1ch_options(vm, radio);
    set_tx_1ch_options(vm, radio);

    auto tnow = radio->device_get_time_now();
    if(tnow.has_value()) {
      LOG_INFO(fmt::format("current FPGA time: {:s}", format_time(tnow.value())));
      } else {
        LOG_ERROR("failed to read FPGA time");
      }

    auto tset = std::chrono::system_clock::now();

    LOG_INFO(fmt::format("setting FPGA time to: {:s}", format_time(tset)));

    if(radio->device_set_time_now(tset)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      tnow = radio->device_get_time_now();
      if(tnow.has_value()) {
        LOG_INFO(fmt::format("current FPGA time: {:s}", format_time(tnow.value())));
      } else {
        LOG_ERROR("failed to read FPGA time after set");
      }
    } else {
      LOG_ERROR("failed to set FPGA time");
    }

    size_t n_samp = std::llround(vm["duration"].as<double>() * vm["rate"].as<double>());

    LOG_INFO(fmt::format("preparing {:.3f} seconds ({:d} samples) of data", vm["duration"].as<double>(), n_samp));

    std::vector<std::complex<int16_t> > data(n_samp);

    for(size_t i = 0; i < n_samp; i++) {
      data[i] = std::complex<int16_t>(10000,0);
    }

    LOG_INFO("sending data...");

    if(not radio->tx_start_now_for_n_samples(n_samp)) {
      LOG_ERROR("error in tx_start_now_for_n_samples");
    }
    size_t n_sent = radio->put_tx_data(data, n_samp);
    if(n_sent != n_samp) {
      LOG_ERROR(fmt::format("error in put_tx_data ({:d} of {:d} sent)", n_sent, n_samp));
    }
    if(not radio->tx_stop_now()) {
      LOG_ERROR("error in tx_stop_now");
    }

    LOG_INFO("send complete");

    std::string filename = vm["prefix"].as<std::string>();
    if(vm.count("no_filename_time") > 0) {
      filename.append(make_filename_no_time(vm["freq"].as<double>(), vm["rate"].as<double>()));
    }
    else {
      filename.append(make_filename_time(std::chrono::system_clock::now(), vm["freq"].as<double>(), vm["rate"].as<double>()));
    }
    filename.append(vm["suffix"].as<std::string>());

    LOG_INFO(fmt::format("receiving data to file {:s}", filename));

    if(not radio->rx_start_now_for_n_samples(n_samp)) {
      LOG_ERROR("error in rx_start_now_for_n_samples");
    }
    size_t n_rec = radio->get_rx_data(data, n_samp);
    if(n_rec != n_samp) {
      LOG_ERROR(fmt::format("error in get_rx_data ({:d} of {:d} received)", n_rec, n_samp));
    }
    if(not radio->tx_stop_now()) {
      LOG_ERROR("error in rx_stop_now");
    }

    LOG_INFO("receive complete");

    size_t n_written = write_cplx_16(filename, data);
    LOG_INFO(fmt::format("{:d} samples written", n_written));
  }
  catch (std::exception& e) {
    LOG_FATAL(fmt::format("exception caught: {:s}", e.what()));
    return EXIT_INTERNAL_ERROR;
  }

  LOG_INFO(fmt::format("{:s} finished", argv[0]));
  return EXIT_SUCCESS;
}
