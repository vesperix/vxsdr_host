#ifndef VXSDR_HPP
#define VXSDR_HPP

#include <atomic>
#include <chrono>
#include <complex>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <future>
#include <optional>
#include <thread>
#include <optional>
#include <vector>
#include <array>

#include <boost/lockfree/queue.hpp>
#include <boost/lockfree/spsc_queue.hpp>

#ifdef USE_NEWORK_TS
#include <experimental/net>
namespace net = std::experimental::net;
#else
#include <boost/asio/ts/net.hpp>
namespace net = boost::asio;
#endif

#ifdef VXSDR_ASYNC_RX
#include <boost/asio/use_future.hpp>
#endif  // VXSDR_ASYNC_RX

#include "logging.hpp"
#include "thread_utils.h"
#include "utility.hpp"

#include "packet_proc.hpp"
#include "vxsdr_header.h"

/*! @class vxsdr vxsdr.hpp
    @brief The vxsdr class contains the host interface for the VXSDR
*/

class vxsdr {
  public:
    enum stream_state_t { STREAM_STOPPED = 0, STREAM_RUNNING, STREAM_WAITING_FOR_START, STREAM_ERROR };
  private:
#ifdef VXSDR_ASYNC_RX
    // socket_receive_timeout setting for async rx
    static constexpr unsigned receiver_socket_timeout_ms = 100;
#endif  // VXSDR_ASYNC_RX
    // packet logging settings
    bool log_nondata_packets;

    // thread realtime priority settings
    bool thread_set_priority;
    int send_thread_priority;
    int receive_thread_priority;

    // thread cpu affinity settings
    bool thread_set_affinity;
    unsigned send_thread_cpu;
    unsigned receive_thread_cpu;

    // parameters of the internal SPSC queues used for data
    static constexpr size_t rx_data_queue_length = 65'536;  // slots in rx data queue; 65536 * 2048 / 160e6 = 840 ms at 160 MSPS
    static constexpr size_t tx_data_queue_length =  1'024;  // slots in tx data queue;  1024 * 2048 / 160e6 =  13 ms at 160 MSPS

    // parameters used to set the OS's network buffer
    static constexpr unsigned network_send_buffer_bytes     =  1'048'576;  // ~1.6 ms at 160 MSPS
    static constexpr unsigned network_receive_buffer_bytes  = 16'777'216;  // ~26 ms at 160 MSPS

    // timeouts and waits between tries for queue push/pop
    static constexpr double tx_data_queue_timeout_s         = 1.0;
    static constexpr unsigned tx_data_queue_wait_us         = 10;

    static constexpr double rx_data_queue_pop_timeout_s     = 1.0;
    static constexpr unsigned rx_data_queue_push_timeout_us = 40'000;
    static constexpr unsigned rx_data_queue_wait_us         = 10;

    // transmit throttling settings
    static constexpr unsigned throttle_hard_percent         = 80;
    static constexpr unsigned throttle_on_percent           = 75;
    static constexpr unsigned throttle_off_percent          = 50;
    static constexpr unsigned throttle_amount_us            = 20;  // send time for 8200 bytes over 10 GbE is about 6.7 us
    static constexpr unsigned send_thread_sleep_us          = 5;

    // parmeters of the internal MPMC queues used for commands, responses, errors, acks, and async messages
    static constexpr size_t command_queue_length            = 32;   // slots in command msg queue
    static constexpr size_t response_queue_length           = 32;   // slots in response msg queue
    static constexpr size_t cmd_ack_queue_length            = 32;   // slots in command ack queue
    static constexpr size_t async_msg_queue_length          = 256;  // slots in async msg queue

    // timeouts and waits for responses from device
    unsigned device_response_timeout_us                     = 1'000'000;
    static constexpr unsigned device_response_wait_us       = 10;

    // how often to check the async queue
    static constexpr unsigned async_queue_check_ms          = 10;

