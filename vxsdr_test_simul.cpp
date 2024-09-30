#include <chrono>
#include <complex>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
//#include <thread>
#include <future>
#include <arpa/inet.h>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <vxsdr.hpp>

#include "errors.hpp"
#include "logging.hpp"
#include "option_utils.hpp"
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
        option_utils::program_options desc("vxsdr_test_simul", "test simultaneous transmit and receive");

        add_common_options(desc);
        add_network_options(desc);
        add_rx_1ch_options(desc);
        add_tx_1ch_options(desc);

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

        if (vm.count("udp_data_transport:mtu_bytes") > 0) {
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

        size_t granularity = 2048;
        size_t n_samp  = std::llround(vm["duration"].as<double>() * vm["rate"].as<double>());
        size_t n_block = granularity * (std::min((long long)n_samp, std::llround(vm["rate"].as<double>())) / granularity);

        std::vector<std::complex<int16_t>> tx_data(n_block);
        std::vector<std::complex<int16_t>> rx_data(n_block);

        LOG_DEBUG("getting radio time");
        auto t1 = radio->get_time_now();
        if (t1.has_value()) {
            LOG_INFO(fmt::format("radio time: {:s}",
                    format_time(std::chrono::round<std::chrono::system_clock::duration>(t1.value()))));
        } else {
            LOG_ERROR("unable to get radio time");
            return EXIT_FAILURE;
        }

        auto start_time = std::chrono::floor<std::chrono::seconds>(t1.value() + vxsdr::duration(2s));
        LOG_INFO(fmt::format("start time: {:s}", format_time(start_time)));

        LOG_INFO(fmt::format("using frequency {:.5e}", radio->get_rx_freq().value_or(-1)));
        LOG_INFO(fmt::format("using rate      {:.5e}", radio->get_rx_rate().value_or(-1)));
        LOG_INFO(fmt::format("using rx_gain   {:.1f}", radio->get_rx_gain().value_or(-1)));

        LOG_DEBUG("getting buffer info");
        auto buf_info = radio->get_buffer_info();
        if (not buf_info) {
             LOG_ERROR("unable to get buffer info");
            return EXIT_FAILURE;
        }

        auto tx_buffer_bytes = buf_info->at(1);
        // how many seconds of data does the tx buffer hold at the current rate?
        double tx_buffer_duration_s = tx_buffer_bytes / (vm["network_bit_rate"].as<double>() / 8);

        LOG_INFO(fmt::format("payload size    {:d} bytes",
                radio->get_max_payload_bytes().value_or(-1)));
        LOG_INFO(fmt::format("payload size    {:d} samples",
                radio->get_max_payload_bytes().value_or(-1) / sizeof(vxsdr::wire_sample)));

        LOG_INFO(fmt::format("buffer size     {:d} bytes", tx_buffer_bytes));
        LOG_INFO(fmt::format("buffer size     {:d} samples", tx_buffer_bytes / sizeof(vxsdr::wire_sample)));
        LOG_INFO(fmt::format("network rate    {:.2e} bytes/s", vm["network_bit_rate"].as<double>() / 8));
        LOG_INFO(fmt::format("buffer duration {:.2e} s", tx_buffer_duration_s));

        if (not radio->rx_start(start_time, n_samp)) {
            LOG_ERROR("rx_start() failed");
        }

        if (not radio->tx_start(start_time, n_samp)) {
            LOG_ERROR("tx_start() failed");
        }

        // start early so the tx buffer will be 1/4 full when tx starts
        vxsdr::duration head_start = std::chrono::microseconds(std::llround(2.5e5 * tx_buffer_duration_s));
        //vxsdr::duration head_start = std::chrono::duration_cast<vxsdr::duration>(std::chrono::duration<double>(0.25 * tx_buffer_duration_s));
        std::this_thread::sleep_until(start_time - head_start);
/*
        size_t n_checked = 0;
        std::thread rx_thread([&n_checked, n_samp, n_block, &rx_data, &radio] ()
                                {
                                    while (n_checked < n_samp) {
                                        auto n_to_get = std::min(n_samp - n_checked, n_block);
                                        auto n_rec = radio->get_rx_data(rx_data, n_to_get);
                                        n_checked += n_rec;
                                        if (n_rec != n_to_get) {
                                            break;
                                        }
                                    }
                                });

        size_t n_sent = 0;
        std::thread tx_thread([&n_sent, n_samp, n_block, &tx_data, &radio] ()
                                {
                                    while (n_sent < n_samp) {
                                        auto n_to_send = std::min(n_samp - n_sent, n_block);
                                        auto n_put = radio->put_tx_data(tx_data, n_to_send);
                                        n_sent += n_put;
                                        if (n_put != n_to_send) {
                                            break;
                                        }
                                    }
                                });
*/


        auto rx_future = std::async(std::launch::async, [n_samp, n_block, &rx_data, &radio] ()
                                    {
                                        size_t n_checked = 0;
                                        while (n_checked < n_samp) {
                                            auto n_to_get = std::min(n_samp - n_checked, n_block);
                                            auto n_rec = radio->get_rx_data(rx_data, n_to_get);
                                            n_checked += n_rec;
                                            if (n_rec != n_to_get) {
                                                break;
                                            }
                                        }
                                        return n_checked;
                                    });


        auto tx_future = std::async(std::launch::async, [n_samp, n_block, &tx_data, &radio] ()
                                    {
                                        size_t n_sent = 0;
                                        while (n_sent < n_samp) {
                                            auto n_to_send = std::min(n_samp - n_sent, n_block);
                                            auto n_put = radio->put_tx_data(tx_data, n_to_send);
                                            n_sent += n_put;
                                            if (n_put != n_to_send) {
                                                break;
                                            }
                                        }
                                        return n_sent;
                                    });

        std::this_thread::sleep_until(start_time + std::chrono::duration<double>(vm["duration"].as<double>()));

/*
        LOG_INFO("joining rx thread");
        rx_thread.join();

        if (not radio->rx_stop()) {
            LOG_ERROR("rx_stop() failed");
        }

        LOG_INFO("receive complete");
        LOG_INFO(fmt::format("{:d} samples checked", n_checked));

        LOG_INFO("joining tx thread");
        tx_thread.join();

        if (not radio->tx_stop()) {
            LOG_ERROR("tx_stop() failed");
        }
*/

        LOG_INFO("receive complete");
        LOG_INFO(fmt::format("{:d} samples checked", rx_future.get()));

        LOG_INFO("transmit complete");
        LOG_INFO(fmt::format("{:d} samples sent", tx_future.get()));

    } catch (std::exception& e) {
        LOG_FATAL(fmt::format("exception caught: {:s}", e.what()));
        return EXIT_INTERNAL_ERROR;
    }

    LOG_INFO(fmt::format("{:s} finished", argv[0]));
    return EXIT_SUCCESS;
}
