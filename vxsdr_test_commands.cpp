#include <iostream>
#include <fstream>
#include <csignal>
#include <complex>
#include <string>
#include <chrono>
#include <ctime>
#include <limits>
#include <cstdlib>
#include <thread>

#include <boost/version.hpp>
#include <boost/program_options.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "logging.hpp"
#include "errors.hpp"

#include "vxsdr.hpp"
#include "host_radio_options.hpp"

unsigned total_errors = 0;
unsigned total_warnings = 0;

void test_hello(std::unique_ptr<vxsdr> &radio) {
  LOG_INFO("starting hello tests");

  LOG_INFO("   testing return with default arguments");
  {
    auto result = radio->device_hello();

    if(result.has_value()) {
      LOG_INFO("      passed");
    } else {
      LOG_ERROR("     no value returned by device_hello()");
      total_errors++;
    }
  }

  LOG_INFO("   testing return with ack requested");
  {
    auto result = radio->device_hello(0, FLAGS_REQUEST_ACK);

    if(result.has_value()) {
      LOG_INFO("      passed");
    } else {
      LOG_ERROR("      no value returned by device_hello((0, FLAGS_REQUEST_ACK)");
      total_errors++;
    }
  }

  LOG_INFO("   testing return with incorrect subdevice");
  {
    auto result = radio->device_hello(99);

    if(result.has_value()) {
      LOG_ERROR("      value returned by device_hello(99)");
      total_errors++;
    } else {
      LOG_INFO("      passed");
    }
  }

  LOG_INFO("completed hello tests");
}

