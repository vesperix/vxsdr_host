#include "vxsdr.hpp"

/*! @file vxsdr_udp.cpp
    @brief Contains the private UDP interface functions
*/

void vxsdr::receiver() {
    LOG_DEBUG("receiver thread started");

    if (vxsdr::thread_set_priority) {
        if (set_current_thread_priority_realtime(vxsdr::receive_thread_priority) != 0) {
            LOG_ERROR("unable to set receiver thread realtime priority");
        }
    }

    if (vxsdr::thread_set_affinity) {
        if (set_current_thread_affinity(vxsdr::receive_thread_cpu) != 0) {
            LOG_ERROR("unable to set receiver thread CPU affinity");
        }
    }

    vxsdr::udp_receiver();
    vxsdr::context.run();
    LOG_DEBUG("receiver thread finished");
}

void vxsdr::sender() {
    LOG_DEBUG("sender thread started");

    if (vxsdr::thread_set_priority) {
        if (set_current_thread_priority_realtime(vxsdr::send_thread_priority) != 0) {
            LOG_ERROR("unable to set sender thread realtime priority");
        }
    }

    if (vxsdr::thread_set_affinity) {
        if (set_current_thread_affinity(vxsdr::send_thread_cpu) != 0) {
            LOG_ERROR("unable to set sender thread CPU affinity");
        }
    }

    vxsdr::udp_sender();
    vxsdr::context.run();
    LOG_DEBUG("sender thread finished");
}

void vxsdr::udp_receiver() {
    vxsdr::receiver_socket.set_option(net::ip::udp::socket::reuse_address(true));
    vxsdr::receiver_socket.set_option(net::socket_base::receive_buffer_size(vxsdr::network_receive_buffer_bytes));
    net::socket_base::receive_buffer_size option;
    vxsdr::receiver_socket.get_option(option);
    unsigned network_buffer_size = option.value();
    if (network_buffer_size != vxsdr::network_receive_buffer_bytes) {
        LOG_ERROR(fmt::format("cannot set network receive buffer size to {:d} (got {:d})", vxsdr::network_receive_buffer_bytes,
                              network_buffer_size));
    } else {
        LOG_INFO(fmt::format("network receive buffer size set to {:d}", network_buffer_size));
    }

    vxsdr::start_receive();
}

