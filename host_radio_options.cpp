#include <fstream>
#include <iostream>

#include <boost/program_options.hpp>

#include "logging.hpp"
#include "errors.hpp"
#include "vxsdr.hpp"

void add_rx_1ch_options(boost::program_options::options_description &desc)
{
  desc.add_options()
    ("rx_rate", boost::program_options::value<double>(), "RX sample rate in Hz")
    ("rx_freq", boost::program_options::value<double>(), "RX center frequency in Hz")
    ("rx_gain", boost::program_options::value<double>()->default_value(0.0), "RX gain in dB")
    ("rx_ant", boost::program_options::value<std::string>(), "RX antenna input selection")
  ;
}

void add_tx_1ch_options(boost::program_options::options_description &desc)
{
  desc.add_options()
    ("tx_rate", boost::program_options::value<double>(), "TX sample rate in Hz")
    ("tx_freq", boost::program_options::value<double>(), "TX center frequency in Hz")
    ("tx_gain", boost::program_options::value<double>()->default_value(0.0), "TX gain in dB")
    ("tx_ant", boost::program_options::value<std::string>(), "TX antenna input selection")
  ;
}

void add_common_options(boost::program_options::options_description &desc)
{
  desc.add_options()
    ("help", "help message")
    ("config_file", boost::program_options::value<std::string>(), "configuration file name")
    ("prefix", boost::program_options::value<std::string>()->default_value("test-"), "prefix for the output file")
    ("suffix", boost::program_options::value<std::string>()->default_value(".dat"), "suffix for the output file")
    ("duration", boost::program_options::value<double>()->default_value(1.0), "duration in seconds")
    ("no_filename_time", "do not include time in output filenames")
    ("clock_source", boost::program_options::value<std::string>(), "source for frequency reference")
    ("time_source", boost::program_options::value<std::string>(), "source for PPS reference")
    ("rate", boost::program_options::value<double>()->required(), "TX/RX sample rate in Hz")
    ("freq", boost::program_options::value<double>()->required(), "TX/RX center frequency in Hz")
    ("verbose_level", boost::program_options::value<int>(), "log level (6 = trace, 5 = debug, 4 = info, 3 = warning, 2 = error, 1 = fatal, 0 = none)")
    ("quit_on_error", boost::program_options::value<bool>()->default_value(true), "quit on errors")
  ;
}

void add_network_options(boost::program_options::options_description &desc)
{
  desc.add_options()
    ("local_address", boost::program_options::value<std::string>()->required(), "IPv4 address of local interface")
    ("local_receive_port", boost::program_options::value<unsigned>()->default_value(1030), "port to receive on local interface")
    ("local_send_port", boost::program_options::value<unsigned>()->default_value(55123), "port to send on local interface")
    ("destination_address", boost::program_options::value<std::string>()->required(), "IPv4 address of destination (including broadcast/multicast)")
    ("netmask", boost::program_options::value<std::string>()->default_value("255.255.255.0"), "IPv4 netmask of local interface")
    ("broadcast", boost::program_options::bool_switch()->default_value(false), "flag to enable broadcast")
    ("destination_port", boost::program_options::value<unsigned>()->default_value(1030), "port to use on destination")
    ("packet_size", boost::program_options::value<size_t>()->default_value(4096), "packet size in bytes")
  ;
}

void init_vm(boost::program_options::options_description &desc, boost::program_options::variables_map &vm, int argc, char *argv[])
{
  // setup the program options
  boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);

  if(vm.count("help") > 0) {
    // just write help to stderr, not to log
    std::cerr << desc << std::endl;
    exit(EXIT_SUCCESS);
  }

  boost::program_options::notify(vm);

  if (vm.count("config_file") > 0) {
    std::ifstream ifs(vm["config_file"].as<std::string>().c_str());
    if (!ifs) {
      LOG_FATAL(fmt::format("Error: cannot open config file {:s}", vm["config_file"].as<std::string>()));
      throw std::runtime_error("Specified config file not found.");
    }
    boost::program_options::store(boost::program_options::parse_config_file(ifs, desc), vm);
    boost::program_options::notify(vm);
  }
}

