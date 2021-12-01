#include <chrono>
#include <complex>
#include <iostream>
#include <thread>
#include <vector>

#include <boost/lockfree/spsc_queue.hpp>

#include "vxsdr_header.hpp"

static constexpr unsigned push_queue_timeout_us = 100000;
static constexpr unsigned pop_queue_timeout_us = 100000;

static constexpr unsigned push_queue_sleep_us = 10;
static constexpr unsigned pop_queue_sleep_us = 10;

static constexpr unsigned push_queue_interval_us = 0;
static constexpr unsigned pop_queue_interval_us = 0;

static constexpr size_t queue_length = 4096;

static boost::lockfree::spsc_queue<largest_data_packet, boost::lockfree::capacity<queue_length> > queue;

void producer(const size_t n_items) {
    auto t0 = std::chrono::system_clock::now();

    for (size_t i = 0; i < n_items; i++) {
        largest_data_packet p;
        p.hdr.sequence_counter = i % UINT16_MAX;

        unsigned wait_time_us = 0;
        while (queue.write_available() == 0 and wait_time_us < push_queue_timeout_us) {
            if constexpr (push_queue_sleep_us > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(push_queue_sleep_us));
                wait_time_us += push_queue_sleep_us;
            }
        }

        if (not queue.push(p)) {
            std::cout << "producer: timeout waiting for push" << std::endl;
            break;
        }
        // std::cout << "producer: pushed " << i << std::endl;
        if constexpr (push_queue_interval_us > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(push_queue_interval_us));
        }
    }

    auto t1 = std::chrono::system_clock::now();
    std::chrono::duration<double> d = t1 - t0;
    std::cout << "producer: " << n_items << " pushed in " << d.count() << " sec" << std::endl;
}

void consumer(const size_t n_items) {
    auto t0 = std::chrono::system_clock::now();

    for (size_t i = 0; i < n_items; i++) {
        largest_data_packet p;

        unsigned wait_time_us = 0;
        while (queue.read_available() == 0 and wait_time_us < pop_queue_timeout_us) {
            if constexpr (pop_queue_sleep_us > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(pop_queue_sleep_us));
                wait_time_us += pop_queue_sleep_us;
            }
        }

        if (not queue.pop(p)) {
            std::cout << "consumer: timeout waiting for pop" << std::endl;
            break;
        }
        // std::cout << "consumer: popped " << i << std::endl;

        if (p.hdr.sequence_counter != i % UINT16_MAX) {
            std::cout << "consumer: data error" << std::endl;
            break;
        }
        if constexpr (pop_queue_interval_us > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(pop_queue_interval_us));
        }
    }

    auto t1 = std::chrono::system_clock::now();
    std::chrono::duration<double> d = t1 - t0;
    std::cout << "consumer: " << n_items << " popped in " << d.count() << " sec" << std::endl;
}

int main() {
    size_t n_items = 7812500;

    queue.reset();

    auto consumer_thread = std::thread(&consumer, n_items);
    auto producer_thread = std::thread(&producer, n_items);

    producer_thread.join();
    consumer_thread.join();

    return 0;
}