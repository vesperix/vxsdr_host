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
        add_rx_1ch_options(desc);

        desc.add_options()
        ("amplitude", boost::program_options::value<double>()->default_value(10'000.0), "signal amplitude in digital counts")
        ("cycles", boost::program_options::value<double>()->default_value(0.0), "number of cycles in tx signal")
        ("tx_delay", boost::program_options::value<double>()->default_value(0.001), "delay between start of rx and start of tx in seconds")
        ("tx_duration", boost::program_options::value<double>()->default_value(0.001), "duration of tx in seconds")
        ("rx_duration", boost::program_options::value<double>()->default_value(0.003), "duration of rx in seconds")
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
        set_rx_1ch_options(vm, radio);

        if (vm["duration"].as<double>() <= 0.0) {
            LOG_FATAL("duration must be positive");
            return EXIT_CMD_LINE_ERROR;
        }

        constexpr unsigned granularity = 2048;

        size_t n_samp_tx = granularity * std::llround(vm["tx_duration"].as<double>() * vm["rate"].as<double>() / granularity);
        size_t n_samp_rx = granularity * std::llround(vm["rx_duration"].as<double>() * vm["rate"].as<double>() / granularity);
        double n_cycles = vm["cycles"].as<double>();
        double a = vm["amplitude"].as<double>();

        std::vector<std::complex<int16_t>> tx_data(n_samp_tx), rx_data(n_samp_rx);

        if (n_cycles != 0.0) {
            for (size_t i = 0; i < n_samp_tx; i++) {
                //double aa = a * sin(M_PI * double(i) / double(n_samp_tx));
                double v =  double(i) / n_samp_tx;
                double re =  a * cos(2. * M_PI * n_cycles * v);
                double im = -a * sin(2. * M_PI * n_cycles * v);
                tx_data[i] = std::complex<int16_t>(std::lround(re), std::lround(im));
            }
        } else {
            const int16_t ra = std::llround(a);
            for (size_t i = 0; i < n_samp_tx; i++) {
                tx_data[i] = std::complex<int16_t>(ra, 0);
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
        LOG_INFO(fmt::format("using rx_gain   {:.1f}", radio->get_rx_gain().value_or(-1)));

        auto t_start = std::chrono::ceil<std::chrono::seconds>(std::chrono::system_clock::now()) + std::chrono::duration(1s);
        LOG_INFO(fmt::format("start time: {:s}", format_time(t_start)));

        std::ofstream tx_file ("tx_data.dat", std::ofstream::out | std::ofstream::binary);
        std::ofstream rx_file ("rx_data.dat", std::ofstream::out | std::ofstream::binary);

        tx_file.write(reinterpret_cast<char *>(tx_data.data()), rx_data.size() * sizeof(std::complex<int16_t>));
        tx_file.close();

        vxsdr::duration tx_del = std::chrono::nanoseconds(std::llround(vm["tx_delay"].as<double>() * 1e9));
        vxsdr::time_point t_start_tx = t_start + tx_del;

        if (not radio->tx_start(t_start_tx, n_samp_tx)) {
            LOG_ERROR("tx_start() failed");
        }

        if (not radio->rx_start(t_start, n_samp_rx)) {
            LOG_ERROR("rx_start() failed");
        }

        double tx_buffer_duration_s = tx_buffer_size_bytes / (vm["network_bit_rate"].as<double>() / 8);
        // start early enough to fill the tx buffer half full at start
        unsigned pre_start_ms = std::lround(1000 * tx_buffer_duration_s / 2);

        LOG_INFO(fmt::format("pre start: {:d} ms", pre_start_ms));
        std::this_thread::sleep_until(t_start - std::chrono::duration(std::chrono::milliseconds(pre_start_ms)));

        auto n_sent = radio->put_tx_data(tx_data);
        if (n_sent != n_samp_tx) {
            LOG_ERROR(fmt::format("{:d} of {:d} samples sent", n_sent, n_samp_tx));
        }

        LOG_INFO("transmit complete");

        auto n_recd = radio->get_rx_data(rx_data, n_samp_rx);
        if (n_recd != n_samp_rx) {
            LOG_ERROR(fmt::format("{:d} of {:d} samples received", n_recd, n_samp_rx));
        }

        LOG_INFO("receive complete");

        rx_file.write(reinterpret_cast<char *>(rx_data.data()), rx_data.size() * sizeof(std::complex<int16_t>));
        rx_file.close();


    } catch (std::exception& e) {
        LOG_FATAL(fmt::format("exception caught: {:s}", e.what()));
        return EXIT_INTERNAL_ERROR;
    }

    LOG_INFO(fmt::format("{:s} finished", argv[0]));
    return EXIT_SUCCESS;
}
