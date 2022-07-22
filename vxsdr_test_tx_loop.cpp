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

void add_modulation_options(boost::program_options::options_description& desc) {
    // clang-format off
    desc.add_options()
        ("modulation_amplitude", boost::program_options::value<double>()->default_value(0.1), "modulation amplitude as fraction of full scale")
        ("modulation_frequency", boost::program_options::value<double>()->default_value(10e6), "modulation frequency in Hz (will be rounded to integer to avoid jumps)")
        ("t_repeat", boost::program_options::value<double>()->default_value(1.0), "repeat time in seconds")
        ("n_repeat", boost::program_options::value<uint32_t>()->default_value(0), "number of repetitions (zero for repeat until stop)")
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

        init_vm(desc, vm, argc, argv);

        auto radio = std::make_unique<vxsdr>(vm["local_address"].as<std::string>(), vm["destination_address"].as<std::string>(),
                                    vm["set_priority"].as<bool>(), vm["thread_priority"].as<int>());

        set_common_options(vm, radio);
        set_tx_1ch_options(vm, radio);

        if (vm["duration"].as<double>() <= 0.0) {
            LOG_FATAL("duration must be positive");
            return EXIT_CMD_LINE_ERROR;
        }

        if (vm["duration"].as<double>() >= vm["t_repeat"].as<double>()) {
            LOG_FATAL("duration must be less that t_repeat");
            return EXIT_CMD_LINE_ERROR;
        }

        double f_s = vm["rate"].as<double>();
        size_t n_samp = std::llround(vm["duration"].as<double>() * f_s);

        std::vector<std::complex<int16_t>> data(std::llround(vm["rate"].as<double>()));
        double a = 32767. * std::max(0.0, std::min(1.0, vm["modulation_amplitude"].as<double>()));
        double f_mod = std::llround(vm["modulation_frequency"].as<double>());
        for (size_t i = 0; i < data.size(); i++) {
            double re = a * cos(M_2_PI * f_mod / f_s);
            double im = a * sin(M_2_PI * f_mod / f_s);
            data[i] = std::complex<int16_t>(std::lround(re), std::lround(im));
        }

        size_t tx_buffer_size_samples = 0;
        LOG_DEBUG("getting buffer size");
        auto b1 = radio->get_buffer_info();
        if (b1.has_value()) {
            tx_buffer_size_samples = b1->at(1) / sizeof(std::complex<int16_t>);
        } else {
            LOG_ERROR("unable to get buffer info");
        }

        if (vm["duration"].as<double>() > tx_buffer_size_samples / f_s) {
            LOG_FATAL(fmt::format("duration is longer than the capacity of the TX buffer ({:.3f} seconds)", tx_buffer_size_samples / f_s));
            return EXIT_CMD_LINE_ERROR;
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

        auto t_repeat = std::chrono::round<vxsdr_duration>(std::chrono::duration<double>(vm["t_repeat"].as<double>()));

        if (not radio->tx_loop_at_time_for_n_samples(t_start, n_samp, t_repeat, vm["n_repeat"].as<uint32_t>())) {
            LOG_ERROR("tx_loop_at_time_for_n_samples() failed");
        }

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
