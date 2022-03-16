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
#include "neldermead.hpp"

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

double radio_rx_measure(std::unique_ptr<vxsdr> &radio, const double freq,
            const double f_s = 1.e6, const double gain = 0.0) {
    const size_t n_samps = 16'384;
    std::vector<std::complex<int16_t>> data;
    data.resize(n_samps);
    if (not radio->set_rx_freq(freq)) {
        return -999.;
    }
    if (std::abs(freq - radio->get_rx_freq().value_or(-1)) > 1.0) {
        return -998.;
    }
    if (not radio->set_rx_rate(f_s)) {
        return -997.;
    }
    if (std::abs(f_s - radio->get_rx_rate().value_or(-1)) > 1.0) {
        return -996.;
    }
    if (not radio->set_rx_gain(gain)) {
        return -995.;
    }
    if (std::abs(gain - radio->get_rx_gain().value_or(-1)) > 1.0) {
        return -994.;
    }
    radio->rx_start_now_for_n_samples(n_samps);
    radio->get_rx_data(data, n_samps);
    double sum = 0.0;
    for (unsigned i = 0; i < n_samps; i++) {
        double dr = data[i].real();
        double di = data[i].imag();
        sum += dr * dr + di * di;
    }
    return 10 * log10(sum / n_samps);
}

std::array<double, 3> find_optimum_tx_bias_nm(std::unique_ptr<vxsdr> &radio, const double f_c, const double f_if) {
    std::vector<double> xmin(2), x0 {0., 0.}, step {0.05,0.05};
    double reqmin = 1e-3, ymin;
    int icount, numres;

    auto res = nelmin([&radio, f_c, f_if] (std::vector<double> &x) -> double {
                radio->set_tx_iq_bias({x[0], x[1]});
                return radio_rx_measure(radio, f_c - f_if);
             },
             2, x0, xmin, ymin, reqmin, step, 10, 300, icount, numres);
    std::cout << fmt::format("{:6d} {:10.5f} {:10.5f} {:10.2f} {:10d}", res, xmin[0], xmin[1], ymin, icount) << std::endl;
    if (res != 0) {
        return { 0.0, 0.0, 999.99 };
    }
    return { xmin[0], xmin[1], ymin };
}

std::array<double, 3> find_optimum_tx_corr_nm(std::unique_ptr<vxsdr> &radio, const double f_c, const double f_if, const double f_mod) {
    std::vector<double> xmin(2), x0 {1., 0.}, step {0.05,0.05};
    double reqmin = 1e-3, ymin;
    int icount, numres;

    auto res = nelmin([&radio, f_c, f_if, f_mod] (std::vector<double> &x) -> double {
                double alpha = x[0];
                double psi = x[1];
                double A = 1. / alpha;
                double C = -sin(psi) / (alpha * cos(psi));
                double D = 1. / cos(psi);
                std::array<double,4> tx_corr = {A, 0., C, D};
                radio->set_tx_iq_corr(tx_corr);
                auto desired = radio_rx_measure(radio, f_c + f_mod);
                auto image = radio_rx_measure(radio, f_c - 2 * f_if - f_mod);
                return image - desired; // negative of rejection since we're minimizing
            },
             2, x0, xmin, ymin, reqmin, step, 10, 300, icount, numres);
    std::cout << fmt::format("{:6d} {:10.5f} {:10.5f} {:10.2f} {:10d}", res, xmin[0], xmin[1], ymin, icount) << std::endl;
    if (res != 0) {
        return { x0[0], x0[1], -999.99 };
    }
    return { xmin[0], xmin[1], ymin };
}

