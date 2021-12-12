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
#define VXSDR_TESTING
#include "logging.hpp"

#include "host_radio_options.hpp"
#include "vxsdr.hpp"

std::string stream_state_to_string(const vxsdr::stream_state_t state) {
    switch (state) {
        case vxsdr::STREAM_WAITING_FOR_START:
            return "WAITING_FOR_START";
        case vxsdr::STREAM_RUNNING:
            return "RUNNING";
        case vxsdr::STREAM_STOPPED:
            return "STOPPED";
        case vxsdr::STREAM_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

unsigned total_errors   = 0;
unsigned total_warnings = 0;

void test_hello(std::unique_ptr<vxsdr>& radio) {
    LOG_INFO("TESTING: starting hello tests");

    LOG_INFO("TESTING:    testing return with default arguments");
    {
        auto result = radio->device_hello();

        if (result) {
            LOG_INFO("TESTING:       passed");
        } else {
            LOG_ERROR("TESTING:      no value returned by device_hello()");
            total_errors++;
        }
    }

    LOG_INFO("TESTING:    testing return with ack requested");
    {
        auto result = radio->device_hello(0, FLAGS_REQUEST_ACK);

        if (result) {
            LOG_INFO("TESTING:       passed");
        } else {
            LOG_ERROR("TESTING:       no value returned by device_hello((0, FLAGS_REQUEST_ACK)");
            total_errors++;
        }
    }
#ifdef TEST_INCORRECT_SUBDEVICE
    LOG_INFO("TESTING:    testing return with incorrect subdevice");
    {
        auto result = radio->device_hello(99);

        if (result) {
            LOG_ERROR("TESTING:       value returned by device_hello(99)");
            total_errors++;
        } else {
            LOG_INFO("TESTING:       passed");
        }
    }
#endif // TEST_INCORRECT_SUBDEVICE
    LOG_INFO("TESTING: completed hello tests");
}

void test_time(std::unique_ptr<vxsdr>& radio, const double time_tol_sec = 0.01) {
    LOG_INFO("TESTING: starting time tests");

    LOG_INFO("TESTING:    testing get_time_now() with default arguments");
    {
        auto result = radio->device_get_time_now();

        if (result) {
            LOG_INFO("TESTING:       passed");
        } else {
            LOG_ERROR("TESTING:      no value returned by device_get_time_now()");
            total_errors++;
        }
    }

    LOG_INFO("TESTING:    testing set_time_now() and get_time_now() with time point");
    {
        auto tp_set = std::chrono::system_clock::now();
        bool sent   = radio->device_set_time_now(tp_set);

        if (not sent) {
            LOG_ERROR("TESTING:      false returned by device_set_time_now()");
            total_errors++;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto result = radio->device_get_time_now();
        if (result) {
            std::chrono::duration<double> diff = result.value() - tp_set;
            if (fabs(1.0 - diff.count()) < time_tol_sec) {
                LOG_INFO("TESTING:       passed with time_point");
            } else {
                LOG_ERROR(fmt::format("TESTING:     time difference is too large: {:e}", diff.count()));
                total_errors++;
            }
        } else {
            LOG_ERROR("TESTING:      no value returned by device_get_time_now()");
            total_errors++;
        }
    }

    LOG_INFO("TESTING:    testing set_time_now and get_time_now with s and ns");
    {
        struct timespec ts_set{};
        timespec_get(&ts_set, TIME_UTC);
        bool sent = radio->device_set_time_now(ts_set.tv_sec, ts_set.tv_nsec);

        if (not sent) {
            LOG_ERROR("TESTING:      false returned by device_set_time_now()");
            total_errors++;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));

        uint32_t t_sec = 0, t_ns = 0;
        bool result = radio->device_get_time_now(t_sec, t_ns);
        if (result) {
            double diff = t_sec - ts_set.tv_sec + 1e-9 * (t_ns - ts_set.tv_nsec);
            if (fabs(1.0 - diff) < time_tol_sec) {
                LOG_INFO("TESTING:       passed with s and ns");
            } else {
                LOG_ERROR(fmt::format("TESTING:     time difference is too large: {:e}", diff));
                total_errors++;
            }
        } else {
            LOG_ERROR("TESTING:      no value returned by device_get_time_now()");
            total_errors++;
        }
    }

    LOG_INFO("TESTING:    testing set_time_now and get_time_now with mixed arguments");
    {
        auto tp_set = std::chrono::system_clock::now();
        bool sent   = radio->device_set_time_now(tp_set);

        if (not sent) {
            LOG_ERROR("TESTING:      false returned by device_set_time_now()");
            total_errors++;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));

        uint32_t t_sec = 0, t_ns = 0;
        bool result = radio->device_get_time_now(t_sec, t_ns);
        if (result) {
            double diff = (t_sec + 1e-9 * t_ns) - std::chrono::duration<double>(tp_set.time_since_epoch()).count();
            if (fabs(1.0 - diff) < time_tol_sec) {
                LOG_INFO("TESTING:       passed set with time_point read with s and ns");
            } else {
                LOG_ERROR(fmt::format("TESTING:     time difference is too large: {:e}", diff));
                total_errors++;
            }
        } else {
            LOG_ERROR("TESTING:      no value returned by device_get_time_now()");
            total_errors++;
        }
        struct timespec ts_set{};
        timespec_get(&ts_set, TIME_UTC);
        sent = radio->device_set_time_now(ts_set.tv_sec, ts_set.tv_nsec);

        if (not sent) {
            LOG_ERROR("TESTING:      false returned by device_set_time_now()");
            total_errors++;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto result2 = radio->device_get_time_now();
        if (result2) {
            double diff =
                std::chrono::duration<double>(result2.value().time_since_epoch()).count() - (ts_set.tv_sec + 1e-9 * ts_set.tv_nsec);
            if (fabs(1.0 - diff) < time_tol_sec) {
                LOG_INFO("TESTING:       passed set with s and ns read with time_point");
            } else {
                LOG_ERROR(fmt::format("TESTING:     time difference is too large: {:e}", diff));
                total_errors++;
            }
        } else {
            LOG_ERROR("TESTING:      no value returned by device_get_time_now()");
            total_errors++;
        }
    }

    LOG_INFO("TESTING: completed time tests");
}

void test_rate(std::unique_ptr<vxsdr>& radio, const double rate_tol_hz = 1.0) {
    LOG_INFO("TESTING: starting rate tests");

    LOG_INFO("TESTING:    testing tx");
    {
        LOG_INFO("TESTING:       getting rate range");

        auto rate = radio->get_tx_rate_range();
        if (rate) {
            LOG_INFO("TESTING:       passed");
        } else {
            LOG_ERROR("TESTING:      no value returned by get_tx_rate_range()");
            total_errors++;
        }

        if (rate) {
            double rmin = rate->at(0), rmax = rate->at(1);

            LOG_INFO("TESTING:       setting maximum rate");
            bool sent = radio->set_tx_rate(rmax);

            if (not sent) {
                LOG_ERROR("TESTING:      false returned by set_tx_rate()");
                total_errors++;
            }

            auto rmx = radio->get_tx_rate();
            if (rmx) {
                double diff = rmx.value() - rmax;
                if (abs(diff) < rate_tol_hz) {
                    LOG_INFO("TESTING:       passed");
                } else {
                    LOG_ERROR("TESTING:       incorrect value returned by get_tx_rate()");
                    total_errors++;
                }
            } else {
                LOG_ERROR("TESTING:      no value returned by get_tx_rate()");
                total_errors++;
            }

            LOG_INFO("TESTING:       setting minimum rate");
            sent = radio->set_tx_rate(rmin);

            if (not sent) {
                LOG_ERROR("TESTING:      false returned by set_tx_rate()");
                total_errors++;
            }

            rmx = radio->get_tx_rate();
            if (rmx) {
                double diff = rmx.value() - rmin;
                if (abs(diff) < rate_tol_hz) {
                    LOG_INFO("TESTING:       passed");
                } else {
                    LOG_ERROR("TESTING:       incorrect value returned by get_tx_rate()");
                    total_errors++;
                }
            } else {
                LOG_ERROR("TESTING:      no value returned by get_tx_rate()");
                total_errors++;
            }

        } else {
            LOG_INFO("TESTING:    skipping tests because of rate range failure");
        }
    }

    LOG_INFO("TESTING:    testing rx");
    {
        LOG_INFO("TESTING:       getting rate range");

        auto rate = radio->get_rx_rate_range();
        if (rate) {
            LOG_INFO("TESTING:       passed");
        } else {
            LOG_ERROR("TESTING:      no value returned by get_rx_rate_range()");
            total_errors++;
        }

        if (rate) {
            double rmin = rate->at(0), rmax = rate->at(1);

            LOG_INFO("TESTING:       setting maximum rate");
            bool sent = radio->set_rx_rate(rmax);

            if (not sent) {
                LOG_ERROR("TESTING:      false returned by set_rx_rate()");
                total_errors++;
            }

            auto rmx = radio->get_rx_rate();
            if (rmx) {
                double diff = rmx.value() - rmax;
                if (abs(diff) < rate_tol_hz) {
                    LOG_INFO("TESTING:       passed");
                } else {
                    LOG_ERROR("TESTING:       incorrect value returned by get_rx_rate()");
                    total_errors++;
                }
            } else {
                LOG_ERROR("TESTING:      no value returned by get_rx_rate()");
                total_errors++;
            }

            LOG_INFO("TESTING:       setting minimum rate");
            sent = radio->set_rx_rate(rmin);

            if (not sent) {
                LOG_ERROR("TESTING:      false returned by set_rx_rate()");
                total_errors++;
            }

            rmx = radio->get_rx_rate();
            if (rmx) {
                double diff = rmx.value() - rmin;
                if (abs(diff) < rate_tol_hz) {
                    LOG_INFO("TESTING:       passed");
                } else {
                    LOG_ERROR("TESTING:       incorrect value returned by get_rx_rate()");
                    total_errors++;
                }
            } else {
                LOG_ERROR("TESTING:      no value returned by get_rx_rate()");
                total_errors++;
            }

        } else {
            LOG_INFO("TESTING:    skipping tests because of rate range failure");
        }
    }

    LOG_INFO("TESTING: completed rate tests");
}

void test_gain(std::unique_ptr<vxsdr>& radio, const double gain_tol_db = 0.1) {
    LOG_INFO("TESTING: starting gain tests");

    LOG_INFO("TESTING:    testing tx");
    {
        LOG_INFO("TESTING:       getting gain range");

        auto gain = radio->get_tx_gain_range();
        if (gain) {
            LOG_INFO("TESTING:       passed");
        } else {
            LOG_ERROR("TESTING:      no value returned by get_tx_gain_range()");
            total_errors++;
        }

        if (gain) {
            double rmin = gain->at(0), rmax = gain->at(1);

            LOG_INFO("TESTING:       setting maximum gain");
            bool sent = radio->set_tx_gain(rmax);

            if (not sent) {
                LOG_ERROR("TESTING:      false returned by set_tx_gain()");
                total_errors++;
            }

            auto rmx = radio->get_tx_gain();
            if (rmx) {
                double diff = rmx.value() - rmax;
                if (abs(diff) < gain_tol_db) {
                    LOG_INFO("TESTING:       passed");
                } else {
                    LOG_ERROR("TESTING:       incorrect value returned by get_tx_gain()");
                    total_errors++;
                }
            } else {
                LOG_ERROR("TESTING:      no value returned by get_tx_gain()");
                total_errors++;
            }

            LOG_INFO("TESTING:       setting minimum gain");
            sent = radio->set_tx_gain(rmin);

            if (not sent) {
                LOG_ERROR("TESTING:      false returned by set_tx_gain()");
                total_errors++;
            }

            rmx = radio->get_tx_gain();
            if (rmx) {
                double diff = rmx.value() - rmin;
                if (abs(diff) < gain_tol_db) {
                    LOG_INFO("TESTING:       passed");
                } else {
                    LOG_ERROR("TESTING:       incorrect value returned by get_tx_gain()");
                    total_errors++;
                }
            } else {
                LOG_ERROR("TESTING:      no value returned by get_tx_gain()");
                total_errors++;
            }

        } else {
            LOG_INFO("TESTING:    skipping tests because of gain range failure");
        }
    }

    LOG_INFO("TESTING:    testing rx");
    {
        LOG_INFO("TESTING:       getting gain range");

        auto gain = radio->get_rx_gain_range();
        if (gain) {
            LOG_INFO("TESTING:       passed");
        } else {
            LOG_ERROR("TESTING:      no value returned by get_rx_gain_range()");
            total_errors++;
        }

        if (gain) {
            double rmin = gain->at(0), rmax = gain->at(1);

            LOG_INFO("TESTING:       setting maximum gain");
            bool sent = radio->set_rx_gain(rmax);

            if (not sent) {
                LOG_ERROR("TESTING:      false returned by set_rx_gain()");
                total_errors++;
            }

            auto rmx = radio->get_rx_gain();
            if (rmx) {
                double diff = rmx.value() - rmax;
                if (abs(diff) < gain_tol_db) {
                    LOG_INFO("TESTING:       passed");
                } else {
                    LOG_ERROR("TESTING:       incorrect value returned by get_rx_gain()");
                    total_errors++;
                }
            } else {
                LOG_ERROR("TESTING:      no value returned by get_rx_gain()");
                total_errors++;
            }

            LOG_INFO("TESTING:       setting minimum gain");
            sent = radio->set_rx_gain(rmin);

            if (not sent) {
                LOG_ERROR("TESTING:      false returned by set_rx_gain()");
                total_errors++;
            }

            rmx = radio->get_rx_gain();
            if (rmx) {
                double diff = rmx.value() - rmin;
                if (abs(diff) < gain_tol_db) {
                    LOG_INFO("TESTING:       passed");
                } else {
                    LOG_ERROR("TESTING:       incorrect value returned by get_rx_gain()");
                    total_errors++;
                }
            } else {
                LOG_ERROR("TESTING:      no value returned by get_rx_gain()");
                total_errors++;
            }

        } else {
            LOG_INFO("TESTING:    skipping tests because of gain range failure");
        }
    }

    LOG_INFO("TESTING: completed gain tests");
}

void test_freq(std::unique_ptr<vxsdr>& radio, const double freq_tol_hz = 1.0) {
    LOG_INFO("TESTING: starting freq tests");

    LOG_INFO("TESTING:    testing tx");
    {
        LOG_INFO("TESTING:       getting freq range");

        auto freq = radio->get_tx_freq_range();
        if (freq) {
            LOG_INFO("TESTING:       passed");
        } else {
            LOG_ERROR("TESTING:      no value returned by get_tx_freq_range()");
            total_errors++;
        }

        if (freq) {
            double rmin = freq->at(0), rmax = freq->at(1);

            LOG_INFO("TESTING:       setting maximum freq");
            bool sent = radio->set_tx_freq(rmax);

            if (not sent) {
                LOG_ERROR("TESTING:      false returned by set_tx_freq()");
                total_errors++;
            }

            auto rmx = radio->get_tx_freq();
            if (rmx) {
                double diff = rmx.value() - rmax;
                if (abs(diff) < freq_tol_hz) {
                    LOG_INFO("TESTING:       passed");
                } else {
                    LOG_ERROR("TESTING:       incorrect value returned by get_tx_freq()");
                    total_errors++;
                }
            } else {
                LOG_ERROR("TESTING:      no value returned by get_tx_freq()");
                total_errors++;
            }

            LOG_INFO("TESTING:       setting minimum freq");
            sent = radio->set_tx_freq(rmin);

            if (not sent) {
                LOG_ERROR("TESTING:      false returned by set_tx_freq()");
                total_errors++;
            }

            rmx = radio->get_tx_freq();
            if (rmx) {
                double diff = rmx.value() - rmin;
                if (abs(diff) < freq_tol_hz) {
                    LOG_INFO("TESTING:       passed");
                } else {
                    LOG_ERROR("TESTING:       incorrect value returned by get_tx_freq()");
                    total_errors++;
                }
            } else {
                LOG_ERROR("TESTING:      no value returned by get_tx_freq()");
                total_errors++;
            }

        } else {
            LOG_INFO("TESTING:    skipping tests because of freq range failure");
        }
    }

    LOG_INFO("TESTING:    testing rx");
    {
        LOG_INFO("TESTING:       getting freq range");

        auto freq = radio->get_rx_freq_range();
        if (freq) {
            LOG_INFO("TESTING:       passed");
        } else {
            LOG_ERROR("TESTING:      no value returned by get_rx_freq_range()");
            total_errors++;
        }

        if (freq) {
            double rmin = freq->at(0), rmax = freq->at(1);

            LOG_INFO("TESTING:       setting maximum freq");
            bool sent = radio->set_rx_freq(rmax);

            if (not sent) {
                LOG_ERROR("TESTING:      false returned by set_rx_freq()");
                total_errors++;
            }

            auto rmx = radio->get_rx_freq();
            if (rmx) {
                double diff = rmx.value() - rmax;
                if (abs(diff) < freq_tol_hz) {
                    LOG_INFO("TESTING:       passed");
                } else {
                    LOG_ERROR("TESTING:       incorrect value returned by get_rx_freq()");
                    total_errors++;
                }
            } else {
                LOG_ERROR("TESTING:      no value returned by get_rx_freq()");
                total_errors++;
            }

            LOG_INFO("TESTING:       setting minimum freq");
            sent = radio->set_rx_freq(rmin);

            if (not sent) {
                LOG_ERROR("TESTING:      false returned by set_rx_freq()");
                total_errors++;
            }

            rmx = radio->get_rx_freq();
            if (rmx) {
                double diff = rmx.value() - rmin;
                if (abs(diff) < freq_tol_hz) {
                    LOG_INFO("TESTING:       passed");
                } else {
                    LOG_ERROR("TESTING:       incorrect value returned by get_rx_freq()");
                    total_errors++;
                }
            } else {
                LOG_ERROR("TESTING:      no value returned by get_rx_freq()");
                total_errors++;
            }

        } else {
            LOG_INFO("TESTING:    skipping tests because of freq range failure");
        }
    }

    LOG_INFO("TESTING: completed freq tests");
}

void test_iq(std::unique_ptr<vxsdr>& radio) {
    LOG_INFO("TESTING: starting iq tests");

    LOG_INFO("TESTING: completed iq tests");
}

void test_stream_state(std::unique_ptr<vxsdr>& radio) {
    LOG_INFO("TESTING: starting stream state tests");

    LOG_INFO("TESTING:    testing tx stop");

    if (not radio->tx_stop_now()) {
        LOG_ERROR("TESTING:       error stopping tx with tx_stop_now()");
        total_errors++;
    }

    auto res = radio->get_tx_stream_state();
    if (res) {
        if (res.value() == vxsdr::STREAM_STOPPED) {
            LOG_INFO("TESTING:       passed");
        } else {
            LOG_ERROR("TESTING:       incorrect value returned by get_tx_stream_state(): {:s}", stream_state_to_string(res.value()));
            total_errors++;
        }
    } else {
        LOG_ERROR("TESTING:      no value returned by get_tx_stream_state()");
        total_errors++;
    }

    LOG_INFO("TESTING:    testing tx start now until stop");

    if (not radio->tx_start_now_until_stop()) {
        LOG_ERROR("TESTING:       error starting tx with tx_start_now_until_stop()");
        total_errors++;
    }

    res = radio->get_tx_stream_state();
    if (res) {
        if (res.value() == vxsdr::STREAM_RUNNING) {
            LOG_INFO("TESTING:       passed");
        } else {
            LOG_ERROR("TESTING:       incorrect value returned by get_tx_stream_state(): {:s}", stream_state_to_string(res.value()));
            total_errors++;
        }
    } else {
        LOG_ERROR("TESTING:      no value returned by get_tx_stream_state()");
        total_errors++;
    }

    if (not radio->tx_stop_now()) {
        LOG_ERROR("TESTING:       error stopping tx with tx_stop_now()");
        total_errors++;
    }

    LOG_INFO("TESTING:    testing tx start at time until stop");

    auto t_start = std::chrono::system_clock::now() + std::chrono::minutes(1);

    if (not radio->tx_start_at_time_until_stop(t_start)) {
        LOG_ERROR("TESTING:       error starting tx with tx_start_at_time_until_stop()");
        total_errors++;
    }

    res = radio->get_tx_stream_state();
    if (res) {
        if (res.value() == vxsdr::STREAM_WAITING_FOR_START) {
            LOG_INFO("TESTING:       passed");
        } else {
            LOG_ERROR("TESTING:       incorrect value returned by get_tx_stream_state(): {:s}", stream_state_to_string(res.value()));
            total_errors++;
        }
    } else {
        LOG_ERROR("TESTING:      no value returned by get_tx_stream_state()");
        total_errors++;
    }

    if (not radio->tx_stop_now()) {
        LOG_ERROR("TESTING:       error stopping tx with tx_stop_now()");
        total_errors++;
    }

    LOG_INFO("TESTING:    testing rx stop");

    if (not radio->rx_stop_now()) {
        LOG_ERROR("TESTING:       error stopping rx with rx_stop_now()");
        total_errors++;
    }

    res = radio->get_rx_stream_state();
    if (res) {
        if (res.value() == vxsdr::STREAM_STOPPED) {
            LOG_INFO("TESTING:       passed");
        } else {
            LOG_ERROR("TESTING:       incorrect value returned by get_rx_stream_state(): {:s}", stream_state_to_string(res.value()));
            total_errors++;
        }
    } else {
        LOG_ERROR("TESTING:      no value returned by get_rx_stream_state()");
        total_errors++;
    }

    LOG_INFO("TESTING:    testing rx start now until stop");

    if (not radio->rx_start_now_until_stop()) {
        LOG_ERROR("TESTING:       error starting rx with rx_start_now_until_stop()");
        total_errors++;
    }

    res = radio->get_rx_stream_state();
    if (res) {
        if (res.value() == vxsdr::STREAM_RUNNING) {
            LOG_INFO("TESTING:       passed");
        } else {
            LOG_ERROR("TESTING:       incorrect value returned by get_rx_stream_state(): {:s}", stream_state_to_string(res.value()));
            total_errors++;
        }
    } else {
        LOG_ERROR("TESTING:      no value returned by get_rx_stream_state()");
        total_errors++;
    }

    if (not radio->rx_stop_now()) {
        LOG_ERROR("TESTING:       error stopping rx with rx_stop_now()");
        total_errors++;
    }

    LOG_INFO("TESTING:    testing rx start at time until stop");

    t_start = std::chrono::system_clock::now() + std::chrono::minutes(1);

    if (not radio->rx_start_at_time_until_stop(t_start)) {
        LOG_ERROR("TESTING:       error starting rx with rx_start_at_time_until_stop()");
        total_errors++;
    }

    res = radio->get_rx_stream_state();
    if (res) {
        if (res.value() == vxsdr::STREAM_WAITING_FOR_START) {
            LOG_INFO("TESTING:       passed");
        } else {
            LOG_ERROR("TESTING:       incorrect value returned by get_rx_stream_state(): {:s}", stream_state_to_string(res.value()));
            total_errors++;
        }
    } else {
        LOG_ERROR("TESTING:      no value returned by get_rx_stream_state()");
        total_errors++;
    }

    if (not radio->rx_stop_now()) {
        LOG_ERROR("TESTING:       error stopping rx with rx_stop_now()");
        total_errors++;
    }


    LOG_INFO("TESTING: completed stream state tests");
}

void test_repeated_commands(std::unique_ptr<vxsdr>& radio, const unsigned iterations) {

    const double rate = 160e6, rate_tol_hz = 1.0, time_tol_sec = 3e-3;;

    for (unsigned i = 0; i < iterations; i++) {
        LOG_INFO(fmt::format("TESTING:   repeated commands iteration {:d} of {:d}", i + 1, iterations));

        LOG_INFO("TESTING:      testing set_time_now() and get_time_now() with time point");
        {
            auto tp_set = std::chrono::system_clock::now();
            bool sent   = radio->device_set_time_now(tp_set);

            if (not sent) {
                LOG_ERROR("TESTING:         false returned by device_set_time_now()");
                total_errors++;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            auto result = radio->device_get_time_now();
            if (result) {
                std::chrono::duration<double> diff = result.value() - tp_set;
                if (fabs(0.01 - diff.count()) < time_tol_sec) {
                    LOG_INFO("TESTING:          passed with time_point");
                } else {
                    LOG_ERROR(fmt::format("TESTING:        time difference is too large: {:e}", diff.count()));
                    total_errors++;
                }
            } else {
                LOG_ERROR("TESTING:         no value returned by device_get_time_now()");
                total_errors++;
            }
        }

        LOG_INFO("TESTING:       setting rx rate");

        bool sent = radio->set_rx_rate(rate);

        if (not sent) {
            LOG_ERROR("TESTING:      false returned by set_rx_rate()");
            total_errors++;
        }

        LOG_INFO("TESTING:       getting rx rate");

        auto res = radio->get_rx_rate();
        if (res) {
            double diff = res.value() - rate;
            if (abs(diff) < rate_tol_hz) {
                LOG_INFO("TESTING:       passed");
            } else {
                LOG_ERROR("TESTING:       incorrect value returned by get_rx_rate()");
                total_errors++;
            }
        } else {
            LOG_ERROR("TESTING:      no value returned by get_rx_rate()");
            total_errors++;
        }
    }
}

void test_capabilities(std::unique_ptr<vxsdr>& radio) {
    LOG_INFO("TESTING: starting capabilities tests");

    LOG_INFO("TESTING: completed capabilities tests");
}

void test_status(std::unique_ptr<vxsdr>& radio) {
    LOG_INFO("TESTING: starting status tests");
    LOG_INFO("TESTING:    getting status");

    auto sta = radio->device_get_status();
    if (sta) {
        LOG_INFO("TESTING:    passed");
    } else {
        LOG_ERROR("TESTING:   no value returned by device_get_status()");
        total_errors++;
    }

    LOG_INFO("TESTING:    clearing status");
    auto sent = radio->device_clear_status();
    if (sent) {
        LOG_INFO("TESTING:    passed");
    } else {
        LOG_ERROR("TESTING:   no value returned by device_clear_status()");
        total_errors++;
    }

    sta = radio->device_get_status();
    if (sta) {
        LOG_INFO("TESTING:    passed");
    } else {
        LOG_ERROR("TESTING:   no value returned by device_get_status()");
        total_errors++;
    }

    LOG_INFO("TESTING: completed status tests");
}

extern "C" void sig_int_handler(int /* sig_num */) {
    LOG_FATAL("terminated by SIGINT (ctrl-C)");
    exit(EXIT_USER_TERMINATED);
}

int main(int argc, char* argv[]) {
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

        net::ip::address_v4 local_ip       = net::ip::address_v4::from_string(vm["local_address"].as<std::string>());
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

        radio = std::make_unique<vxsdr>(local_send_endpoint, local_receive_endpoint, destination_endpoint,
                                        vm["log_packets"].as<bool>());
        // let radio complete initialization
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        set_common_options(vm, radio);
    } catch (std::exception& e) {
        LOG_FATAL(fmt::format("setup exception caught: {:s}", e.what()));
        return EXIT_INTERNAL_ERROR;
    }

    try {
        LOG_INFO(fmt::format("TESTING: tests in {:s} started", argv[0]));
        test_hello(radio);
        test_freq(radio);
        test_rate(radio);
        test_time(radio);
        test_gain(radio);
        test_stream_state(radio);
        test_repeated_commands(radio, 1'000);
        test_status(radio);
        // test_iq(radio);
        // test_capabilities(radio);
    } catch (std::exception& e) {
        LOG_ERROR(fmt::format("TESTING: testing exception caught: {:s}", e.what()));
        return EXIT_INTERNAL_ERROR;
    }

    LOG_INFO(fmt::format("TESTING: tests in {:s} finished with {:d} errors and {:d} warnings", argv[0], total_errors, total_warnings));
    if (total_errors > 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