    // the UDP tx waits for the rx to reach RUNNING state before entering RUNNING itself
    static constexpr unsigned udp_tx_ready_timeout_ms       = 1'000;
    static constexpr unsigned udp_tx_ready_wait_ms          = 1;

    // parameters used to monitor status of the radio's internal buffers (on the other end of the network connection)
    std::atomic<unsigned> tx_buffer_size_bytes = 0, tx_buffer_used_bytes = 0;
    std::atomic<unsigned> rx_buffer_size_bytes = 0, rx_buffer_used_bytes = 0;
    std::atomic<unsigned> tx_buffer_fill_percent = 0, rx_buffer_fill_percent = 0;
    // last number of sequence errors reported by the radio for tx packets
    std::atomic<unsigned> tx_packet_oos_count = 0;

    using data_queue_element      = largest_data_packet;
    using command_queue_element   = largest_cmd_or_rsp_packet;
    using response_queue_element  = largest_cmd_or_rsp_packet;
    using cmd_ack_queue_element   = largest_cmd_or_rsp_packet;
    using async_msg_queue_element = largest_cmd_or_rsp_packet;

    // data queues are SPSC
    boost::lockfree::spsc_queue<data_queue_element, boost::lockfree::capacity<rx_data_queue_length>> rx_data_queue;
    boost::lockfree::spsc_queue<data_queue_element, boost::lockfree::capacity<tx_data_queue_length>> tx_data_queue;
    // other queues are MPMC
    boost::lockfree::queue<command_queue_element, boost::lockfree::capacity<command_queue_length>> command_queue;
    boost::lockfree::queue<response_queue_element, boost::lockfree::capacity<response_queue_length>> response_queue;
    boost::lockfree::queue<cmd_ack_queue_element, boost::lockfree::capacity<cmd_ack_queue_length>> cmd_ack_queue;
    boost::lockfree::queue<async_msg_queue_element, boost::lockfree::capacity<async_msg_queue_length>> async_msg_queue;

    // net context and sockets
    net::io_context context;
    net::ip::udp::socket sender_socket;
    net::ip::udp::socket receiver_socket;

    // threads used for sending and receiving
    std::thread sender_thread;
    std::thread receiver_thread;
    std::thread context_thread;
    std::thread async_handler_thread;

    net::ip::udp::endpoint destination_endpoint;

    // flags used to indicate the state of the data transport
    enum transport_state_t { TRANSPORT_UNINITIALIZED, TRANSPORT_STARTING, TRANSPORT_RUNNING, TRANSPORT_SHUTDOWN, TRANSPORT_ERROR };
    std::atomic<transport_state_t> udp_tx_state{TRANSPORT_UNINITIALIZED}, udp_rx_state{TRANSPORT_UNINITIALIZED};

    // flags used to indicate the data transfer stream state
    std::atomic<stream_state_t> tx_stream_state{STREAM_STOPPED}, rx_stream_state{STREAM_STOPPED};
    // number of samples in current stream (0 if continuous)
    uint64_t desired_tx_stream_samples = 0, desired_rx_stream_samples = 0;

    // flags used for orderly shutdown
    std::atomic<bool> sender_thread_stop_flag = false, receiver_thread_stop_flag = false;

    // statistics
    uint64_t send_errors = 0, packets_sent = 0, samples_sent = 0, bytes_sent = 0;
    uint64_t send_errors_current_stream = 0, samples_sent_current_stream = 0;

    std::array<uint64_t, UINT8_MAX + 1> packet_types_sent = {0};

    uint64_t sequence_errors = 0, packets_received = 0, samples_received = 0, bytes_received = 0;
    uint64_t sequence_errors_current_stream = 0, samples_received_current_stream = 0;
    std::array<uint64_t, UINT8_MAX + 1> packet_types_received = {0};

