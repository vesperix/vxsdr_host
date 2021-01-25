#include <vector>
#include <functional>

#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

#include "logging.hpp"
#include "cycle_counter.h"
#include "thread_utils.h"

#include "vxsdr_header.hpp"
#include "packet_proc.hpp"

#ifndef VXSDR_HPP
#define VXSDR_HPP

class vxsdr {
  public:
    vxsdr(const boost::asio::ip::udp::endpoint &local_receive_endpoint,
          const boost::asio::ip::udp::endpoint &local_send_endpoint,
          const boost::asio::ip::udp::endpoint &destination_endpoint) {
      boost::asio::io_context io_context;
      receiver_thread = std::thread(&vxsdr::receiver, this, std::ref(io_context), std::cref(local_receive_endpoint));
      sender_thread = std::thread(&vxsdr::sender, this, std::ref(io_context), std::cref(local_send_endpoint), std::cref(destination_endpoint));
      //FIXME: more init needed
    };
    ~vxsdr() {
      set_rx_stop_flag();
      set_tx_stop_flag();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      receiver_thread.join();
      sender_thread.join();
      //FIXME:
    };

    static std::string packet_type_to_name(const uint8_t number) {
      switch(number) {
        case PACKET_TYPES::TX_SIGNAL_DATA:
          return "TX_SIGNAL_DATA";
        case PACKET_TYPES::RX_SIGNAL_DATA:
          return "RX_SIGNAL_DATA";
        case PACKET_TYPES::DEVICE_CMD:
          return "DEVICE_CMD";
        case PACKET_TYPES::TX_RADIO_CMD:
          return "TX_RADIO_CMD";
        case PACKET_TYPES::RX_RADIO_CMD:
          return "RX_RADIO_CMD";
        case PACKET_TYPES::DEVICE_CMD_ERR:
          return "DEVICE_CMD_ERR";
        case PACKET_TYPES::TX_RADIO_CMD_ERR:
          return "TX_RADIO_CMD_ERR";
        case PACKET_TYPES::RX_RADIO_CMD_ERR:
          return "RX_RADIO_CMD_ERR";
        case PACKET_TYPES::DEVICE_CMD_RSP:
          return "DEVICE_CMD_RSP";
        case PACKET_TYPES::TX_RADIO_CMD_RSP:
          return "TX_RADIO_CMD_RSP";
        case PACKET_TYPES::RX_RADIO_CMD_RSP:
          return "RX_RADIO_CMD_RSP";
        case PACKET_TYPES::TX_SIGNAL_DATA_ACK:
          return "TX_SIGNAL_DATA_ACK";
        case PACKET_TYPES::RX_SIGNAL_DATA_ACK:
          return "RX_SIGNAL_DATA_ACK";
        case PACKET_TYPES::DEVICE_CMD_ACK:
          return "DEVICE_CMD_ACK";
        case PACKET_TYPES::TX_RADIO_CMD_ACK:
          return "TX_RADIO_CMD_ACK";
        case PACKET_TYPES::RX_RADIO_CMD_ACK:
          return "RX_RADIO_CMD_ACK";
      }
      return "UNDEFINED";
    }
  private:
    // threads used for sending and receiving
    std::thread sender_thread;
    std::thread receiver_thread;

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
    static constexpr size_t rx_msg_queue_length            =     1024;    // slots in rx msg queue
    static constexpr size_t tx_msg_queue_length            =     1024;    // slots in tx msg queue
    // data queues
    static boost::lockfree::spsc_queue<largest_data_packet, boost::lockfree::capacity<rx_data_queue_length> > rx_data_queue;
    static boost::lockfree::spsc_queue<largest_data_packet, boost::lockfree::capacity<tx_data_queue_length> > tx_data_queue;
    // command and response queues
    static boost::lockfree::spsc_queue<largest_cmd_or_rsp_packet, boost::lockfree::capacity<rx_msg_queue_length> > command_queue;
    static boost::lockfree::spsc_queue<largest_cmd_or_rsp_packet, boost::lockfree::capacity<tx_msg_queue_length> > response_queue;