void vxsdr::start_receive() {
    LOG_DEBUG("start_receive started");
    net::ip::udp::endpoint remote_endpoint;
    uint16_t last_seq       = 0;

    vxsdr::bytes_received   = 0;
    vxsdr::samples_received = 0;
    vxsdr::packets_received = 0;
    vxsdr::sequence_errors  = 0;

#ifdef VXSDR_TX_BUFFER_DIAG
    constexpr size_t buffer_reserve_entries = 65536;
    vxsdr::buffer_data_times.reserve(buffer_reserve_entries);
    vxsdr::buffer_fill_bytes.reserve(buffer_reserve_entries);
#endif // VXSDR_TX_BUFFER_DIAG

    vxsdr::udp_rx_state     = TRANSPORT_RUNNING;

    while (not vxsdr::receiver_thread_stop_flag) {
        static data_queue_element recv_buffer;
        size_t bytes_in_packet = 0, n_samps = 0;
#ifndef VXSDR_ASYNC_RX
        bytes_in_packet = vxsdr::receiver_socket.receive_from(net::buffer(&recv_buffer, sizeof(recv_buffer)), remote_endpoint);
#else
        /*
            https://www.boost.org/doc/libs/1_77_0/doc/html/boost_asio/example/cpp11/futures/daytime_client.cpp

            std::array<char, 128> recv_buf;
            udp::endpoint sender_endpoint;
            std::future<std::size_t> recv_length =
              socket.async_receive_from(
                  net::buffer(recv_buf),
                  sender_endpoint,
                  net::use_future);

            // Do other things here while the receive completes.

            std::cout.write(
                recv_buf.data(),
                recv_length.get()); // Blocks until receive is complete.
        */
        std::future<size_t> result = vxsdr::receiver_socket.async_receive_from(net::buffer(&recv_buffer, sizeof(recv_buffer)),
                                                                               remote_endpoint, net::use_future);
        if (result.wait_for(std::chrono::milliseconds(vxsdr::receiver_socket_timeout_ms)) == std::future_status::timeout) {
            bytes_in_packet = 0;
            // LOG_DEBUG("timeout");
        } else {
            bytes_in_packet = result.get();
        }
#endif  // VXSDR_ASYNC_RX

        if (bytes_in_packet >= sizeof(packet_header) and remote_endpoint == destination_endpoint) {
            n_samps = 0;
            vxsdr::packets_received++;
            vxsdr::packet_types_received[recv_buffer.hdr.packet_type]++;
            vxsdr::bytes_received += bytes_in_packet;
            bool has_time   = (recv_buffer.hdr.flags & (uint8_t)FLAGS_TIME_PRESENT) != 0;
            bool has_stream = (recv_buffer.hdr.flags & (uint8_t)FLAGS_STREAM_ID_PRESENT) != 0;
            if (recv_buffer.hdr.packet_size != bytes_in_packet) {
                LOG_ERROR(fmt::format("packet_size in {:s} header incorrect ({:d}, {:d})",
                                      packet_type_to_name(recv_buffer.hdr.packet_type), recv_buffer.hdr.packet_size,
                                      bytes_in_packet));
                recv_buffer.hdr.packet_size = std::min(bytes_in_packet, (unsigned long)recv_buffer.hdr.packet_size);
            }

            if (vxsdr::log_nondata_packets and recv_buffer.hdr.packet_type != PACKET_TYPE_TX_SIGNAL_DATA and
                recv_buffer.hdr.packet_type != PACKET_TYPE_RX_SIGNAL_DATA and
                recv_buffer.hdr.packet_type != PACKET_TYPE_TX_SIGNAL_DATA_ACK and
                recv_buffer.hdr.packet_type != PACKET_TYPE_RX_SIGNAL_DATA_ACK) {
                auto* p = reinterpret_cast<largest_cmd_or_rsp_packet*>(&recv_buffer);
                log_packet_info(*p, -1);
            }

            if (recv_buffer.hdr.packet_type == PACKET_TYPE_RX_SIGNAL_DATA and
                (vxsdr::rx_stream_state == STREAM_WAITING_FOR_START or vxsdr::rx_stream_state == STREAM_RUNNING)) {
                if (vxsdr::rx_stream_state == STREAM_WAITING_FOR_START) {
                    vxsdr::rx_stream_state = STREAM_RUNNING;
                }
                uint16_t header_size = sizeof(packet_header);
                if (has_time) {
                    header_size += sizeof(time_spec_t);
                }
                if (has_stream) {
                    header_size += sizeof(stream_spec_t);
                }
                n_samps = (recv_buffer.hdr.packet_size - header_size) / sizeof(std::complex<int16_t>);
                vxsdr::samples_received += n_samps;
                vxsdr::samples_received_current_stream+= n_samps;
            }
            if (vxsdr::packets_received > 1 and recv_buffer.hdr.sequence_counter != (uint16_t)(last_seq + 1)) {
                vxsdr::sequence_errors++;
                vxsdr::sequence_errors_current_stream++;
#ifdef VXSDR_SEQ_ERR_DIAG
                vxsdr::seq_error_times.push_back(std::chrono::system_clock::now());
                vxsdr::seq_error_packet_type.push_back(recv_buffer.hdr.packet_type);
                vxsdr::seq_error_counter.push_back(recv_buffer.hdr.sequence_counter);
                vxsdr::seq_error_expected.push_back((uint16_t)(last_seq + 1));
#endif  // VXSDR_SEQ_ERR_DIAG
            }

            last_seq = recv_buffer.hdr.sequence_counter;
            switch (recv_buffer.hdr.packet_type) {
                // receive buffers go into the rx data queue
                case PACKET_TYPE_RX_SIGNAL_DATA:
                    if (vxsdr::rx_stream_state == STREAM_RUNNING) {
                        if (not vxsdr::rx_data_queue.push(recv_buffer)) {
                            unsigned wait_time_us = 0;
                            while (vxsdr::rx_data_queue.write_available() == 0 and
                                   wait_time_us < vxsdr::rx_data_queue_push_timeout_us) {
                                if (vxsdr::rx_data_queue_wait_us > 0) {
                                    std::this_thread::sleep_for(std::chrono::microseconds(vxsdr::rx_data_queue_wait_us));
                                    wait_time_us += vxsdr::rx_data_queue_wait_us;
                                } else {
                                    break;  // avoid an endless loop if rx_data_queue_wait_us = 0
                                }
                            }
                            if (not vxsdr::rx_data_queue.push(recv_buffer)) {
                                LOG_ERROR(fmt::format("timeout pushing to rx data queue: {:d}", vxsdr::samples_received));
                                vxsdr::rx_stream_state = STREAM_ERROR;
                            }
                        }
                    }
                    break;
                // tx sginal data acks special cased since they contain throttling information
                case PACKET_TYPE_TX_SIGNAL_DATA_ACK: {
                    auto* r                     = reinterpret_cast<tx_signal_data_ack_packet*>(&recv_buffer);
                    vxsdr::tx_buffer_used_bytes = r->tx_buffer_used_bytes;
#ifdef VXSDR_TX_BUFFER_DIAG
                    vxsdr::buffer_data_times.push_back(std::chrono::system_clock::now());
                    vxsdr::buffer_fill_bytes.push_back(r->tx_buffer_used_bytes);
#endif // VXSDR_TX_BUFFER_DIAG
                    if(vxsdr::tx_buffer_size_bytes > 0) {
                        vxsdr::tx_buffer_fill_percent =
                            std::min(100U, (100U * vxsdr::tx_buffer_used_bytes) / vxsdr::tx_buffer_size_bytes);
                    } else {
                        vxsdr::tx_buffer_fill_percent = 0;
                    }
                    // DO NOT PUSH DATA ACKS TO COMMAND ACK QUEUE!!
                } break;

                case PACKET_TYPE_RX_RADIO_CMD_RSP:
                case PACKET_TYPE_TX_RADIO_CMD_RSP:
                case PACKET_TYPE_DEVICE_CMD_ERR:
                case PACKET_TYPE_TX_RADIO_CMD_ERR:
                case PACKET_TYPE_RX_RADIO_CMD_ERR: {
                    auto* p = reinterpret_cast<response_queue_element*>(&recv_buffer);
                    vxsdr::response_queue.bounded_push(*p);
                } break;

                // the buffer info commands are special cased so they automatically update the buffer sizes and use
                // since they are not part of the public interface, they are not pushed into any queue
                case PACKET_TYPE_DEVICE_CMD_RSP: {
                    switch (recv_buffer.hdr.command) {
                        case DEVICE_CMD_GET_BUFFER_USE: {
                            auto* r                     = reinterpret_cast<two_uint32_packet*>(&recv_buffer);
                            vxsdr::tx_buffer_used_bytes = r->value1;
#ifdef VXSDR_TX_BUFFER_DIAG
                            vxsdr::buffer_data_times.push_back(std::chrono::system_clock::now());
                            vxsdr::buffer_fill_bytes.push_back(r->value1);
#endif // VXSDR_TX_BUFFER_DIAG
                            vxsdr::rx_buffer_used_bytes = r->value2;
                            if(vxsdr::tx_buffer_size_bytes > 0) {
                                vxsdr::tx_buffer_fill_percent =
                                    std::min(100U, (100U * vxsdr::tx_buffer_used_bytes) / vxsdr::tx_buffer_size_bytes);
                            } else {
                                vxsdr::tx_buffer_fill_percent = 0;
                            }
                            if(vxsdr::rx_buffer_size_bytes > 0) {
                                vxsdr::rx_buffer_fill_percent =
                                    std::min(100U, (100U * vxsdr::rx_buffer_used_bytes) / vxsdr::rx_buffer_size_bytes);
                            } else {
                                vxsdr::rx_buffer_fill_percent = 0;
                            }
                        } break;
                        case DEVICE_CMD_GET_BUFFER_INFO: {
                            auto* r                     = reinterpret_cast<two_uint32_packet*>(&recv_buffer);
                            vxsdr::tx_buffer_size_bytes = r->value1;
                            vxsdr::rx_buffer_size_bytes = r->value2;
                        } break;
                        default:
                            break;
                    }
                    auto* p = reinterpret_cast<response_queue_element*>(&recv_buffer);
                    vxsdr::response_queue.bounded_push(*p);
                } break;

                case PACKET_TYPE_DEVICE_CMD_ACK:
                case PACKET_TYPE_RX_RADIO_CMD_ACK:
                case PACKET_TYPE_TX_RADIO_CMD_ACK: {
                    auto* p = reinterpret_cast<cmd_ack_queue_element*>(&recv_buffer);
                    vxsdr::cmd_ack_queue.bounded_push(*p);
                } break;

                // DEVICE_CMD_RSP handled above
                case PACKET_TYPE_ASYNC_MSG: {
                    // FIXME: do whatever is needed to notify or shut down!!!
                    auto* p = reinterpret_cast<async_msg_queue_element*>(&recv_buffer);
                    vxsdr::async_msg_queue.bounded_push(*p);
                } break;

                // packet types that should never show up at the host are ignored
                case PACKET_TYPE_TX_SIGNAL_DATA:
                case PACKET_TYPE_DEVICE_CMD:
                case PACKET_TYPE_RX_RADIO_CMD:
                case PACKET_TYPE_TX_RADIO_CMD:
                default:  // unrecognized packet types
                    break;
            }
        }
    }

    vxsdr::udp_rx_state = TRANSPORT_SHUTDOWN;

    LOG_INFO("start_receive exiting:");
    LOG_INFO(fmt::format("   {:12d} packets received", vxsdr::packets_received));
    for (unsigned i = 0; i < vxsdr::packet_types_received.size(); i++) {
        if (vxsdr::packet_types_received[i] > 0) {
            LOG_INFO(fmt::format("   {:12d} {:20s} ({:d})", vxsdr::packet_types_received[i], packet_type_to_name(i), i));
        }
    }
    LOG_INFO(fmt::format("   {:12d} samples received", vxsdr::samples_received));
    LOG_INFO(fmt::format("   {:12d} bytes received", vxsdr::bytes_received));
    LOG_INFO(fmt::format("   {:12d} sequence errors", vxsdr::sequence_errors));
#ifdef VXSDR_SEQ_ERR_DIAG
    for (unsigned i = 0; i < vxsdr::seq_error_times.size(); i++) {
        LOG_INFO(fmt::format(" {:30s} {:20s} {:8d} {:8d}", format_time(vxsdr::seq_error_times[i]),
                             packet_type_to_name(vxsdr::seq_error_packet_type[i]), vxsdr::seq_error_counter[i],
                             vxsdr::seq_error_expected[i]));
    }
#endif  // VXSDR_SEQ_ERR_DIAG
#ifdef VXSDR_TX_BUFFER_DIAG
    LOG_INFO(fmt::format("   {:12d} buffer data points", vxsdr::buffer_data_times.size()));
    for (unsigned i = 0; i < vxsdr::buffer_data_times.size(); i++) {
        LOG_INFO(fmt::format(" {:30s} {:8d}", format_time(vxsdr::buffer_data_times[i]), vxsdr::buffer_fill_bytes[i]));
        vxsdr::buffer_fill_min = std::min(vxsdr::buffer_fill_bytes[i], vxsdr::buffer_fill_min);
        vxsdr::buffer_fill_max = std::max(vxsdr::buffer_fill_bytes[i], vxsdr::buffer_fill_max);
    }
    LOG_INFO(fmt::format("   {:12d} bytes minimum", vxsdr::buffer_fill_min));
    LOG_INFO(fmt::format("   {:12d} bytes maximum", vxsdr::buffer_fill_max));
#endif // VXSDR_TX_BUFFER_DIAG
}

