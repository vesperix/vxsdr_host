#include <iostream>
#include <fstream>
#include <csignal>
#include <complex>
#include <string>
#include <chrono>
#include <ctime>
#include <limits>
#include <cstdlib>

#include <boost/version.hpp>
#include <boost/program_options.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <boost/log/trivial.hpp>

#include "udp_rx.hpp"

void init_vm(boost::program_options::options_description &desc, boost::program_options::variables_map &vm, int argc, char *argv[])
{
  // setup the program options
  boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
  boost::program_options::notify(vm);

  if (vm.count("config_file")) {
    std::ifstream ifs(vm["config_file"].as<std::string>().c_str());
    if (!ifs) {
      BOOST_LOG_TRIVIAL(error) << "cannot open config file " << vm["config_file"].as<std::string>();
      exit(3);
    }
    boost::program_options::store(boost::program_options::parse_config_file(ifs, desc), vm);
    boost::program_options::notify(vm);
  }

  if(vm.count("help")) {
    std::cout << desc << std::endl;
    exit(0);
  }
}

extern "C" void sig_int_handler(int) {
  BOOST_LOG_TRIVIAL(fatal) << "terminated by SIGINT (ctrl-C)";
  exit(99);
}

int main(int argc, char *argv[])
{
  BOOST_LOG_TRIVIAL(info) << argv[0] << " started";
  signal(SIGINT, &sig_int_handler);
  boost::program_options::options_description desc("Command line options");

  desc.add_options()
    ("help,H", "show help message")
    ("config_file,C", boost::program_options::value<std::string>(), "configuration file giving command line options")
    ("local_address,A", boost::program_options::value<std::string>()->default_value(""), "IPv4 address of local interface")
    ("local_port,L", boost::program_options::value<unsigned>()->default_value(1030), "port to use on local interface")
    ("multicast_address,M", boost::program_options::value<std::string>()->default_value(""), "IPv4 address of multicast group to join")
    ("packet_size,S", boost::program_options::value<size_t>()->default_value(65536), "packet size in bytes")
    ("num_packets,N", boost::program_options::value<size_t>()->default_value(1024), "number of packets to receive")
    ("output_filename,O", boost::program_options::value<std::string>()->default_value("test.dat"), "filename to save recieved data")
  ;

  boost::program_options::variables_map vm;
  init_vm(desc, vm, argc, argv);

  if(vm["local_address"].as<std::string>().size() == 0) {
    BOOST_LOG_TRIVIAL(fatal) << "local IP address must be specified";
    exit(1);
  }

  boost::asio::ip::address_v4 local_ip = boost::asio::ip::address_v4::from_string(vm["local_address"].as<std::string>());
  boost::asio::ip::udp::endpoint local_endpoint(local_ip, vm["local_port"].as<unsigned>());
  boost::asio::ip::address_v4 multicast_ip;
  bool use_multicast = false;
  if(vm["multicast_address"].as<std::string>().size() > 0) {
    multicast_ip = boost::asio::ip::address_v4::from_string(vm["multicast_address"].as<std::string>());
    if(multicast_ip.is_multicast())
      use_multicast = true;
    else {
      BOOST_LOG_TRIVIAL(fatal) << "Invalid multicast address: " << vm["multicast_address"].as<std::string>();
      exit(2);
    }
  }

  try {
    std::string outfile_name = vm["output_filename"].as<std::string>();
//    boost::thread handler_thread(std::ref(writer), std::cref(outfile_name), vm["num_packets"].as<size_t>());
    boost::thread handler_thread(std::ref(sequence_checker), vm["num_packets"].as<size_t>());
    boost::asio::io_service io_service;
    if(use_multicast) {
      BOOST_LOG_TRIVIAL(info) << "joining multicast group " << vm["multicast_address"].as<std::string>();
      udp_rx server(io_service, local_endpoint, multicast_ip);
      io_service.run();
    }
    else {
      udp_rx server(io_service, local_endpoint);
      io_service.run();
    }
    handler_thread.join();
  }
  catch (std::exception& e) {
    BOOST_LOG_TRIVIAL(error) << "exception caught: " << e.what();
  }

  BOOST_LOG_TRIVIAL(info) << argv[0] << " finished";
}