    // diagnostic info gathering for sequence errors
#ifdef VXSDR_SEQ_ERR_DIAG
    std::vector<std::chrono::time_point<std::chrono::system_clock>> seq_error_times;
    std::vector<uint8_t> seq_error_packet_type;
    std::vector<uint16_t> seq_error_counter;
    std::vector<uint16_t> seq_error_expected;
#endif  // VXSDR_SEQ_ERR_DIAG
#ifdef VXSDR_TX_BUFFER_DIAG
    std::vector<std::chrono::time_point<std::chrono::system_clock>> buffer_data_times;
    std::vector<uint32_t> buffer_fill_bytes;
    uint32_t buffer_fill_min = UINT32_MAX, buffer_fill_max = 0;
#endif // VXSDR_TX_BUFFER_DIAG
  public:
    /*!
      @brief Constructor for the @p vxsdr host interface class.
      @param local_send_endpoint the endpoint (IP address and port) used to send
      @param local_receive_endpoint the endpoint used to receive
      @param device_endpoint the endpoint of the target device
      @param nondata_packet_logging log all nondata packets (debugging only)
      @param set_affinity controls whether threads set affinity for a specific processor
      @param base_cpu_number the starting processor to use if affinity is set
      @param set_priority controls whether threads set realtime priority
      @param base_priority the priority to use if realtime priority is set
    */
    vxsdr(const net::ip::udp::endpoint& local_send_endpoint,
          const net::ip::udp::endpoint& local_receive_endpoint,
          const net::ip::udp::endpoint& device_endpoint,
          const bool nondata_packet_logging = false,
          const bool set_affinity           = false,
          const unsigned base_cpu_number    = 0,
          const bool set_priority           = true,
          const int base_priority           = 5);

    //! Destructor for the vxsdr host interface class.
    ~vxsdr();

    //! @brief Deleted copy constructor for the @p vxsdr host interface class.
    vxsdr(const vxsdr&) = delete;

    //! @brief Deleted copy assignment for the @p vxsdr host interface class.
    vxsdr& operator=(const vxsdr&) = delete;

    //! @brief Deleted move constructor for the @p vxsdr host interface class.
    vxsdr(vxsdr&&)                 = delete;

    //! @brief Deleted move assignment for the @p vxsdr host interface class.
    vxsdr& operator=(vxsdr&&) = delete;

    /*!
      @brief Receives data from the device and returns it in a vector.
      @returns the number of samples received before a sequence error, or @p n_desired if no sequence errors occur
      @param data the vector for the received data
      @param n_desired the number of samples to be received
      @param timeout_s timeout in seconds
    */
    size_t get_rx_data(std::vector<std::complex<int16_t>>& data,
                       size_t n_desired       = 0,
                       const double timeout_s = rx_data_queue_pop_timeout_s);

    /*!
      @brief Receives data from the device and writes to an open file stream.
      @returns the number of samples written before any error, or @p n_desired if no errors occur
      @param out_stream the file to write the data to
      @param n_desired the number of samples to be received
      @param write_size_samples the block size for file writes
      @param timeout_s timeout in seconds
    */
    size_t write_rx_data(std::ofstream& out_stream,
                         const size_t n_desired,
                         const double timeout_s    = rx_data_queue_pop_timeout_s,
                         size_t write_size_samples = 65536);

    /*!
      @brief Receives data from the device and only checks packet sequence, discarding the data.
      @returns the number of samples received before a sequence error, or @p n_desired if no sequence errors occur
      @param n_desired the number of samples to be received
      @param timeout_s timeout in seconds
    */
    size_t check_rx_data_sequence_only(const size_t n_desired, const double timeout_s = rx_data_queue_pop_timeout_s);

    /*!
      @brief Sends transmit data to the device.
      @returns the number of samples placed in the queue for transmission
      @param data the complex @p itn16_t samples to be sent
      @param subdev the subdevice number
      @param timeout_s timeout in seconds
    */
    size_t put_tx_data(std::vector<std::complex<int16_t>> data,
                       const uint8_t subdev   = 0,
                       const double timeout_s = tx_data_queue_timeout_s);

