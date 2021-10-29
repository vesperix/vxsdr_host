#ifndef VXSDR_HPP
#define VXSDR_HPP

#include <complex>
#include <vector>
#include <cstring>
#include <tuple>
#include <cstdint>
#include <optional>
#include <fstream>
#include <chrono>
#include <atomic>
#include <thread>
#include <future>

#include <boost/lockfree/spsc_queue.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/udp.hpp>
#ifdef VXSDR_ASYNC_RX
#include <boost/asio/use_future.hpp>
#endif // VXSDR_ASYNC_RX

#include "logging.hpp"
#include "thread_utils.h"
#include "utility.hpp"

#include "vxsdr_header.h"
#include "packet_proc.hpp"

/*! @class vxsdr vxsdr.hpp
    @brief The vxsdr class contains the host interface for the VXSDR
*/

class vxsdr {

 private:
#ifdef VXSDR_ASYNC_RX
  // socket_receive_timeout setting for async rx
  static constexpr unsigned receiver_socket_timeout_ms    =      100;
#endif // VXSDR_ASYNC_RX

  // thread realtime priority settings
  static constexpr bool thread_set_priority = true;
  static constexpr int send_thread_priority = 20;
  static constexpr int receive_thread_priority = 20;

  // thread cpu affinity settings
  static constexpr bool thread_set_affinity = true;
  static constexpr int send_thread_cpu = 0;
  static constexpr int receive_thread_cpu = 2;

  // parameters of the internal spsc queues (ring buffers)
  static constexpr size_t rx_data_queue_length           =    65536;    // slots in rx data queue; 65536 * 2048 / 160e6 = 840 ms at 160 MSPS
  static constexpr size_t tx_data_queue_length           =     1024;    // slots in tx data queue;  1024 * 2048 / 160e6 =  13 ms at 160 MSPS
  static constexpr size_t command_queue_length           =       64;    // slots in command msg queue
  static constexpr size_t response_queue_length          =      256;    // slots in response msg queue
  static constexpr size_t error_queue_length             =      256;    // slots in error queue
  static constexpr size_t cmd_ack_queue_length           =      256;    // slots in command ack queue
  static constexpr size_t async_msg_queue_length         =      256;    // slots in async msg queue

  // timeouts and waits between tries for queue push/pop

  static constexpr unsigned tx_data_queue_timeout_us     =   100000;
  static constexpr unsigned tx_data_queue_wait_us        =       10;

  static constexpr unsigned rx_data_queue_timeout_us     =    40000;
  static constexpr unsigned rx_data_queue_wait_us        =       10;

  // timeouts and waits for responses from device
  static constexpr unsigned device_response_timeout_ms   =      100;
  static constexpr unsigned device_response_wait_ms      =        1;

  // the UDP tx waits for the rx to reach RUNNING state before entering RUNNING itself
  static constexpr unsigned udp_tx_ready_timeout_ms      =     1000;
  static constexpr unsigned udp_tx_ready_wait_ms         =        1;

  // parameters used to set the OS's network buffer
  static constexpr unsigned network_send_buffer_bytes    =   4194304;   // ~6.6 ms at 160 MSPS
  static constexpr unsigned network_receive_buffer_bytes =  16777216;   // ~26 ms at 160 MSPS

  // transmit throttling settings
  static constexpr float throttle_on_percent             =        75;
  static constexpr float throttle_off_percent            =        50;
  static constexpr unsigned throttle_amount_us           =        10; // send time for 8200 bytes over 10 GbE is about 6.7 us
  static constexpr unsigned send_thread_sleep_us         =         5;


  // parameters used to monitor status of the radio's internal buffers (on the other end of the network connection)
  std::atomic<unsigned> tx_buffer_size_bytes = 0, tx_buffer_used_bytes = 0;
  std::atomic<unsigned> rx_buffer_size_bytes = 0, rx_buffer_used_bytes = 0;
  std::atomic<unsigned> tx_buffer_fill_percent = 0, rx_buffer_fill_percent = 0;

