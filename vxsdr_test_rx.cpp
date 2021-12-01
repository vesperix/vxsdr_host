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

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/program_options.hpp>
#include <boost/version.hpp>

#include "errors.hpp"
#include "logging.hpp"

#include "host_radio_options.hpp"
#include "vxsdr.hpp"

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
        add_rx_1ch_options(desc);

        init_vm(desc, vm, argc, argv);

        net::ip::address_v4 local_ip = net::ip::address_v4::from_string(vm["local_address"].as<std::string>());
        net::ip::address_v4 destination_ip = net::ip::address_v4::from_string(vm["destination_address"].as<std::string>());
        net::ip::udp::endpoint local_send_endpoint(local_ip, vm["local_send_port"].as<unsigned>());
        LOG_INFO(fmt::format("using local send IP {:s} and port {:d}", local_send_endpoint.address().to_string(),
                             local_send_endpoint.port()));
        net::ip::udp::endpoint local_receive_endpoint(local_ip, vm["local_receive_port"].as<unsigned>());
        LOG_INFO(fmt::format("using local receive IP {:s} and port {:d}", local_receive_endpoint.address().to_string(),
                             local_receive_endpoint.port()));
        net::ip::udp::endpoint destination_endpoint(destination_ip, vm["destination_port"].as<unsigned>());
        LOG_INFO(fmt::format("using destination IP {:s} and port {:d}", destination_endpoint.address().to_string(),
                             destination_endpoint.port()));

        auto radio =
            std::make_unique<vxsdr>(local_send_endpoint, local_receive_endpoint, destination_endpoint, vm["log_packets"].as<bool>(),
                                    vm["set_affinity"].as<bool>(), vm["affinity_base_cpu"].as<unsigned>(),
                                    vm["set_priority"].as<bool>(), vm["thread_priority"].as<int>());
        // let radio complete initialization
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        set_common_options(vm, radio);
        set_rx_1ch_options(vm, radio);

        if (vm["duration"].as<double>() <= 0.0) {
            LOG_FATAL("duration must be positive");
            return EXIT_CMD_LINE_ERROR;
        }

        size_t n_samp = std::llround(vm["duration"].as<double>() * vm["rate"].as<double>());

        std::string filename = vm["prefix"].as<std::string>();
        if (vm.count("no_filename_time") > 0) {
            filename.append(make_filename_no_time(vm["freq"].as<double>(), vm["rate"].as<double>()));
        } else {
            filename.append(make_filename_time(std::chrono::system_clock::now(), vm["freq"].as<double>(), vm["rate"].as<double>()));
        }
        filename.append(vm["suffix"].as<std::string>());

        LOG_DEBUG("sleeping for 1 s");
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        LOG_DEBUG("getting time once");
        auto t1 = radio->device_get_time_now();
        if (t1.has_value()) {
            LOG_INFO(fmt::format("t1:           {:s}", format_time(t1.value())));
        } else {
            LOG_INFO("t1 has no value");
        }
        LOG_DEBUG("sleeping for 1 s");
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        LOG_DEBUG("getting time again");
        auto t2 = radio->device_get_time_now();
        if (t2.has_value()) {
            LOG_INFO(fmt::format("t2:           {:s}", format_time(t2.value())));
        } else {
            LOG_INFO("t2 has no value");
        }

        LOG_DEBUG("setting time to system clock");
        if (not radio->device_set_time_now(std::chrono::system_clock::now())) {
            LOG_ERROR("device_set_time_now() failed");
        }
        LOG_DEBUG("getting time again");
        auto t3 = radio->device_get_time_now();
        if (t3.has_value()) {
            LOG_INFO(fmt::format("t3:           {:s}", format_time(t3.value())));
        } else {
            LOG_INFO("t3 has no value");
        }

        LOG_INFO(fmt::format("using frequency {:.5e}", radio->get_rx_freq().value_or(-1)));
        LOG_INFO(fmt::format("using rate      {:.5e}", radio->get_rx_rate().value_or(-1)));
        LOG_INFO(fmt::format("using rx_gain   {:.1f}", radio->get_rx_gain().value_or(-1)));

        // radio->rx_start_now_for_n_samples(n_samp);
        if (not radio->rx_start_now_until_stop()) {
            LOG_ERROR("rx_start_now_until_stop() failed");
        }
        size_t n_written = radio->check_rx_data_sequence_only(n_samp);
        if (not radio->rx_stop_now()) {
            LOG_ERROR("rx_stop_now() failed");
        }

        LOG_INFO("receive complete");
        // outfile.close();
        // LOG_INFO(fmt::format("{:d} samples written", n_written));
        LOG_INFO(fmt::format("{:d} samples checked", n_written));

    } catch (std::exception& e) {
        LOG_FATAL(fmt::format("exception caught: {:s}", e.what()));
        return EXIT_INTERNAL_ERROR;
    }

    LOG_INFO(fmt::format("{:s} finished", argv[0]));
    return EXIT_SUCCESS;
}
