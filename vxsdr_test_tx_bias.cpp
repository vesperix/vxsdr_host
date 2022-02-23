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
#include <future>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/program_options.hpp>
#include <boost/version.hpp>
#include <vxsdr.hpp>

#include "errors.hpp"
#include "logging.hpp"
#include "host_radio_options.hpp"
#include "utility.hpp"

#include "inst_interface.hpp"

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

std::atomic<bool> tx_stop_flag = false;

size_t do_transmit(std::unique_ptr<vxsdr> &radio, const std::vector<std::complex<int16_t>> data) {
    size_t n_total = 0;

    while(not tx_stop_flag) {
        auto n_sent = radio->put_tx_data(data);
        if (n_sent != data.size()) {
            LOG_ERROR(fmt::format("{:d} of {:d} samples sent at sample {:d}", n_sent, data.size(), n_total));
        }
        n_total += n_sent;
    }
    LOG_INFO(fmt::format("sent {:d} samples", n_total));
    LOG_INFO("transmit complete");
    return n_total;
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
        set_rx_1ch_options(vm, radio);

        if (vm["duration"].as<double>() <= 0.0) {
            LOG_FATAL("duration must be positive");
            return EXIT_CMD_LINE_ERROR;
        }

        double f_s = vm["rate"].as<double>();

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
        auto b1 = radio->device_get_buffer_info();
        if (b1.has_value()) {
            tx_buffer_size_samples = b1->at(1) / sizeof(std::complex<int16_t>);
        } else {
            LOG_ERROR("unable to get buffer info");
        }

        LOG_DEBUG("getting radio time");
        auto t1 = radio->device_get_time_now();
        if (t1.has_value()) {
            LOG_INFO(fmt::format("radio time: {:s}", format_time(t1.value())));
        } else {
            LOG_ERROR("unable to get radio time");
        }

        LOG_INFO(fmt::format("using frequency {:.5e}", radio->get_tx_freq().value_or(-1)));
        LOG_INFO(fmt::format("using rate      {:.5e}", radio->get_tx_rate().value_or(-1)));
        LOG_INFO(fmt::format("using tx_gain   {:.1f}", radio->get_tx_gain().value_or(-1)));

        net::io_context ctx;
        tcp_connection conn(ctx, "192.168.8.41", 1234);
        constexpr unsigned hp_8564_addr = 18;
        hp_856x_set_ref_level(conn, hp_8564_addr, -20);

        const double f_if = 160e6;

        hp_856x_set_freq(conn, hp_8564_addr,  radio->get_tx_freq().value_or(-1) - f_if);
        hp_856x_set_span(conn, hp_8564_addr, 0);
        auto sweep_time = hp_856x_read_sweep_time(conn, hp_8564_addr);


        if (not radio->tx_start_now_until_stop()) {
            LOG_ERROR("tx_start_now_until_stop() failed");
        }
        auto tx_future = std::async(std::launch::async, [&] { return do_transmit(radio, data); });

        for (double i_bias = -1.0; i_bias <= 1.0; i_bias += 0.2) {
            for (double q_bias = -1.0; q_bias <= 1.0; q_bias += 0.2) {
                std::array<double,2> tx_bias = {i_bias, q_bias};
                radio->set_tx_iq_bias(tx_bias);
                auto lo_feedthrough = hp_856x_read_carron(conn, hp_8564_addr, sweep_time);
                std::cout << fmt::format("{:10.3f} {:10.3f} {:10.2f}", i_bias, q_bias, lo_feedthrough) << std::endl;
            }
        }
        tx_stop_flag = true;
        radio->tx_stop_now();

    } catch (std::exception& e) {
        LOG_FATAL(fmt::format("exception caught: {:s}", e.what()));
        return EXIT_INTERNAL_ERROR;
    }

    LOG_INFO(fmt::format("{:s} finished", argv[0]));
    return EXIT_SUCCESS;
}
