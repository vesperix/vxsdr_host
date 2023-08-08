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
        add_tx_1ch_options(desc);

        init_vm(desc, vm, argc, argv);

        auto radio = std::make_unique<vxsdr>(vm["local_address"].as<std::string>(), vm["device_address"].as<std::string>(),
                                             vm["transmit_packet_queue_size"].as<unsigned>(), vm["receive_packet_queue_size"].as<unsigned>(),
                                             vm["transmit_buffer_size"].as<unsigned>(), vm["receive_buffer_size"].as<unsigned>(),
                                             vm["thread_priority"].as<int>());

        set_common_options(vm, radio);
        set_rx_1ch_options(vm, radio);

        if (vm["duration"].as<double>() <= 0.0) {
            LOG_FATAL("duration must be positive");
            return EXIT_CMD_LINE_ERROR;
        }

        size_t n_samp        = std::llround(vm["duration"].as<double>() * vm["rate"].as<double>());

        std::vector<std::complex<int16_t>> tx_data(std::llround(vm["rate"].as<double>()));

        LOG_DEBUG("getting radio time");
        auto t1 = radio->get_time_now();
        if (t1.has_value()) {
            LOG_INFO(fmt::format("radio time: {:s}", format_time(t1.value())));
        } else {
            LOG_ERROR("unable to get radio time");
            return EXIT_FAILURE;
        }

        auto start_time = std::chrono::floor<std::chrono::seconds>(t1.value() + vxsdr::duration(2s));
        LOG_INFO(fmt::format("start time: {:s}", format_time(start_time)));

        LOG_INFO(fmt::format("using frequency {:.5e}", radio->get_rx_freq().value_or(-1)));
        LOG_INFO(fmt::format("using rate      {:.5e}", radio->get_rx_rate().value_or(-1)));
        LOG_INFO(fmt::format("using rx_gain   {:.1f}", radio->get_rx_gain().value_or(-1)));

        if (not radio->rx_start(start_time, n_samp)) {
            LOG_ERROR("rx_start() failed");
        }

        if (not radio->tx_start(start_time, n_samp)) {
            LOG_ERROR("tx_start() failed");
        }

        std::this_thread::sleep_until(start_time - std::chrono::duration(10ms));

        size_t n_sent = 0;
        std::thread tx_thread([&] { n_sent = 0;
                                    while (n_sent < n_samp) {
                                        auto n = radio->put_tx_data(tx_data);
                                        n_sent += n;
                                        if (n != tx_data.size()) {
                                            break;
                                        }
                                    }
                                    });

        auto n_checked = radio->check_rx_data_sequence_only(n_samp);

        if (not radio->rx_stop_now()) {
            LOG_ERROR("rx_stop_now() failed");
        }

        LOG_INFO("receive complete");
        LOG_INFO(fmt::format("{:d} samples checked", n_checked));

        LOG_INFO("joining tx thread");
        tx_thread.join();

        if (not radio->tx_stop_now()) {
            LOG_ERROR("tx_stop_now() failed");
        }

        LOG_INFO("transmit complete");
        LOG_INFO(fmt::format("{:d} samples sent", n_sent));


    } catch (std::exception& e) {
        LOG_FATAL(fmt::format("exception caught: {:s}", e.what()));
        return EXIT_INTERNAL_ERROR;
    }

    LOG_INFO(fmt::format("{:s} finished", argv[0]));
    return EXIT_SUCCESS;
}