    // parameters for the send thread
    static constexpr int    send_thread_sleep_us           =       10;

    // parameters for the receive thread
    static constexpr int    receive_thread_sleep_us        =        0;

    // parameters used to set the OS's network buffer
    static constexpr unsigned network_send_buffer_bytes    =   4194304;
    static constexpr unsigned network_receive_buffer_bytes =   4194304;

    // parameters used to monitor status of the radio's internal buffers (on the other end of the network connection)
    static std::atomic<unsigned> tx_buffer_size, tx_buffer_used;
    static std::atomic<unsigned> rx_buffer_size, rx_buffer_used;
    static std::atomic<float> tx_buffer_fill_percent, rx_buffer_fill_percent;

    // flags used for orderly shutdown
    static std::atomic<bool> rx_stop_flag, tx_stop_flag, complete_flag;

    static void set_rx_stop_flag() {
      rx_stop_flag = true;
    }

    static void set_tx_stop_flag() {
      tx_stop_flag = true;
    }

    static void set_complete_flag() {
      complete_flag = true;
    }

    class udp_rx {
      public:
        udp_rx(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint)
          : socket(io_context, local_endpoint) {
          socket.set_option(boost::asio::ip::udp::socket::reuse_address(true));
          socket.set_option(boost::asio::socket_base::receive_buffer_size(network_receive_buffer_bytes));

          boost::asio::socket_base::receive_buffer_size option;
          socket.get_option(option);
          int network_buffer_size = option.value();
          LOGGER(info) << "network receive buffer size set to " << network_buffer_size;

          if(not rx_stop_flag)
            start_receive();
        }
      private:
        const bool forward_acks = false;
        boost::asio::ip::udp::socket socket;
        boost::asio::ip::udp::endpoint remote;
        largest_data_packet recv_buffer;
        uint64_t packets_received = 0, seq_errors = 0;
        std::array<uint64_t, UINT8_MAX + 1> packet_types_received = { 0 };
        uint16_t last_seq = 0;
        //unsigned percent, min_buffer_percent = 100, max_buffer_percent = 0;
        void start_receive() {
          LOGGER(info) << "start_receive started";
          // FIXME: need to add timeout if no packets are coming in
          while(not rx_stop_flag) {
            socket.receive_from(boost::asio::buffer(&recv_buffer, sizeof(recv_buffer)), remote);
            packets_received++;
            packet_types_received[recv_buffer.hdr.packet_type]++;
            if(packets_received > 1 and recv_buffer.hdr.sequence_counter != (uint16_t)(last_seq + 1))
              seq_errors++;
            last_seq = recv_buffer.hdr.sequence_counter;
            switch(recv_buffer.hdr.packet_type) {
              // receive buffers go into the rx data queue
              case PACKET_TYPES::RX_SIGNAL_DATA:
                rx_data_queue.push(recv_buffer);
                break;
              // tx sginal data acks special cased since they contain throttling information
              case PACKET_TYPES::TX_SIGNAL_DATA_ACK:
                {
                  tx_signal_data_ack_packet *r = reinterpret_cast<tx_signal_data_ack_packet *>(&recv_buffer);
                  tx_buffer_used = r->tx_buffer_used_bytes;
                  tx_buffer_fill_percent = std::min(100., (100. * tx_buffer_used) / tx_buffer_size);
                  if(forward_acks) {
                    largest_cmd_or_rsp_packet p;
                    memcpy(&p, r, std::min((size_t)r->hdr.packet_size, sizeof(p)));
                    response_queue.push(p);
                  }
                }
                break;
              // the buffer info commands are special cased since they can also be used for TX throttling
              case PACKET_TYPES::DEVICE_CMD_RSP:
                switch(recv_buffer.hdr.command) {
                  case DEVICE_CMD_RSP::GET_BUFFER_USE_RSP:
                    {
                      get_buffer_use_rsp_packet *r = reinterpret_cast<get_buffer_use_rsp_packet *>(&recv_buffer);
                      tx_buffer_used = r->tx_buffer_used_bytes;
                      rx_buffer_used = r->rx_buffer_used_bytes;
                      tx_buffer_fill_percent = std::min(100., (100. * tx_buffer_used) / tx_buffer_size);
                      rx_buffer_fill_percent = std::min(100., (100. * rx_buffer_used) / rx_buffer_size);
                      //largest_cmd_or_rsp_packet p;
                      //memcpy(&p, r, std::min((size_t)r->hdr.packet_size, sizeof(p)));
                      //response_queue.push(p);
                    }
                    break;
                  case DEVICE_CMD_RSP::GET_BUFFER_INFO_RSP:
                    {
                      get_buffer_info_rsp_packet *r = reinterpret_cast<get_buffer_info_rsp_packet *>(&recv_buffer);
                      tx_buffer_size = r->tx_buffer_size_bytes;
                      rx_buffer_size = r->rx_buffer_size_bytes;
                      largest_cmd_or_rsp_packet p;
                      memcpy(&p, r, std::min((size_t)r->hdr.packet_size, sizeof(p)));
                      response_queue.push(p);
                    }
                    break;
                  default:
                    {
                      largest_cmd_or_rsp_packet p;
                      memcpy(&p, &recv_buffer, std::min((size_t)recv_buffer.hdr.packet_size, sizeof(p)));
                      response_queue.push(p);
                    }
                    break;
                }
                break;
              case PACKET_TYPES::DEVICE_CMD_ACK:
              case PACKET_TYPES::RX_RADIO_CMD_ACK:
              case PACKET_TYPES::TX_RADIO_CMD_ACK:
                if(forward_acks) {
                  largest_cmd_or_rsp_packet p;
                  memcpy(&p, &recv_buffer, std::min((size_t)recv_buffer.hdr.packet_size, sizeof(p)));
                  response_queue.push(p);
                }
                break;
              // DEVICE_CMD_RSP handled above
              case PACKET_TYPES::DEVICE_CMD_ERR:
              case PACKET_TYPES::RX_RADIO_CMD_RSP:
              case PACKET_TYPES::RX_RADIO_CMD_ERR:
              case PACKET_TYPES::TX_RADIO_CMD_RSP:
              case PACKET_TYPES::TX_RADIO_CMD_ERR:
                {
                  largest_cmd_or_rsp_packet p;
                  memcpy(&p, &recv_buffer, std::min((size_t)recv_buffer.hdr.packet_size, sizeof(p)));
                  response_queue.push(p);
                }
                break;
              // packet types that should never show up at the host are dropped
              case PACKET_TYPES::TX_SIGNAL_DATA:
              case PACKET_TYPES::DEVICE_CMD:
              case PACKET_TYPES::RX_RADIO_CMD:
              case PACKET_TYPES::TX_RADIO_CMD:
              default: // unrecognized packet types
                break;
            }
            if(receive_thread_sleep_us > 0)
              std::this_thread::sleep_for(std::chrono::microseconds(receive_thread_sleep_us));
          }

          LOGGER(info) << "start_receive exiting; " << packets_received << " packets received, " << seq_errors << " sequence errors";
          for(unsigned i = 0; i < packet_types_received.size(); i++)
            if(packet_types_received[i] > 0)
              LOGGER(info) << (boost::format("   %12d %20s (%d)") % packet_types_received[i] % packet_type_to_name(i) % i);
        }
    };

