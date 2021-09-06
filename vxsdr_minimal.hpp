#include <complex>
#include <vector>
#include <cstring>
#include <future>

#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/use_future.hpp>

#include "logging.hpp"
#include "thread_utils.h"
#include "utility.hpp"

#include "vxsdr_header.hpp"
#include "packet_proc.hpp"

#ifndef VXSDR_MINIMAL_HPP
#define VXSDR_MINIMAL_HPP

class vxsdr {

  private:
    // socket_receive_timeout setting
    static constexpr unsigned receiver_socket_timeout_ms    =      100;

    // thread realtime priority settings
    static constexpr bool thread_set_priority = true;
    static constexpr int send_thread_priority = 20;
    static constexpr int receive_thread_priority = 20;

    // thread cpu affinity settings
    static constexpr bool thread_set_affinity = true;
    static constexpr int send_thread_cpu = 0;
    static constexpr int receive_thread_cpu = 2;

    // parameters of the internal spsc queues (ring buffers)
    static constexpr size_t rx_data_queue_length           =     4096;    // slots in rx data queue
    static constexpr size_t tx_data_queue_length           =     4096;    // slots in tx data queue
    static constexpr size_t command_queue_length           =       64;    // slots in command msg queue
    static constexpr size_t response_queue_length          =      256;    // slots in response msg queue

    // timeouts and waits between tries for queue push/pop

    static constexpr unsigned tx_data_queue_timeout_us     =   100000;
    static constexpr unsigned tx_data_queue_wait_us        =       10;

    static constexpr unsigned rx_data_queue_timeout_us     =   100000;
    static constexpr unsigned rx_data_queue_wait_us        =       10;

    static constexpr unsigned command_queue_timeout_us     =   100000;
    static constexpr unsigned command_queue_wait_us        =       10;

    static constexpr unsigned response_queue_timeout_us    =   100000;
    static constexpr unsigned response_queue_wait_us       =       10;

    // parameters used to set the OS's network buffer
    static constexpr unsigned network_send_buffer_bytes    =   4194304;   // ~6 ms at 160 MSPS
    static constexpr unsigned network_receive_buffer_bytes =   4194304;   // ~6 ms at 160 MSPS

    // parameters used to monitor status of the radio's internal buffers (on the other end of the network connection)
    std::atomic<unsigned> tx_buffer_size = 0, tx_buffer_used = 0;
    std::atomic<unsigned> rx_buffer_size = 0, rx_buffer_used = 0;
    std::atomic<unsigned> tx_buffer_fill_percent = 0, rx_buffer_fill_percent = 0;

    using data_queue_element = largest_data_packet;
    using command_queue_element = largest_cmd_or_rsp_packet;
    using response_queue_element = largest_cmd_or_rsp_packet;

    // data queues
    boost::lockfree::spsc_queue<data_queue_element, boost::lockfree::capacity<rx_data_queue_length> > rx_data_queue;
    boost::lockfree::spsc_queue<data_queue_element, boost::lockfree::capacity<tx_data_queue_length> > tx_data_queue;
    // command and response queues
    boost::lockfree::spsc_queue<command_queue_element, boost::lockfree::capacity<command_queue_length> > command_queue;
    boost::lockfree::spsc_queue<response_queue_element, boost::lockfree::capacity<response_queue_length> > response_queue;

    // Boost context
    boost::asio::io_context context;

    // threads used for sending and receiving
    std::thread sender_thread;
    std::thread receiver_thread;
    std::thread context_thread;

    boost::asio::ip::udp::endpoint destination_endpoint;

    // flags used for orderly shutdown
    std::atomic<bool> sender_thread_stop_flag = false, receiver_thread_stop_flag = false;

    const bool forward_acks = false;

    uint64_t send_errors = 0, packets_sent = 0, bytes_sent = 0;
    std::array<uint64_t, UINT8_MAX + 1> packet_types_sent = { 0 };

    uint64_t packets_received = 0, sequence_errors = 0;
    std::array<uint64_t, UINT8_MAX + 1> packet_types_received = { 0 };