  using data_queue_element = largest_data_packet;
  using command_queue_element = largest_cmd_or_rsp_packet;
  using response_queue_element = largest_cmd_or_rsp_packet;
  using error_queue_element = largest_cmd_or_rsp_packet;
  using cmd_ack_queue_element = largest_cmd_or_rsp_packet;
  using async_msg_queue_element = largest_cmd_or_rsp_packet;

  // data queues are SPSC
  boost::lockfree::spsc_queue<data_queue_element, boost::lockfree::capacity<rx_data_queue_length> > rx_data_queue;
  boost::lockfree::spsc_queue<data_queue_element, boost::lockfree::capacity<tx_data_queue_length> > tx_data_queue;
  // other queues are MPMC
  boost::lockfree::queue<command_queue_element, boost::lockfree::capacity<command_queue_length> > command_queue;
  boost::lockfree::queue<response_queue_element, boost::lockfree::capacity<response_queue_length> > response_queue;
  boost::lockfree::queue<error_queue_element, boost::lockfree::capacity<error_queue_length> > error_queue;
  boost::lockfree::queue<cmd_ack_queue_element, boost::lockfree::capacity<cmd_ack_queue_length> > cmd_ack_queue;
  boost::lockfree::queue<async_msg_queue_element, boost::lockfree::capacity<async_msg_queue_length> > async_msg_queue;

  // Boost context and sockets
  boost::asio::io_context context;
  boost::asio::ip::udp::socket sender_socket;
  boost::asio::ip::udp::socket receiver_socket;

  // threads used for sending and receiving
  std::thread sender_thread;
  std::thread receiver_thread;
  std::thread context_thread;

  boost::asio::ip::udp::endpoint destination_endpoint;

  // flags used for startup
  enum udp_interface_state { UNINITIALIZED, STARTING, WAITING, RUNNING, SHUTTING_DOWN, SHUTDOWN };
  std::atomic<udp_interface_state> udp_tx_state { UNINITIALIZED }, udp_rx_state { UNINITIALIZED };

  // flags used for orderly shutdown
  std::atomic<bool> sender_thread_stop_flag = false, receiver_thread_stop_flag = false;

  // statistics
  uint64_t send_errors = 0, packets_sent = 0, samples_sent = 0, bytes_sent = 0;
  std::array<uint64_t, UINT8_MAX + 1> packet_types_sent = { 0 };

  uint64_t sequence_errors = 0, packets_received = 0, samples_received = 0, bytes_received = 0;
  std::array<uint64_t, UINT8_MAX + 1> packet_types_received = { 0 };

  // diagnostic info gathering for sequence errors
#ifdef VXSDR_SEQ_ERR_DIAG
  std::vector<std::chrono::time_point<std::chrono::system_clock> > seq_error_times;
  std::vector<uint8_t> seq_error_packet_type;
  std::vector<uint16_t> seq_error_counter;
  std::vector<uint16_t> seq_error_expected;
#endif // VXSDR_SEQ_ERR_DIAG