std::array<double, 3> find_optimum_bias(std::unique_ptr<vxsdr> &radio, const double f_c, const double f_if) {

    int i_best = 999, q_best = 999;
    double lo_best = 999.99;

    for (int i = -64; i < 64; i+=2) {
        double i_bias = i / 64.;
        double q_bias = 0.;
        std::array<double,2> tx_bias = {i_bias, q_bias};
        radio->set_tx_iq_bias(tx_bias);
        auto lo_feedthrough = radio_rx_measure(radio, f_c - f_if);
        if (lo_feedthrough < lo_best) {
            i_best = i;
            lo_best = lo_feedthrough;
        }
        //std::cout << fmt::format("{:10.5f} {:10.5f} {:10.2f}", i_bias, q_bias, lo_feedthrough) << std::endl;
    }
    //std::cout << fmt::format("{:10d} {:10d} {:10.2f}", i_best, 0, lo_best) << std::endl;
    lo_best = 999.99;
    for (int q = -64; q < 64; q+=2)  {
        double i_bias = 0.;
        double q_bias = q / 64.;
        std::array<double,2> tx_bias = {i_bias, q_bias};
        radio->set_tx_iq_bias(tx_bias);
        auto lo_feedthrough = radio_rx_measure(radio, f_c - f_if);
        if (lo_feedthrough < lo_best) {
            q_best = q;
            lo_best = lo_feedthrough;
        }
        //std::cout << fmt::format("{:10.5f} {:10.5f} {:10.2f}", i_bias, q_bias, lo_feedthrough) << std::endl;
    }
    //std::cout << fmt::format("{:10d} {:10d} {:10.2f}", 0, q_best, lo_best) << std::endl;
    //std::cout << fmt::format("{:10.4f} {:10.4f}", i_best / 64., q_best / 64.) << std::endl;
    int i_opt = 999, q_opt = 999;
    lo_best = 999.99;
    for (int i = -3; i < 4; i++) {
        for (int q = -3; q < 4; q++) {
            double i_bias = (i + i_best) / 64.;
            double q_bias = (q + q_best) / 64.;
            std::array<double,2> tx_bias = {i_bias, q_bias};
            radio->set_tx_iq_bias(tx_bias);
            auto lo_feedthrough = radio_rx_measure(radio, f_c - f_if);
            if (lo_feedthrough < lo_best) {
                i_opt = i + i_best;
                q_opt = q + q_best;
                lo_best = lo_feedthrough;
            }
            //std::cout << fmt::format("{:10.5f} {:10.5f} {:10.2f}", i_bias, q_bias, lo_feedthrough) << std::endl;
        }
    }
    std::array<double, 3> result = {i_opt / 64., q_opt / 64., lo_best};
    return result;
}

std::array<double, 3> find_optimum_bias_8564(std::unique_ptr<vxsdr> &radio, tcp_connection &conn, const unsigned hp_8564_addr,
        const double f_c, const double f_if) {
    hp_856x_set_ref_level(conn, hp_8564_addr, 10);
    hp_856x_set_freq(conn, hp_8564_addr,  f_c - f_if);
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
        //std::cout << fmt::format("{:10.5f} {:10.5f} {:10.2f}", i_bias, q_bias, lo_feedthrough) << std::endl;
    }
    //std::cout << fmt::format("{:10d} {:10d} {:10.2f}", i_best, 0, lo_best) << std::endl;
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
        //std::cout << fmt::format("{:10.5f} {:10.5f} {:10.2f}", i_bias, q_bias, lo_feedthrough) << std::endl;
    }
    //std::cout << fmt::format("{:10d} {:10d} {:10.2f}", 0, q_best, lo_best) << std::endl;
    //std::cout << fmt::format("{:10.4f} {:10.4f}", i_best / 64., q_best / 64.) << std::endl;
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
            //std::cout << fmt::format("{:10.5f} {:10.5f} {:10.2f}", i_bias, q_bias, lo_feedthrough) << std::endl;
        }
    }
    std::array<double, 3> result = {i_opt / 64., q_opt / 64., lo_best};
    return result;
}

