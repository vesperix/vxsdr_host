#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>

#include "errors.hpp"
#include "logging.hpp"
#include "vxsdr.hpp"

void add_rx_1ch_options(boost::program_options::options_description& desc) {
    // clang-format off
    desc.add_options()
        ("rx_rate", boost::program_options::value<double>(), "RX sample rate in Hz")
        ("rx_freq", boost::program_options::value<double>(), "RX center frequency in Hz")
        ("rx_gain", boost::program_options::value<double>()->default_value(0.0), "RX gain in dB")
        ("rx_ant", boost::program_options::value<std::string>(), "RX antenna input selection")
        ;
    // clang-format on
}

void add_tx_1ch_options(boost::program_options::options_description& desc) {
    // clang-format off
    desc.add_options()
        ("tx_rate", boost::program_options::value<double>(), "TX sample rate in Hz")
        ("tx_freq", boost::program_options::value<double>(), "TX center frequency in Hz")
        ("tx_gain", boost::program_options::value<double>()->default_value(0.0), "TX gain in dB")
        ("tx_ant", boost::program_options::value<std::string>(), "TX antenna output selection")
        ;
    // clang-format on
}

void add_common_options(boost::program_options::options_description& desc) {
    // clang-format off
    desc.add_options()
        ("help", "help message")
        ("config_file", boost::program_options::value<std::string>(), "configuration file name")
        ("prefix", boost::program_options::value<std::string>()->default_value("test-"), "prefix for the output file")
        ("suffix", boost::program_options::value<std::string>()->default_value(".dat"), "suffix for the output file")
        ("duration", boost::program_options::value<double>()->default_value(1.0), "duration in seconds")
        ("no_filename_time", "do not include time in output filenames")
        //("clock_source", boost::program_options::value<std::string>(), "source for frequency reference")
        ("time_source", boost::program_options::value<std::string>()->default_value("host"),
                 "source for time reference (host or pps)")
        ("rate", boost::program_options::value<double>()->required(), "TX/RX sample rate in Hz")
        ("freq", boost::program_options::value<double>()->required(), "TX/RX center frequency in Hz")
        ("verbose_level", boost::program_options::value<int>(),
                "log level (6 = trace, 5 = debug, 4 = info, 3 = warning, 2 = error, 1 = fatal, 0 = none)")
        ("quit_on_error", boost::program_options::value<bool>()->default_value(true), "quit on errors")
        ;
    // clang-format on
}

void add_network_options(boost::program_options::options_description& desc) {
    // clang-format off
    desc.add_options()
        ("local_address", boost::program_options::value<std::string>()->required(), "IPv4 address of local interface")
        ("device_address", boost::program_options::value<std::string>()->required(),
                "IPv4 address of device (including broadcast/multicast)")
        ("netmask", boost::program_options::value<std::string>()->default_value("255.255.255.0"), "IPv4 netmask of local interface")
        ("payload_size", boost::program_options::value<unsigned>()->default_value(8192), "maximum data packet payload size in bytes")
        ("network_send_buffer_bytes", boost::program_options::value<unsigned>()->default_value(262144), "network transmit buffer size in bytes")
        ("network_receive_buffer_bytes", boost::program_options::value<unsigned>()->default_value(8388608), "network receive buffer size in bytes")
        ("tx_data_queue_packets", boost::program_options::value<unsigned>()->default_value(512), "number of packets in the transmit packet queue")
        ("rx_data_queue_packets", boost::program_options::value<unsigned>()->default_value(262144), "number of packets in the receive packet queue")
        ("net_thread_priority", boost::program_options::value<int>()->default_value(1),
                "priority to use for UDP handler threads when realtime priority is used (set to a negative number to not use realtime priority)")
        ("thread_affinity_offset", boost::program_options::value<int>()->default_value(0),
                "offset in CPU number for UDP handler threads when CPU affinity is used (set to a negative number to not use CPU affinity)")
        ("network_bit_rate", boost::program_options::value<double>()->default_value(10e9), "the bit rate of the network interface")
        ;
    // clang-format on
}

