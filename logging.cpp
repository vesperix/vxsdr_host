#include "logging.hpp"

void my_log_init() {
  // console logger and its settings
  auto clog = std::make_shared<spdlog::sinks::stdout_sink_mt>();

  // file logger and its settings
  auto flog = std::make_shared<spdlog::sinks::basic_file_sink_mt>("log.txt");

  spdlog::sinks_init_list sink_list = { flog, clog };

  auto multi_logger = std::make_shared<spdlog::logger>("multi", sink_list.begin(), sink_list.end());
  multi_logger->set_level(spdlog::level::debug);
  multi_logger->set_pattern("[%Y-%m-%d %T.%f] [%t] [%=8l] %v");
  spdlog::register_logger(multi_logger);
}
