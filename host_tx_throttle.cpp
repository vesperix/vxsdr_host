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
#include "udp_tx_rx.hpp"

void define_options(boost::program_options::options_description &desc)
{
  desc.add_options()
    ("help,H", "show help message")
    ("config_file,C", boost::program_options::value<std::string>(), "configuration file giving command line options")
    ("local_address,A", boost::program_options::value<std::string>()->default_value(""), "IPv4 address of local interface")
    ("local_receive_port,L", boost::program_options::value<unsigned>()->default_value(1030), "port to receive on local interface")
    ("local_send_port,M", boost::program_options::value<unsigned>()->default_value(55123), "port to send on local interface")
    ("destination_address,D", boost::program_options::value<std::string>()->default_value(""), "IPv4 address of destination (including broadcast/multicast)")
    ("netmask,K", boost::program_options::value<std::string>()->default_value("255.255.255.0"), "IPv4 netmask of local interface")
    ("broadcast,B", boost::program_options::bool_switch()->default_value(false), "flag to enable broadcast")
    ("destination_port,P", boost::program_options::value<unsigned>()->default_value(1030), "port to use on destination")
    ("packet_size,S", boost::program_options::value<size_t>()->default_value(1024), "packet size in bytes")
    ("num_packets,N", boost::program_options::value<uint64_t>()->default_value(1024), "number of packets to send")
    ("wait_us,W", boost::program_options::value<unsigned>()->default_value(1000000), "wait between packets in us")
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
    LOGGER(info) << argv[0] << " started";
    signal(SIGINT, &sig_int_handler);
    boost::program_options::options_description desc("Command line options");
    boost::program_options::variables_map vm;

    init_options(desc, vm, argc, argv);
    check_options(vm);

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

    std::thread generator_thread(std::ref(generator), vm["num_packets"].as<uint64_t>(), vm["packet_size"].as<size_t>(), vm["wait_us"].as<unsigned>());

    boost::asio::io_context io_context;

    std::thread packet_counter_thread(std::ref(data_packet_counter), vm["num_packets"].as<uint64_t>(), vm["packet_size"].as<size_t>());
    std::thread receiver_thread(std::ref(receiver), std::ref(io_context), std::cref(local_receive_endpoint));

    std::thread response_logger_thread(std::ref(response_packet_printer));
    std::thread sender_thread(std::ref(sender), std::ref(io_context), std::cref(local_send_endpoint), std::cref(destination_endpoint), vm["broadcast"].as<bool>());

    generator_thread.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    set_tx_stop_flag();
    sender_thread.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    set_rx_stop_flag();
    receiver_thread.join();
    packet_counter_thread.join();
    set_complete_flag();
    response_logger_thread.join();
  }
  catch (std::exception& e) {
    LOGGER(fatal) << "exception caught: " << e.what();
    return EXIT_INTERNAL_ERROR;
  }

  LOGGER(info) << argv[0] << " finished";
  return EXIT_SUCCESS;
}