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

  private:
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

    // parameters for the send thread
    static constexpr int    send_thread_sleep_us           =       10;

    // parameters for the receive thread
    static constexpr int    receive_thread_sleep_us        =        0;

    // parameters used to set the OS's network buffer
    static constexpr unsigned network_send_buffer_bytes    =   4194304;
    static constexpr unsigned network_receive_buffer_bytes =   4194304;

    // parameters used to monitor status of the radio's internal buffers (on the other end of the network connection)
    std::atomic<unsigned> tx_buffer_size, tx_buffer_used;
    std::atomic<unsigned> rx_buffer_size, rx_buffer_used;
    std::atomic<float> tx_buffer_fill_percent, rx_buffer_fill_percent;

    // data queues
    boost::lockfree::spsc_queue<largest_data_packet, boost::lockfree::capacity<rx_data_queue_length> > rx_data_queue;
    boost::lockfree::spsc_queue<largest_data_packet, boost::lockfree::capacity<tx_data_queue_length> > tx_data_queue;
    // command and response queues
    boost::lockfree::spsc_queue<largest_cmd_or_rsp_packet, boost::lockfree::capacity<rx_msg_queue_length> > command_queue;
    boost::lockfree::spsc_queue<largest_cmd_or_rsp_packet, boost::lockfree::capacity<tx_msg_queue_length> > response_queue;

    // threads used for sending and receiving
    std::thread sender_thread;
    std::thread receiver_thread;

    std::string packet_type_to_name(const uint8_t number) {
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

    // flags used for orderly shutdown
    std::atomic<bool> rx_stop_flag, tx_stop_flag;

  public:
    // constructor
    vxsdr(const boost::asio::ip::udp::endpoint &local_receive_endpoint,
          const boost::asio::ip::udp::endpoint &local_send_endpoint,
          const boost::asio::ip::udp::endpoint &destination_endpoint) {
      boost::asio::io_context io_context;
      receiver_thread = std::thread(&vxsdr::receiver, this, std::ref(io_context), std::cref(local_receive_endpoint));
      sender_thread = std::thread(&vxsdr::sender, this, std::ref(io_context), std::cref(local_send_endpoint), std::cref(destination_endpoint));
      tx_stop_flag = false;
      rx_stop_flag = false;
      //FIXME: more init needed
    };
    // destructor
    ~vxsdr() {
      rx_stop_flag = true;
      tx_stop_flag = true;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      receiver_thread.join();
      sender_thread.join();
      //FIXME:
    };
    // copy constructor
    // copy assignment
    // move constructor
    // move assignment

    void send_device_hello(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::HELLO, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

    void send_device_reset(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::RESET, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

    void send_device_open_session(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::OPEN_SESSION, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

    void send_device_close_session(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::CLOSE_SESSION, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

    void send_device_get_buffer_info(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::GET_BUFFER_INFO, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

    void send_device_get_buffer_use(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::GET_BUFFER_USE, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

    void send_device_clear_status(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::CLEAR_STATUS, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

    void send_device_get_status(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::GET_STATUS, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

    void send_tx_start_now_until_stop(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::TX_RADIO_CMD, RADIO_CMD::START_NOW_UNTIL_STOP, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

    void send_rx_start_now_until_stop(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::RX_RADIO_CMD, RADIO_CMD::START_NOW_UNTIL_STOP, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

    void send_tx_start_now_for_n_samples(const uint64_t n, const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet q;
      auto *p = reinterpret_cast<stream_ctrl_packet_samples *>(&q);
      p->hdr = { PACKET_TYPES::TX_RADIO_CMD, RADIO_CMD::START_NOW_FOR_N_SAMPLES, subdev, flags, sizeof(packet_header), 0 };
      p->n_samples = n;
      command_queue.push(q);
    }

    void send_rx_start_now_for_n_samples(const uint64_t n, const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet q;
      auto *p = reinterpret_cast<stream_ctrl_packet_samples *>(&q);
      p->hdr = { PACKET_TYPES::RX_RADIO_CMD, RADIO_CMD::START_NOW_FOR_N_SAMPLES, subdev, flags, sizeof(packet_header), 0 };
      p->n_samples = n;
      command_queue.push(q);
    }

    void send_tx_stop_now(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::TX_RADIO_CMD, RADIO_CMD::STOP_NOW, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

    void send_rx_stop_now(const uint8_t subdev = 0, const uint8_t flags = 0) {
      largest_cmd_or_rsp_packet p;
      p.hdr = { PACKET_TYPES::RX_RADIO_CMD, RADIO_CMD::STOP_NOW, subdev, flags, sizeof(packet_header), 0 };
      command_queue.push(p);
    }

  private:
    void receiver(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint)
    {
      LOGGER(info) << "receiver thread started";

      if(thread_set_priority)
        if(set_current_thread_priority_realtime(receive_thread_priority) != 0)
          LOGGER(error) << "unable to set receiver thread realtime priority";

      if(thread_set_affinity)
        if(set_current_thread_affinity(receive_thread_cpu) != 0)
          LOGGER(error) << "unable to set receiver thread CPU affinity";

      udp_rx server(this, io_context, local_endpoint);
      io_context.run();
      LOGGER(info) << "receiver thread finished";
    }

    void sender(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint,
          const boost::asio::ip::udp::endpoint &destination_endpoint)
    {
      LOGGER(info) << "sender thread started";

      if(thread_set_priority)
        if(set_current_thread_priority_realtime(send_thread_priority) != 0)
          LOGGER(error) << "unable to set sender thread realtime priority";

      if(thread_set_affinity)
        if(set_current_thread_affinity(send_thread_cpu) != 0)
          LOGGER(error) << "unable to set sender thread CPU affinity";

      udp_tx server(this, io_context, local_endpoint, destination_endpoint);
      io_context.run();
      LOGGER(info) << "sender thread finished";
    }

    class udp_rx {

      public:
        // constructor
        udp_rx(vxsdr *p, boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint)
          : socket(io_context, local_endpoint) {
          sdrptr = p;
          socket.set_option(boost::asio::ip::udp::socket::reuse_address(true));
          socket.set_option(boost::asio::socket_base::receive_buffer_size(network_receive_buffer_bytes));

          boost::asio::socket_base::receive_buffer_size option;
          socket.get_option(option);
          int network_buffer_size = option.value();
          LOGGER(info) << "network receive buffer size set to " << network_buffer_size;

          if(not sdrptr->rx_stop_flag)
            start_receive();
        }
        // destructor
        ~udp_rx() {
          //FIXME
          socket.close();
        }
        // copy constructor
        // copy assignment
        // move constructor
        // move assignment

      private:
        const bool forward_acks = false;
        vxsdr *sdrptr;
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
          while(not sdrptr->rx_stop_flag) {
            socket.receive_from(boost::asio::buffer(&recv_buffer, sizeof(recv_buffer)), remote);
            packets_received++;
            packet_types_received[recv_buffer.hdr.packet_type]++;
            if(packets_received > 1 and recv_buffer.hdr.sequence_counter != (uint16_t)(last_seq + 1))
              seq_errors++;
            last_seq = recv_buffer.hdr.sequence_counter;
            switch(recv_buffer.hdr.packet_type) {
              // receive buffers go into the rx data queue
              case PACKET_TYPES::RX_SIGNAL_DATA:
                sdrptr->rx_data_queue.push(recv_buffer);
                break;
              // tx sginal data acks special cased since they contain throttling information
              case PACKET_TYPES::TX_SIGNAL_DATA_ACK:
                {
                  auto *r = reinterpret_cast<tx_signal_data_ack_packet *>(&recv_buffer);
                  sdrptr->tx_buffer_used = r->tx_buffer_used_bytes;
                  sdrptr->tx_buffer_fill_percent = std::min(100., (100. * sdrptr->tx_buffer_used) / sdrptr->tx_buffer_size);
                  if(forward_acks) {
                    auto *p = reinterpret_cast<largest_cmd_or_rsp_packet *>(&recv_buffer);
                    sdrptr->response_queue.push(*p);
                  }
                }
                break;
              // the buffer info commands are special cased since they can also be used for TX throttling
              case PACKET_TYPES::DEVICE_CMD_RSP:
                switch(recv_buffer.hdr.command) {
                  case DEVICE_CMD_RSP::GET_BUFFER_USE_RSP:
                    {
                      auto *r = reinterpret_cast<get_buffer_use_rsp_packet *>(&recv_buffer);
                      sdrptr->tx_buffer_used = r->tx_buffer_used_bytes;
                      sdrptr->rx_buffer_used = r->rx_buffer_used_bytes;
                      sdrptr->tx_buffer_fill_percent = std::min(100., (100. * sdrptr->tx_buffer_used) / sdrptr->tx_buffer_size);
                      sdrptr->rx_buffer_fill_percent = std::min(100., (100. * sdrptr->rx_buffer_used) / sdrptr->rx_buffer_size);
                      auto *p = reinterpret_cast<largest_cmd_or_rsp_packet *>(&recv_buffer);
                      sdrptr->response_queue.push(*p);
                    }
                    break;
                  case DEVICE_CMD_RSP::GET_BUFFER_INFO_RSP:
                    {
                      auto *r = reinterpret_cast<get_buffer_info_rsp_packet *>(&recv_buffer);
                      sdrptr->tx_buffer_size = r->tx_buffer_size_bytes;
                      sdrptr->rx_buffer_size = r->rx_buffer_size_bytes;
                      auto *p = reinterpret_cast<largest_cmd_or_rsp_packet *>(&recv_buffer);
                      sdrptr->response_queue.push(*p);
                    }
                    break;
                  default:
                    {
                      auto *p = reinterpret_cast<largest_cmd_or_rsp_packet *>(&recv_buffer);
                      sdrptr->response_queue.push(*p);
                    }
                    break;
                }
                break;
              case PACKET_TYPES::DEVICE_CMD_ACK:
              case PACKET_TYPES::RX_RADIO_CMD_ACK:
              case PACKET_TYPES::TX_RADIO_CMD_ACK:
                if(forward_acks) {
                  auto *p = reinterpret_cast<largest_cmd_or_rsp_packet *>(&recv_buffer);
                  sdrptr->response_queue.push(*p);
                }
                break;
              // DEVICE_CMD_RSP handled above
              case PACKET_TYPES::DEVICE_CMD_ERR:
              case PACKET_TYPES::RX_RADIO_CMD_RSP:
              case PACKET_TYPES::RX_RADIO_CMD_ERR:
              case PACKET_TYPES::TX_RADIO_CMD_RSP:
              case PACKET_TYPES::TX_RADIO_CMD_ERR:
                {
                  auto *p = reinterpret_cast<largest_cmd_or_rsp_packet *>(&recv_buffer);
                  sdrptr->response_queue.push(*p);
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
              LOGGER(info) << (boost::format("   %12d %20s (%d)") % packet_types_received[i] % sdrptr->packet_type_to_name(i) % i);
        }
    };

    class udp_tx {

      public:
        // constructor
        udp_tx(vxsdr *p, boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint,
          const boost::asio::ip::udp::endpoint &destination_endpoint) : socket(io_context, local_endpoint) {
          sdrptr = p;
          destination = destination_endpoint;
          socket.set_option(boost::asio::socket_base::send_buffer_size(network_send_buffer_bytes));

          boost::asio::socket_base::send_buffer_size option;
          socket.get_option(option);
          int network_buffer_size = option.value();
          LOGGER(info) << "network send buffer size set to " << network_buffer_size;

          if(not sdrptr->tx_stop_flag)
            start_send();
        }
        // destructor
        ~udp_tx() {
          socket.close();
          //FIXME
        }
        // copy constructor
        // copy assignment
        // move constructor
        // move assignment
      private:
        vxsdr *sdrptr;
        boost::asio::ip::udp::socket socket;
        boost::asio::ip::udp::endpoint destination;
        std::array<uint64_t, UINT8_MAX + 1> packet_types_processed = { 0 };
        uint64_t send_errors = 0, packets_sent = 0, bytes_sent = 0;

        void send_packet(packet &packet) {
          packet_types_processed[packet.hdr.packet_type]++;
          packet.hdr.sequence_counter = (uint16_t)(packets_sent++ % (UINT16_MAX + 1));
          size_t bytes = socket.send_to(boost::asio::buffer(&packet, packet.hdr.packet_size), destination);
          if(bytes != packet.hdr.packet_size)
            send_errors++;
          bytes_sent += bytes;
        }

        void initialize_tx(const uint8_t device_subdev, const uint8_t tx_subdev) {
          sdrptr->send_tx_stop_now(tx_subdev);
          // get device buffer sizes
          sdrptr->send_device_get_buffer_info(device_subdev);
          // wait for the response come back and be processed
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          // clear the status registers
          sdrptr->send_device_clear_status(device_subdev);
          sdrptr->send_device_get_status(device_subdev);
        }

        void close_tx(const uint8_t device_subdev, const uint8_t tx_subdev) {
          // get the status at the end
          sdrptr->send_device_get_status(device_subdev);
          // send a stop, requesting an ack so that the rx will see an incoming packet
          sdrptr->send_tx_stop_now(tx_subdev, FLAGS::REQUEST_ACK);
        }

        void start_send() {
          LOGGER(info) << "start_send started";
          static constexpr size_t data_buffer_size = 16, cmd_buffer_size = 16;
          std::array<largest_data_packet, data_buffer_size> data_buffer;
          std::array<largest_cmd_or_rsp_packet, cmd_buffer_size> cmd_buffer;

          constexpr float throttle_halt_percent  = 90;
          constexpr float throttle_on_percent    = 75;
          constexpr float throttle_off_percent   = 50;
          bool throttling = false;
          constexpr unsigned throttle_amount_us  = 10; // send time for 8200 bytes over 10 GbE is about 6.7 us

          const uint8_t device_subdev = 0, tx_subdev = 0;

          initialize_tx(device_subdev, tx_subdev);

          uint64_t last_check = 0;
          constexpr uint64_t buffer_check_interval = 100; // current buffer size is about 1000 data packets

          while(not sdrptr->tx_stop_flag) {
            if(sdrptr->command_queue.read_available() > 0) {
              size_t n_popped = sdrptr->command_queue.pop(&cmd_buffer.front(), cmd_buffer.size());
              for(size_t i = 0; i < n_popped; i++) {
                send_packet(cmd_buffer[i]);
              }
            }
            // FIXME: is a timeout needed?
            else if(sdrptr->tx_data_queue.read_available() > 0 and sdrptr->tx_buffer_fill_percent < throttle_halt_percent) {
              size_t n_popped = sdrptr->tx_data_queue.pop(&data_buffer.front(), data_buffer.size());
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
                // throttle if needed
                //FIXME: needs a timeout!!
                //while(sdrptr->tx_buffer_fill_percent > throttle_halt_percent)
                //  std::this_thread::sleep_for(std::chrono::microseconds(throttle_amount_us));
                if(sdrptr->tx_buffer_fill_percent > throttle_on_percent)
                  throttling = true;
                else if(sdrptr->tx_buffer_fill_percent <= throttle_off_percent)
                  throttling = false;
                if(throttling)
                  std::this_thread::sleep_for(std::chrono::microseconds(throttle_amount_us));
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
              LOGGER(info) << (boost::format("   %12d %20s (%d)") % packet_types_processed[i] % sdrptr->packet_type_to_name(i) % i);

        }
    };
};

#endif // VXSDR_HPP