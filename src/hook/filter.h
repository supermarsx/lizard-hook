#pragma once

#include <string>

namespace lizard::app {
class Config;
}

namespace hook {

// Returns true if an event should be delivered to callbacks based on config
// settings. `injected` indicates whether the event was synthetically
// generated. `process_name` is the executable name of the originating
// process.
auto should_deliver_event(const lizard::app::Config &cfg, bool injected,
                          const std::string &process_name) -> bool;

} // namespace hook
