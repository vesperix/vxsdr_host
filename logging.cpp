#include "logging.hpp"

void log_init(const std::string &logfilename) {
  // console logger and its settings
  auto clog = std::make_shared<spdlog::sinks::stdout_sink_mt>();

  // file logger and its settings
  auto flog = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logfilename + ".log");

  spdlog::sinks_init_list sink_list = { flog, clog };

  auto multi_logger = std::make_shared<spdlog::logger>("multi", sink_list.begin(), sink_list.end());
  multi_logger->set_level(spdlog::level::debug);
  multi_logger->set_pattern("[%Y-%m-%d %T.%f] [%t] [%=8l] %v");
  spdlog::register_logger(multi_logger);
}

void log_close() {
  spdlog::shutdown();
}

void log_set_level(const int verbose) {
  switch(verbose) {
    case 6:
      spdlog::get("multi")->set_level(spdlog::level::trace);
      break;
    case 5:
      spdlog::get("multi")->set_level(spdlog::level::debug);
      break;
    case 4:
      spdlog::get("multi")->set_level(spdlog::level::info);
      break;
    case 3:
      spdlog::get("multi")->set_level(spdlog::level::warn);
      break;
    case 2:
      spdlog::get("multi")->set_level(spdlog::level::err);
      break;
    case 1:
      spdlog::get("multi")->set_level(spdlog::level::critical);
      break;
    case 0:
      spdlog::get("multi")->set_level(spdlog::level::off);
      break;
    default:
      LOG_ERROR("unknown log level requested");
  }
}
