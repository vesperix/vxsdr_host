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

#include "fft.hpp"
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

template <class T> void hann(std::vector<T> &w, size_t len = 0)
{
    if(len > 0)
        w.resize(len);
    const size_t N = w.size() - 1;
    const T fac =  2. * M_PI / N;
    for(unsigned n = 0; n < N; n++)
        w[n] = 0.5 * (1.0 - cos(fac * n));
    w[N] = 0.0;
}

template <class T, class U> void pwelch(const std::vector<std::complex<T> > &x, std::vector<U> &w, std::vector<U> &S,
            fft<U> &fft, const double step = 0.5) {
    unsigned nstep = std::round(fft.size() * step);
    S.resize(fft.size());
    std::fill(S.begin(), S.end(), 0.0);
    // compute window energy
    U W = 0.0;
    for (auto e : w)
        W += e * e;
    // N is the number of spectra
    unsigned N = ceil((double)(x.size() - fft.size()) / nstep);
    for (unsigned n = 0; n < N; n++) {
        std::vector<std::complex<U>> tmp(fft.size());
        for(unsigned j = 0; j < tmp.size(); j++) {
            tmp[j] = w[j] * std::complex<U>(x[n * nstep + j].real(), x[n * nstep + j].imag());
        }
        std::vector<std::complex<U>> xform;
        fft.forward(tmp, xform);
        for(unsigned j = 0; j < fft.size() / 2; j++) {
            // put zero frequency in the center, square magnitude to get power
            unsigned k = j + fft.size() / 2;
            S[j] += xform[k].real() * xform[k].real() + xform[k].imag() * xform[k].imag();
            S[k] += xform[j].real() * xform[j].real() + xform[j].imag() * xform[j].imag();
        }
    }
    // normalize
    for(unsigned i = 0; i < S.size(); i++)
      S[i] /= (N * W);
}

double radio_rx_measure(std::unique_ptr<vxsdr> &radio, const double f_c,
            const double gain = 0.0, const double f_s = 1.e6) {
    constexpr size_t n_fft = 1'024;
    constexpr size_t n_samps = 16 * n_fft;
    double f_off = 0.125 * f_s;
    std::vector<std::complex<int16_t>> data;
    data.resize(n_samps);
    if (not radio->set_rx_freq(f_c - f_off)) {
        return -998.;
    }
    if (std::abs(f_c - f_off - radio->get_rx_freq().value_or(-1)) > 1.0) {
        return -997.;
    }
    if (not radio->set_rx_rate(f_s)) {
        return -996.;
    }
    if (std::abs(f_s - radio->get_rx_rate().value_or(-1)) > 1.0) {
        return -995.;
    }
    if (not radio->set_rx_gain(gain)) {
        return -994.;
    }
    if (std::abs(gain - radio->get_rx_gain().value_or(-1)) > 1.0) {
        return -993.;
    }
    radio->rx_start(radio->get_time_now().value() + std::chrono::milliseconds(1), n_samps);
    radio->get_rx_data(data, n_samps);
    for (unsigned i = 0; i < n_samps; i++) {
        constexpr unsigned max_val = 30000;
        if (std::abs(data[i].real()) > max_val or std::abs(data[i].imag()) > max_val) {
            return -992.;
        }
    }

    fft<float> F(n_fft);
    std::vector<float> w, S;
    hann(w, n_fft);
    pwelch(data, w, S, F);
    return 10 * log10(S.at(F.size() / 2 + std::lround(f_off / f_s * n_fft)));
}

void tx_bias_grid(std::ofstream& os, std::unique_ptr<vxsdr> &radio, const double f_c, const double f_if) {
    for (int i = -64; i < 64; i+=4) {
        for (int q = -64; q < 64; q+=4)  {
            radio->set_tx_iq_bias({i / 64., q / 64.});
            auto lo_feedthrough = radio_rx_measure(radio, f_c - f_if);
            os << fmt::format("{:10.3e} {:10.6f} {:10.6f} {:8.2f}", f_c, i / 64., q / 64., lo_feedthrough) << std::endl;
            std::cout << fmt::format("bias {:10.3e} {:10.6f} {:10.6f} {:8.2f}", f_c, i / 64., q / 64., lo_feedthrough) << std::endl;
        }
    }
    radio->set_tx_iq_bias({0., 0.});
}

void tx_corr_grid(std::ofstream& os, std::unique_ptr<vxsdr> &radio, const double f_c, const double f_if, const double f_mod) {
    constexpr double gain_base = 0.0, gain_change = 20.0;
    constexpr double amin = 0.8, amax = 1.25, astep = 0.01;
    constexpr double pmin = -0.4, pmax = 0.4, pstep = 0.05;

    for (double alpha = amin; alpha <= amax + 0.5 * astep; alpha += astep) {
        for (double psi = pmin; psi <= pmax + 0.5 * pstep; psi += pstep) {
            double A = 1. / alpha;
            double C = -tan(psi) / alpha;
            double D = 1. / cos(psi);
            radio->set_tx_iq_corr({A, 0., C, D});
            auto desired = radio_rx_measure(radio, f_c + f_mod, gain_base);
            auto image = radio_rx_measure(radio, f_c - 2 * f_if - f_mod, gain_base + gain_change);
            auto rejection =  desired - image + gain_change;
            os << fmt::format("{:10.3e} {:10.6f} {:10.6f} {:8.2f}", f_c, alpha, psi, rejection) << std::endl;
            std::cout << fmt::format("corr {:10.3e} {:10.6f} {:10.6f} {:8.2f}", f_c, alpha, psi, rejection) << std::endl;
        }
    }
    radio->set_tx_iq_corr({1., 0., 0., 1.});
}