void vxsdr::udp_sender() {
    vxsdr::sender_socket.set_option(net::socket_base::send_buffer_size(network_send_buffer_bytes));

    net::socket_base::send_buffer_size option;
    vxsdr::sender_socket.get_option(option);
    int network_buffer_size = option.value();

    if (network_buffer_size != network_send_buffer_bytes) {
        LOG_ERROR(
            fmt::format("cannot set network send buffer size to {:d} (got {:d})", network_send_buffer_bytes, network_buffer_size));
    } else {
        LOG_INFO(fmt::format("network send buffer size set to {:d}", network_buffer_size));
    }

    vxsdr::start_send();
}

bool vxsdr::send_packet(packet& packet) {
    bool retval = true;
    vxsdr::packet_types_sent[packet.hdr.packet_type]++;
    packet.hdr.sequence_counter = (uint16_t)(vxsdr::packets_sent % (UINT16_MAX + 1));
    vxsdr::packets_sent++;

    if (vxsdr::log_nondata_packets and
        packet.hdr.packet_type != PACKET_TYPE_TX_SIGNAL_DATA and
        packet.hdr.packet_type != PACKET_TYPE_RX_SIGNAL_DATA and
        packet.hdr.packet_type != PACKET_TYPE_TX_SIGNAL_DATA_ACK and
        packet.hdr.packet_type != PACKET_TYPE_RX_SIGNAL_DATA_ACK) {
        auto* p = reinterpret_cast<largest_cmd_or_rsp_packet*>(&packet);
        log_packet_info(*p, 1);
    }

    size_t bytes = vxsdr::sender_socket.send_to(net::buffer(&packet, packet.hdr.packet_size), vxsdr::destination_endpoint);
    if (bytes != packet.hdr.packet_size) {
        vxsdr::send_errors++;
        vxsdr::send_errors_current_stream++;
        retval = false;
    }
    if (packet.hdr.packet_type == PACKET_TYPE_TX_SIGNAL_DATA) {
        uint16_t header_size = sizeof(packet_header);
        if ((bool)(packet.hdr.flags & FLAGS_TIME_PRESENT)) {
            header_size += sizeof(time_spec_t);
        }
        if ((bool)(packet.hdr.flags & FLAGS_STREAM_ID_PRESENT)) {
            header_size += sizeof(stream_spec_t);
        }
        vxsdr::samples_sent += (packet.hdr.packet_size - header_size) / sizeof(std::complex<int16_t>);
        vxsdr::samples_sent_current_stream += (packet.hdr.packet_size - header_size) / sizeof(std::complex<int16_t>);
    }
    vxsdr::bytes_sent += bytes;
    return retval;
}