void test_time(std::unique_ptr<vxsdr> &radio) {
  LOG_INFO("starting time tests");

  LOG_INFO("   testing get_time_now() with default arguments");
  {
    auto result = radio->device_get_time_now();

    if(result.has_value()) {
      LOG_INFO("      passed");
    } else {
      LOG_ERROR("     no value returned by device_get_time_now()");
      total_errors++;
    }
  }

  LOG_INFO("   testing set_time_now() and get_time_now() with time point");
  {
    auto tp_set = std::chrono::system_clock::now();
    bool sent = radio->device_set_time_now(tp_set);

    if(not sent) {
      LOG_ERROR("     false returned by device_set_time_now()");
      total_errors++;
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto result = radio->device_get_time_now();
    if(result.has_value()) {
      std::chrono::duration<double> diff = result.value() - tp_set;
      if(diff.count() > 0.99 and diff.count() <= 1.01) {
        LOG_INFO("      passed with time_point");
      } else {
      LOG_ERROR(fmt::format("     time difference is too large: {:e}", diff.count()));
      total_errors++;
      }
    } else {
      LOG_ERROR("     no value returned by device_get_time_now()");
      total_errors++;
    }
  }

  LOG_INFO("   testing set_time_now and get_time_now with s and ns");
  {
    struct timespec ts_set;
    timespec_get(&ts_set, TIME_UTC);
    bool sent = radio->device_set_time_now(ts_set.tv_sec, ts_set.tv_nsec);

    if(not sent) {
      LOG_ERROR("     false returned by device_set_time_now()");
      total_errors++;
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    uint32_t t_sec, t_ns;
    bool result = radio->device_get_time_now(t_sec, t_ns);
    if(result) {
      double diff = t_sec - ts_set.tv_sec + 1e-9 * (t_ns - ts_set.tv_nsec);
      if(diff > 0.99 and diff <= 1.01) {
        LOG_INFO("      passed with s and ns");
      } else {
      LOG_ERROR(fmt::format("     time difference is too large: {:e}", diff));
      total_errors++;
      }
    } else {
      LOG_ERROR("     no value returned by device_get_time_now()");
      total_errors++;
    }
  }

  LOG_INFO("   testing set_time_now and get_time_now with mixed arguments");
  {
    auto tp_set = std::chrono::system_clock::now();
    bool sent = radio->device_set_time_now(tp_set);

    if(not sent) {
      LOG_ERROR("     false returned by device_set_time_now()");
      total_errors++;
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    uint32_t t_sec, t_ns;
    bool result = radio->device_get_time_now(t_sec, t_ns);
    if(result) {
      double diff = std::chrono::duration<double>(tp_set.time_since_epoch()).count() - (t_sec + 1e-9 * t_ns);
      if(diff > 0.99 and diff <= 1.01) {
        LOG_INFO("      passed set with time_point read with s and ns");
      } else {
      LOG_ERROR(fmt::format("     time difference is too large: {:e}", diff));
      total_errors++;
      }
    } else {
      LOG_ERROR("     no value returned by device_get_time_now()");
      total_errors++;
    }
    struct timespec ts_set;
    timespec_get(&ts_set, TIME_UTC);
    sent = radio->device_set_time_now(ts_set.tv_sec, ts_set.tv_nsec);

    if(not sent) {
      LOG_ERROR("     false returned by device_set_time_now()");
      total_errors++;
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto result2 = radio->device_get_time_now();
    if(result2.has_value()) {
      double diff = std::chrono::duration<double>(result2.value().time_since_epoch()).count() - (ts_set.tv_sec + 1e-9 * ts_set.tv_nsec);
      if(diff > 0.99 and diff <= 1.01) {
        LOG_INFO("      passed set with s and ns read with time_point");
      } else {
      LOG_ERROR(fmt::format("     time difference is too large: {:e}", diff));
      total_errors++;
      }
    } else {
      LOG_ERROR("     no value returned by device_get_time_now()");
      total_errors++;
    }
  }

  LOG_INFO("completed time tests");
}

void test_rate(std::unique_ptr<vxsdr> &radio) {
  LOG_INFO("starting rate tests");

  LOG_INFO("completed rate tests");
}

void test_gain(std::unique_ptr<vxsdr> &radio) {
  LOG_INFO("starting gain tests");

  LOG_INFO("completed gain tests");
}

void test_freq(std::unique_ptr<vxsdr> &radio) {
  LOG_INFO("starting freq tests");

  LOG_INFO("completed freq tests");
}

void test_iq(std::unique_ptr<vxsdr> &radio) {
  LOG_INFO("starting iq tests");

  LOG_INFO("completed iq tests");
}

void test_status(std::unique_ptr<vxsdr> &radio) {
  LOG_INFO("starting status tests");

  LOG_INFO("completed status tests");
}

void test_capabilities(std::unique_ptr<vxsdr> &radio) {
  LOG_INFO("starting capabilities tests");

  LOG_INFO("completed capabilities tests");
}

void test_stream(std::unique_ptr<vxsdr> &radio) {
  LOG_INFO("starting stream tests");

  LOG_INFO("completed stream tests");
}

extern "C" void sig_int_handler(int /* sig_num */) {
  LOG_FATAL("terminated by SIGINT (ctrl-C)");
  exit(EXIT_USER_TERMINATED);
}

int main(int argc, char *argv[])
{
  LOG_INIT(argv[0]);
  std::unique_ptr<vxsdr> radio;
  try {
    LOG_INFO(fmt::format("{:s} started", argv[0]));
    signal(SIGINT, &sig_int_handler);
    boost::program_options::options_description desc("Command line options");
    boost::program_options::variables_map vm;

    add_common_options(desc);
    add_network_options(desc);

    init_vm(desc, vm, argc, argv);

    boost::asio::ip::address_v4 local_ip = boost::asio::ip::address_v4::from_string(vm["local_address"].as<std::string>());
    boost::asio::ip::address_v4 destination_ip = boost::asio::ip::address_v4::from_string(vm["destination_address"].as<std::string>());
    boost::asio::ip::udp::endpoint local_send_endpoint(local_ip, vm["local_send_port"].as<unsigned>());
    LOG_INFO(fmt::format("using local send IP {:s} and port {:d}", local_send_endpoint.address().to_string(), local_send_endpoint.port()));
    boost::asio::ip::udp::endpoint local_receive_endpoint(local_ip, vm["local_receive_port"].as<unsigned>());
    LOG_INFO(fmt::format("using local receive IP {:s} and port {:d}", local_receive_endpoint.address().to_string(), local_receive_endpoint.port()));
    boost::asio::ip::udp::endpoint destination_endpoint(destination_ip, vm["destination_port"].as<unsigned>());
    LOG_INFO(fmt::format("using destination IP {:s} and port {:d}", destination_endpoint.address().to_string(), destination_endpoint.port()));

    radio = std::make_unique<vxsdr>(local_send_endpoint, local_receive_endpoint, destination_endpoint, vm["log_packets"].as<bool>());
    // let radio complete initialization
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    set_common_options(vm, radio);
  }
  catch (std::exception& e) {
    LOG_FATAL(fmt::format("setup exception caught: {:s}", e.what()));
    return EXIT_INTERNAL_ERROR;
  }

  try {
    test_hello(radio);
    test_time(radio);
    test_freq(radio);
    test_rate(radio);
    test_gain(radio);
    test_iq(radio);
    test_status(radio);
    test_capabilities(radio);
  }
    catch (std::exception& e) {
    LOG_FATAL(fmt::format("testing exception caught: {:s}", e.what()));
    return EXIT_INTERNAL_ERROR;
  }

  LOG_INFO(fmt::format("{:s} finished", argv[0]));
  return EXIT_SUCCESS;
}
