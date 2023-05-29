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

std::array<double, 3> find_optimum_bias(std::unique_ptr<vxsdr> &radio, tcp_connection &conn) {
    constexpr unsigned hp_8564_addr = 18;
    const double f_if = 160.e6;
    hp_856x_set_ref_level(conn, hp_8564_addr, 10);
    hp_856x_set_freq(conn, hp_8564_addr,  radio->get_tx_freq().value_or(-1) - f_if);
    hp_856x_set_span(conn, hp_8564_addr, 0);
    hp_856x_set_rbw(conn, hp_8564_addr, 1e3);
    auto sweep_time = hp_856x_read_sweep_time(conn, hp_8564_addr);

    int i_best = 999, q_best = 999;
    double lo_best = 999.99;

    for (int i = -64; i < 64; i+=2) {
        double i_bias = i / 64.;
        double q_bias = 0.;
        std::array<double,2> tx_bias = {i_bias, q_bias};
        radio->set_tx_iq_bias(tx_bias);
        auto lo_feedthrough = hp_856x_read_carron(conn, hp_8564_addr, sweep_time);
        if (lo_feedthrough < lo_best) {
            i_best = i;
            lo_best = lo_feedthrough;
        }
        std::cout << fmt::format("{:10.5f} {:10.5f} {:10.2f}", i_bias, q_bias, lo_feedthrough) << std::endl;
    }
    std::cout << fmt::format("{:10d} {:10d} {:10.2f}", i_best, 0, lo_best) << std::endl;
    lo_best = 999.99;
    for (int q = -64; q < 64; q+=2)  {
        double i_bias = 0.;
        double q_bias = q / 64.;
        std::array<double,2> tx_bias = {i_bias, q_bias};
        radio->set_tx_iq_bias(tx_bias);
        auto lo_feedthrough = hp_856x_read_carron(conn, hp_8564_addr, sweep_time);
        if (lo_feedthrough < lo_best) {
            q_best = q;
            lo_best = lo_feedthrough;
        }
        std::cout << fmt::format("{:10.5f} {:10.5f} {:10.2f}", i_bias, q_bias, lo_feedthrough) << std::endl;
    }
    std::cout << fmt::format("{:10d} {:10d} {:10.2f}", 0, q_best, lo_best) << std::endl;
    std::cout << fmt::format("{:10.4f} {:10.4f}", i_best / 64., q_best / 64.) << std::endl;
    int i_opt = 999, q_opt = 999;
    lo_best = 999.99;
    for (int i = -3; i < 4; i++) {
        for (int q = -3; q < 4; q++) {
            double i_bias = (i + i_best) / 64.;
            double q_bias = (q + q_best) / 64.;
            std::array<double,2> tx_bias = {i_bias, q_bias};
            radio->set_tx_iq_bias(tx_bias);
            auto lo_feedthrough = hp_856x_read_carron(conn, hp_8564_addr, sweep_time);
            if (lo_feedthrough < lo_best) {
                i_opt = i + i_best;
                q_opt = q + q_best;
                lo_best = lo_feedthrough;
            }
            std::cout << fmt::format("{:10.5f} {:10.5f} {:10.2f}", i_bias, q_bias, lo_feedthrough) << std::endl;
        }
    }
    std::array<double, 3> result = {i_opt / 64., q_opt / 64., lo_best};
    return result;
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

        auto radio = std::make_unique<vxsdr>(vm["local_address"].as<std::string>(), vm["device_address"].as<std::string>(),
                                    vm["set_priority"].as<bool>(), vm["thread_priority"].as<int>());

        set_common_options(vm, radio);
        set_tx_1ch_options(vm, radio);
        set_rx_1ch_options(vm, radio);

        if (vm["duration"].as<double>() <= 0.0) {
            LOG_FATAL("duration must be positive");
            return EXIT_CMD_LINE_ERROR;
        }

        double f_s = vm["rate"].as<double>();

        std::vector<std::complex<int16_t>> data(std::llround(0.1 * vm["rate"].as<double>()));
        double a = 32767. * std::max(0.0, std::min(1.0, vm["modulation_amplitude"].as<double>()));
        double f_mod = 10 * std::llround(0.1 * vm["modulation_frequency"].as<double>());
        for (size_t i = 0; i < data.size(); i++) {
            double re = a * cos(M_2_PI * f_mod / f_s);
            double im = a * sin(M_2_PI * f_mod / f_s);
            data[i] = std::complex<int16_t>(std::lround(re), std::lround(im));
        }

        LOG_DEBUG("getting radio time");
        auto t1 = radio->get_time_now();
        if (t1) {
            LOG_INFO(fmt::format("radio time: {:s}", format_time(t1.value())));
        } else {
            LOG_ERROR("unable to get radio time");
        }

        LOG_INFO(fmt::format("using frequency {:.5e}", radio->get_tx_freq().value_or(-1)));
        LOG_INFO(fmt::format("using rate      {:.5e}", radio->get_tx_rate().value_or(-1)));
        LOG_INFO(fmt::format("using tx_gain   {:.1f}", radio->get_tx_gain().value_or(-1)));


        if (not radio->tx_start(t1.value() + std::chrono::milliseconds(100))) {
            LOG_ERROR("tx_start() failed");
        }
        auto tx_future = std::async(std::launch::async, [&] { return do_transmit(radio, data); });

        net::io_context ctx;
        tcp_connection conn(ctx, "192.168.8.41", 1234);

        std::vector<double> freq, i_b, q_b, lof;

        for (double ff = 5e9; ff <= 22e9; ff += 0.2e9) {
            radio->set_tx_freq(ff);

            auto opt_bias = find_optimum_bias(radio, conn);
            std::cout << fmt::format("{:10.2e} {:10.5f} {:10.5f} {:10.2f}",
                    radio->get_tx_freq().value_or(-1), opt_bias[0], opt_bias[1], opt_bias[2]) << std::endl;

            freq.push_back(radio->get_tx_freq().value_or(-1));
            i_b.push_back(opt_bias[0]);
            q_b.push_back(opt_bias[1]);
            lof.push_back(opt_bias[2]);
        }
        tx_stop_flag = true;
        std::this_thread::sleep_for(110ms);
        radio->tx_stop_now();

        std::cout << std::endl;
        for (unsigned i = 0; i < freq.size(); i++) {
            std::cout << fmt::format("{:10.2e} {:10.6f} {:10.6f} {:10.2f}", freq[i], i_b[i], q_b[i], lof[i]) << std::endl;
        }

    } catch (std::exception& e) {
        LOG_FATAL(fmt::format("exception caught: {:s}", e.what()));
        return EXIT_INTERNAL_ERROR;
    }

    LOG_INFO(fmt::format("{:s} finished", argv[0]));
    return EXIT_SUCCESS;
}