void init_vm(boost::program_options::options_description& desc, boost::program_options::variables_map& vm, int argc, char* argv[]) {
    // setup the program options
    try {
        boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
    } catch (std::exception& e) {
        LOG_FATAL(fmt::format("error parsing command line options (store): {:s}", e.what()));
        exit(EXIT_CMD_LINE_ERROR);
    }

    if (vm.count("help") > 0) {
        // just write help to stderr, not to log
        std::cerr << desc << std::endl;
        LOG_INFO("exiting after showing help");
        exit(EXIT_SUCCESS);
    }

    if (vm.count("config_file") > 0) {
        std::ifstream ifs(vm["config_file"].as<std::string>().c_str());
        if (!ifs) {
            LOG_FATAL(fmt::format("Error: cannot open config file {:s}", vm["config_file"].as<std::string>()));
            exit(EXIT_CMD_LINE_ERROR);
        }
        try {
            boost::program_options::store(boost::program_options::parse_config_file(ifs, desc), vm);
            // command line options take precedence over config file
            boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
        } catch (std::exception& e) {
            LOG_FATAL(fmt::format("error parsing command line options (config file): {:s}", e.what()));
            exit(EXIT_CMD_LINE_ERROR);
        }
    }

    try {
        boost::program_options::notify(vm);
    } catch (std::exception& e) {
        LOG_FATAL(fmt::format("error parsing command line options (notify): {:s}", e.what()));
        exit(EXIT_CMD_LINE_ERROR);
    }
}

int set_common_options(boost::program_options::variables_map& vm, std::unique_ptr<vxsdr>& radio) {
    if (vm.count("time_source") > 0) {
        std::string ts_str = vm["time_source"].as<std::string>();
        boost::algorithm::to_lower(ts_str);
        if (ts_str.compare("host") == 0) {
            if (not radio->set_time_now(std::chrono::system_clock::now())) {
                LOG_ERROR("error in set_common_options: set_time_now");
            }
        } else if (ts_str.compare("pps") == 0) {
            constexpr unsigned max_host_clock_error_ms = 200; // cannot be 500 or more!
            auto t_now = std::chrono::system_clock::now();
            auto msecs = std::chrono::time_point_cast<std::chrono::milliseconds>(t_now) -
                         std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::floor<std::chrono::seconds>(t_now));
            std::chrono::time_point<std::chrono::system_clock> t_set;
            if (msecs.count() < 1000 - max_host_clock_error_ms) {
                // set time at next second (i.e. ceil(t_now))
                t_set = std::chrono::ceil<std::chrono::seconds>(t_now);
            } else {
                // too close to second boundary, wait until second after next
                t_set = std::chrono::ceil<std::chrono::seconds>(t_now) + std::chrono::seconds(1);
            }
            // wait until nearly t_set, then send the command
            std::this_thread::sleep_until(t_set - std::chrono::milliseconds(max_host_clock_error_ms));
            if (not radio->set_time_next_pps(std::chrono::time_point_cast<vxsdr::duration>(t_set))) {
                LOG_ERROR("error in set_common_options: set_time_next_pps");
            }
        } else {
            LOG_FATAL(fmt::format("Error: unknown option value for --time_source: {:s} (allowed values are host or pps)",
                     vm["time_source"].as<std::string>()));
            exit(EXIT_CMD_LINE_ERROR);
        }
    }
    if (vm.count("verbose_level") > 0) {
        log_set_level(vm["verbose_level"].as<int>());
    }
    return 0;
}