    class udp_tx {
      public:
        udp_tx(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint,
          const boost::asio::ip::udp::endpoint &destination_endpoint) : socket(io_context, local_endpoint) {
          destination = destination_endpoint;
          socket.set_option(boost::asio::socket_base::send_buffer_size(network_send_buffer_bytes));

          boost::asio::socket_base::send_buffer_size option;
          socket.get_option(option);
          int network_buffer_size = option.value();
          LOGGER(info) << "network send buffer size set to " << network_buffer_size;

          if(not tx_stop_flag)
            start_send();
        }

      private:
        boost::asio::ip::udp::socket socket;
        boost::asio::ip::udp::endpoint destination;
        std::array<uint64_t, UINT8_MAX + 1> packet_types_processed = { 0 };
        uint64_t send_errors = 0, packets_sent = 0, bytes_sent = 0;

        void send_packet(packet &packet) {
          packet_types_processed[packet.hdr.packet_type]++;
          packet.hdr.sequence_counter = (uint16_t)(packets_sent++ % (1u << 16));
          size_t bytes = socket.send_to(boost::asio::buffer(&packet, packet.hdr.packet_size), destination);
          if(bytes != packet.hdr.packet_size)
            send_errors++;
          bytes_sent += bytes;
        }

        void initialize_tx(const uint8_t device_subdev, const uint8_t tx_subdev) {
          send_tx_stop_now(tx_subdev);
          // get device buffer sizes
          send_device_get_buffer_info(device_subdev);
          // wait for the response come back and be processed
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          // clear the status registers
          send_device_clear_status(device_subdev);
          send_device_get_status(device_subdev);
        }

