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

size_t do_transmit(std::unique_ptr<vxsdr> &radio, std::vector<std::complex<int16_t>> &data) {
    size_t n_total = 0;

    while (not tx_stop_flag) {
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

        auto radio = std::make_unique<vxsdr>(vm["local_address"].as<std::string>(),
                                    vm["destination_address"].as<std::string>(),
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
            double re =  a * cos(2 * M_PI * f_mod / f_s * i);
            double im = -a * sin(2 * M_PI * f_mod / f_s * i);
            data[i] = std::complex<int16_t>(std::lround(re), std::lround(im));
        }

        LOG_DEBUG("getting radio time");
        auto t1 = radio->device_get_time_now();
        if (t1) {
            LOG_INFO(fmt::format("radio time: {:s}", format_time(t1.value())));
        } else {
            LOG_ERROR("unable to get radio time");
        }

        LOG_INFO(fmt::format("using rate      {:.5e}", radio->get_tx_rate().value_or(-1)));
        LOG_INFO(fmt::format("using tx_gain   {:.1f}", radio->get_tx_gain().value_or(-1)));

        const double f_if = 160e6;

        net::io_context ctx;
        tcp_connection conn(ctx, "192.168.8.41", 1234);
        constexpr unsigned hp_8564_addr = 18;
        hp_856x_set_ref_level(conn, hp_8564_addr, 0);
        hp_856x_set_freq(conn, hp_8564_addr, 5e9);
        hp_856x_set_span(conn, hp_8564_addr, 0);
        hp_856x_set_rbw(conn, hp_8564_addr, 1e3);
        auto sweep_time = hp_856x_read_sweep_time(conn, hp_8564_addr);

        if (not radio->tx_start_now_until_stop()) {
            LOG_ERROR("tx_start_now_until_stop() failed");
        }
        auto tx_future = std::async(std::launch::async, [&] { return do_transmit(radio, data); });

        std::array<double,2> tx_bias = {0.0, 0.0};
        radio->set_tx_iq_bias(tx_bias);

        // 25 dB image rejection ~= 1 dB amplitude imbalance or 0.1 radian phase imbalance

        std::vector<double> ff;
        std::vector<double> aa;
        std::vector<double> pp;
        std::vector<double> rr;

        for (double f_c = 5e9; f_c <= 22e9; f_c += 1e9) {
            radio->set_tx_freq(f_c);

            double amin = 0.8, amax = 1.25, astep = (amax - amin) / 8;
            double pmin = -0.3, pmax = 0.3, pstep = (pmax - pmin) / 8;

            double abest = 999.99, pbest = 999.99, rbest = -999.99;

            for (int k = 0; k < 3; k++) {
                for (double alpha = amin; alpha <= amax + 0.5 * astep; alpha += astep) {
                    for (double psi = pmin; psi <= pmax + 0.5 * pstep; psi += pstep) {
                        double A = 1. / alpha;
                        double C = -sin(psi) / (alpha * cos(psi));
                        double D = 1. / cos(psi);
                        std::array<double,4> tx_corr = {A, 0., C, D};
                        //std::cout << fmt::format("{:8.3f} {:8.3f} {:10.5f} {:10.5f} {:10.5f} {:10.5f}",
                        //        alpha, psi, tx_corr.at(0), tx_corr.at(1), tx_corr.at(2), tx_corr.at(3)) << std::endl;;
                        radio->set_tx_iq_corr(tx_corr);
                        hp_856x_set_freq(conn, hp_8564_addr, f_c + f_mod);
                        std::this_thread::sleep_for(100ms);
                        auto desired = hp_856x_read_carron(conn, hp_8564_addr, sweep_time);
                        std::this_thread::sleep_for(100ms);
                        hp_856x_set_freq(conn, hp_8564_addr, f_c - 2 * f_if - f_mod);
                        std::this_thread::sleep_for(100ms);
                        auto image = hp_856x_read_carron(conn, hp_8564_addr, sweep_time);
                        auto rejection =  desired - image;
                        std::cout << fmt::format("{{:10.5f} {:10.5f} {:10.2f} {:10.2f} {:10.2f}", alpha, psi, desired, image, rejection);
                        if (rejection > rbest) {
                            abest = alpha;
                            pbest = psi;
                            rbest = rejection;
                        }
                    }
                }
                amin = abest - astep;
                amax = abest + astep;
                astep = astep / 4;
                pmin = pbest - pstep;
                pmax = pbest + pstep;
                pstep = pstep / 4;
            }
            ff.push_back(f_c);
            aa.push_back(abest);
            pp.push_back(pbest);
            rr.push_back(rbest);
        }
        tx_stop_flag = true;
        std::this_thread::sleep_for(110ms);
        radio->tx_stop_now();

        for (unsigned i = 0; i < ff.size(); i++) {
            std::cout << fmt::format("{:10.2e} {:10.5f} {:10.5f} {:10.2f}", ff[i], aa[i], pp[i], rr[i]) << std::endl;
        }


    } catch (std::exception& e) {
        LOG_FATAL(fmt::format("exception caught: {:s}", e.what()));
        return EXIT_INTERNAL_ERROR;
    }

    LOG_INFO(fmt::format("{:s} finished", argv[0]));
    return EXIT_SUCCESS;
}