    /*!
      @brief Requests basic information from the device.
      @returns a std::optional with a std::array containing the
          - device identifier
          - FPGA code version
          - MCU code version,
          - device serial number
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<std::array<uint32_t, 4>> device_hello(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Resets the device.
      @returns @b true if the device responds to the command, @b false otherwise
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool device_reset(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Clears the device status.
      @returns @b true if the device responds to the command, @b false otherwise
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool device_clear_status(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the device status.
      @returns a std::optional with a std::array containing device-dependent status information
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<std::array<uint32_t, 8>> device_get_status(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Sets the device time immediately.
      @returns @b true if the device responds to the command, @b false otherwise
      @param t the time to set
      @param subdev the subdevice number
      @param flags the packet flags (FLAGS_TIME_PRESENT is added automatically)
    */
    bool device_set_time_now(const std::chrono::time_point<std::chrono::system_clock>& t,
                             const uint8_t subdev = 0,
                             const uint8_t flags  = 0);

    /*!
      @brief Sets the device time when the next PPS is recieved by the device.
      @returns @b true if the device responds to the command, @b false otherwise; note that the
      device waits for the next PPS to respond
      @param t the time to set
      @param subdev the subdevice number
      @param flags the packet flags (FLAGS_TIME_PRESENT is added automatically)
    */
    bool device_set_time_next_pps(const std::chrono::time_point<std::chrono::system_clock>& t,
                                  const uint8_t subdev = 0,
                                  const uint8_t flags  = 0);

