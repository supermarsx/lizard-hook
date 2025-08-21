#pragma once

#include <functional>
#include <memory>

namespace lizard::app {
class Config;
}

namespace hook {

// Callback invoked for each key event.
// keycode: platform-specific virtual key or scancode.
// pressed: true for key down, false for key up.
using KeyCallback = std::function<void(int keycode, bool pressed)>;

// Interface representing a platform-specific keyboard hook.
class KeyboardHook {
public:
  virtual ~KeyboardHook() = default;

  // Starts the hook. Returns true on success.
  virtual auto start() -> bool = 0;

  // Stops the hook. Safe to call multiple times.
  virtual void stop() = 0;

  // Factory to create a platform-appropriate hook implementation.
  static auto create(KeyCallback callback, const lizard::app::Config &cfg)
      -> std::unique_ptr<KeyboardHook>;
};

} // namespace hook