void vxsdr::start_send() {
    LOG_DEBUG("start_send started");

    vxsdr::packets_sent                      = 0;
    vxsdr::samples_sent                      = 0;
    vxsdr::bytes_sent                        = 0;

    static constexpr size_t data_buffer_size = 16;
    std::array<data_queue_element, data_buffer_size> data_buffer;
    command_queue_element cmd_buffer;

    unsigned throttling                      = 0;

    uint64_t last_check                               = 0;
    uint64_t buffer_check_interval_samples            = 2048;

    // wait for the rx to reach RUNNING
    unsigned total_wait_ms                            = 0;
    LOG_INFO("waiting for udp rx to reach RUNNING state");
    while (vxsdr::udp_rx_state != TRANSPORT_RUNNING and total_wait_ms < vxsdr::udp_tx_ready_timeout_ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(vxsdr::udp_tx_ready_wait_ms));
        total_wait_ms += vxsdr::udp_tx_ready_wait_ms;
    }

    if (total_wait_ms >= vxsdr::udp_tx_ready_timeout_ms) {
        LOG_ERROR("udp tx timed out waiting for udp rx to reach RUNNING state; shutting down");
        vxsdr::receiver_thread_stop_flag = true;
        vxsdr::sender_thread_stop_flag   = true;
    } else {
        LOG_INFO("udp rx in RUNNING state");
    }

    vxsdr::udp_tx_state = TRANSPORT_RUNNING;
    LOG_INFO("udp tx entering RUNNING state");

    while (not vxsdr::sender_thread_stop_flag) {
        while (vxsdr::command_queue.pop(cmd_buffer)) {
            vxsdr::send_packet(cmd_buffer);
        }

        if ((vxsdr::tx_stream_state == STREAM_WAITING_FOR_START or vxsdr::tx_stream_state == STREAM_RUNNING) and
                   vxsdr::tx_data_queue.read_available() > 0) {
            size_t n_popped = 0;
            if(vxsdr::tx_buffer_fill_percent < vxsdr::throttle_hard_percent) {
                n_popped = vxsdr::tx_data_queue.pop(&data_buffer.front(), data_buffer.size());
                for (size_t i = 0; i < n_popped; i++) {
                    if (vxsdr::samples_sent - last_check >= buffer_check_interval_samples) {
                        // request ack to update buffer use
                        data_buffer[i].hdr.flags |= FLAGS_REQUEST_ACK;
                        vxsdr::send_packet(data_buffer[i]);
                        last_check = vxsdr::samples_sent;
                    } else {
                        vxsdr::send_packet(data_buffer[i]);
                    }
                    // throttle if needed
                    if (vxsdr::tx_buffer_fill_percent > vxsdr::throttle_on_percent) {
                        throttling = true;
                    } else if (vxsdr::tx_buffer_fill_percent <= vxsdr::throttle_off_percent) {
                        throttling = false;
                    }
                    if (throttling) {
                        std::this_thread::sleep_for(std::chrono::microseconds(vxsdr::throttle_amount_us));
                    }
                }
            } else {
                // throttle hard means send one empty data packet and request ack to update buffer use
                data_buffer[0].hdr = { PACKET_TYPE_TX_SIGNAL_DATA, 0, 0, FLAGS_REQUEST_ACK, sizeof(header_only_packet), 0 };
                vxsdr::send_packet(data_buffer[0]);
                last_check = vxsdr::samples_sent;
                std::this_thread::sleep_for(std::chrono::microseconds(vxsdr::throttle_amount_us));
            }
        } else {
            if (vxsdr::send_thread_sleep_us > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(vxsdr::send_thread_sleep_us));
            }
        }
    }

    vxsdr::udp_tx_state = TRANSPORT_SHUTDOWN;

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    LOG_INFO("start_send exiting:");
    LOG_INFO(fmt::format("   {:12d} packets sent", packets_sent));
    for (unsigned i = 0; i < vxsdr::packet_types_sent.size(); i++) {
        if (vxsdr::packet_types_sent[i] > 0) {
            LOG_INFO(fmt::format("   {:12d} {:20s} ({:d})", vxsdr::packet_types_sent[i], packet_type_to_name(i), i));
        }
    }
    LOG_INFO(fmt::format("   {:12d} samples sent", vxsdr::samples_sent));
    LOG_INFO(fmt::format("   {:12d} bytes sent", vxsdr::bytes_sent));
    LOG_INFO(fmt::format("   {:12d} send errors", vxsdr::send_errors));
}
