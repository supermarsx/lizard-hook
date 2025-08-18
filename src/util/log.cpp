#include "log.h"

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

namespace lizard::util {

void init_logging(std::string_view level, std::size_t queue_size, std::size_t worker_count) {
  auto logger = spdlog::get("lizard");
  if (!logger) {
    spdlog::init_thread_pool(queue_size, worker_count);
    logger = spdlog::rotating_logger_mt<spdlog::async_factory>("lizard", "lizard.log",
                                                               1024 * 1024 * 5, 3);
  }
  spdlog::set_default_logger(logger);
  auto lvl = spdlog::level::from_str(std::string(level));
  if (lvl == spdlog::level::off && level != "off") {
    lvl = spdlog::level::info;
  }
  spdlog::set_level(lvl);
  spdlog::flush_on(spdlog::level::err);
}

} // namespace lizard::util