std::array<double, 4> find_optimum_tx_bias(std::unique_ptr<vxsdr> &radio, const double f_c, const double f_if) {

    int i_best = 999, q_best = 999;
    double lo_best = 999.99, lo_uncorr = 999.99;

    radio->set_tx_iq_bias({ 0., 0.});
    lo_uncorr = radio_rx_measure(radio, f_c - f_if);

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
    }
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
    }
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
        }
    }
    std::array<double, 4> result = {i_opt / 64., q_opt / 64., lo_best, lo_uncorr};
    return result;
}


std::array<double, 4> find_optimum_tx_corr_search(std::unique_ptr<vxsdr> &radio, const double f_c, const double f_if, const double f_mod) {
    constexpr double rtarget = 70.0;
    const double gain_base = 0.0, gain_change = 20.0;
    double nstep = 8.;

    double amin = 0.8, amax = 1.25, astep;
    double pmin = -0.4, pmax = 0.4, pstep;

    double abest = 999.99, pbest = 999.99, rbest = -999.99, runcorr = -999.99;

    radio->set_tx_iq_corr({1., 0., 0., 1.});
    runcorr = radio_rx_measure(radio, f_c + f_mod, gain_base)
                - radio_rx_measure(radio, f_c - 2 * f_if - f_mod, gain_base + gain_change) + gain_change;

    for (int k = 0; k < 5; k++) {
        std::vector<double> a, p, r;
        astep = (amax - amin) / nstep;
        pstep = (pmax - pmin) / nstep;
        for (double alpha = amin; alpha <= amax + 0.5 * astep; alpha += astep) {
            for (double psi = pmin; psi <= pmax + 0.5 * pstep; psi += pstep) {
                double A = 1. / alpha;
                double C = -tan(psi) / alpha;
                double D = 1. / cos(psi);
                radio->set_tx_iq_corr({A, 0., C, D});
                auto desired = radio_rx_measure(radio, f_c + f_mod, gain_base);
                auto image = radio_rx_measure(radio, f_c - 2 * f_if - f_mod, gain_base + gain_change);
                auto rejection =  desired - image + gain_change;
                //std::cout << fmt::format("{:10.5f} {:10.5f} {:10.2f}", alpha, psi, rejection) << std::endl;
                if (rejection > rbest) {
                    abest = alpha;
                    pbest = psi;
                    rbest = rejection;
                }
            }
        }
        amin = abest - astep;
        amax = abest + astep;
        pmin = pbest - pstep;
        pmax = pbest + pstep;

        //std::cout << fmt::format("{:6d} {:10.5f} {:10.5f} {:10.2f}", k, abest, pbest, rbest) << std::endl;
        if (rbest > rtarget) {
            break;
        }
    }
    return {abest, pbest, rbest, runcorr};
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

        LOG_INFO(fmt::format("using tx_rate   {:.5e}", radio->get_tx_rate().value_or(-1)));
        LOG_INFO(fmt::format("using tx_gain   {:.1f}", radio->get_tx_gain().value_or(-1)));

        const double f_if = 160e6;


        if (not radio->tx_start(radio->get_time_now().value() + std::chrono::milliseconds(1))) {
            LOG_ERROR("tx_start() failed");
        }
        auto tx_future = std::async(std::launch::async, [&] { return do_transmit(radio, data); });

        for (double f_c = 5.0e9; f_c <= 22.0e9 - f_mod; f_c += 1e9) {
            radio->set_tx_freq(f_c);
            /*
            std::ofstream ob(fmt::format("bias-{:.3f}-ghz.txt", f_c / 1e9), std::ofstream::out);
            tx_bias_grid(ob, radio, f_c, f_if);
            ob.close();
            std::ofstream oc(fmt::format("corr-{:.3f}-ghz.txt", f_c / 1e9), std::ofstream::out);
            tx_corr_grid(oc, radio, f_c, f_if, f_mod);
            oc.close();
            */
            auto txb = find_optimum_tx_bias(radio, f_c, f_if);
            radio->set_tx_iq_bias({ txb[0], txb[1] });
            auto txc = find_optimum_tx_corr_search(radio, f_c, f_if, f_mod);
            std::cout << fmt::format("{:10.3e} {:10.5f} {:10.5f} {:8.2f} {:8.2f} ", f_c, txb[0], txb[1], txb[2], txb[3]);
            std::cout << fmt::format("{:10.6f} {:10.6f} {:8.2f} {:8.2f}", txc[0], txc[1], txc[2], txc[3]) << std::endl;
        }

        if (not radio->tx_stop_now()) {
            LOG_ERROR("tx_stop_now() failed");
        }

    } catch (std::exception& e) {
        LOG_FATAL(fmt::format("exception caught: {:s}", e.what()));
        return EXIT_INTERNAL_ERROR;
    }

    LOG_INFO(fmt::format("{:s} finished", argv[0]));
    return EXIT_SUCCESS;
}
