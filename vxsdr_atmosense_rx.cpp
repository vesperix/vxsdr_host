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

#include "vxsdr_at_atmosense.hpp"
#include "host_radio_options.hpp"

extern "C" void sig_int_handler(int /* sig_num */) {
  LOG_FATAL("terminated by SIGINT (ctrl-C)");
  exit(EXIT_USER_TERMINATED);
}

void add_custom_common_options(boost::program_options::options_description &desc)
{
  desc.add_options()
    ("help", "help message")
    ("config_file", boost::program_options::value<std::string>(), "configuration file name")
    ("prefix", boost::program_options::value<std::string>()->default_value("test-"), "prefix for the output files")
    ("suffix", boost::program_options::value<std::string>()->default_value(".dat"), "suffix for the output files")
    ("duration", boost::program_options::value<double>()->default_value(1.0), "duration in seconds")
    ("no_filename_time", "do not include time in output filenames")
    ("clock_source", boost::program_options::value<std::string>()->default_value("internal"), "source for frequency reference")
    ("time_source", boost::program_options::value<std::string>()->default_value("system"), "source for PPS reference")
    ("startup_delay", boost::program_options::value<unsigned>()->default_value(3), "startup delay in seconds")
    ("rate", boost::program_options::value<double>()->default_value(80e6), "RX sample rate in Hz")
    ("demod_freqs", boost::program_options::value<std::string>()->required(),
            "comma-separated list of demodulation frequencies in Hz")
    ("filename_format", boost::program_options::value<std::string>()->default_value("{:.4e}-{:02d}"),
            "fmt::format filename format with entries for demod frequency and channel number")
    ("cic_r", boost::program_options::value<unsigned>()->default_value(125), "CIC filter rate reduction factor")
    ("verbose_level", boost::program_options::value<int>(),
            "log level (6 = trace, 5 = debug, 4 = info, 3 = warning, 2 = error, 1 = fatal, 0 = none)")
    ("quit_on_error", boost::program_options::value<bool>()->default_value(true), "quit on errors")
  ;
}

int main(int argc, char *argv[])
{
  INIT_LOGGER(argv[0]);
  try {
    LOG_INFO(fmt::format("{:s} started", argv[0]));
    signal(SIGINT, &sig_int_handler);
    boost::program_options::options_description desc("Command line options");
    boost::program_options::variables_map vm;

    add_custom_common_options(desc);
    add_network_options(desc);

    init_vm(desc, vm, argc, argv);

    boost::asio::ip::address_v4 local_ip = boost::asio::ip::address_v4::from_string(vm["local_address"].as<std::string>());
    boost::asio::ip::address_v4 destination_ip = boost::asio::ip::address_v4::from_string(vm["destination_address"].as<std::string>());
    boost::asio::ip::udp::endpoint local_send_endpoint(local_ip, vm["local_send_port"].as<unsigned>());
    LOG_INFO(fmt::format("using local send IP {:s} and port {:d}", local_send_endpoint.address().to_string(), local_send_endpoint.port()));
    boost::asio::ip::udp::endpoint local_receive_endpoint(local_ip, vm["local_receive_port"].as<unsigned>());
    LOG_INFO(fmt::format("using local receive IP {:s} and port {:d}", local_receive_endpoint.address().to_string(), local_receive_endpoint.port()));
    boost::asio::ip::udp::endpoint destination_endpoint(destination_ip, vm["destination_port"].as<unsigned>());
    LOG_INFO(fmt::format("using destination IP {:s} and port {:d}", destination_endpoint.address().to_string(), destination_endpoint.port()));

    std::vector<double> demod_freqs;
    std::stringstream ss(vm["demod_freqs"].as<std::string>());
    while(ss.good()) {
      std::string substr;
      std::getline(ss, substr, ',');
      demod_freqs.push_back(std::stod(substr));
    }

    if(demod_freqs.size() < 1) {
      LOG_FATAL("at least one demod_freq must be provided");
      return EXIT_CMD_LINE_ERROR;
    }

    for(unsigned i = 0; i < demod_freqs.size(); i++) {
      if(demod_freqs[i] <= 0.0 or demod_freqs[i] >= 0.5 * vm["rate"].as<double>()) {
        LOG_FATAL("demod_freqs must be positive and less than f_s / 2");
        return EXIT_CMD_LINE_ERROR;
      }
    }

    LOG_INFO(fmt::format("using sampling rate {:.5e}", vm["rate"].as<double>()));
    LOG_INFO(fmt::format("using demodulation frequencies:"));
    for(auto&& f : demod_freqs) {
      LOG_INFO(fmt::format("    {:e}", f));
    }
    if(vm["duration"].as<double>() <= 0.0) {
      LOG_FATAL("duration must be positive");
      return EXIT_CMD_LINE_ERROR;
    }

    auto radio = std::make_unique<vxsdr>(local_send_endpoint, local_receive_endpoint, destination_endpoint);
    // let radio complete initialization
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    set_common_options(vm, radio);
    radio->set_rx_rate(vm["rate"].as<double>());

    auto t_start = std::chrono::ceil<std::chrono::seconds>(std::chrono::system_clock::now() + std::chrono::seconds(vm["startup_delay"].as<unsigned>()));

    size_t n_samp = std::llround(vm["duration"].as<double>() * vm["rate"].as<double>());

    std::string fileformat = vm["prefix"].as<std::string>();
    if(vm.count("no_filename_time") == 0) {
      fileformat.append(format_time(t_start, 0, true, "%Y-%m-%d-%H.%M.%S-"));
    }
    fileformat.append(vm["filename_format"].as<std::string>());
    fileformat.append(vm["suffix"].as<std::string>());

    //radio->rx_start_at_time_for_n_samples(t_start, n_samp);

    radio->rx_start_now_until_stop();
    radio->check_rx_data_sequence_only(n_samp);
    radio->rx_stop_now();

    LOG_INFO("receive complete");
  }
  catch (std::exception& e) {
    LOG_FATAL(fmt::format("exception caught: {:s}", e.what()));
    return EXIT_INTERNAL_ERROR;
  }

  LOG_INFO(fmt::format("{:s} finished", argv[0]));
  return EXIT_SUCCESS;
}
