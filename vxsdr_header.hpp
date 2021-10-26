#include <cstdint>
#include <complex>

#ifndef VXSDR_HEADER_HPP
#define VXSDR_HEADER_HPP

#include "packet_defs.h"

class packet {
  public:
    packet_header          hdr;
};

class header_only_packet : public packet {};

//FIXME: Need to add number of samples so that we don't always use MAX_DATA_LENGTH_SAMPLES!

class data_packet : public packet {
  public:
    std::complex<int16_t>  data[MAX_DATA_LENGTH_SAMPLES];
};

class data_packet_time : public packet {
  public:
    time_spec_t            time;
    std::complex<int16_t>  data[MAX_DATA_LENGTH_SAMPLES];
};

class data_packet_stream : public packet {
  public:
    stream_spec_t          stream_id;
    std::complex<int16_t>  data[MAX_DATA_LENGTH_SAMPLES];
};

class data_packet_time_stream : public packet {
  public:
    time_spec_t            time;
    stream_spec_t          stream_id;
    std::complex<int16_t>  data[MAX_DATA_LENGTH_SAMPLES];
};

class largest_data_packet : public data_packet_time_stream {};

class async_msg_packet : public packet {
  public:
    uint8_t                severity = 0;
    uint8_t                device_subdev = 0;
    uint8_t                tx_subdev = 0;
    uint8_t                rx_subdev = 0;
};

class cmd_or_rsp_packet_time : public header_only_packet {
  public:
    time_spec_t            time;
};

class cmd_or_rsp_packet_time_stream : public header_only_packet {
  public:
    time_spec_t            time;
    stream_spec_t          stream_id;
};

class cmd_or_rsp_packet_samples : public header_only_packet {
  public:
    uint64_t               n_samples = 0;
};

class cmd_or_rsp_packet_time_samples : public header_only_packet {
  public:
    time_spec_t            time;
    uint64_t               n_samples = 0;
};

class largest_cmd_or_rsp_packet : public cmd_or_rsp_packet_time_stream {
    [[maybe_unused]]
     uint8_t               payload[MAX_PAYLOAD_LENGTH_BYTES] = { 0 };
};

class tx_signal_data_ack_packet : public header_only_packet {
  public:
    uint64_t               requester_seq_num = 0;
    uint32_t               tx_buffer_used_bytes = 0;
    uint32_t               sequence_error_count = 0;
};

class get_buffer_use_rsp_packet : public header_only_packet {
  public:
    uint32_t               rx_buffer_used_bytes = 0;
    uint32_t               tx_buffer_used_bytes = 0;
};

class get_buffer_info_rsp_packet : public header_only_packet {
  public:
    uint32_t               rx_buffer_size_bytes = 0;
    uint32_t               tx_buffer_size_bytes = 0;
};

class clear_data_buffer_packet : public header_only_packet {
  public:
    uint8_t                clear_all = 0;
    uint8_t                device_subdev = 0;
    uint8_t                tx_subdev = 0;
    uint8_t                rx_subdev = 0;
};

class hello_rsp_packet : public header_only_packet {
  public:
    uint32_t               mcu_sw_version = 0;
    uint32_t               fpga_fw_version = 0;
};

class status_rsp_packet : public header_only_packet {
  public:
    uint16_t               fpga_temperature = 0;
    uint16_t               dac_temperature = 0;
    uint32_t               reserved = 0;
    uint32_t               sequence_error_count = 0;
    uint32_t               data_flow_error_flags = 0;
    uint32_t               ddr_arbiter_status = 0;
    uint32_t               ethernet_status = 0;
};

class dev_read_write_reg_packet : public header_only_packet {
  public:
    uint32_t               register_number = 0;
    uint32_t               value = 0;
};

class transport_addr_packet : public header_only_packet {
  public:
    uint32_t               ip_addr = 0;
    uint32_t               reserved = 0;
};

class transport_pktsiz_packet : public header_only_packet {
  public:
    uint32_t               packet_size_bytes = 0;  // must be multiple of 8 bytes, min = 1024, max = 8216
    uint32_t               reserved = 0;
};

class dataflow_status_packet : public header_only_packet {
  public:
    uint16_t               tx_radio_status = 0;
    uint16_t               rx_radio_status = 0;
    uint32_t               device_status = 0;
};

class radio_rf_info_packet : public header_only_packet {
  public:
    double                 min_freq = 0.0;
    double                 max_freq = 0.0;
    double                 min_gain = 0.0;
    double                 max_gain = 0.0;
    double                 min_rate = 0.0;
    double                 max_rate = 0.0;
    double                 min_bw   = 0.0;
    double                 max_bw   = 0.0;
    uint16_t               num_ports = 0;
    uint16_t               reserved = 0;
    uint32_t               capabilities = 0;
};

class radio_rf_freq_packet : public header_only_packet {
  public:
    double                 freq = 0.0;
    double                 rtol = 0.0;
};

class radio_rf_gain_packet : public header_only_packet {
  public:
    double                 gain = 0.0;
};

class radio_sample_rate_packet : public header_only_packet {
  public:
    double                 rate = 0;
};

class radio_rf_enable_packet : public header_only_packet {
  public:
    uint32_t               enabled = 0; //TX/RX path enable (1 = on, 0 = off)
    uint32_t               reserved = 0;
};

class radio_iq_bias_packet : public header_only_packet {
  public:
    double                 i_bias = 0; // range = +/- 1 covering min to max
    double                 q_bias = 0;
};

class radio_iq_corr_packet : public header_only_packet {
  public:
    double                 ii_corr = 1;
    double                 iq_corr = 0;
    double                 qi_corr = 0;
    double                 qq_corr = 1;
};

#endif // VXSDR_HEADER_HPP
