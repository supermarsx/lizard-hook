#include "util/log.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <spdlog/spdlog.h>

TEST_CASE("logs rotate", "[log]") {
  using namespace std::filesystem;
  auto tempdir = temp_directory_path() / "lizard_log_rotate";
  create_directories(tempdir);
  auto prev = current_path();
  current_path(tempdir);

  remove("lizard.log");
  remove("lizard.1.log");

  lizard::util::init_logging("info", 8192, 1);

  std::string big(1024, 'x');
  for (int i = 0; i < 6000; ++i) {
    spdlog::info(big);
  }
  spdlog::default_logger()->flush();
  spdlog::shutdown();

  REQUIRE(exists("lizard.log"));
  REQUIRE(exists("lizard.1.log"));

  current_path(prev);
  remove_all(tempdir);
}