std::vector<double> find_optimum_corr(std::unique_ptr<vxsdr> &radio, const double f_c, const double f_if, const double f_mod) {
    double amin = 0.8, amax = 1.25, astep = (amax - amin) / 8;
    double pmin = -0.4, pmax = 0.4, pstep = (pmax - pmin) / 8;

    double abest = 999.99, pbest = 999.99, rbest = -999.99;

    for (int k = 0; k < 6; k++) {
        for (double alpha = amin; alpha <= amax + 0.5 * astep; alpha += astep) {
            for (double psi = pmin; psi <= pmax + 0.5 * pstep; psi += pstep) {
                double A = 1. / alpha;
                double C = -sin(psi) / (alpha * cos(psi));
                double D = 1. / cos(psi);
                std::array<double,4> tx_corr = {A, 0., C, D};
                radio->set_tx_iq_corr(tx_corr);
                /*
                auto tx_set = radio->get_tx_iq_corr();
                if (tx_set) {
                    std::cout << fmt::format("{:8.4f} {:8.4f} {:10.5f} {:10.5f} {:10.5f} {:10.5f}",
                            alpha, psi, tx_set->at(0), tx_set->at(1), tx_set->at(2), tx_set->at(3));
                }
                */
#ifdef USE_8564
                hp_856x_set_freq(conn, hp_8564_addr, f_c + f_mod);
                std::this_thread::sleep_for(100ms);
                auto desired = hp_856x_read_carron(conn, hp_8564_addr, sweep_time);
                std::this_thread::sleep_for(100ms);
                hp_856x_set_freq(conn, hp_8564_addr, f_c - 2 * f_if - f_mod);
                std::this_thread::sleep_for(100ms);
                auto image = hp_856x_read_carron(conn, hp_8564_addr, sweep_time);
#else
                auto desired = radio_rx_measure(radio, f_c + f_mod);
                auto image = radio_rx_measure(radio, f_c - 2 * f_if - f_mod);
#endif
                auto rejection =  desired - image;
                //std::cout << fmt::format(" {:10.2f} {:10.2f} {:10.2f}", desired, image, rejection) << std::endl;
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
    std::vector<double> result = { abest, pbest, rbest };
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
        add_rx_1ch_options(desc);
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
//#define USE_8564
#ifdef USE_8564
        net::io_context ctx;
        tcp_connection conn(ctx, "192.168.8.41", 1234);
        constexpr unsigned hp_8564_addr = 18;
        hp_856x_set_ref_level(conn, hp_8564_addr, 0);
        hp_856x_set_freq(conn, hp_8564_addr, 5e9);
        hp_856x_set_span(conn, hp_8564_addr, 0);
        hp_856x_set_rbw(conn, hp_8564_addr, 1e3);
        auto sweep_time = hp_856x_read_sweep_time(conn, hp_8564_addr);
#endif

        if (not radio->tx_start_now_until_stop()) {
            LOG_ERROR("tx_start_now_until_stop() failed");
        }
        auto tx_future = std::async(std::launch::async, [&] { return do_transmit(radio, data); });

        std::vector<double> ff;
        std::vector<double> aa;
        std::vector<double> pp;
        std::vector<double> rr;
        std::vector<double> bi;
        std::vector<double> bq;
        std::vector<double> br;

        for (double f_c = 5.5e9; f_c <= 22.0e9 - f_mod; f_c += 1e8) {
            radio->set_tx_freq(f_c);

#ifdef USE_8564
            auto txb = find_optimum_bias_8564(radio, conn, hp_8664_addr, f_c, f_if);
#else
            auto txb = find_optimum_tx_bias_nm(radio, f_c, f_if);
#endif

            std::array<double,2> tx_bias = { txb[0], txb[1] };
            radio->set_tx_iq_bias(tx_bias);

            std::cout << fmt::format("{:10.3e} {:10.5f} {:10.5f} {:8.2f} ", f_c, txb[0], txb[1], txb[2]);

            ff.push_back(f_c);
            bi.push_back(txb[0]);
            bq.push_back(txb[1]);
            br.push_back(txb[2]);

            auto txc = find_optimum_tx_corr_nm(radio, f_c, f_if, f_mod);

            std::cout << fmt::format("{:10.5f} {:10.5f} {:10.2f}", txc[0], txc[1], txc[2]) << std::endl;

            aa.push_back(txc[0]);
            pp.push_back(txc[1]);
            rr.push_back(txc[2]);
        }
        tx_stop_flag = true;
        std::this_thread::sleep_for(110ms);
        radio->tx_stop_now();

        for (unsigned i = 0; i < ff.size(); i++) {
            std::cout << fmt::format("{:10.2e} {:10.5f} {:10.5f} {:10.2f} {:10.5f} {:10.5f} {:10.2f}",
                    ff[i], bi[i], bq[i], br[i], aa[i], pp[i], rr[i]) << std::endl;
        }


    } catch (std::exception& e) {
        LOG_FATAL(fmt::format("exception caught: {:s}", e.what()));
        return EXIT_INTERNAL_ERROR;
    }

    LOG_INFO(fmt::format("{:s} finished", argv[0]));
    return EXIT_SUCCESS;
}
