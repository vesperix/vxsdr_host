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

template <class T, class U> void pwelch(const std::vector<std::complex<T> > &x, std::vector<U> &w, std::vector<U> &S, fft<U> &fft, const double step = 0.5)
{
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
        std::vector<std::complex<T>> tmp = std::vector<std::complex<T> >(x.begin() + n * nstep, x.begin() + n * nstep + fft.size());
        for(unsigned j = 0; j < tmp.size(); j++)
            tmp[j] *= w[j];
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

double radio_rx_measure(std::unique_ptr<vxsdr> &radio, const double freq,
            const double gain = 0.0, const double f_s = 1.e6) {
    constexpr size_t n_fft = 16'384;
    constexpr size_t n_samps = 10 * n_fft;
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
    for (unsigned i = 0; i < n_samps; i++) {
        constexpr unsigned max_val = 30000;
        if (std::abs(data[i].real()) > max_val or std::abs(data[i].imag()) > max_val) {
            return -993.;
        }
    }

    fft<float> F(n_fft);
    std::vector<float> w, S;
    hann(w, n_fft);
    pwelch(data, w, S, F);

    return 10 * log10(S[F.size() / 2]);
}

void tx_bias_grid(std::ofstream& os, std::unique_ptr<vxsdr> &radio, const double f_c, const double f_if) {
    for (int i = -64; i < 64; i++) {
        for (int q = -64; q < 64; q++)  {
            radio->set_tx_iq_bias({i / 64., q / 64.});
            auto lo_feedthrough = radio_rx_measure(radio, f_c - f_if);
            os << fmt::format("{:10.3e} {:10.6f} {:10.6f} {:8.2f}", f_c, i / 64., q / 64., lo_feedthrough) << std::endl;
        }
    }
    radio->set_tx_iq_bias({0., 0.});
}

void tx_corr_grid(std::ofstream& os, std::unique_ptr<vxsdr> &radio, const double f_c, const double f_if, const double f_mod) {
    const double gain_base = 0.0, gain_change = 20.0;

    constexpr double amin = 0.8, amax = 1.25, astep = 0.05;
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
        }
    }
    radio->set_tx_iq_corr({1., 0., 0., 1.});
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

        auto radio = std::make_unique<vxsdr>(vm["local_address"].as<std::string>(), vm["destination_address"].as<std::string>());

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


        if (not radio->tx_start_now_until_stop()) {
            LOG_ERROR("tx_start_now_until_stop() failed");
        }
        auto tx_future = std::async(std::launch::async, [&] { return do_transmit(radio, data); });

        for (double f_c = 5.0e9; f_c <= 22.0e9 - f_mod; f_c += 1e9) {
            radio->set_tx_freq(f_c);
            std::ofstream ob(fmt::format("bias-{:.3f}-ghz.txt", f_c / 1e9), std::ofstream::out);
            tx_bias_grid(ob, radio, f_c, f_if);
            ob.close();
            std::ofstream oc(fmt::format("corr-{:.3f}-ghz.txt", f_c / 1e9), std::ofstream::out);
            tx_corr_grid(oc, radio, f_c, f_if, f_mod);
            oc.close();
        }


    } catch (std::exception& e) {
        LOG_FATAL(fmt::format("exception caught: {:s}", e.what()));
        return EXIT_INTERNAL_ERROR;
    }

    LOG_INFO(fmt::format("{:s} finished", argv[0]));
    return EXIT_SUCCESS;
}