int set_common_options(boost::program_options::variables_map &vm, std::unique_ptr<vxsdr> &radio)
{
  if(vm.count("verbose_level") > 0) {
    log_set_level(vm["verbose_level"].as<int>());
  }
  return 0;
}

int set_rx_1ch_options(boost::program_options::variables_map &vm, std::unique_ptr<vxsdr> &radio)
{
  if(vm.count("rate") > 0) {
    radio->set_rx_rate(vm["rate"].as<double>());
    if(vm.count("rx_rate") > 0) {
      LOG_WARN("Global option --rate overrides --rx_rate");
    }
  }
  else if(vm.count("rx_rate") > 0) {
    radio->set_rx_rate(vm["rx_rate"].as<double>());
  }
  else {
    LOG_FATAL("Please specify the global sample rate with --rate or the RX sample rate with --rx_rate");
    exit(EXIT_CMD_LINE_ERROR);
  }

  if(vm.count("freq") > 0) {
    radio->set_rx_freq(vm["freq"].as<double>());
    if(vm.count("rx_freq") > 0) {
      LOG_WARN("Global option --freq overrides --rx_freq");
    }
  }
  else if(vm.count("rx_freq") > 0) {
    radio->set_rx_freq(vm["rx_freq"].as<double>());
  }
  else {
    LOG_FATAL("Please specify the global center frequency with --freq or the RX center frequency with --rx_freq");
    exit(EXIT_CMD_LINE_ERROR);
  }

  if (vm.count("rx_ant") > 0) {
    radio->set_rx_antenna(vm["rx_ant"].as<std::string>());
  }

  if (vm.count("rx_gain") > 0) {
    radio->set_rx_gain(vm["rx_gain"].as<double>());
  }

  return 0;
}

int set_tx_1ch_options(boost::program_options::variables_map &vm, std::unique_ptr<vxsdr> &radio)
{
  if(vm.count("rate") > 0) {
    radio->set_tx_rate(vm["rate"].as<double>());
    if(vm.count("tx_rate") > 0) {
      LOG_WARN("Global option --rate overrides --tx_rate");
    }
  }
  else if(vm.count("tx_rate") > 0) {
    radio->set_tx_rate(vm["tx_rate"].as<double>());
  }
  else {
    LOG_FATAL("Please specify the global sample rate with --rate or the TX sample rate with --tx_rate");
    exit(EXIT_CMD_LINE_ERROR);
  }

  if(vm.count("freq") > 0) {
    radio->set_tx_freq(vm["freq"].as<double>());
    if(vm.count("tx_freq") > 0) {
      LOG_WARN("Global option --freq overrides --tx_freq");
    }
  }
  else if(vm.count("tx_freq") > 0) {
    radio->set_tx_freq(vm["tx_freq"].as<double>());
  }
  else {
    LOG_WARN("Please specify the global center frequency with --freq or the TX center frequency with --tx_freq");
    exit(EXIT_CMD_LINE_ERROR);
  }

  if(vm.count("tx_ant") > 0) {
    radio->set_tx_antenna(vm["tx_ant"].as<std::string>());
  }

  if(vm.count("tx_gain") > 0) {
    radio->set_tx_gain(vm["tx_gain"].as<double>());
  }

  return 0;
}

int set_time_options(boost::program_options::variables_map &vm, std::unique_ptr<vxsdr> &radio)
{
  /*
  if(vm.count("time_source") > 0) {
    radio->set_time_source(vm["clock_source"].as<string>());
  }
  else if(vm.count("no_ext_time") > 0) {
    radio->set_no_time_ref();
  }
  else {
    radio->set_external_time_ref();
  }

  if(vm.count("clock_source") > 0) {
    radio->set_clock_source(vm["clock_source"].as<string>());
    if(vm.count("no_ext_ref") > 0)
      LOG_WARN(<< "Both clock_source and no_ext_ref specified; clock_source takes precedence");
  }
  else if(vm.count("no_ext_ref") > 0) {
    radio->set_internal_freq_ref();
  }
  else {
    radio->set_external_freq_ref();
  }

  radio->check_reference_lock();

  radio->check_lo_lock();

  if(vm.count("no_time_synch") == 0 and vm.count("no_ext_time") == 0) {
    radio->synch_external_time_ref(0, true);
  }
  else {
    radio->synch_system_time();
  }
  */
  return 0;
}
