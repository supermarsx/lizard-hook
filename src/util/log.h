#pragma once

#include <cstddef>
#include <string_view>

namespace lizard::util {

void init_logging(std::string_view level, std::size_t queue_size, std::size_t worker_count);

} // namespace lizard::util
