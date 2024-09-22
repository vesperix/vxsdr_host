#ifndef HOST_RADIO_OPTIONS_HPP
#define HOST_RADIO_OPTIONS_HPP

#include "option_utils.hpp"
#include "vxsdr.hpp"

void add_rx_1ch_options(option_utils::program_options& desc);
void add_tx_1ch_options(option_utils::program_options& desc);
void add_common_options(option_utils::program_options& desc);
void add_network_options(option_utils::program_options& desc);

int set_common_options(option_utils::parsed_options& vm, std::unique_ptr<vxsdr>& radio);
int set_rx_1ch_options(option_utils::parsed_options& vm, std::unique_ptr<vxsdr>& radio);
int set_tx_1ch_options(option_utils::parsed_options& vm, std::unique_ptr<vxsdr>& radio);

#endif  // HOST_RADIO_OPTIONS_HPP
