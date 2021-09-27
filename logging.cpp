#include "logging.hpp"

void my_log_init() {
  spdlog::set_level(spdlog::level::debug);
  spdlog::set_pattern("[%Y-%m-%d %T.%f] [%t] [%=8l] %v");
}