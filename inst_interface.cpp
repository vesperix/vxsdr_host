#include <iostream>

#include "inst_interface.hpp"

int main() {
  net::io_context ctx;
  tcp_connection conn(ctx, "192.168.8.41", 1234);
  constexpr unsigned hp_8564_addr = 15;
  hp_856x_set_ref_level(conn, hp_8564_addr);
  hp_856x_set_zero_span(conn, hp_8564_addr);
  hp_856x_set_freq(conn, hp_8564_addr, 10e9);
  auto sweep_time = hp_856x_read_sweep_time(conn, hp_8564_addr);
  std::cout << hp_856x_read_power(conn, hp_8564_addr, sweep_time) << std::endl;
}