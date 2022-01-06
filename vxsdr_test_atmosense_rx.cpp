#include <chrono>
#include <complex>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <thread>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/program_options.hpp>
#include <boost/version.hpp>
#include <vxsdr.hpp>

#include "errors.hpp"
#include "logging.hpp"
#include "host_radio_options.hpp"
#include "utility.hpp"

extern "C" void sig_int_handler(int /* sig_num */) {
    LOG_FATAL("terminated by SIGINT (ctrl-C)");
    exit(EXIT_USER_TERMINATED);
}

void add_custom_common_options(boost::program_options::options_description& desc) {
    desc.add_options()
        //clang-format off
        ("help", "help message")("config_file", boost::program_options::value<std::string>(), "configuration file name")
        ("prefix", boost::program_options::value<std::string>()->default_value("test-"), "prefix for the output files")
#ifdef USE_FLAC
        ("suffix", boost::program_options::value<std::string>()->default_value(".flac"), "suffix for the output files")
#else
        ("suffix", boost::program_options::value<std::string>()->default_value(".dat"), "suffix for the output files")
#endif  // USE_FLAC
        ("duration", boost::program_options::value<double>()->default_value(1.0), "duration in seconds")
        ("no_filename_time", "do not include time in output filenames")
        ("clock_source", boost::program_options::value<std::string>()->default_value("internal"), "source for frequency reference")
        ("time_source", boost::program_options::value<std::string>()->default_value("host"), "source for PPS reference")
        ("startup_delay", boost::program_options::value<unsigned>()->default_value(3), "startup delay in seconds")
        ("rate", boost::program_options::value<double>()->default_value(80e6), "RX sample rate in Hz")
        ("demod_freqs", boost::program_options::value<std::string>()->required(),
                "comma-separated list of demodulation frequencies in Hz")
        ("filename_format", boost::program_options::value<std::string>()->default_value("{:.4e}-khz-ch-{:02d}"),
                "fmt::format filename format with entries for demod frequency and channel number")
        ("cic_r", boost::program_options::value<unsigned>()->default_value(125), "CIC filter rate reduction factor")
        ("verbose", boost::program_options::value<int>(),
                "log level (6 = trace, 5 = debug, 4 = info, 3 = warning, 2 = error, 1 = fatal, 0 = none)")
        ("quit_on_error", boost::program_options::value<bool>()->default_value(true), "quit on errors")
        ("set_affinity", boost::program_options::value<bool>()->default_value(false),
                 "set CPU affinity for UDP handler threads")
        ("affinity_base_cpu", boost::program_options::value<unsigned>()->default_value(0),
                "lowest CPU number to use for UDP handler threads when affinity is used")
        ("set_priority", boost::program_options::value<bool>()->default_value(true),
                "set realtime priority for UDP handler threads")
        ("thread_priority", boost::program_options::value<int>()->default_value(5),
                "priority to use for UDP handler threads when affinity is used")
        //clang-format on
        ;
}

