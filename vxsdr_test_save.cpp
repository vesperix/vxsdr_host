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
#include <arpa/inet.h>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <vxsdr.hpp>

#include "errors.hpp"
#include "logging.hpp"
#include "host_radio_options.hpp"
#include "utility.hpp"

extern "C" void sig_int_handler(int /* sig_num */) {
    LOG_FATAL("terminated by SIGINT (ctrl-C)");
    exit(EXIT_USER_TERMINATED);
}

int main(int argc, char* argv[]) {
    LOG_INIT(argv[0]);
    try {
        LOG_INFO(fmt::format("{:s} started", argv[0]));
        signal(SIGINT, &sig_int_handler);
        option_utils::program_options desc("vxsdr_test_save", "test receive and save to file");

        add_common_options(desc);
        add_network_options(desc);
        add_tx_1ch_options(desc);
        add_rx_1ch_options(desc);

        auto vm = desc.parse(argc, argv);

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

        if (vm.count("network_mtu") > 0) {
            settings["udp_data_transport:mtu_bytes"] = vm["network_mtu"].as<unsigned>();
        }

        auto radio = std::make_unique<vxsdr>(settings);

        set_common_options(vm, radio);
        set_network_options(vm, radio);
        set_tx_1ch_options(vm, radio);
        set_rx_1ch_options(vm, radio);

        if (vm["duration"].as<double>() <= 0.0) {
            LOG_FATAL("duration must be positive");
            return EXIT_CMD_LINE_ERROR;
        }

        constexpr unsigned granularity = 32;

        unsigned n_loop  = 4096;
        unsigned n_repeat = 100;
        size_t n_samp  = granularity * std::ceil(vm["duration"].as<double>() * vm["rate"].as<double>() / granularity);
        size_t n_block = granularity * std::ceil(std::min((long long)n_samp, std::llround(vm["rate"].as<double>())) / granularity);

        std::vector<std::complex<int16_t>> tx_data(n_loop);
        std::vector<std::complex<int16_t>> rx_data(n_block);

        for (unsigned i = 0; i < n_loop; i++) {
            tx_data[i] = std::complex<int16_t>(0, 0);
        }

        LOG_DEBUG("getting radio time");
        auto t1 = radio->get_time_now();
        if (t1.has_value()) {
            LOG_INFO(fmt::format("radio time: {:s}",
                    format_time(std::chrono::round<std::chrono::system_clock::duration>(t1.value()))));
        } else {
            LOG_FATAL("unable to get radio time");
            return EXIT_INTERNAL_ERROR;
        }

        auto start_time = std::chrono::floor<std::chrono::seconds>(t1.value() + vxsdr::duration(2s));
        LOG_INFO(fmt::format("start time: {:s}", format_time(start_time)));

        LOG_INFO(fmt::format("using tx_freq   {:.5e}", radio->get_tx_freq().value_or(-1)));
        LOG_INFO(fmt::format("using tx_rate   {:.5e}", radio->get_tx_rate().value_or(-1)));
        LOG_INFO(fmt::format("using tx_gain   {:.1f}", radio->get_tx_gain().value_or(-1)));

        LOG_INFO(fmt::format("using rx_freq   {:.5e}", radio->get_rx_freq().value_or(-1)));
        LOG_INFO(fmt::format("using rx_rate   {:.5e}", radio->get_rx_rate().value_or(-1)));
        LOG_INFO(fmt::format("using rx_gain   {:.1f}", radio->get_rx_gain().value_or(-1)));

        LOG_INFO(fmt::format("payload size {:d} bytes ({:d} samples)",
                radio->get_max_payload_bytes().value_or(-1),
                radio->get_max_payload_bytes().value_or(-1) / sizeof(vxsdr::wire_sample)));

        size_t n_received = 0;

        if (not radio->tx_loop(start_time + vxsdr::duration(1ms), n_loop, vxsdr::duration::zero(), n_repeat)) {
            LOG_ERROR("tx_loop() failed");
        }

        if (not radio->rx_start(start_time, n_samp)) {
            LOG_ERROR("rx_start() failed");
        }

        constexpr bool write_file{true};

        if (write_file) {
            std::ofstream rx_file ("test_rx_data.dat", std::ofstream::out | std::ofstream::binary);
            while (n_received < n_samp) {
                size_t n_get = std::min(n_samp - n_received, n_block);
                size_t n_rec = radio->get_rx_data(rx_data, n_get);
                n_received += n_rec;
                rx_file.write(reinterpret_cast<char *>(rx_data.data()), n_rec * sizeof(std::complex<int16_t>));
                if (n_rec != n_get) {
                    LOG_ERROR("incorrect number of samples received");
                    break;
                }
            }
        } else {
            while (n_received < n_samp) {
                size_t n_get = std::min(n_samp - n_received, n_block);
                size_t n_rec = radio->get_rx_data(rx_data, n_get);
                n_received += n_rec;
                if (n_rec != n_get) {
                    LOG_ERROR("incorrect number of samples received");
                    break;
                }
            }
        }

        if (not radio->rx_stop()) {
            LOG_ERROR("rx_stop() failed");
        }

        LOG_INFO("receive complete");

        LOG_INFO(fmt::format("{:d} samples received", n_received));

    } catch (std::exception& e) {
        LOG_FATAL(fmt::format("exception caught: {:s}", e.what()));
        return EXIT_INTERNAL_ERROR;
    }

    LOG_INFO(fmt::format("{:s} finished", argv[0]));
    return EXIT_SUCCESS;
}
