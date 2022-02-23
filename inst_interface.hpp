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
    }
    ~tcp_connection() {
      sock.close();
    }
    size_t send(const std::string &str) {
      return net::write(sock, net::buffer(str, str.size()));
    }
    std::string recv(const size_t maxlen = 4096) {
      std::string s;
      s.reserve(maxlen);
      auto len = net::read(sock, net::buffer(s));
      return s;
    }
};

double str2double(const std::string s) {
  return std::atof(s.c_str());
}

bool hp_856x_set_ref_level(tcp_connection &conn, unsigned gpib_addr, const double ref_lvl = 0.0) {
  conn.send(fmt::format("++addr {:d}\n", gpib_addr));
  conn.send("++clr\n");
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

bool hp_856x_set_freq(tcp_connection &conn, unsigned gpib_addr, const double freq) {
  conn.send(fmt::format("++addr {:d}\n", gpib_addr));
  conn.send("++clr\n");
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

bool hp_856x_set_zero_span(tcp_connection &conn, unsigned gpib_addr) {
  conn.send(fmt::format("++addr {:d}\n", gpib_addr));
  conn.send("++clr\n");
  conn.send("++auto 0\n");
  conn.send("SP ZERO\n");
  conn.send("++auto 1\n");
  conn.send("SP?\n");
  auto sp = str2double(conn.recv());
  conn.send("++auto 0\n");
  if (sp != 0.0) {
    return false;
  }
  return true;
}

double hp_856x_read_sweep_time(tcp_connection &conn, unsigned gpib_addr) {
  conn.send(fmt::format("++addr {:d}\n", gpib_addr));
  conn.send("++clr\n");
  conn.send("++auto 0\n");
  conn.send("ST?\n");
  conn.send("++auto 1\n");
  auto sweep_time = str2double(conn.recv());
  conn.send("++auto 0\n");
  return sweep_time;
}

double hp_856x_read_power(tcp_connection &conn, unsigned gpib_addr, double sweep_time) {
  conn.send(fmt::format("++addr {:d}\n", gpib_addr));
  conn.send("++clr\n");
  conn.send("++auto 0\n");
  conn.send("TS?\n");
  conn.send("++auto 1\n");
  std::this_thread::sleep_for(std::chrono::milliseconds(std::lround(2000 * sweep_time)));
  conn.send("CARRON TRA,?\n");
  auto res = str2double(conn.recv());
  conn.send("++auto 0\n");
  return res;
}