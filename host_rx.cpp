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
#include "udp_rx.hpp"

void define_options(boost::program_options::options_description &desc)
{
  desc.add_options()
    ("help,H", "show help message")
    ("config_file,C", boost::program_options::value<std::string>(), "configuration file giving command line options")
    ("local_address,A", boost::program_options::value<std::string>()->default_value(""), "IPv4 address of local interface")
    ("local_port,L", boost::program_options::value<unsigned>()->default_value(1030), "port to use on local interface")
    ("multicast_address,M", boost::program_options::value<std::string>()->default_value(""), "IPv4 address of multicast group to join")
    ("broadcast_netmask,B", boost::program_options::value<std::string>()->default_value(""), "enable broadcast reception with this IPv4 netmask")
    ("packet_size,S", boost::program_options::value<size_t>()->default_value(65536), "packet size in bytes")
    ("num_packets,N", boost::program_options::value<size_t>()->default_value(1024), "number of packets to receive")
    ("output_filename,O", boost::program_options::value<std::string>()->default_value("test.dat"), "filename to save recieved data")
  ;
}

void check_options(boost::program_options::variables_map &vm)
{
  if(vm["local_address"].as<std::string>().empty()) {
    LOGGER(fatal) << "local IP address must be specified";
    exit(EXIT_CMD_LINE_ERROR);
  }
  if((not vm["multicast_address"].as<std::string>().empty()) and (not vm["broadcast_netmask"].as<std::string>().empty())) {
    LOGGER(fatal) << "Cannot specify both multicast and broadcast";
    exit(EXIT_CMD_LINE_ERROR);
  }
  if(not vm["multicast_address"].as<std::string>().empty()) {
    boost::asio::ip::address_v4 multicast_ip = boost::asio::ip::address_v4::from_string(vm["multicast_address"].as<std::string>());
    if(!multicast_ip.is_multicast()) {
      LOGGER(fatal) << "Invalid multicast address: " << vm["multicast_address"].as<std::string>();
      exit(EXIT_CMD_LINE_ERROR);
    }
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
    std::cerr << desc << std::endl;
    exit(EXIT_SUCCESS);
  }
}

extern "C" void sig_int_handler(int sig_num) {
  LOGGER(fatal) << "terminated by SIGINT (ctrl-C)";
  exit(99);
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
    boost::asio::ip::udp::endpoint local_endpoint(local_ip, vm["local_port"].as<unsigned>());

    boost::asio::ip::address_v4 multicast_ip;
    bool use_multicast = false;
    if(not vm["multicast_address"].as<std::string>().empty()) {
      multicast_ip = boost::asio::ip::address_v4::from_string(vm["multicast_address"].as<std::string>());
      use_multicast = true;
    }
    boost::asio::ip::address_v4 broadcast_mask;
    bool use_broadcast = false;
    if(not vm["broadcast_netmask"].as<std::string>().empty()) {
      broadcast_mask = boost::asio::ip::address_v4::from_string(vm["broadcast_netmask"].as<std::string>());
      use_broadcast = true;
    }

    std::string outfile_name = vm["output_filename"].as<std::string>();
    std::thread handler_thread(std::ref(writer), std::cref(outfile_name), vm["num_packets"].as<size_t>());
  //  std::thread handler_thread(std::ref(sequence_timer), vm["num_packets"].as<size_t>(), vm["packet_size"].as<size_t>());
    if(use_multicast) {
      std::thread multicast_receiver_thread(std::ref(multicast_receiver), std::cref(local_endpoint), std::cref(multicast_ip));
      multicast_receiver_thread.join();
    }
    else if(use_broadcast) {
      std::thread broadcast_thread(std::ref(broadcast_receiver), std::cref(local_endpoint), std::cref(broadcast_mask));
      broadcast_thread.join();
    }
    else {
      std::thread receiver_thread(std::ref(receiver), std::cref(local_endpoint));
      receiver_thread.join();
    }
    handler_thread.join();
  }
  catch (std::exception& e) {
    LOGGER(fatal) << "exception caught: " << e.what();
    return EXIT_INTERNAL_ERROR;
  }

  LOGGER(info) << argv[0] << " finished";
  return EXIT_SUCCESS;
}