  public:
    // constructor
    vxsdr(const boost::asio::ip::udp::endpoint &local_send_endpoint,
          const boost::asio::ip::udp::endpoint &local_receive_endpoint,
          const boost::asio::ip::udp::endpoint &radio_endpoint) :
          sender_socket(context, local_send_endpoint),
          receiver_socket(context, local_receive_endpoint) {
      LOGGER(debug) << "vxsdr constructor entered";
      tx_data_queue.reset();
      rx_data_queue.reset();
      command_queue.reset();
      response_queue.reset();

      // https://www.boost.org/doc/libs/1_77_0/doc/html/boost_asio/example/cpp11/futures/daytime_client.cpp
      // We run the io_context off in its own thread so that it operates
      // completely asynchronously with respect to the rest of the program.
      auto work = boost::asio::require(context.get_executor(), boost::asio::execution::outstanding_work.tracked);
      context_thread = std::thread([=] { context.run(); });

      destination_endpoint = radio_endpoint;
      receiver_thread = std::thread([=] { receiver(); });
      sender_thread = std::thread([=] { sender(); });
      std::this_thread::sleep_for(std::chrono::milliseconds(1));

      LOGGER(debug) << "vxsdr constructor complete";
    };
    // destructor
    ~vxsdr() {
      LOGGER(debug) << "vxsdr destructor entered";
      LOGGER(debug) << "joining receiver_thread";
      receiver_thread_stop_flag = true;
      if(receiver_thread.joinable()) {
        receiver_thread.join();
      }
      LOGGER(debug) << "joining sender thread";
      sender_thread_stop_flag = true;
      if(sender_thread.joinable()) {
        sender_thread.join();
      }
      LOGGER(debug) << "joining context thread";
      context_thread.join();
      LOGGER(debug) << "vxsdr destructor complete";
    };
    // copy constructor deleted
    vxsdr(const vxsdr &) = delete;
    // copy assignment deleted
    vxsdr& operator=(const vxsdr &) = delete;
    // move constructor deleted
    vxsdr(vxsdr &&) = delete;
    // move assignment deleted
    vxsdr& operator=(vxsdr &&) = delete;

