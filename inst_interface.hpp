#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>
#include <thread>

#define FMT_HEADER_ONLY
#include <spdlog/fmt/fmt.h>

#include <boost/asio/ts/io_context.hpp>
#include <boost/asio/ts/socket.hpp>
#include <boost/asio/ts/net.hpp>
#include <boost/asio/ts/buffer.hpp>
namespace net = boost::asio;

class tcp_connection {
  private:
    net::ip::tcp::socket sock;
  public:
    tcp_connection(net::io_context &context, const std::string &target_addr, const unsigned port) : sock(context) {
      net::ip::tcp::endpoint ep(net::ip::make_address_v4(target_addr), port);
      sock.connect(ep);
      sock.send(net::buffer("++savecfg 0\n"));
      sock.send(net::buffer("++mode 1\n"));
      sock.send(net::buffer("++auto 1\n"));
      sock.send(net::buffer("++read_tmo_ms 3000\n"));
      sock.send(net::buffer("++eos 3\n"));
      sock.send(net::buffer("++eoi 1\n"));
      sock.send(net::buffer("++clr\n"));
    }
    ~tcp_connection() {
      sock.close();
    }
    size_t send(const std::string &str) {
      std::cerr << str;
      return net::write(sock, net::buffer(str, str.size()));
    }
    std::string recv() {
      char s[4096];
      auto len = sock.read_some(net::buffer(s));
      if (len > 0)
        std::cerr << std::string(s, len);
      return std::string(s, len);
    }
};

double str2double(const std::string s) {
  return std::atof(s.c_str());
}

bool hp_856x_set_ref_level(tcp_connection &conn, const unsigned gpib_addr, const double ref_lvl = 0.0) {
  conn.send(fmt::format("++addr {:d}\n", gpib_addr));
  conn.send("++auto 0\n");
  conn.send(fmt::format("RL {:d} DBM\n", std::lround(ref_lvl)));
  conn.send("++auto 1\n");
  conn.send("RL?\n");
  auto rl = str2double(conn.recv());
  conn.send("++auto 0\n");
  if (std::abs(ref_lvl - rl) > 1) {
    return false;
  }
  return true;
}

bool hp_856x_set_freq(tcp_connection &conn, const unsigned gpib_addr, const double freq) {
  conn.send(fmt::format("++addr {:d}\n", gpib_addr));
  conn.send("++auto 0\n");
  conn.send(fmt::format("CF {:e} HZ\n", freq));
  conn.send("++auto 1\n");
  conn.send("CF?\n");
  auto setfreq = str2double(conn.recv());
  conn.send("++auto 0\n");
  if (std::abs(freq - setfreq) > 1.0) {
    return false;
  }
  return true;
}

bool hp_856x_set_span(tcp_connection &conn, const unsigned gpib_addr, const double span) {
  conn.send(fmt::format("++addr {:d}\n", gpib_addr));
  conn.send("++auto 0\n");
  if (span == 0.0)
    conn.send("SP ZERO\n");
  else
    conn.send(fmt::format("SP {:e} HZ\n", span));
  conn.send("++auto 1\n");
  conn.send("SP?\n");
  auto sp = str2double(conn.recv());
  conn.send("++auto 0\n");
  if (std::abs(sp - span) > 1e-3 * span) {
    return false;
  }
  return true;
}

bool hp_856x_set_mkr_freq(tcp_connection &conn, const unsigned gpib_addr, const double freq) {
  conn.send(fmt::format("++addr {:d}\n", gpib_addr));
  conn.send("++auto 0\n");
  conn.send(fmt::format("MKF {:e} HZ\n", freq));
  conn.send("++auto 1\n");
  conn.send("MKF?\n");
  auto fr = str2double(conn.recv());
  conn.send("++auto 0\n");
  if (std::abs(fr - freq) > 1e-3 * freq) {
    return false;
  }
  return true;
}

void hp_856x_set_mkr_peak(tcp_connection &conn, const unsigned gpib_addr) {
  conn.send(fmt::format("++addr {:d}\n", gpib_addr));
  conn.send("++auto 0\n");
  conn.send("MKPK HI\n");
}

double hp_856x_read_sweep_time(tcp_connection &conn, const unsigned gpib_addr) {
  conn.send(fmt::format("++addr {:d}\n", gpib_addr));
  conn.send("++auto 0\n");
  conn.send("ST?\n");
  conn.send("++auto 1\n");
  auto sweep_time = str2double(conn.recv());
  conn.send("++auto 0\n");
  return sweep_time;
}

double hp_856x_read_mkr_amp(tcp_connection &conn, const unsigned gpib_addr) {
  conn.send(fmt::format("++addr {:d}\n", gpib_addr));
  conn.send("++auto 0\n");
  conn.send("AUNITS DBM;TS;MKA?\n");
  conn.send("++auto 1\n");
  auto res = str2double(conn.recv());
  conn.send("++auto 0\n");
  return res;
}

double hp_856x_read_carron(tcp_connection &conn, const unsigned gpib_addr, double sweep_time) {
  conn.send(fmt::format("++addr {:d}\n", gpib_addr));
  conn.send("++auto 0\n");
  //conn.send("AUNITS DBM;TS;CARRON TRA,?\n");
  conn.send("TS;CARRON TRA,?\n");
  conn.send("++auto 1\n");
  std::this_thread::sleep_for(std::chrono::milliseconds(std::lround(1100 * sweep_time)));
  auto res = str2double(conn.recv());
  conn.send("++auto 0\n");
  return res;
}