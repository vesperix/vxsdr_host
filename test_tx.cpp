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
#include "test_tx_rx.hpp"

void define_options(boost::program_options::options_description &desc)
{
  desc.add_options()
    ("help,H", "show help message")
    ("config_file", boost::program_options::value<std::string>(), "configuration file giving command line options")
    ("local_address", boost::program_options::value<std::string>()->default_value(""), "IPv4 address of local interface")
    ("local_receive_port", boost::program_options::value<unsigned>()->default_value(1030), "port to receive on local interface")
    ("local_send_port", boost::program_options::value<unsigned>()->default_value(55123), "port to send on local interface")
    ("destination_address", boost::program_options::value<std::string>()->default_value(""), "IPv4 address of destination (including broadcast/multicast)")
    ("netmask", boost::program_options::value<std::string>()->default_value("255.255.255.0"), "IPv4 netmask of local interface")
    ("broadcast", boost::program_options::bool_switch()->default_value(false), "flag to enable broadcast")
    ("destination_port", boost::program_options::value<unsigned>()->default_value(1030), "port to use on destination")
    ("packet_size", boost::program_options::value<size_t>()->default_value(8200), "packet size in bytes")
    //("num_packets,N", boost::program_options::value<uint64_t>()->default_value(1024), "number of packets to send")
    ("rate", boost::program_options::value<double>()->default_value(160e6), "TX/RX sample rate in Hz")
    ("freq", boost::program_options::value<double>()->required(), "TX/RX center frequency in Hz")
    ("rx_gain", boost::program_options::value<double>()->default_value(0), "RX gain in dB")
    ("tx_gain", boost::program_options::value<double>()->default_value(0), "TX gain in dB")
    ("duration", boost::program_options::value<double>()->required(), "duration in seconds")
    ("prefix", boost::program_options::value<std::string>()->default_value("test-"), "prefix for the output file")
    ("suffix", boost::program_options::value<std::string>()->default_value(".dat"), "suffix for the output file")
    ("no_filename_time", "do not include time in output filenames")
  ;
}

void check_options( boost::program_options::variables_map &vm)
{
  if(vm["local_address"].as<std::string>().empty()) {
    LOGGER(fatal) << "local IP address must be specified";
    exit(EXIT_CMD_LINE_ERROR);
  }
  if(vm["destination_address"].as<std::string>().empty()) {
    LOGGER(fatal) << "destination IP address must be specified";
    exit(EXIT_CMD_LINE_ERROR);
  }
}

void init_options(boost::program_options::options_description &desc, boost::program_options::variables_map &vm, const int argc, char *argv[])
{
  // define the allowed options
  define_options(desc);
  // setup the program options from the command line
  boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
  boost::program_options::notify(vm);
  // if a config file is specified on the command line, load it
  if (vm.count("config_file") > 0) {
    std::ifstream ifs(vm["config_file"].as<std::string>().c_str());
    if (!ifs) {
      std::cerr << "Error: cannot open config file " << vm["config_file"].as<std::string>() << "!" << std::endl;
      throw std::runtime_error("Specified config file not found.");
    }
    boost::program_options::store(boost::program_options::parse_config_file(ifs, desc), vm);
    boost::program_options::notify(vm);
  }
  // now reload the command line options so that they take precedence over the config file
  boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
  boost::program_options::notify(vm);
  // if --help was specified, show help and exit
  if(vm.count("help") > 0) {
    std::cout << desc << std::endl;
    exit(EXIT_SUCCESS);
  }
}

extern "C" void sig_int_handler(int sig_num) {
  LOGGER(fatal) << "terminated by SIGINT (ctrl-C)";
  exit(EXIT_USER_TERMINATED);
}

int main(int argc, char *argv[])
{
  INIT_LOGGER();
  try {
    LOGGER(debug) << argv[0] << " started";
    signal(SIGINT, &sig_int_handler);
    boost::program_options::options_description desc("Command line options");
    boost::program_options::variables_map vm;

    init_options(desc, vm, argc, argv);
    check_options(vm);

    std::string filename = vm["prefix"].as<std::string>();
    if(vm.count("no_filename_time"))
      filename.append(make_filename_no_time(vm["freq"].as<double>(), vm["rate"].as<double>()));
    else
      filename.append(make_filename_time(std::chrono::system_clock::now(), vm["freq"].as<double>(), vm["rate"].as<double>()));
    filename.append(vm["suffix"].as<std::string>());

    //FIXME!!!
    global_kludge_rx_freq = vm["freq"].as<double>();
    global_kludge_tx_freq = vm["freq"].as<double>();

    global_kludge_rx_gain = vm["rx_gain"].as<double>();
    global_kludge_tx_gain = vm["tx_gain"].as<double>();

    global_kludge_rx_rate = vm["rate"].as<double>();
    global_kludge_tx_rate = vm["rate"].as<double>();

    LOGGER(info) << "Output filename: " << filename;

    boost::asio::ip::address_v4 local_ip = boost::asio::ip::address_v4::from_string(vm["local_address"].as<std::string>());
    boost::asio::ip::address_v4 destination_ip = boost::asio::ip::address_v4::from_string(vm["destination_address"].as<std::string>());
    //boost::asio::ip::address_v4 netmask = boost::asio::ip::address_v4::from_string(vm["netmask"].as<std::string>());
    //boost::asio::ip::address_v4 broadcast_ip = boost::asio::ip::address_v4::broadcast(local_ip, netmask);
    boost::asio::ip::udp::endpoint local_send_endpoint(local_ip, vm["local_send_port"].as<unsigned>());
    LOGGER(info) << "using local send IP " << local_send_endpoint.address().to_string() << " and port " << local_send_endpoint.port();
    boost::asio::ip::udp::endpoint local_receive_endpoint(local_ip, vm["local_receive_port"].as<unsigned>());
    LOGGER(info) << "using local receive IP " << local_receive_endpoint.address().to_string() << " and port " << local_receive_endpoint.port();
    boost::asio::ip::udp::endpoint destination_endpoint(destination_ip, vm["destination_port"].as<unsigned>());
    LOGGER(info) << "using destination IP " << destination_endpoint.address().to_string() << " and port " << destination_endpoint.port();

    boost::asio::io_context io_context;

    uint64_t num_packets = std::llround(vm["rate"].as<double>() * vm["duration"].as<double>() / 2048);
    int ncycles = 1000;
    double a = 16000;
    std::thread generator_thread(std::ref(generator), num_packets, vm["packet_size"].as<size_t>(), a, ncycles);
    std::thread counter_thread(std::ref(data_packet_counter), num_packets);
    std::thread receiver_thread(std::ref(receiver), std::ref(io_context), std::cref(local_receive_endpoint));
    std::thread response_logger_thread(std::ref(response_packet_printer));
    std::thread sender_thread(std::ref(sender), std::ref(io_context), std::cref(local_send_endpoint), std::cref(destination_endpoint), true, false);

    // allow time for the packets in tx queue to be sent
    std::this_thread::sleep_for(std::chrono::milliseconds(std::llround(1000 * vm["duration"].as<double>() + 100)));
    generator_thread.join();
    set_tx_stop_flag();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    set_rx_stop_flag();
    receiver_thread.join();
    sender_thread.join();
    counter_thread.join();
    set_complete_flag();
    response_logger_thread.join();
  }
  catch (std::exception& e) {
    LOGGER(fatal) << "exception caught: " << e.what();
    return EXIT_INTERNAL_ERROR;
  }

  LOGGER(debug) << argv[0] << " finished";
  return EXIT_SUCCESS;
}