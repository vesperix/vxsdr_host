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
            ("tx_file_name", boost::program_options::value<std::string>()->required(), "TX data file name")
            ("rx_file_name", boost::program_options::value<std::string>()->required(), "RX data file name")
            ("rx_samples", boost::program_options::value<size_t>()->required(), "number of samples to receive")
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

        size_t tx_samp, rx_samp;
        std::ifstream tx_file;
        std::ofstream rx_file;
        tx_file.open(vm["tx_file_name"].as<std::string>(), std::ios::binary | std::ios::in);

        rx_samp = vm["rx_samples"].as<size_t>();

        rx_file.open(vm["rx_file_name"].as<std::string>(), std::ios::binary | std::ios::out);

        if (tx_file.good()) {
            tx_file.seekg(0, std::ios::end);
            auto pos = tx_file.tellg();
            tx_file.seekg(0, std::ios::beg);
            tx_samp = pos / sizeof(std::complex<int16_t>);
        } else {
            LOG_FATAL("unable to open TX data file " + vm["tx_file_name"].as<std::string>());
            return EXIT_CMD_LINE_ERROR;
        }

        if (not rx_file.good()) {
            LOG_FATAL("unable to open RX data file " + vm["rx_file_name"].as<std::string>());
            return EXIT_CMD_LINE_ERROR;
        }

        std::vector<std::complex<int16_t>> tx_data(tx_samp);
        std::vector<std::complex<int16_t>> rx_data(rx_samp);

        tx_file.read(reinterpret_cast<char *>(tx_data.data()), tx_samp * sizeof(std::complex<int16_t>));
        tx_file.close();

        LOG_INFO(fmt::format("using {:d} TX samples", tx_samp));

        LOG_DEBUG("getting radio time");
        auto t1 = radio->get_time_now();
        if (t1.has_value()) {
            LOG_INFO(fmt::format("radio time: {:s}",
                    format_time(std::chrono::round<std::chrono::system_clock::duration>(t1.value()))));
        } else {
            LOG_ERROR("unable to get radio time");
        }

        LOG_INFO(fmt::format("using duration  {:.3f}", vm["duration"].as<double>()));
        LOG_INFO(fmt::format("using frequency {:.5e}", radio->get_tx_freq().value_or(-1)));
        LOG_INFO(fmt::format("using rate      {:.5e}", radio->get_tx_rate().value_or(-1)));
        LOG_INFO(fmt::format("using tx_gain   {:.1f}", radio->get_tx_gain().value_or(-1)));

        auto tx_start = std::chrono::ceil<std::chrono::seconds>(std::chrono::system_clock::now()) + std::chrono::duration(1s);
        unsigned rx_pre_start_ms = 1;
        auto rx_start = tx_start - std::chrono::duration(std::chrono::milliseconds(rx_pre_start_ms));
        LOG_INFO(fmt::format("tx start time: {:s}", format_time(tx_start)));
        LOG_INFO(fmt::format("rx start time: {:s}", format_time(rx_start)));

        if (not radio->tx_loop(tx_start, tx_samp)) {
            LOG_ERROR("tx_loop() failed");
        }

        if (not radio->rx_start(rx_start, rx_samp)) {
            LOG_ERROR("rx_start() failed");
        }

        unsigned tx_pre_start_ms = 100;
        auto tx_load_data = tx_start - std::chrono::duration(std::chrono::milliseconds(tx_pre_start_ms));
        std::this_thread::sleep_until(tx_load_data);

        auto n_sent = radio->put_tx_data(tx_data, tx_samp);
        if (n_sent != tx_samp) {
            LOG_ERROR(fmt::format("{:d} of {:d} samples sent", n_sent, tx_samp));
        }

        auto n_rec = radio->get_rx_data(rx_data, rx_samp);
        if (n_rec != rx_samp) {
            LOG_ERROR(fmt::format("{:d} of {:d} samples received", n_rec, rx_samp));
        }

        radio->tx_stop_now();
        radio->rx_stop_now();

        rx_file.write(reinterpret_cast<char *>(rx_data.data()), rx_samp * sizeof(std::complex<int16_t>));
        rx_file.close();

        LOG_INFO("transmit and receive complete");
    } catch (std::exception& e) {
        LOG_FATAL(fmt::format("exception caught: {:s}", e.what()));
        return EXIT_INTERNAL_ERROR;
    }

    LOG_INFO(fmt::format("{:s} finished", argv[0]));
    return EXIT_SUCCESS;
}
