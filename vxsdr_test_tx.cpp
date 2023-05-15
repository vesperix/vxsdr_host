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

using namespace std::chrono_literals;

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
        add_tx_1ch_options(desc);

        init_vm(desc, vm, argc, argv);

        auto radio = std::make_unique<vxsdr>(vm["local_address"].as<std::string>(), vm["destination_address"].as<std::string>(),
                                    vm["set_priority"].as<bool>(), vm["thread_priority"].as<int>());

        set_common_options(vm, radio);
        set_tx_1ch_options(vm, radio);

        if (vm["duration"].as<double>() <= 0.0) {
            LOG_FATAL("duration must be positive");
            return EXIT_CMD_LINE_ERROR;
        }

        size_t n_samp        = std::llround(vm["duration"].as<double>() * vm["rate"].as<double>());

        std::vector<std::complex<int16_t>> data(n_samp);
        for (size_t i = 0; i < n_samp; i++) {
            data[i] = std::complex<int16_t>(10000, 0);
        }

        size_t tx_buffer_size_samples = 0;
        LOG_DEBUG("getting buffer size");
        auto b1 = radio->get_buffer_info();
        if (b1.has_value()) {
            tx_buffer_size_samples = b1->at(1) / sizeof(std::complex<int16_t>);
        } else {
            LOG_ERROR("unable to get buffer info");
        }

        LOG_DEBUG("getting radio time");
        auto t1 = radio->get_time_now();
        if (t1.has_value()) {
            LOG_INFO(fmt::format("radio time: {:s}", format_time(t1.value())));
        } else {
            LOG_ERROR("unable to get radio time");
        }

        LOG_INFO(fmt::format("using frequency {:.5e}", radio->get_tx_freq().value_or(-1)));
        LOG_INFO(fmt::format("using rate      {:.5e}", radio->get_tx_rate().value_or(-1)));
        LOG_INFO(fmt::format("using tx_gain   {:.1f}", radio->get_tx_gain().value_or(-1)));

        auto t_start = std::chrono::ceil<std::chrono::seconds>(std::chrono::system_clock::now()) + std::chrono::duration(1s);
        LOG_INFO(fmt::format("start time: {:s}", format_time(t_start)));

        if (not radio->tx_start(t_start, n_samp)) {
            LOG_ERROR("tx_start() failed");
        }

        double tx_buffer_duration_s = tx_buffer_size_samples / radio->get_tx_rate().value_or(-1);
        // start early enough to fill the tx buffer half full at start
        unsigned pre_start_ms = std::lround(1000 * tx_buffer_duration_s / 2);
        LOG_INFO(fmt::format("pre start: {:d} ms", pre_start_ms));
        std::this_thread::sleep_until(t_start - std::chrono::duration(std::chrono::milliseconds(pre_start_ms)));

        auto n_sent = radio->put_tx_data(data);
        if (n_sent != n_samp) {
            LOG_ERROR(fmt::format("{:d} of {:d} samples sent", n_sent, n_samp));
        }

        LOG_INFO("transmit complete");
    } catch (std::exception& e) {
        LOG_FATAL(fmt::format("exception caught: {:s}", e.what()));
        return EXIT_INTERNAL_ERROR;
    }

    LOG_INFO(fmt::format("{:s} finished", argv[0]));
    return EXIT_SUCCESS;
}