int main(int argc, char* argv[]) {
    LOG_INIT(argv[0]);
    try {
        LOG_INFO(fmt::format("{:s} started", argv[0]));
        signal(SIGINT, &sig_int_handler);
        boost::program_options::options_description desc("Command line options");
        boost::program_options::variables_map vm;

        add_custom_common_options(desc);
        add_network_options(desc);

        init_vm(desc, vm, argc, argv);

        net::ip::address_v4 local_ip       = net::ip::address_v4::from_string(vm["local_address"].as<std::string>());
        net::ip::address_v4 destination_ip = net::ip::address_v4::from_string(vm["destination_address"].as<std::string>());
        net::ip::udp::endpoint local_send_endpoint(local_ip, vm["local_send_port"].as<unsigned>());
        LOG_INFO(fmt::format("using local send IP {:s} and port {:d}", local_send_endpoint.address().to_string(),
                             local_send_endpoint.port()));
        net::ip::udp::endpoint local_receive_endpoint(local_ip, vm["local_receive_port"].as<unsigned>());
        LOG_INFO(fmt::format("using local receive IP {:s} and port {:d}", local_receive_endpoint.address().to_string(),
                             local_receive_endpoint.port()));
        net::ip::udp::endpoint destination_endpoint(destination_ip, vm["destination_port"].as<unsigned>());
        LOG_INFO(fmt::format("using destination IP {:s} and port {:d}", destination_endpoint.address().to_string(),
                             destination_endpoint.port()));

        std::vector<double> demod_freqs;
        std::stringstream ss(vm["demod_freqs"].as<std::string>());
        while (ss.good()) {
            std::string substr;
            std::getline(ss, substr, ',');
            demod_freqs.push_back(std::stod(substr));
        }

        if (demod_freqs.empty()) {
            LOG_FATAL("at least one demod_freq must be provided");
            LOG_CLOSE();
            return EXIT_CMD_LINE_ERROR;
        }

        for (double demod_freq : demod_freqs) {
            if (demod_freq <= 0.0 or demod_freq >= 0.5 * vm["rate"].as<double>()) {
                LOG_FATAL("demod_freqs must be positive and less than f_s / 2");
                LOG_CLOSE();
                return EXIT_CMD_LINE_ERROR;
            }
        }

        if (vm["duration"].as<double>() <= 0.0) {
            LOG_FATAL("duration must be positive");
            return EXIT_CMD_LINE_ERROR;
        }

        auto radio = std::make_unique<vxsdr>(vm["local_address"].as<std::string>(), vm["destination_address"].as<std::string>(),
                                    vm["set_priority"].as<bool>(), vm["thread_priority"].as<int>());
        // let radio complete initialization
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        set_common_options(vm, radio);

        LOG_INFO(fmt::format("setting sampling rate {:.5e}", vm["rate"].as<double>()));

        radio->set_rx_rate(vm["rate"].as<double>());
        auto radio_rate = radio->get_rx_rate();
        if (not radio_rate) {
            LOG_FATAL("failed to get radio sample rate");
            throw std::runtime_error("failed to get radio sample rate");
        }
        LOG_INFO(fmt::format("using sampling rate {:.5e}", radio_rate.value()));
        LOG_INFO(fmt::format("using demodulation frequencies:"));
        for (auto&& f : demod_freqs) {
            LOG_INFO(fmt::format("    {:e}", f));
        }
        auto t_start = std::chrono::ceil<std::chrono::seconds>(std::chrono::system_clock::now() +
                                                               std::chrono::seconds(vm["startup_delay"].as<unsigned>()));
        auto t_wake  = t_start + std::chrono::milliseconds(std::lround(1e3 * vm["duration"].as<double>() + 100));

        LOG_INFO(fmt::format("current time: {:s}", format_time(std::chrono::system_clock::now())));
        auto radio_time = radio->device_get_time_now();
        if (not radio_time) {
            LOG_FATAL("failed to get radio time");
            throw std::runtime_error("failed to get radio time");
        }

        LOG_INFO(fmt::format("radio time:   {:s}", format_time(radio_time.value())));
        LOG_INFO(fmt::format("start time:   {:s}", format_time(t_start)));
        LOG_INFO(fmt::format("wake time:    {:s}", format_time(t_wake)));

        size_t n_samp = std::llround(vm["duration"].as<double>() * vm["rate"].as<double>());

        radio->rx_start_at_time_for_n_samples(t_start, n_samp);
        radio->check_rx_data_sequence_only(n_samp);

        LOG_INFO("receive complete");
    } catch (std::exception& e) {
        const unsigned cleanup_wait_ms = 1000;
        LOG_FATAL(fmt::format("exception caught: {:s}", e.what()));
        LOG_FATAL(fmt::format("waiting {:d} milliseconds for cleanup", cleanup_wait_ms));
        std::this_thread::sleep_for(std::chrono::milliseconds(cleanup_wait_ms));
        LOG_FATAL(fmt::format("{:s} finished with exception", argv[0]));
        LOG_CLOSE();
        return EXIT_INTERNAL_ERROR;
    }

    LOG_INFO(fmt::format("{:s} finished", argv[0]));
    LOG_CLOSE();
    return EXIT_SUCCESS;
}
