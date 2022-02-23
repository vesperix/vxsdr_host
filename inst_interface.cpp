#include <iostream>

#include "inst_interface.hpp"

int main() {
  net::io_context ctx;
  tcp_connection conn(ctx, "192.168.8.41", 1234);
  constexpr unsigned hp_8564_addr = 18;
  hp_856x_set_ref_level(conn, hp_8564_addr, -20);

  double f_out = 12e9;
  double f_if = 160e6;

  hp_856x_set_freq(conn, hp_8564_addr, f_out - f_if);
  hp_856x_set_span(conn, hp_8564_addr, 0);

  auto sweep_time = hp_856x_read_sweep_time(conn, hp_8564_addr);
  hp_856x_set_freq(conn, hp_8564_addr, f_out);
  std::cout << hp_856x_read_carron(conn, hp_8564_addr, sweep_time) << std::endl;
  hp_856x_set_freq(conn, hp_8564_addr, f_out - f_if);
  std::cout << hp_856x_read_carron(conn, hp_8564_addr, sweep_time) << std::endl;
  hp_856x_set_freq(conn, hp_8564_addr, f_out - 2 * f_if);
  std::cout << hp_856x_read_carron(conn, hp_8564_addr, sweep_time) << std::endl;

  hp_856x_set_span(conn, hp_8564_addr, 1e4);
  sweep_time = hp_856x_read_sweep_time(conn, hp_8564_addr);
  std::this_thread::sleep_for(std::chrono::milliseconds(std::lround(1000 * sweep_time)));

  hp_856x_set_freq(conn, hp_8564_addr, f_out);
  hp_856x_set_mkr_peak(conn, hp_8564_addr);
  std::cout << hp_856x_read_mkr_amp(conn, hp_8564_addr) << std::endl;
  hp_856x_set_freq(conn, hp_8564_addr, f_out - f_if);
  hp_856x_set_mkr_peak(conn, hp_8564_addr);
  std::cout << hp_856x_read_mkr_amp(conn, hp_8564_addr) << std::endl;
  hp_856x_set_freq(conn, hp_8564_addr, f_out - 2 * f_if);
  hp_856x_set_mkr_peak(conn, hp_8564_addr);
  std::cout << hp_856x_read_mkr_amp(conn, hp_8564_addr) << std::endl;
}