    /*!
      @brief Sets the device time immediately.
      @returns @b true if the input parameters @p sec and @p nsec are in range and the
      device responds to the command, @b false otherwise

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
      @returns @b true if the input parameters @p sec and @p nsec are in range and the
      device responds to the command, @b false otherwise; note that the
      device waits for the next PPS to respond

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
      @returns @b true if the device responds to the command, @b false otherwise
      @param sec device time seconds  (UINT32_MAX >= @p sec >= 0)
      @param nsec device time nanoseconds (1,000,000,000 > @p nsec >= 0)
      @param subdev the subdevice number
      @param flags the packet flags (FLAGS_TIME_PRESENT is added automatically)
    */
    bool device_get_time_now(uint32_t& sec, uint32_t& nsec, const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the device time immediately.
      @returns a std::optional with a std::chrono::time_point containing the device time
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<std::chrono::time_point<std::chrono::system_clock>> device_get_time_now(const uint8_t subdev = 0,
                                                                                          const uint8_t flags  = 0);

    /*!
      @brief Gets the current radio transmit stream state.
      @returns  a std::optional with a stream_state_t containing the transmit stream state
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<stream_state_t> get_tx_stream_state(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the current radio receive stream state.
      @returns  a std::optional with a stream_state_t containing the receive stream state
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<stream_state_t> get_rx_stream_state(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Starts transmitting immediately until a stop command is sent.
      @returns @b true if the device responds to the command, @b false otherwise
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool tx_start_now_until_stop(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Starts receiving immediately until a stop command is sent.
      @returns @b true if the device responds to the command, @b false otherwise
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool rx_start_now_until_stop(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Starts transmitting immediately until @p n samples are sent.
      @returns @b true if the device responds to the command, @b false otherwise
      @param n the number of samples to send
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool tx_start_now_for_n_samples(const uint64_t n, const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Starts receiving immediately until @p n samples are received.
      @returns @b true if the device responds to the command, @b false otherwise
      @param n the number of samples to send
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool rx_start_now_for_n_samples(const uint64_t n, const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Starts transmitting at time @p t until @p n samples are sent.
      @returns @b true if the device responds to the command, @b false otherwise
      @param t the start time
      @param n the number of samples to send
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool tx_start_at_time_for_n_samples(const std::chrono::time_point<std::chrono::system_clock>& t,
                                        const uint64_t n,
                                        const uint8_t subdev = 0,
                                        const uint8_t flags  = 0);

    /*!
      @brief Starts receiving at time @p t until @p n samples are received.
      @returns @b true if the device responds to the command, @b false otherwise
      @param t the start time
      @param n the number of samples to receive
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool rx_start_at_time_for_n_samples(const std::chrono::time_point<std::chrono::system_clock>& t,
                                        const uint64_t n,
                                        const uint8_t subdev = 0,
                                        const uint8_t flags  = 0);

    /*!
      @brief Starts transmitting at time @p t until a stop command is sent.
      @returns @b true if the device responds to the command, @b false otherwise
      @param t the start time
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool tx_start_at_time_until_stop(const std::chrono::time_point<std::chrono::system_clock>& t,
                                     const uint8_t subdev = 0,
                                     const uint8_t flags  = 0);

    /*!
      @brief Starts receiving at time @p t until a stop command is sent.
      @returns @b true if the device responds to the command, @b false otherwise
      @param t the start time
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool rx_start_at_time_until_stop(const std::chrono::time_point<std::chrono::system_clock>& t,
                                     const uint8_t subdev = 0,
                                     const uint8_t flags  = 0);

    /*!
      @brief Sets the IQ DC bias for the transmitter to control LO feedthrough.
      @returns @b true if the device responds to the command, @b false otherwise
      @param i_bias the I channel bias (-1 >= @p i_bias >= 1)
      @param q_bias the Q channel bias (-1 >= @p q_bias >= 1)
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool set_tx_iq_bias(const double i_bias, const double q_bias, const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Sets the IQ DC bias for the transmitter to control LO feedthrough.
      @returns @b true if the device responds to the command, @b false otherwise
      @param bias   a std::array containing i_bias, q_bias in that order
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool set_tx_iq_bias(const std::array<double, 2> bias, const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Sets the IQ DC bias for the receiver to control LO feedthrough.
      @returns @b true if the device responds to the command, @b false otherwise
      @param i_bias the I channel bias (-1 >= @p i_bias >= 1)
      @param q_bias the Q channel bias (-1 >= @p q_bias >= 1)
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool set_rx_iq_bias(const double i_bias, const double q_bias, const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Sets the IQ DC bias for the receiver to control LO feedthrough.
      @returns @b true if the device responds to the command, @b false otherwise
      @param bias   a std::array containing i_bias, q_bias in that order
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool set_rx_iq_bias(const std::array<double, 2> bias, const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the IQ DC bias for the transmitter.
      @returns a std::optional with a std::array containing (i_bias, q_bias) with
          (-1 >= @p i_bias >= 1) and (-1 >= @p q_bias >= 1)
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<std::array<double, 2>> get_tx_iq_bias(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the IQ DC bias for the receiver.
      @returns a std::optional with a std::array containing (i_bias, q_bias) with
          (-1 >= @p i_bias >= 1) and (-1 >= @p q_bias >= 1)
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<std::array<double, 2>> get_rx_iq_bias(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Sets the IQ correction for the transmitter to control image rejection.
      @returns @b true if the device responds to the command, @b false otherwise
      @param ii_corr I-I channel correction
      @param iq_corr I-Q channel correction
      @param qi_corr Q-I channel correction
      @param qq_corr Q-Q channel correction
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool set_tx_iq_corr(const double ii_corr,
                        const double iq_corr,
                        const double qi_corr,
                        const double qq_corr,
                        const uint8_t subdev = 0,
                        const uint8_t flags  = 0);

    /*!
      @brief Sets the IQ correction for the transmitter to control image rejection.
      @returns @b true if the device responds to the command, @b false otherwise
      @param corr    a std::array containing ii_corr, iq_corr, qi_corr, and qq_corr, in that order
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool set_tx_iq_corr(const std::array<double, 4> corr, const uint8_t subdev = 0, const uint8_t flags  = 0);

    /*!
      @brief Sets the IQ correction for the receiver to control image rejection.
      @returns @b true if the device responds to the command, @b false otherwise
      @param ii_corr I-I channel correction
      @param iq_corr I-Q channel correction
      @param qi_corr Q-I channel correction
      @param qq_corr Q-Q channel correction
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool set_rx_iq_corr(const double ii_corr,
                        const double iq_corr,
                        const double qi_corr,
                        const double qq_corr,
                        const uint8_t subdev = 0,
                        const uint8_t flags  = 0);

    /*!
      @brief Sets the IQ correction for the receiver to control image rejection.
      @returns @b true if the device responds to the command, @b false otherwise
      @param corr    a std::array containing ii_corr, iq_corr, qi_corr, and qq_corr, in that order
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool set_rx_iq_corr(const std::array<double, 4> corr, const uint8_t subdev = 0, const uint8_t flags  = 0);

    /*!
      @brief Gets the IQ correction for the transmitter.
      @returns a std::optional with a std::array containing
          - ii_corr
          - iq_corr
          - qi_corr
          - qq_corr
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<std::array<double, 4>> get_tx_iq_corr(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the IQ correction for the receiver.
      @returns a std::optional with a std::array containing
          - ii_corr
          - iq_corr
          - qi_corr
          - qq_corr
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<std::array<double, 4>> get_rx_iq_corr(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Stops transmitting immediately.
      @returns @b true if the device responds to the command, @b false otherwise
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool tx_stop_now(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Stops receiving immediately.
      @returns @b true if the device responds to the command, @b false otherwise
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool rx_stop_now(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the transmit center frequency range.
      @returns a std::optional with a  std::array containing the minimum and maximum frequencies in Hz
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<std::array<double, 2>> get_tx_freq_range(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the receive center frequency range.
      @returns a std::optional with a  std::array containing the minimum and maximum frequencies in Hz
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<std::array<double, 2>> get_rx_freq_range(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Sets the transmit center frequency.
      @returns @b true if the device responds to the command, @b false otherwise
      @param freq_hz the desired frequency in Hz
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool set_tx_freq(const double freq_hz, const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Sets the receive center frequency.
      @returns @b true if the device responds to the command, @b false otherwise
      @param freq_hz the desired frequency in Hz
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool set_rx_freq(const double freq_hz, const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the current transmit center frequency.
      @returns a std::optional with the center frequency in Hz
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<double> get_tx_freq(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the current receive center frequency.
      @returns a std::optional with the center frequency in Hz
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<double> get_rx_freq(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the transmit gain range.
      @returns a std::optional with a std::array containing the minimum and maximum gains in dB
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<std::array<double, 2>> get_tx_gain_range(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the receive gain range.
      @returns a std::optional with a std::array containing the minimum and maximum gains in dB
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<std::array<double, 2>> get_rx_gain_range(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Sets the transmit gain.
      @returns @b true if the device responds to the command, @b false otherwise
      @param gain_db the desired gain in dB
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool set_tx_gain(const double gain_db, const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Sets the receive gain.
      @returns @b true if the device responds to the command, @b false otherwise
      @param gain_db the desired gain in dB
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool set_rx_gain(const double gain_db, const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the current transmit gain.
      @returns a std::optional with the gain in dB
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<double> get_tx_gain(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the current receive gain.
      @returns a std::optional with the gain in dB
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<double> get_rx_gain(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the transmit sample rate range.
      @returns a std::optional with a std::array containing the minimum and maximum sample rates in samples/s
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<std::array<double, 2>> get_tx_rate_range(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the receive sample rate range.
      @returns a std::optional with a std::array containing the minimum and maximum sample rates in samples/s
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<std::array<double, 2>> get_rx_rate_range(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Sets the transmit sample rate.
      @returns @b true if the device responds to the command, @b false otherwise
      @param rate_samples_sec the desired rate in samples/s
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool set_tx_rate(const double rate_samples_sec, const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Sets the receive sample rate.
      @returns @b true if the device responds to the command, @b false otherwise
      @param rate_samples_sec the desired rate in samples/s
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool set_rx_rate(const double rate_samples_sec, const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the current transmit sample rate.
      @returns a std::optional with the rate in samples/s
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<double> get_tx_rate(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the current receive sample rate.
      @returns a std::optional with the rate in samples/s
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<double> get_rx_rate(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the number of transmit output ports.
      @returns a std::optional with the number of output ports
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<unsigned> get_tx_num_ports(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
     @brief Gets the number of receive input ports.
     @returns a std::optional with the number of input ports
     @param subdev the subdevice number
     @param flags the packet flags
   */
    std::optional<unsigned> get_rx_num_ports(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the name of a transmit output port.
      @returns a std::optional with the name of the specified output port
      @param port_num the port number
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<std::string> get_tx_port_name(const unsigned port_num, const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the name of a receive input port.
      @returns a std::optional with the name of the specified input port
      @param port_num the port number
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<std::string> get_rx_port_name(const unsigned port_num, const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Sets the transmit output port by number.
      @returns @b true if the device responds to the command, @b false otherwise
      @param port_num the desired output port number
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool set_tx_port(const unsigned port_num, const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Sets the transmit output port by name.
      @returns @b true if the device responds to the command, @b false otherwise
      @param port_name the desired output port name
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool set_tx_port(const std::string& port_name, const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Sets the receive input port by number.
      @returns @b true if the device responds to the command, @b false otherwise
      @param port_num the desired input port number
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool set_rx_port(const unsigned port_num, const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Sets the receive input port by name.
      @returns @b true if the device responds to the command, @b false otherwise
      @param port_name the desired input port name
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool set_rx_port(const std::string& port_name, const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the current transmit output port.
      @returns a std::optional with the port number
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<unsigned> get_tx_port(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the current receive input port.
      @returns a std::optional with the port number
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<unsigned> get_rx_port(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Enables or disables the transmit RF section.
      @returns @b true if the device responds to the command, @b false otherwise
      @param enabled the desired state
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool set_tx_enable(const bool enabled, const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Enables or disables the receive RF section.
      @returns @b true if the device responds to the command, @b false otherwise
      @param enabled the desired state
      @param subdev the subdevice number
      @param flags the packet flags
    */
    bool set_rx_enable(const bool enabled, const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the transmit capabilites.
      @returns a std::optional with a @p capabilities_t indicating the transmit capabilities
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<capabilities_t> get_tx_capabilities(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the receive capabilites.
      @returns a std::optional with a @p capabilities_t indicating the receive capabilities
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<capabilities_t> get_rx_capabilities(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the size of the device transmit and receive buffers.
      @returns a std::optional with a std::array containing the receive and transmit buffer sizes in bytes, in that order
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<std::array<uint32_t, 2>> device_get_buffer_info(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the current number of bytes used in the device transmit and receive buffers.
      @returns a std::optional with a std::array containing the receive and transmit buffer usage in bytes, in that order
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<std::array<uint32_t, 2>> device_get_buffer_use(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the status of the device timing references.
      @returns a std::optional with a std::array containing
          - the external PPS lock status
          - external 10 MHz lock status
          - internal reference oscillator lock status

        Note that the first two will be false if no external PPS and 10 MHz are connected.
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<std::array<bool, 3>> device_get_timing_status(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Gets the resolution of the device's clock.
      @returns a std::optional with a double containing the resolution in seconds
      @param subdev the subdevice number
      @param flags the packet flags
    */
    std::optional<double> device_get_timing_resolution(const uint8_t subdev = 0, const uint8_t flags = 0);

    /*!
      @brief Sets the timeout used by the host for commands sent to the device.
      We do not recommend values less than 0.5 seconds
      @returns @p true if the timeout is set, @p false if it is out of range
      @param timeout_s the timeout in seconds (must be greater than 0 and less than or equal to 3600)
    */
    bool set_host_device_command_timeout(const double timeout_s);

    /*!
      @brief Gets the current timeout used by the host for commands sent to the device.
      @returns the timeout in seconds
    */
    double get_host_device_command_timeout() const;

    /*!
      @brief
      Broadcasts a device discovery packet to the given IPv4 broadcast address, and returns the
      IPv4 addresses of the devices which respond.
      @returns a std::vector containing the IPv4 addresses of discovered devices (which may have zero length if no devices are
      found)
      @param local_addr the local address to send from
      @param broadcast_addr the broadcast address
      @param destination_port the port to broadcast to
      @param timeout_s the time in seconds to wait for responses
    */
    std::vector<net::ip::address_v4> device_discover_ipv4_addresses(const net::ip::address_v4& local_addr,
                                                                    const net::ip::address_v4& broadcast_addr,
                                                                    const unsigned destination_port = 1030,
                                                                    const double timeout_s = 10);

    /*!
      @brief Sets the IPv4 address of the device.
      This will disconnect the device if the given address is different from its current address.
      The IP address is not saved to nonvolatile memory in the device by this command.
      @returns @p true if the address is set, @p false otherwise
      @param device_address the device address to set
    */
    bool device_set_ipv4_address(const net::ip::address_v4& device_address);

    /*!
      @brief Saves the IPv4 address of the device to nonvolatile memory in the device.
      The IP address provided must be the same as the device's current IP address. A special-purpose program
      is required to change and save a device's IP address.
      @returns @p true if the address is saved, @p false otherwise
      @param device_address the device address to save
    */
    bool device_save_ipv4_address(const net::ip::address_v4& device_address);

    // FIXME: check the packet size computations below!!!

    /*!
      @brief Gets the maximum payload size in bytes for transport to and from the device. Note
      that the maximum packet size (corresponding the the minimum MTU) is the maximum payload size
      plus sizeof(packet_header) plus sizeof(IPv4 header), or maximum_payload_bytes + 24.
      @returns a std::optional with the maximum payload in bytes
    */
    std::optional<unsigned> device_get_max_payload_bytes();

    /*!
      @brief Sets the maximum payload size in bytes for transport to and from the device.
      The payload size must be a multiple of 8 bytes and 1024 <= max_payload_bytes <= 8192. Note
      that the maximum packet size (corresponding the the minimum MTU) is the maximum payload size
      plus sizeof(packet_header) plus sizeof(IPv4 header), or maximum_payload_bytes + 24.
      @returns @p true if the size is set, @p false otherwise
      @param max_payload_bytes the maximum payload size in bytes
    */
    bool device_set_max_payload_bytes(const unsigned max_payload_bytes);

  private:
    bool send_packet_and_check_response(packet& p, const std::string& cmd_name = "unknown");
    std::optional<response_queue_element> send_packet_and_return_response(packet& p, const std::string& cmd_name = "unknown");
    static void time_point_to_time_spec_t(const std::chrono::time_point<std::chrono::system_clock>& t, time_spec_t& ts);
    static std::string stream_state_to_string(const stream_state_t state);
    static std::string transport_state_to_string(const transport_state_t state);
    static std::string error_to_string(const uint32_t reason);

    [[nodiscard]] bool cmd_queue_push_check(packet& p, const std::string& cmd_name = "unknown");
    void async_handler();
    bool reset_rx_stream(const uint64_t n_samples_desired = 0);
    bool reset_tx_stream(const uint64_t n_samples_desired = 0);
    void receiver();
    void sender();
    void udp_receiver();
    void start_receive();
    void udp_sender();
    bool send_packet(packet& packet);
    void initialize_sdr(const uint8_t device_subdev = 0, const uint8_t tx_subdev = 0, const uint8_t rx_subdev = 0);
    void stop_sdr(const uint8_t device_subdev = 0, const uint8_t tx_subdev = 0, const uint8_t rx_subdev = 0);
    void start_send();
};

#endif  // VXSDR_HPP
