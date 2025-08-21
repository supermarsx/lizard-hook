#include "hook/filter.h"

#include "app/config.h"

#include <algorithm>
#include <cctype>

namespace hook {

namespace {
std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}
} // namespace

auto should_deliver_event(const lizard::app::Config &cfg, bool injected,
                          const std::string &process_name) -> bool {
  if (cfg.ignore_injected() && injected) {
    return false;
  }
  auto excludes = cfg.exclude_processes();
  std::string proc_lower = to_lower(process_name);
  for (const auto &name : excludes) {
    if (to_lower(name) == proc_lower) {
      return false;
    }
  }
  return true;
}

} // namespace hook
