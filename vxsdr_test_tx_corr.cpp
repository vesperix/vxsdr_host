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
#include <numbers>
#include <arpa/inet.h>

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

        uint32_t local_addr = ntohl(inet_addr(vm["local_address"].as<std::string>().c_str()));
        uint32_t device_addr = ntohl(inet_addr(vm["device_address"].as<std::string>().c_str()));

        std::map<std::string, int64_t> settings = {
            {"udp_transport:local_address",  local_addr},
            {"udp_transport:device_address", device_addr},
            {"tx_data_queue_packets",        vm["tx_data_queue_packets"].as<unsigned>()},
            {"rx_data_queue_packets",        vm["rx_data_queue_packets"].as<unsigned>()},
            {"network_send_buffer_bytes",    vm["network_send_buffer_bytes"].as<unsigned>()},
            {"network_receive_buffer_bytes", vm["network_receive_buffer_bytes"].as<unsigned>()},
            {"net_thread_priority",          vm["net_thread_priority"].as<int>()},
            {"thread_affinity_offset",       vm["thread_affinity_offset"].as<int>()}
        };

        auto radio = std::make_unique<vxsdr>(settings);

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
            double re =  a * cos(2 * std::numbers::pi_v<double> * f_mod / f_s * i);
            double im = -a * sin(2 * std::numbers::pi_v<double> * f_mod / f_s * i);
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

        double f_c = radio->get_tx_freq().value_or(-1);
        const double f_if = 160e6;

#define DO_MEASUREMENTS

#ifdef DO_MEASUREMENTS
        net::io_context ctx;
        tcp_connection conn(ctx, "192.168.8.41", 1234);
        constexpr unsigned hp_8564_addr = 18;
        hp_856x_set_ref_level(conn, hp_8564_addr, 0);
        hp_856x_set_freq(conn, hp_8564_addr, f_c);
        hp_856x_set_span(conn, hp_8564_addr, 0);
        hp_856x_set_rbw(conn, hp_8564_addr, 1e3);
        auto sweep_time = hp_856x_read_sweep_time(conn, hp_8564_addr);
#endif // DO_MEASUREMENTS

        if (not radio->tx_start(radio->get_time_now().value() + std::chrono::milliseconds(1))) {
            LOG_ERROR("tx_start() failed");
        }
        auto tx_future = std::async(std::launch::async, [&] { return do_transmit(radio, data); });

        std::array<double,2> tx_bias = {0.0, 0.0};
        radio->set_tx_iq_bias(tx_bias);

        // 25 dB image rejection ~= 1 dB amplitude imbalance or 0.1 radian phase imbalance

        for (double alpha = 0.800; alpha <= 1.251; alpha += 0.005) {
            for (double psi = -0.300; psi <= 0.301; psi += 0.005) {
                double A = 1. / alpha;
                double C = -sin(psi) / (alpha * cos(psi));
                double D = 1. / cos(psi);
                std::array<double,4> tx_corr = {A, 0., C, D};
                //std::cout << fmt::format("{:8.3f} {:8.3f} {:10.5f} {:10.5f} {:10.5f} {:10.5f}",
                //        alpha, psi, tx_corr.at(0), tx_corr.at(1), tx_corr.at(2), tx_corr.at(3)) << std::endl;;
                radio->set_tx_iq_corr(tx_corr);
                auto res = radio->get_tx_iq_corr();
                if (res) {
                    std::cout << fmt::format("{:8.3f} {:8.3f} {:10.5f} {:10.5f} {:10.5f} {:10.5f}",
                            alpha, psi, res->at(0), res->at(1), res->at(2), res->at(3));
                } else {
                    std::cerr << "ERROR! no readback" << std::endl;
                    return EXIT_INTERNAL_ERROR;
                }
#ifdef DO_MEASUREMENTS
                hp_856x_set_freq(conn, hp_8564_addr, f_c + f_mod);
                std::this_thread::sleep_for(100ms);
                auto desired = hp_856x_read_carron(conn, hp_8564_addr, sweep_time);
                std::this_thread::sleep_for(100ms);
                hp_856x_set_freq(conn, hp_8564_addr, f_c - 2 * f_if - f_mod);
                std::this_thread::sleep_for(100ms);
                auto image = hp_856x_read_carron(conn, hp_8564_addr, sweep_time);
                auto rejection =  desired - image;
                std::cout << fmt::format("{:10.2f} {:10.2f} {:10.2f}", desired, image, rejection);
#else
                std::this_thread::sleep_for(1s);
#endif // DO_MEASUREMENTS
                std::cout << std::endl;
            }
        }

        tx_stop_flag = true;
        std::this_thread::sleep_for(110ms);
        radio->tx_stop_now();
    } catch (std::exception& e) {
        LOG_FATAL(fmt::format("exception caught: {:s}", e.what()));
        return EXIT_INTERNAL_ERROR;
    }

    LOG_INFO(fmt::format("{:s} finished", argv[0]));
    return EXIT_SUCCESS;
}
