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

#define JUST_CHECK_SEQUENCE

int main(int argc, char* argv[]) {
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

        std::vector<int> thread_priority = { 1, 1, 1 };
        std::vector<int> thread_affinity = { 1, 2, 3 };

        auto radio = std::make_unique<vxsdr>(vm["local_address"].as<std::string>(), vm["device_address"].as<std::string>(),
                                             vm["transmit_packet_queue_size"].as<unsigned>(), vm["receive_packet_queue_size"].as<unsigned>(),
                                             vm["transmit_buffer_size"].as<unsigned>(), vm["receive_buffer_size"].as<unsigned>(),
                                             thread_priority, thread_affinity);

        set_common_options(vm, radio);
        set_rx_1ch_options(vm, radio);

        if (vm["duration"].as<double>() <= 0.0) {
            LOG_FATAL("duration must be positive");
            return EXIT_CMD_LINE_ERROR;
        }

        size_t n_samp        = std::llround(vm["duration"].as<double>() * vm["rate"].as<double>());
#ifndef JUST_CHECK_SEQUENCE
        std::string filename = vm["prefix"].as<std::string>();
        if (vm.count("no_filename_time") > 0) {
            filename.append(make_filename_no_time(vm["freq"].as<double>(), vm["rate"].as<double>()));
        } else {
            filename.append(make_filename_time(std::chrono::system_clock::now(), vm["freq"].as<double>(), vm["rate"].as<double>()));
        }
        filename.append(vm["suffix"].as<std::string>());
#endif
        LOG_DEBUG("getting radio time");
        auto t1 = radio->get_time_now();
        if (t1.has_value()) {
            LOG_INFO(fmt::format("radio time: {:s}", format_time(t1.value())));
        } else {
            LOG_ERROR("unable to get radio time");
        }

        LOG_INFO(fmt::format("using frequency {:.5e}", radio->get_rx_freq().value_or(-1)));
        LOG_INFO(fmt::format("using rate      {:.5e}", radio->get_rx_rate().value_or(-1)));
        LOG_INFO(fmt::format("using rx_gain   {:.1f}", radio->get_rx_gain().value_or(-1)));

        size_t n_written = 0;

        if (not radio->rx_start(t1.value(), n_samp)) {
            LOG_ERROR("rx_start() failed");
        }
#ifndef JUST_CHECK_SEQUENCE
        std::ofstream outfile(filename, std::ios::binary);
        n_written = radio->write_rx_data(outfile, n_samp);
        outfile.close();
#else
        n_written = radio->check_rx_data_sequence_only(n_samp);
#endif

        if (not radio->rx_stop_now()) {
            LOG_ERROR("rx_stop_now() failed");
        }

        LOG_INFO("receive complete");
#ifndef JUST_CHECK_SEQUENCE
        LOG_INFO(fmt::format("{:d} samples written", n_written));
#else
        LOG_INFO(fmt::format("{:d} samples checked", n_written));
#endif
    } catch (std::exception& e) {
        LOG_FATAL(fmt::format("exception caught: {:s}", e.what()));
        return EXIT_INTERNAL_ERROR;
    }

    LOG_INFO(fmt::format("{:s} finished", argv[0]));
    return EXIT_SUCCESS;
}
