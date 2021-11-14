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
  LOG_INIT(argv[0]);
  try {
    LOG_INFO(fmt::format("{:s} started", argv[0]));
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
    LOG_INFO(fmt::format("using local send IP {:s} and port {:d}", local_send_endpoint.address().to_string(), local_send_endpoint.port()));
    boost::asio::ip::udp::endpoint local_receive_endpoint(local_ip, vm["local_receive_port"].as<unsigned>());
    LOG_INFO(fmt::format("using local receive IP {:s} and port {:d}", local_receive_endpoint.address().to_string(), local_receive_endpoint.port()));
    boost::asio::ip::udp::endpoint destination_endpoint(destination_ip, vm["destination_port"].as<unsigned>());
    LOG_INFO(fmt::format("using destination IP {:s} and port {:d}", destination_endpoint.address().to_string(), destination_endpoint.port()));

    auto radio = std::make_unique<vxsdr>(local_send_endpoint, local_receive_endpoint, destination_endpoint);
    // let radio complete initialization
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    set_common_options(vm, radio);
    set_rx_1ch_options(vm, radio);

    if(vm["duration"].as<double>() <= 0.0) {
      LOG_FATAL("duration must be positive");
      return EXIT_CMD_LINE_ERROR;
    }

    size_t n_samp = std::llround(vm["duration"].as<double>() * vm["rate"].as<double>());

    std::string filename = vm["prefix"].as<std::string>();
    if(vm.count("no_filename_time") > 0) {
      filename.append(make_filename_no_time(vm["freq"].as<double>(), vm["rate"].as<double>()));
    }
    else {
      filename.append(make_filename_time(std::chrono::system_clock::now(), vm["freq"].as<double>(), vm["rate"].as<double>()));
    }
    filename.append(vm["suffix"].as<std::string>());

    //std::ofstream outfile(filename, std::ofstream::binary | std::ofstream::out);
    //LOG_INFO(fmt::format("receiving data to file {:s}", filename));

    //radio->rx_start_now_for_n_samples(n_samp);
    //size_t n_written = radio->write_rx_data(outfile, n_samp);
    //radio->rx_stop_now();

    LOG_DEBUG("sleeping for 1 s");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    LOG_DEBUG("getting time once");
    auto t1 = radio->device_get_time_now();
    if(t1.has_value()) {
      LOG_INFO(fmt::format("t1:           {:s}", format_time(t1.value())));
    } else {
      LOG_INFO("t1 has no value");
    }
    LOG_DEBUG("sleeping for 1 s");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    LOG_DEBUG("getting time again");
    auto t2 = radio->device_get_time_now();
    if(t2.has_value()) {
      LOG_INFO(fmt::format("t2:           {:s}", format_time(t2.value())));
    } else {
      LOG_INFO("t2 has no value");
    }

    LOG_INFO(fmt::format("using frequency {:.5e}", radio->get_rx_freq().value_or(-1)));
    LOG_INFO(fmt::format("using rate      {:.5e}", radio->get_rx_rate().value_or(-1)));
    LOG_INFO(fmt::format("using rx_gain   {:.1f}", radio->get_rx_gain().value_or(-1)));

    //radio->rx_start_now_for_n_samples(n_samp);
    radio->rx_start_now_until_stop();
    size_t n_written = radio->check_rx_data_sequence_only(n_samp);
    radio->rx_stop_now();

    LOG_INFO("receive complete");
    //outfile.close();
    //LOG_INFO(fmt::format("{:d} samples written", n_written));
    LOG_INFO(fmt::format("{:d} samples checked", n_written));
  }
  catch (std::exception& e) {
    LOG_FATAL(fmt::format("exception caught: {:s}", e.what()));
    return EXIT_INTERNAL_ERROR;
  }

  LOG_INFO(fmt::format("{:s} finished", argv[0]));
  return EXIT_SUCCESS;
}
