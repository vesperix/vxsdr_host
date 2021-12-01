#ifndef HOST_RADIO_OPTIONS_HPP
#define HOST_RADIO_OPTIONS_HPP

#include <boost/program_options.hpp>

#include "vxsdr.hpp"

void add_rx_1ch_options(boost::program_options::options_description& desc);
void add_tx_1ch_options(boost::program_options::options_description& desc);
void add_common_options(boost::program_options::options_description& desc);
void add_network_options(boost::program_options::options_description& desc);
void init_vm(boost::program_options::options_description& desc, boost::program_options::variables_map& vm, int argc, char* argv[]);

int set_common_options(boost::program_options::variables_map& vm, std::unique_ptr<vxsdr>& radio);
int set_rx_1ch_options(boost::program_options::variables_map& vm, std::unique_ptr<vxsdr>& radio);
int set_tx_1ch_options(boost::program_options::variables_map& vm, std::unique_ptr<vxsdr>& radio);

#endif  // HOST_RADIO_OPTIONS_HPP