    void device_hello(const uint8_t subdev = 0, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::HELLO, subdev, flags, sizeof(p), 0 };
      cmd_queue_push(p);
    }

   private:
    boost::asio::ip::udp::socket sender_socket;
    boost::asio::ip::udp::socket receiver_socket;

    void receiver()
    {
      LOGGER(debug) << "receiver thread started";

      if(thread_set_priority) {
        if(set_current_thread_priority_realtime(receive_thread_priority) != 0) {
          LOGGER(error) << "unable to set receiver thread realtime priority";
        }
      }

      if(thread_set_affinity) {
        if(set_current_thread_affinity(receive_thread_cpu) != 0) {
          LOGGER(error) << "unable to set receiver thread CPU affinity";
        }
      }

      udp_receiver();
      context.run();
      LOGGER(debug) << "receiver thread finished";
    }

    void sender()
    {
      LOGGER(debug) << "sender thread started";

      if(thread_set_priority) {
        if(set_current_thread_priority_realtime(send_thread_priority) != 0) {
          LOGGER(error) << "unable to set sender thread realtime priority";
        }
      }

      if(thread_set_affinity) {
        if(set_current_thread_affinity(send_thread_cpu) != 0) {
          LOGGER(error) << "unable to set sender thread CPU affinity";
        }
      }

      udp_sender();
      context.run();
      LOGGER(debug) << "sender thread finished";
    }

    void udp_receiver() {

      receiver_socket.set_option(boost::asio::ip::udp::socket::reuse_address(true));
      receiver_socket.set_option(boost::asio::socket_base::receive_buffer_size(network_receive_buffer_bytes));
      boost::asio::socket_base::receive_buffer_size option;
      receiver_socket.get_option(option);
      unsigned network_buffer_size = option.value();
      if(network_buffer_size != network_receive_buffer_bytes) {
        LOGGER(error) << "cannot set network receive buffer size to "
                      << network_receive_buffer_bytes << " (got " << network_buffer_size << ")";
      }
      else {
        LOGGER(info) << "network receive buffer size set to " << network_buffer_size;
      }

      start_receive();
      receiver_socket.close();
    }

    void start_receive() {
      LOGGER(debug) << "start_receive started";
      boost::asio::ip::udp::endpoint remote_endpoint;
      uint16_t last_seq = 0;

      while(not receiver_thread_stop_flag) {

        data_queue_element recv_buffer;
        size_t bytes_received = 0;
#ifdef SYNCH_RX
        bytes_received = receiver_socket.receive_from(boost::asio::buffer(&recv_buffer, sizeof(recv_buffer)), remote_endpoint);
#else
        std::future<size_t> result = receiver_socket.async_receive_from(boost::asio::buffer(&recv_buffer,
                                                                        sizeof(recv_buffer)), remote_endpoint,
                                                                        boost::asio::use_future);
        if(result.wait_for(std::chrono::milliseconds(receiver_socket_timeout_ms)) == std::future_status::timeout) {
          bytes_received = 0;
          //LOGGER(debug) << "timeout";
        }
        else {
          bytes_received = result.get();
        }
#endif
        if(bytes_received >= sizeof(packet) and remote_endpoint == destination_endpoint) {
          LOGGER(debug) << "packet received";
          packets_received++;
          packet_types_received[recv_buffer.hdr.packet_type]++;
          if(packets_received > 1 and recv_buffer.hdr.sequence_counter != (uint16_t)(last_seq + 1)) {
            sequence_errors++;
          }
          LOGGER(info) << "received " << packet_type_to_name(recv_buffer.hdr.packet_type);
        }
      }
      LOGGER(info) << "start_receive exiting; " << packets_received << " packets received, "
                  << sequence_errors << " sequence errors";
      for(unsigned i = 0; i < packet_types_received.size(); i++) {
        if(packet_types_received[i] > 0) {
          LOGGER(info) << (boost::format("   %12d %20s (%d)") % packet_types_received[i] % packet_type_to_name(i) % i);
        }
      }

    }

    void udp_sender()
    {

      sender_socket.set_option(boost::asio::socket_base::send_buffer_size(network_send_buffer_bytes));

      boost::asio::socket_base::send_buffer_size option;
      sender_socket.get_option(option);
      int network_buffer_size = option.value();

      if(network_buffer_size != network_send_buffer_bytes) {
        LOGGER(error) << "cannot set network send buffer size to "
                      << network_send_buffer_bytes << "(got " << network_buffer_size << ")";
      }
      else {
        LOGGER(info) << "network send buffer size set to " << network_buffer_size;
      }

      start_send();
      sender_socket.close();
    }

    void send_packet(packet &packet) {
      packet_types_sent[packet.hdr.packet_type]++;
      packet.hdr.sequence_counter = (uint16_t)(packets_sent++ % (UINT16_MAX + 1));
      size_t bytes = sender_socket.send_to(boost::asio::buffer(&packet, packet.hdr.packet_size), destination_endpoint);
      if(bytes != packet.hdr.packet_size) {
        send_errors++;
      }
      bytes_sent += bytes;
      LOGGER(debug) << "sent " << packet_type_to_name(packet.hdr.packet_type);
    }

    void cmd_queue_push(packet &p) {
      command_queue_element q;
      std::memcpy(&q, &p, std::min((size_t)p.hdr.packet_size, sizeof(q)));
      unsigned wait_time_us = 0;
      // setting command_queue_wait_us = 0 results in a busy wait with no timeout
      while(command_queue.write_available() == 0 and wait_time_us < command_queue_timeout_us) {
        if constexpr(command_queue_wait_us > 0) {
          std::this_thread::sleep_for(std::chrono::microseconds(command_queue_wait_us));
          wait_time_us += command_queue_wait_us;
        }
      }

      if(not command_queue.push(q)) {
        LOGGER(error) << "timeout pushing to command queue";
      }
    }

    void start_send() {
      LOGGER(debug) << "start_send started";
      static constexpr size_t data_buffer_size = 16, cmd_buffer_size = 16;
      std::array<data_queue_element, data_buffer_size> data_buffer;
      std::array<command_queue_element, cmd_buffer_size> cmd_buffer;

      constexpr float throttle_on_percent     = 75;
      constexpr float throttle_off_percent    = 50;
      bool throttling = false;
      constexpr unsigned throttle_amount_us   = 10; // send time for 8200 bytes over 10 GbE is about 6.7 us
      constexpr unsigned send_thread_sleep_us =  5;

      const uint8_t device_subdev = 0, tx_subdev = 0, rx_subdev = 0;

      uint64_t last_check = 0;
      constexpr uint64_t buffer_check_interval = 100; // current buffer size is about 1000 data packets

      while(not sender_thread_stop_flag) {
        if(command_queue.read_available() > 0) {
          size_t n_popped = command_queue.pop(&cmd_buffer.front(), cmd_buffer.size());
          for(size_t i = 0; i < n_popped; i++) {
            if(packets_sent - last_check > buffer_check_interval) {
              // request ack to update buffer use
              cmd_buffer[i].hdr.flags |= FLAGS::REQUEST_ACK;
              send_packet(cmd_buffer[i]);
              last_check = packets_sent;
            }
            else {
              send_packet(cmd_buffer[i]);
            }
          }
        }
        // FIXME: is a timeout needed?
        else if(tx_data_queue.read_available() > 0) {
          size_t n_popped = tx_data_queue.pop(&data_buffer.front(), data_buffer.size());
          for(size_t i = 0; i < n_popped; i++) {
            if(packets_sent - last_check > buffer_check_interval) {
              // request ack to update buffer use
              data_buffer[i].hdr.flags |= FLAGS::REQUEST_ACK;
              send_packet(data_buffer[i]);
              last_check = packets_sent;
            }
            else {
              send_packet(data_buffer[i]);
            }
            // throttle if needed
            if(tx_buffer_fill_percent > throttle_on_percent) {
              throttling = true;
            }
            else if(tx_buffer_fill_percent <= throttle_off_percent) {
              throttling = false;
            }
            if(throttling) {
              std::this_thread::sleep_for(std::chrono::microseconds(throttle_amount_us));
            }
          }
        }
        else {
          if constexpr(send_thread_sleep_us > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(send_thread_sleep_us));
          }
        }
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      LOGGER(info) << "start_send exiting; " << packets_sent << " packets sent, " << send_errors << " send errors";
      for(unsigned i = 0; i < packet_types_sent.size(); i++) {
        if(packet_types_sent[i] > 0) {
          LOGGER(info) << (boost::format("   %12d %20s (%d)") % packet_types_sent[i] % packet_type_to_name(i) % i);
        }
      }
    }
};

#endif // VXSDR_MINIMAL_HPP