 public:
  /*!
    @brief Constructor for the @p vxsdr host interface class.
  */
  vxsdr(const boost::asio::ip::udp::endpoint &local_send_endpoint,
        const boost::asio::ip::udp::endpoint &local_receive_endpoint,
        const boost::asio::ip::udp::endpoint &radio_endpoint);
  //! Destructor for the vxsdr host interface class.
  ~vxsdr();
  //! @brief Deleted copy constructor for the @p vxsdr host interface class.
  vxsdr(const vxsdr &) = delete;
  //! @brief Deleted copy assignment for the @p vxsdr host interface class.
  vxsdr& operator=(const vxsdr &) = delete;
  //! @brief Deleted move constructor for the @p vxsdr host interface class.
  vxsdr(vxsdr &&) = delete;
  //! @brief Deleted move assignment for the @p vxsdr host interface class.
  vxsdr& operator=(vxsdr &&) = delete;
  /*!
    @brief Receives data from the device and returns it in a vector.
    - returns the number of samples received
    @param data the vector for the received data
    @param n_desired the number of samples to be received
  */
  size_t get_rx_data(std::vector<std::complex<int16_t> > &data, size_t n_desired = 0);
  /*!
    @brief Receives data from the device and writes to an open file.
    - returns the number of samples written
    @param out_stream the file to write the data to
    @param n_desired the number of samples to be received
    @param write_size_samples the block size for file writes
  */
  size_t write_rx_data(std::ofstream &out_stream, size_t n_desired, size_t write_size_samples = 65536);
  /*!
    @brief Receives data from the device and checks packet sequence, discarding the data.
    - returns the number of samples received
    @param n_desired the number of samples to be received
  */
  size_t check_rx_data_sequence_only(size_t n_desired);
  /*!
    @brief Sends transmit data to the device.
    - returns the number of samples placed in the queue for transmission
    @param data the complex @p itn16_t samples to be sent
    @param subdev the subdevice number
  */
  size_t put_tx_data(std::vector<std::complex<int16_t> > data, const uint8_t subdev = 0);
  /*!
    @brief Requests basic information from the device.
    - returns a tuple containing the FPGA and MCU code version numbers
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<std::tuple<uint32_t, uint32_t>> device_hello(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Resets the device.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if the command is sent,
        @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if the command is sent
        and an ACK is received, @b false otherwise
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool device_reset(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Opens a session on the device.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if the command is sent,
        @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if the command is sent
        and an ACK is received, @b false otherwise
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool device_open_session(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Closes a session on the device.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if the command is sent,
        @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if the command is sent
        and an ACK is received, @b false otherwise
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool device_close_session(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Clears the device status.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if the command is sent,
        @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if the command is sent
        and an ACK is received, @b false otherwise
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool device_clear_status(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Gets the device status.
    - returns a device-dependent status type
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<std::tuple<uint16_t, uint16_t, uint32_t, uint32_t, uint32_t, uint32_t>> device_get_status(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Sets the device time immediately.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if the command is sent,
        @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if the command is sent
        and an ACK is received, @b false otherwise
    @param t the time to set
    @param subdev the subdevice number
    @param flags the packet flags (FLAGS_TIME_PRESENT is added automatically)
  */
  bool device_set_time_now(const std::chrono::time_point<std::chrono::system_clock> &t, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Sets the device time when the next PPS is recieved by the device.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if the command is sent,
        @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if the command is sent
        and an ACK is received, @b false otherwise
    @param t the time to set
    @param subdev the subdevice number
    @param flags the packet flags (FLAGS_TIME_PRESENT is added automatically)
  */
  bool device_set_time_next_pps(const std::chrono::time_point<std::chrono::system_clock> &t, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Sets the device time immediately.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the input parameters @p sec and @p nsec are in range and the command is sent,
        @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
        the input parameters @p sec and @p nsec are in range, the command is sent
        and an ACK is received, @b false otherwise

  The @p sec and @p nsec input types are chosen to be compatible with Posix
  <tt>struct timespec</tt> on 64-bit systems, but the ranges are checked for
  compatibility with the underlying @p uint32_t values passed to the device
  and for the limitations on @p nsec in the Posix standard.

    @param sec seconds to set (UINT32_MAX >= @p sec >= 0)
    @param nsec nanoseconds to set (1,000,000,000 > @p nsec >= 0)
    @param subdev the subdevice number
    @param flags the packet flags (FLAGS_TIME_PRESENT is added automatically)
  */
  bool device_set_time_now(const int64_t sec, const int32_t nsec, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Sets the device time when the next PPS is recieved by the device.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the input parameters @p sec and @p nsec are in range and the command is sent,
        @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
        the input parameters @p seconds and @p nanoseconds in range, the command is sent
        and an ACK is received, @b false otherwise

  The @p sec and @p nsec input types are chosen to be compatible with Posix
  <tt>struct timespec</tt> on 64-bit systems, but the ranges are checked for
  compatibility with the underlying @p uint32_t values passed to the device
  and for the limitations on @p nsec in the Posix standard.

    @param sec seconds to set (UINT32_MAX >= @p sec >= 0)
    @param nsec nanoseconds to set (1,000,000,000 > @p nsec >= 0)
    @param subdev the subdevice number
    @param flags the packet flags (FLAGS_TIME_PRESENT is added automatically)
  */
  bool device_set_time_next_pps(const int64_t sec, const int32_t nsec, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Gets the device time immediately.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        a reponse is received,  @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
        an ACK and a reponse are received,  @b false otherwise

    @param sec device time seconds  (UINT32_MAX >= @p sec >= 0)
    @param nsec device time nanoseconds (1,000,000,000 > @p nsec >= 0)
    @param subdev the subdevice number
    @param flags the packet flags (FLAGS_TIME_PRESENT is added automatically)
  */
  bool device_get_time_now(uint32_t &sec, uint32_t &nsec, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Gets the device time immediately.
    - returns a @p time_point containing the device time
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<std::chrono::time_point<std::chrono::system_clock>> device_get_time_now(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Starts transmitting immediately until a stop command is sent.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool tx_start_now_until_stop(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Starts receiving immediately until a stop command is sent.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool rx_start_now_until_stop(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Starts transmitting immediately until @p n samples are sent.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param n the number of samples to send
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool tx_start_now_for_n_samples(const uint64_t n, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Starts receiving immediately until @p n samples are received.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param n the number of samples to send
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool rx_start_now_for_n_samples(const uint64_t n, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Starts transmitting at time @p t until @p n samples are sent.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param t the start time
    @param n the number of samples to send
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool tx_start_at_time_for_n_samples(const std::chrono::time_point<std::chrono::system_clock> &t, const uint64_t n, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Starts receiving at time @p t until @p n samples are received.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param t the start time
    @param n the number of samples to receive
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool rx_start_at_time_for_n_samples(const std::chrono::time_point<std::chrono::system_clock> &t,const uint64_t n, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Starts transmitting at time @p t until a stop command is sent.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param t the start time
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool tx_start_at_time_until_stop(const std::chrono::time_point<std::chrono::system_clock> &t, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Starts receiving at time @p t until a stop command is sent.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param t the start time
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool rx_start_at_time_until_stop(const std::chrono::time_point<std::chrono::system_clock> &t, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Sets the IQ DC bias for the transmitter to control LO feedthrough.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param i_bias the I channel bias (-1 >= @p i_bias >= 1)
    @param q_bias the Q channel bias (-1 >= @p q_bias >= 1)
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool tx_set_iq_bias(const double i_bias, const double q_bias, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Sets the IQ DC bias for the receiver to control LO feedthrough.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param i_bias the I channel bias (-1 >= @p i_bias >= 1)
    @param q_bias the Q channel bias (-1 >= @p q_bias >= 1)
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool rx_set_iq_bias(const double i_bias, const double q_bias, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Sets the IQ correction for the transmitter to control image rejection.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param ii_corr I-I channel correction
    @param iq_corr I-Q channel correction
    @param qi_corr Q-I channel correction
    @param qq_corr Q-Q channel correction
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool tx_set_iq_corr(const double ii_corr, const double iq_corr, const double qi_corr, const double qq_corr, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Sets the IQ correction for the receiver to control image rejection.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param ii_corr I-I channel correction
    @param iq_corr I-Q channel correction
    @param qi_corr Q-I channel correction
    @param qq_corr Q-Q channel correction
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool rx_set_iq_corr(const double ii_corr, const double iq_corr, const double qi_corr, const double qq_corr, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Stops transmitting immediately.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool tx_stop_now(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Stops receiving immediately.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool rx_stop_now(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Gets the transmit center frequency range.
    - returns a tuple containing the minimum and maximum frequencies in Hz
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<std::tuple<double, double>> get_tx_freq_range(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Gets the receive center frequency range.
    - returns a tuple containing the minimum and maximum frequencies in Hz
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<std::tuple<double, double>> get_rx_freq_range(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Sets the transmit center frequency.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param freq_hz the desired frequency in Hz
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool set_tx_freq(const double freq_hz, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Sets the receive center frequency.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param freq_hz the desired frequency in Hz
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool set_rx_freq(const double freq_hz, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Gets the current transmit center frequency.
    - returns the center frequency in Hz
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<double> get_tx_freq(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Gets the current receive center frequency.
    - returns the center frequency in Hz
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<double> get_rx_freq(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Gets the transmit gain range.
    - returns a tuple containing the minimum and maximum gains in dB
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<std::tuple<double, double>> get_tx_gain_range(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Gets the receive gain range.
    - returns a tuple containing the minimum and maximum gains in dB
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<std::tuple<double, double>> get_rx_gain_range(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Sets the transmit gain.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param gain_db the desired gain in dB
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool set_tx_gain(const double gain_db, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Sets the receive gain.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param gain_db the desired gain in dB
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool set_rx_gain(const double gain_db, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Gets the current transmit gain.
    - returns the gain in dB
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<double> get_tx_gain(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Gets the current receive gain.
    - returns the gain in dB
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<double> get_rx_gain(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Gets the transmit sample rate range.
    - returns a tuple containing the minimum and maximum sample rates in samples/s
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<std::tuple<double, double>> get_tx_rate_range(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Gets the receive sample rate range.
    - returns a tuple containing the minimum and maximum sample rates in samples/s
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<std::tuple<double, double>> get_rx_rate_range(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Sets the transmit sample rate.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param rate_samples_sec the desired rate in samples/s
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool set_tx_rate(const double rate_samples_sec, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Sets the receive sample rate.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param rate_samples_sec the desired rate in samples/s
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool set_rx_rate(const double rate_samples_sec, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Gets the current transmit sample rate.
    - returns the rate in samples/s
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<double> get_tx_rate(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Gets the current receive sample rate.
    - returns the rate in samples/s
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<double> get_rx_rate(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Gets the number of transmit output ports.
    - returns the number of output ports
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<unsigned> get_num_tx_ports(const uint8_t subdev = 0, const uint8_t flags = 0);
   /*!
    @brief Gets the number of receive input ports.
    - returns the number of input ports
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<unsigned> get_num_rx_ports(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Gets the name of a transmit output port.
    - returns the name of the specified output port
    @param port_num the port number
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<std::string> get_tx_port_name(const unsigned port_num, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Gets the name of a receive input port.
    - returns the name of the specified input port
    @param port_num the port number
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<std::string> get_rx_port_name(const unsigned port_num, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Sets the transmit output port.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param port_name the desired output port name
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool set_tx_rf_port(const std::string &port_name, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Sets the receive input port.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param port_name the desired input port name
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool set_rx_rf_port(const std::string &port_name, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Gets the current transmit output port.
    - returns the port name
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<std::string> get_tx_rf_port(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Gets the current receive input port.
    - returns the port name
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<std::string> get_rx_rf_port(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Enables or disables the transmit RF section.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param enabled the desired state
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool set_tx_enable(const bool enabled, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Enables or disables the receive RF section.
    - if the FLAG_REQUEST_ACK bit in @p flags is not set, returns @b true if
        the command is sent, @b false otherwise
    - if the FLAG_REQUEST_ACK bit in @p flags is set, returns @b true if
         the command is sent and an ACK is received, @b false otherwise
    @param enabled the desired state
    @param subdev the subdevice number
    @param flags the packet flags
  */
  bool set_rx_enable(const bool enabled, const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Gets the transmit capabilites.
    - returns a @p capabilities_t indicating the transmit capabilities
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<capabilities_t> get_tx_capabilities(const uint8_t subdev = 0, const uint8_t flags = 0);
  /*!
    @brief Gets the receive capabilites.
    - returns a @p capabilities_t indicating the receive capabilities
    @param subdev the subdevice number
    @param flags the packet flags
  */
  std::optional<capabilities_t> get_rx_capabilities(const uint8_t subdev = 0, const uint8_t flags = 0);

 private:

  static void time_point_to_time_spec_t(const std::chrono::time_point<std::chrono::system_clock> &t, time_spec_t &ts);
  bool device_get_buffer_info(const uint8_t subdev, const uint8_t flags = 0);
  bool device_get_buffer_use(const uint8_t subdev, const uint8_t flags = 0);
  [[nodiscard]] bool cmd_queue_push_check(packet &p);
  void receiver();
  void sender();
  void udp_receiver();
  void start_receive();
  void udp_sender();
  void send_packet(packet &packet);
  void initialize_sdr(const uint8_t device_subdev, const uint8_t tx_subdev, const uint8_t rx_subdev);
  void stop_sdr(const uint8_t device_subdev, const uint8_t tx_subdev, const uint8_t rx_subdev);
  void start_send();

};

#endif // VXSDR_HPP
