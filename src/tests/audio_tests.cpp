#include <catch2/catch_test_macros.hpp>

#include "audio/engine.h"
#include "miniaudio.h"
#include <chrono>
#include <filesystem>

using namespace std::chrono_literals;

namespace lizard::assets {
extern const unsigned char lizard_processed_clean_no_meta_flac[] = {0};
extern const unsigned int lizard_processed_clean_no_meta_flac_len = 0;
} // namespace lizard::assets

TEST_CASE("engine reinitializes on device change", "[audio]") {
  lizard::audio::Engine engine(1, 0ms);
  auto path = std::filesystem::path(TEST_FLAC_PATH);
  REQUIRE(std::filesystem::exists(path));
  REQUIRE(engine.init(path));

  auto *maEng = reinterpret_cast<ma_engine *>(&engine);
  ma_device *device = ma_engine_get_device(maEng);
  REQUIRE(device != nullptr);

  engine.play();

  ma_device_notification notif{};
  notif.pDevice = device;
  notif.type = ma_device_notification_type_rerouted;
  device->onNotification(&notif);

  REQUIRE_NOTHROW(engine.play());
}
