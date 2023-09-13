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
#include <cmath>
#include <numbers>

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

void add_modulation_options(boost::program_options::options_description& desc) {
    // clang-format off
    desc.add_options()
        ("modulation_amplitude", boost::program_options::value<double>()->default_value(0.1), "modulation amplitude as fraction of full scale")
        ("modulation_frequency", boost::program_options::value<double>()->default_value(10e6), "modulation frequency in Hz (will be rounded to integer to avoid jumps)")
        ;
    // clang-format on
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
        add_modulation_options(desc);

        desc.add_options()
        ("amplitude", boost::program_options::value<double>()->default_value(10'000.0), "signal amplitude in digital counts")
        ("cycles", boost::program_options::value<double>()->default_value(0.0), "number of cycles in byffer")
        ;

        init_vm(desc, vm, argc, argv);

        std::map<std::string, int64_t> settings = {
            {"tx_data_queue_packets",        vm["tx_data_queue_packets"].as<unsigned>()},
            {"rx_data_queue_packets",        vm["rx_data_queue_packets"].as<unsigned>()},
            {"network_send_buffer_bytes",    vm["network_send_buffer_bytes"].as<unsigned>()},
            {"network_receive_buffer_bytes", vm["network_receive_buffer_bytes"].as<unsigned>()},
            {"net_thread_priority",          vm["net_thread_priority"].as<int>()},
            {"thread_affinity_offset",       vm["thread_affinity_offset"].as<int>()}
        };

        auto radio = std::make_unique<vxsdr>(vm["local_address"].as<std::string>(), vm["device_address"].as<std::string>(), settings);

        set_common_options(vm, radio);
        set_tx_1ch_options(vm, radio);

        if (vm["duration"].as<double>() <= 0.0) {
            LOG_FATAL("duration must be positive");
            return EXIT_CMD_LINE_ERROR;
        }

        size_t n_samp    = std::llround(vm["duration"].as<double>() * vm["rate"].as<double>());
        size_t n_sec     = (size_t)std::llround(vm["rate"].as<double>());
        size_t n_block   = std::min(n_sec, n_samp);
        int64_t n_cycles = std::llround(vm["cycles"].as<double>());
        double a = vm["amplitude"].as<double>();

        std::vector<std::complex<int16_t>> data(n_block);

        if (n_cycles != 0.0) {
            for (size_t i = 0; i < n_block; i++) {
                double v =  double(i) / n_block;
                double re =  a * cos(2. * std::numbers::pi_v<double> * n_cycles * v);
                double im = -a * sin(2. * std::numbers::pi_v<double> * n_cycles * v);
                data[i] = std::complex<int16_t>(std::lround(re), std::lround(im));
            }
        } else {
            const int16_t ra = std::llround(a);
            for (size_t i = 0; i < n_block; i++) {
                data[i] = std::complex<int16_t>(ra, 0);
            }
        }

        size_t tx_buffer_size_bytes = 0;
        LOG_DEBUG("getting buffer size");
        auto b1 = radio->get_buffer_info();
        if (b1.has_value()) {
            tx_buffer_size_bytes = b1->at(1);
        } else {
            LOG_ERROR("unable to get buffer info");
        }

        LOG_DEBUG("getting radio time");
        auto t1 = radio->get_time_now();
        if (t1.has_value()) {
            LOG_INFO(fmt::format("radio time: {:s}",
                    format_time(std::chrono::round<std::chrono::system_clock::duration>(t1.value()))));
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

        double tx_buffer_duration_s = tx_buffer_size_bytes / (vm["network_bit_rate"].as<double>() / 8);
        // start early enough to fill the tx buffer half full at start
        unsigned pre_start_ms = std::lround(1000 * tx_buffer_duration_s / 2);
        LOG_INFO(fmt::format("pre start: {:d} ms", pre_start_ms));
        std::this_thread::sleep_until(t_start - std::chrono::duration(std::chrono::milliseconds(pre_start_ms)));

        size_t n_total = 0;

        while(n_total < n_samp) {
            if(n_samp - n_total < data.size()) {
                data.resize(n_samp - n_total);
            }
            auto n_sent = radio->put_tx_data(data);
            if (n_sent != data.size()) {
                LOG_ERROR(fmt::format("{:d} of {:d} samples sent at sample {:d}", n_sent, data.size(), n_total));
            }
            n_total += n_sent;
        }

        LOG_INFO("transmit complete");
    } catch (std::exception& e) {
        LOG_FATAL(fmt::format("exception caught: {:s}", e.what()));
        return EXIT_INTERNAL_ERROR;
    }

    LOG_INFO(fmt::format("{:s} finished", argv[0]));
    return EXIT_SUCCESS;
}
