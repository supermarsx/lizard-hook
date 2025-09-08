#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <cstdlib>
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

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

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
  std::atomic<bool> enabled{cfg.enabled()};
  std::atomic<bool> muted{cfg.mute()};
  std::atomic<bool> fullscreen_pause{cfg.fullscreen_pause()};
  lizard::platform::TrayState tray_state{enabled.load(), muted.load(), fullscreen_pause.load(),
                                         lizard::platform::FpsMode::Auto, 60};

#ifdef _WIN32
  constexpr int KEY_CTRL_L = 0xA2;
  constexpr int KEY_CTRL_R = 0xA3;
  constexpr int KEY_SHIFT_L = 0xA0;
  constexpr int KEY_SHIFT_R = 0xA1;
  constexpr int KEY_F9 = 0x78;
  constexpr int KEY_F10 = 0x79;
  constexpr int KEY_F11 = 0x7A;
#elif defined(__APPLE__)
  constexpr int KEY_CTRL_L = 59;
  constexpr int KEY_CTRL_R = 62;
  constexpr int KEY_SHIFT_L = 56;
  constexpr int KEY_SHIFT_R = 60;
  constexpr int KEY_F9 = 101;
  constexpr int KEY_F10 = 109;
  constexpr int KEY_F11 = 103;
#else
  constexpr int KEY_CTRL_L = 37;
  constexpr int KEY_CTRL_R = 105;
  constexpr int KEY_SHIFT_L = 50;
  constexpr int KEY_SHIFT_R = 62;
  constexpr int KEY_F9 = 75;
  constexpr int KEY_F10 = 76;
  constexpr int KEY_F11 = 95;
#endif

  auto update_state = [&] {
    bool fs = fullscreen.load();
    bool paused = (!enabled.load()) || (fullscreen_pause.load() && fs);
    overlay.set_paused(paused);
    if (paused || muted.load()) {
      engine.set_volume(0.0f);
    } else {
      engine.set_volume(static_cast<float>(cfg.volume_percent()) / 100.0f);
    }
  };

  update_state();
  std::jthread fullscreen_thread([&](std::stop_token st) {
    using namespace std::chrono_literals;
    while (!st.stop_requested()) {
      fullscreen = lizard::platform::fullscreen_window_present();
      update_state();
      std::this_thread::sleep_for(500ms);
    }
  });

  std::atomic<bool> running{true};
  lizard::platform::TrayCallbacks tray_callbacks{
      [&](bool v) {
        enabled = v;
        tray_state.enabled = v;
        update_state();
        lizard::platform::update_tray(tray_state);
      },
      [&](bool v) {
        muted = v;
        tray_state.muted = v;
        update_state();
        lizard::platform::update_tray(tray_state);
      },
      [&](bool v) {
        fullscreen_pause = v;
        tray_state.fullscreen_pause = v;
        update_state();
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
      [&]() {
        auto path = cfg.logging_path().parent_path() / "lizard.json";
        if (!std::filesystem::exists(path)) {
          path = cfg.logging_path().parent_path() / "lizard.json";
        }
#ifdef _WIN32
        std::wstring w = path.wstring();
        ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
#ifdef __APPLE__
        std::string cmd = "open \"" + path.string() + "\"";
#else
        std::string cmd = "xdg-open \"" + path.string() + "\"";
#endif
        int rc = std::system(cmd.c_str());
        (void)rc;
#endif
      },
      [&]() {
        auto path = cfg.logging_path();
#ifdef _WIN32
        std::wstring w = path.wstring();
        ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
#ifdef __APPLE__
        std::string cmd = "open \"" + path.string() + "\"";
#else
        std::string cmd = "xdg-open \"" + path.string() + "\"";
#endif
        int rc2 = std::system(cmd.c_str());
        (void)rc2;
#endif
      },
      [&]() { running = false; }};
  lizard::platform::init_tray(tray_state, tray_callbacks);

  bool ctrl_down = false;
  bool shift_down = false;
  bool f9_down = false;
  bool f10_down = false;
  bool f11_down = false;
  auto hook = hook::KeyboardHook::create(
      [&](int key, bool pressed) {
        if (key == KEY_CTRL_L || key == KEY_CTRL_R) {
          ctrl_down = pressed;
        } else if (key == KEY_SHIFT_L || key == KEY_SHIFT_R) {
          shift_down = pressed;
        } else if (key == KEY_F9) {
          if (!pressed) {
            f9_down = false;
          } else if (ctrl_down && shift_down) {
            if (!f9_down) {
              f9_down = true;
              enabled = !enabled.load();
              tray_state.enabled = enabled.load();
              update_state();
              lizard::platform::update_tray(tray_state);
            }
            f9_down = true;
            return;
          } else {
            f9_down = true;
          }
        } else if (key == KEY_F10) {
          if (!pressed) {
            f10_down = false;
          } else if (ctrl_down && shift_down) {
            if (!f10_down) {
              f10_down = true;
              muted = !muted.load();
              tray_state.muted = muted.load();
              update_state();
              lizard::platform::update_tray(tray_state);
            }
            f10_down = true;
            return;
          } else {
            f10_down = true;
          }
        } else if (key == KEY_F11) {
          if (!pressed) {
            f11_down = false;
          } else if (ctrl_down && shift_down) {
            if (!f11_down) {
              f11_down = true;
              cfg.reload();
              cfg.reload_cv().notify_all();
            }
            f11_down = true;
            return;
          } else {
            f11_down = true;
          }
        }

        if (pressed && enabled.load()) {
          bool paused = fullscreen_pause.load() && fullscreen.load();
          if (!paused) {
            if (!muted.load()) {
              engine.play();
            }
            float bx = 0.0f;
            float by = 0.0f;
            if (cfg.badge_spawn_strategy() == "cursor_follow") {
              auto [cx, cy] = lizard::platform::cursor_pos();
              bx = cx;
              by = cy;
            }
            overlay.spawn_badge(bx, by);
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
      enabled = tray_state.enabled;
      muted = tray_state.muted;
      fullscreen_pause = tray_state.fullscreen_pause;
      lizard::platform::update_tray(tray_state);
      update_state();
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