        void close_tx(const uint8_t device_subdev, const uint8_t tx_subdev) {
          // get the status at the end
          send_device_get_status(device_subdev);
          // send a stop, requesting an ack so that the rx will see an incoming packet
          send_tx_stop_now(tx_subdev, FLAGS::REQUEST_ACK);
        }

        void start_send() {
          LOGGER(info) << "start_send started";
          static constexpr size_t data_buffer_size = 16, cmd_buffer_size = 16;
          std::array<largest_data_packet, data_buffer_size> data_buffer;
          std::array<largest_cmd_or_rsp_packet, cmd_buffer_size> cmd_buffer;

          unsigned tx_throttle_wait_us = 0;
          constexpr unsigned throttle_start_percent = 75;
          constexpr unsigned throttle_stop_percent  = 50;
          constexpr unsigned throttle_amount_us =     10; // send time for 8200 bytes over 10 GbE is about 6.7 us

          const uint8_t device_subdev = 0, tx_subdev = 0;

          initialize_tx(device_subdev, tx_subdev);

          uint64_t last_check = 0;
          constexpr uint64_t buffer_check_interval = 100; // current buffer size is about 1000 data packets

          while(not tx_stop_flag) {
            if(command_queue.read_available()) {
              size_t n_popped = command_queue.pop(&cmd_buffer.front(), cmd_buffer.size());
              for(size_t i = 0; i < n_popped; i++) {
                send_packet(cmd_buffer[i]);
              }
            }
            else if(tx_data_queue.read_available()) {
              size_t n_popped = tx_data_queue.pop(&data_buffer.front(), data_buffer.size());
              for(size_t i = 0; i < n_popped; i++) {
                if(packets_sent - last_check > buffer_check_interval) {
                  // request ack to update buffer use
                  data_buffer[i].hdr.flags |= FLAGS::REQUEST_ACK;
                  send_packet(cmd_buffer[i]);
                  last_check = packets_sent;
                }
                else {
                  send_packet(data_buffer[i]);
                }
                // update throttling
                if(tx_buffer_fill_percent > throttle_start_percent)
                  tx_throttle_wait_us = throttle_amount_us;
                else if (tx_buffer_fill_percent < throttle_stop_percent)
                  tx_throttle_wait_us = 0;

                if(tx_throttle_wait_us > 0)
                  std::this_thread::sleep_for(std::chrono::microseconds(tx_throttle_wait_us));
              }
            }
            else {
              if(send_thread_sleep_us > 0)
                std::this_thread::sleep_for(std::chrono::microseconds(send_thread_sleep_us));
            }
          }
          close_tx(device_subdev, tx_subdev);
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          LOGGER(info) << "start_send exiting; " << packets_sent << " packets sent, " << send_errors << " send errors";
          for(unsigned i = 0; i < packet_types_processed.size(); i++)
            if(packet_types_processed[i] > 0)
              LOGGER(info) << (boost::format("   %12d %20s (%d)") % packet_types_processed[i] % packet_type_to_name(i) % i);

        }
    };

