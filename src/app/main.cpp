#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <optional>
#include <thread>
#include <iostream>

#include <cxxopts.hpp>
#include <spdlog/spdlog.h>

#include "app/config.h"
#include "audio/engine.h"
#include "hook/keyboard_hook.h"
#include "platform/tray.hpp"
#include "platform/window.hpp"
#include "util/log.h"

#ifndef LIZARD_TEST
#include "glad/glad.h"
#include "overlay/gl_raii.cpp"
#include "overlay/overlay.cpp"
#endif

int main(int argc, char **argv) {
  cxxopts::Options opts("lizard-hook", "Keyboard reactive overlay");
  opts.add_options()("config", "Config path", cxxopts::value<std::string>())(
      "log-level", "Logging level",
      cxxopts::value<std::string>())("log-queue", "Logging queue size", cxxopts::value<int>())(
      "log-workers", "Logging worker count", cxxopts::value<int>())("help", "Show help");
  auto result = opts.parse(argc, argv);
  if (result.count("help")) {
    std::cout << opts.help() << "\n";
    return 0;
  }

  std::optional<std::filesystem::path> config_path;
  if (result.count("config")) {
    config_path = result["config"].as<std::string>();
  }

  auto exe_dir = std::filesystem::canonical(argv[0]).parent_path();
  lizard::app::Config cfg(exe_dir, config_path);

  auto level =
      result.count("log-level") ? result["log-level"].as<std::string>() : cfg.logging_level();
  auto queue = result.count("log-queue") ? static_cast<std::size_t>(result["log-queue"].as<int>())
                                         : static_cast<std::size_t>(cfg.logging_queue_size());
  auto workers = result.count("log-workers")
                     ? static_cast<std::size_t>(result["log-workers"].as<int>())
                     : static_cast<std::size_t>(cfg.logging_worker_count());
  lizard::util::init_logging(level, queue, workers, cfg.logging_path());

  lizard::audio::Engine engine(static_cast<std::uint32_t>(cfg.max_concurrent_playbacks()),
                               std::chrono::milliseconds(cfg.sound_cooldown_ms()));
  engine.init(cfg.sound_path(), cfg.volume_percent(), cfg.audio_backend(),
              static_cast<std::uint32_t>(cfg.max_concurrent_playbacks()));

  lizard::overlay::Overlay overlay;
  overlay.init(cfg, cfg.emoji_atlas());
  std::jthread overlay_thread([&](std::stop_token st) { overlay.run(st); });
  std::atomic<bool> fullscreen{false};
  std::jthread fullscreen_thread([&](std::stop_token st) {
    using namespace std::chrono_literals;
    bool last = false;
    while (!st.stop_requested()) {
      bool fs = lizard::platform::fullscreen_window_present();
      fullscreen = fs;
      bool paused = cfg.fullscreen_pause() && fs;
      overlay.set_paused(paused);
      if (paused != last) {
        if (paused) {
          engine.set_volume(0.0f);
        } else {
          engine.set_volume(cfg.mute() ? 0.0f
                                      : static_cast<float>(cfg.volume_percent()) / 100.0f);
        }
        last = paused;
      }
      std::this_thread::sleep_for(500ms);
    }
  });

  std::atomic<bool> running{true};
  lizard::platform::TrayState tray_state{cfg.enabled(), cfg.mute(), cfg.fullscreen_pause(),
                                         lizard::platform::FpsMode::Auto, 60};
  lizard::platform::TrayCallbacks tray_callbacks{
      [&](bool v) {
        tray_state.enabled = v;
        lizard::platform::update_tray(tray_state);
      },
      [&](bool v) {
        tray_state.muted = v;
        lizard::platform::update_tray(tray_state);
      },
      [&](bool v) {
        tray_state.fullscreen_pause = v;
        lizard::platform::update_tray(tray_state);
      },
      [&](lizard::platform::FpsMode m) {
        tray_state.fps_mode = m;
        overlay.set_fps_mode(m);
        lizard::platform::update_tray(tray_state);
      },
      [&](int v) {
        tray_state.fps_mode = lizard::platform::FpsMode::Fixed;
        tray_state.fps_fixed = v;
        overlay.set_fps_mode(lizard::platform::FpsMode::Fixed);
        overlay.set_fps_fixed(v);
        lizard::platform::update_tray(tray_state);
      },
      []() {},
      []() {},
      [&]() { running = false; }};
  lizard::platform::init_tray(tray_state, tray_callbacks);

  auto hook = hook::KeyboardHook::create(
      [&](int /*key*/, bool pressed) {
        if (pressed && cfg.enabled()) {
          bool paused = cfg.fullscreen_pause() && fullscreen.load();
          if (!paused) {
            if (!cfg.mute()) {
              engine.play();
            }
            overlay.spawn_badge(0, 0.5f, 0.5f);
          }
        }
      },
      cfg);
  hook->start();

  std::jthread reload_thread([&](std::stop_token st) {
    std::mutex m;
    std::unique_lock lk(m);
    while (!st.stop_requested()) {
      cfg.reload_cv().wait(lk);
      if (st.stop_requested()) {
        break;
      }
      engine.shutdown();
      engine.init(cfg.sound_path(), cfg.volume_percent(), cfg.audio_backend());
      tray_state.enabled = cfg.enabled();
      tray_state.muted = cfg.mute();
      tray_state.fullscreen_pause = cfg.fullscreen_pause();
      lizard::platform::update_tray(tray_state);
    }
  });

  using namespace std::chrono_literals;
  while (running) {
    std::this_thread::sleep_for(100ms);
  }

  reload_thread.request_stop();
  fullscreen_thread.request_stop();
  overlay_thread.request_stop();
  hook->stop();
  overlay.shutdown();
  engine.shutdown();
  lizard::platform::shutdown_tray();
  return 0;
}
