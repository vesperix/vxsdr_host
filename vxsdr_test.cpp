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
#include <vxsdr.hpp>

#include "errors.hpp"
#include "logging.hpp"
#include "utility.hpp"

#include "host_radio_options.hpp"

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

        init_vm(desc, vm, argc, argv);

        std::unique_ptr<vxsdr> radio;
        try {
            radio = std::make_unique<vxsdr>(vm["local_address"].as<std::string>(), vm["destination_address"].as<std::string>());
        } catch (std::runtime_error& e) {
            LOG_FATAL(fmt::format("exception caught during vxsdr initialization: {:s}", e.what()));
            LOG_FATAL(fmt::format("{:s} finished with exception", argv[0]));
            return EXIT_INTERNAL_ERROR;
        }


        LOG_INFO(fmt::format("using frequency {:.5e}", vm["freq"].as<double>()));
        LOG_INFO(fmt::format("using rate      {:.5e}", vm["rate"].as<double>()));
        LOG_INFO(fmt::format("using tx_gain   {:.1f}", vm["tx_gain"].as<double>()));
        LOG_INFO(fmt::format("using rx_gain   {:.1f}", vm["rx_gain"].as<double>()));

        if (vm["duration"].as<double>() <= 0.0) {
            LOG_FATAL("duration must be positive");
            return EXIT_CMD_LINE_ERROR;
        }

        set_common_options(vm, radio);
        set_rx_1ch_options(vm, radio);
        set_tx_1ch_options(vm, radio);

        auto tnow = radio->get_time_now();
        if (tnow) {
            LOG_INFO(fmt::format("current FPGA time: {:s}", format_time(tnow.value())));
        } else {
            LOG_ERROR("failed to read FPGA time");
        }

        auto tset = std::chrono::system_clock::now();

        LOG_INFO(fmt::format("setting FPGA time to: {:s}", format_time(tset)));

        if (radio->set_time_now(tset)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            tnow = radio->get_time_now();
            if (tnow) {
                LOG_INFO(fmt::format("current FPGA time: {:s}", format_time(tnow.value())));
            } else {
                LOG_ERROR("failed to read FPGA time after set");
            }
        } else {
            LOG_ERROR("failed to set FPGA time");
        }

        size_t n_samp = std::llround(vm["duration"].as<double>() * vm["rate"].as<double>());

        LOG_INFO(fmt::format("preparing {:.3f} seconds ({:d} samples) of data", vm["duration"].as<double>(), n_samp));

        std::vector<std::complex<int16_t> > data(n_samp);

        for (size_t i = 0; i < n_samp; i++) {
            data[i] = std::complex<int16_t>(10000, 0);
        }

        LOG_INFO("getting status...");
        auto status = radio->get_status();
        LOG_INFO("clearing status...");
        radio->clear_status();

        LOG_INFO("sending data...");

        auto t     = std::chrono::system_clock::now();
        auto secs  = std::chrono::time_point_cast<std::chrono::seconds>(t);
        auto t_start = secs + std::chrono::seconds(2);

        if (not radio->tx_start(t_start, n_samp)) {
            LOG_ERROR("error in tx_start_at_time_for_n_samples");
        }
        size_t n_sent = radio->put_tx_data(data, 0, 3.0);
        if (n_sent != n_samp) {
            LOG_ERROR(fmt::format("error in put_tx_data ({:d} of {:d} sent)", n_sent, n_samp));
        }

        LOG_INFO("send complete");

        LOG_INFO("getting status...");
        status = radio->get_status();
        LOG_INFO("clearing status...");
        radio->clear_status();

        std::string filename = vm["prefix"].as<std::string>();
        if (vm.count("no_filename_time") > 0) {
            filename.append(make_filename_no_time(vm["freq"].as<double>(), vm["rate"].as<double>()));
        } else {
            filename.append(make_filename_time(std::chrono::system_clock::now(), vm["freq"].as<double>(), vm["rate"].as<double>()));
        }
        filename.append(vm["suffix"].as<std::string>());

        LOG_INFO(fmt::format("receiving data to file {:s}", filename));

        t     = std::chrono::system_clock::now();
        secs  = std::chrono::time_point_cast<std::chrono::seconds>(t);
        t_start = secs + std::chrono::seconds(2);

        if (not radio->rx_start(t_start, n_samp)) {
            LOG_ERROR("error in rx_start_at_time_for_n_samples");
        }
        //size_t n_rec = radio->get_rx_data(data, n_samp);
        size_t n_rec = radio->check_rx_data_sequence_only(n_samp);
        if (n_rec != n_samp) {
            LOG_ERROR(fmt::format("error in get_rx_data ({:d} of {:d} received)", n_rec, n_samp));
        }
        if (not radio->tx_stop_now()) {
            LOG_ERROR("error in rx_stop_now");
        }

        LOG_INFO("receive complete");

        //size_t n_written = write_cplx_16(filename, data);
        //LOG_INFO(fmt::format("{:d} samples written", n_written));

        LOG_INFO("getting status...");
        status = radio->get_status();
        LOG_INFO("clearing status...");
        radio->clear_status();

    } catch (std::exception& e) {
        LOG_FATAL(fmt::format("exception caught: {:s}", e.what()));
        return EXIT_INTERNAL_ERROR;
    }

    LOG_INFO(fmt::format("{:s} finished", argv[0]));
    return EXIT_SUCCESS;
}