    void receiver(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint)
    {
      LOGGER(info) << "receiver thread started";

      if(thread_set_priority)
        if(set_current_thread_priority_realtime(receive_thread_priority))
          LOGGER(error) << "unable to set receiver thread realtime priority";

      if(thread_set_affinity)
        if(set_current_thread_affinity(receive_thread_cpu))
          LOGGER(error) << "unable to set receiver thread CPU affinity";

      udp_rx server(io_context, local_endpoint);
      io_context.run();
      LOGGER(info) << "receiver thread finished";
    }

    void sender(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint,
          const boost::asio::ip::udp::endpoint &destination_endpoint)
    {
      LOGGER(info) << "sender thread started";

      if(thread_set_priority)
        if(set_current_thread_priority_realtime(send_thread_priority))
          LOGGER(error) << "unable to set sender thread realtime priority";

      if(thread_set_affinity)
        if(set_current_thread_affinity(send_thread_cpu))
          LOGGER(error) << "unable to set sender thread CPU affinity";

      udp_tx server(io_context, local_endpoint, destination_endpoint);
      io_context.run();
      LOGGER(info) << "sender thread finished";
    }

  public:
    static void send_device_hello(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::HELLO, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

    static void send_device_reset(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::RESET, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

    static void send_device_open_session(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::OPEN_SESSION, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

    static void send_device_close_session(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::CLOSE_SESSION, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

    static void send_device_get_buffer_info(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::GET_BUFFER_INFO, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

    static void send_device_get_buffer_use(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::GET_BUFFER_USE, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

    static void send_device_clear_status(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::CLEAR_STATUS, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

    static void send_device_get_status(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::GET_STATUS, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

    static void send_tx_start_now_until_stop(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::TX_RADIO_CMD, RADIO_CMD::START_NOW_UNTIL_STOP, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

    static void send_rx_start_now_until_stop(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::RX_RADIO_CMD, RADIO_CMD::START_NOW_UNTIL_STOP, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

    static void send_tx_start_now_for_n_samples(const uint64_t n, const uint8_t subdev = 0, const uint8_t flags = 0) {
      stream_ctrl_packet_samples p;
      p.hdr = { PACKET_TYPES::TX_RADIO_CMD, RADIO_CMD::START_NOW_UNTIL_STOP, subdev, flags, sizeof(packet_header), 0 };
      p.n_samples = n;
      largest_cmd_or_rsp_packet q;
      memcpy(&q, &p, p.hdr.packet_size);
      command_queue.push(q);
    }

    static void send_rx_start_now_for_n_samples(const uint64_t n, const uint8_t subdev = 0, const uint8_t flags = 0) {
      stream_ctrl_packet_samples p;
      p.hdr = { PACKET_TYPES::RX_RADIO_CMD, RADIO_CMD::START_NOW_UNTIL_STOP, subdev, flags, sizeof(packet_header), 0 };
      p.n_samples = n;
      largest_cmd_or_rsp_packet q;
      memcpy(&q, &p, p.hdr.packet_size);
      command_queue.push(q);
    }

    static void send_tx_stop_now(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::TX_RADIO_CMD, RADIO_CMD::STOP_NOW, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

    static void send_rx_stop_now(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::RX_RADIO_CMD, RADIO_CMD::STOP_NOW, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }
};

#endif // VXSDR_HPP