int set_rx_1ch_options(boost::program_options::variables_map& vm, std::unique_ptr<vxsdr>& radio) {
    if (vm.count("rate") > 0) {
        if (not radio->set_rx_rate(vm["rate"].as<double>())) {
            LOG_ERROR("error in set_rx_1ch_options: set_rx_rate");
        }
        if (vm.count("rx_rate") > 0) {
            LOG_WARN("Global option --rate overrides --rx_rate");
        }
    } else if (vm.count("rx_rate") > 0) {
        if (not radio->set_rx_rate(vm["rx_rate"].as<double>())) {
            LOG_ERROR("error in set_rx_1ch_options: set_rx_rate");
        }
    } else {
        LOG_FATAL("Please specify the global sample rate with --rate or the RX sample rate with --rx_rate");
        exit(EXIT_CMD_LINE_ERROR);
    }

    if (vm.count("freq") > 0) {
        if (not radio->set_rx_freq(vm["freq"].as<double>())) {
            LOG_ERROR("error in set_rx_1ch_options: set_rx_freq");
        }
        if (vm.count("rx_freq") > 0) {
            LOG_WARN("Global option --freq overrides --rx_freq");
        }
    } else if (vm.count("rx_freq") > 0) {
        if (not radio->set_rx_freq(vm["rx_freq"].as<double>())) {
            LOG_ERROR("error in set_rx_1ch_options: set_rx_freq");
        }
    } else {
        LOG_FATAL("Please specify the global center frequency with --freq or the RX center frequency with --rx_freq");
        exit(EXIT_CMD_LINE_ERROR);
    }

    if (vm.count("rx_ant") > 0) {
        bool port_set = false;
        for (unsigned n = 0; n < radio->get_rx_num_ports().value_or(-1); n++) {
            if (radio->get_rx_port_name(n).value_or("") == vm["rx_ant"].as<std::string>()) {
                if (radio->set_rx_port(n)) {
                    port_set = true;
                    break;
                }
            }
        }
        if (not port_set) {
            LOG_ERROR("error in set_rx_1ch_options: set_rx_port");
        }
    }

    if (vm.count("rx_gain") > 0) {
        if (not radio->set_rx_gain(vm["rx_gain"].as<double>())) {
            LOG_ERROR("error in set_rx_1ch_options: set_rx_gain");
        }
    }

    return 0;
}

int set_tx_1ch_options(boost::program_options::variables_map& vm, std::unique_ptr<vxsdr>& radio) {
    if (vm.count("rate") > 0) {
        if (not radio->set_tx_rate(vm["rate"].as<double>())) {
            LOG_ERROR("error in set_tx_1ch_options: set_tx_rate");
        }
        if (vm.count("tx_rate") > 0) {
            LOG_WARN("Global option --rate overrides --tx_rate");
        }
    } else if (vm.count("tx_rate") > 0) {
        if (not radio->set_tx_rate(vm["tx_rate"].as<double>())) {
            LOG_ERROR("error in set_tx_1ch_options: set_tx_rate");
        }
    } else {
        LOG_FATAL("Please specify the global sample rate with --rate or the TX sample rate with --tx_rate");
        exit(EXIT_CMD_LINE_ERROR);
    }

    if (vm.count("freq") > 0) {
        if (not radio->set_tx_freq(vm["freq"].as<double>())) {
            LOG_ERROR("error in set_tx_1ch_options: set_tx_freq");
        }
        if (vm.count("tx_freq") > 0) {
            LOG_WARN("Global option --freq overrides --tx_freq");
        }
    } else if (vm.count("tx_freq") > 0) {
        if (not radio->set_tx_freq(vm["tx_freq"].as<double>())) {
            LOG_ERROR("error in set_tx_1ch_options: set_tx_freq");
        }
    } else {
        LOG_WARN("Please specify the global center frequency with --freq or the TX center frequency with --tx_freq");
        exit(EXIT_CMD_LINE_ERROR);
    }

    if (vm.count("tx_ant") > 0) {
        bool port_set = false;
        for (unsigned n = 0; n < radio->get_tx_num_ports().value_or(-1); n++) {
            if (radio->get_tx_port_name(n).value_or("") == vm["tx_ant"].as<std::string>()) {
                if (radio->set_tx_port(n)) {
                    port_set = true;
                    break;
                }
            }
        }
        if (not port_set) {
            LOG_ERROR("error in set_tx_1ch_options: set_tx_port");
        }
    }

    if (vm.count("tx_gain") > 0) {
        if (not radio->set_tx_gain(vm["tx_gain"].as<double>())) {
            LOG_ERROR("error in set_tx_1ch_options: set_tx_gain");
        }
    }

    return 0;
}
