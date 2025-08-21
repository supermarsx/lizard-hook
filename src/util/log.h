#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string_view>

namespace lizard::util {

void init_logging(std::string_view level, std::size_t queue_size, std::size_t worker_count,
                  std::optional<std::filesystem::path> file_path = std::nullopt);

} // namespace lizard::util
