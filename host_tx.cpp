#include <iostream>
#include <fstream>
#include <csignal>
#include <complex>
#include <string>
#include <thread>
#include <chrono>
#include <ctime>
#include <limits>
#include <cstdlib>

#include <boost/version.hpp>
#include <boost/program_options.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/format.hpp>

#include "udp_tx.hpp"

void init_vm(boost::program_options::options_description &desc, boost::program_options::variables_map &vm, int argc, char *argv[])
{
  // setup the program options
  boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
  boost::program_options::notify(vm);

  if (vm.count("config_file")) {
    std::ifstream ifs(vm["config_file"].as<std::string>().c_str());
    if (!ifs) {
      std::cerr << "Error: cannot open config file " << vm["config_file"].as<std::string>() << "!" << std::endl;
      throw std::runtime_error("Specified config file not found.");
    }
    boost::program_options::store(boost::program_options::parse_config_file(ifs, desc), vm);
    boost::program_options::notify(vm);
  }

  if(vm.count("help")) {
    std::cout << desc << std::endl;
    exit(0);
  }
}

int main(int argc, char *argv[])
{
  boost::program_options::options_description desc("Allowed options");

  desc.add_options()
    ("local_ip_address", boost::program_options::value<std::string>()->default_value(""), "IPv4 address of local interface")
    ("multicast_address", boost::program_options::value<std::string>()->default_value(""), "IPv4 address of multicast group to join")
    ("local_port", boost::program_options::value<unsigned>()->default_value(1030), "port to use on local interface")
    ("destination_ip_address", boost::program_options::value<std::string>()->default_value(""), "IPv4 address of destination SDR")
    ("destination_port", boost::program_options::value<unsigned>()->default_value(1030), "port to receive data at destination SDR")
    ("packet_size", boost::program_options::value<size_t>()->default_value(65536), "packet size in bytes")
  ;

  boost::program_options::variables_map vm;
  init_vm(desc, vm, argc, argv);

  try {
    boost::asio::io_service io_service;
    if(vm["multicast_address"].as<std::string>().size() > 0)
      udp_tx server(io_service, boost::asio::ip::address_v4::from_string(vm["local_ip_address"].as<std::string>()), vm["rx_port"].as<unsigned>(),
                        boost::asio::ip::address_v4::from_string(vm["multicast_address"].as<std::string>()));
    else
      udp_tx server(io_service, boost::asio::ip::address_v4::from_string(vm["local_ip_address"].as<std::string>()), vm["rx_port"].as<unsigned>());
    io_service.run();
  }
  catch (std::exception& e) {
    std::cerr << e.what() << std::endl;
  }

}