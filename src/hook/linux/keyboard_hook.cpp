#include "hook/keyboard_hook.h"

// Note: Wayland compositors generally prohibit global keyboard hooks.
// This implementation requires an X11 session.

#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/record.h>

#include <cerrno>
#include <future>
#include <algorithm>
#include <sys/select.h>
#include <unistd.h>
#include <thread>

#include <spdlog/spdlog.h>

namespace hook {

#ifdef LIZARD_TEST
namespace testing {
void set_xrecord_alloc_range(XRecordRange *(*)());
}
#endif

namespace {

class LinuxKeyboardHook : public KeyboardHook {
public:
  using AllocRangeFn = XRecordRange *(*)();
  explicit LinuxKeyboardHook(KeyCallback cb) : callback_(std::move(cb)) {}
  ~LinuxKeyboardHook() override { stop(); }

  bool start() override {
    if (running_) {
      return false;
    }
    std::promise<bool> ready;
    auto fut = ready.get_future();
    thread_ = std::jthread(
        [this, p = std::move(ready)](std::stop_token st) mutable { run(st, std::move(p)); });
    bool ok = fut.get();
    running_ = ok;
    if (!ok && thread_.joinable()) {
      thread_.join();
    }
    return ok;
  }

  void stop() override {
    if (!running_) {
      return;
    }
    running_ = false;
    if (thread_.joinable()) {
      thread_.request_stop();
      if (wake_fds_[1] != -1) {
        [[maybe_unused]] ssize_t n = write(wake_fds_[1], "\0", 1);
      }
      thread_.join();
    }
  }

private:
  void run(std::stop_token st, std::promise<bool> started) {
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) {
      spdlog::error("XOpenDisplay failed: {}", errno);
      started.set_value(false);
      return;
    }
    if (pipe(wake_fds_) != 0) {
      spdlog::error("pipe failed: {}", errno);
      started.set_value(false);
      XCloseDisplay(dpy);
      return;
    }
    int xi_opcode = 0, event, error;
    if (XQueryExtension(dpy, "XInputExtension", &xi_opcode, &event, &error)) {
      XIEventMask mask;
      unsigned char m[XIMaskLen(XI_LASTEVENT)] = {0};
      mask.deviceid = XIAllMasterDevices;
      mask.mask_len = sizeof(m);
      mask.mask = m;
      XISetMask(m, XI_RawKeyPress);
      XISetMask(m, XI_RawKeyRelease);
      XISelectEvents(dpy, DefaultRootWindow(dpy), &mask, 1);
      started.set_value(true);
      XEvent ev;
      int xfd = XConnectionNumber(dpy);
      int nfds = std::max(xfd, wake_fds_[0]) + 1;
      while (!st.stop_requested()) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(xfd, &set);
        FD_SET(wake_fds_[0], &set);
        if (select(nfds, &set, nullptr, nullptr, nullptr) <= 0) {
          continue;
        }
        if (FD_ISSET(wake_fds_[0], &set)) {
          char buf[8];
          [[maybe_unused]] ssize_t n = read(wake_fds_[0], buf, sizeof(buf));
        }
        if (FD_ISSET(xfd, &set)) {
          while (XPending(dpy)) {
            XNextEvent(dpy, &ev);
            if (ev.xcookie.type == GenericEvent && ev.xcookie.extension == xi_opcode &&
                XGetEventData(dpy, &ev.xcookie)) {
              if (ev.xcookie.evtype == XI_RawKeyPress || ev.xcookie.evtype == XI_RawKeyRelease) {
                auto *raw = static_cast<XIRawEvent *>(ev.xcookie.data);
                bool pressed = ev.xcookie.evtype == XI_RawKeyPress;
                callback_(raw->detail, pressed);
              }
              XFreeEventData(dpy, &ev.xcookie);
            }
          }
        }
      }
    } else {
      // Fallback to legacy XRecord if XInput2 is unavailable.
      int major, minor;
      if (XRecordQueryVersion(dpy, &major, &minor)) {
        Display *data_dpy = XOpenDisplay(nullptr);
        if (!data_dpy) {
          spdlog::error("XOpenDisplay failed: {}", errno);
          started.set_value(false);
          XCloseDisplay(dpy);
          return;
        }
        XRecordRange *range = alloc_range_();
        if (!range) {
          spdlog::error("XRecordAllocRange failed");
          started.set_value(false);
          XCloseDisplay(data_dpy);
          XCloseDisplay(dpy);
          return;
        }
        range->device_events.first = KeyPress;
        range->device_events.last = KeyRelease;
        XRecordClientSpec clients = XRecordAllClients;
        auto handler = [](XPointer ctx, XRecordInterceptData *data) {
          auto *self = reinterpret_cast<LinuxKeyboardHook *>(ctx);
          if (data->category == XRecordFromServer) {
            const xEvent *ev = reinterpret_cast<const xEvent *>(data->data);
            bool pressed = ev->u.u.type == KeyPress;
            unsigned int key = ev->u.u.detail;
            self->callback_(key, pressed);
          }
          XRecordFreeData(data);
        };
        XRecordContext rec = XRecordCreateContext(dpy, 0, &clients, 1, &range, 1);
        if (!rec ||
            !XRecordEnableContextAsync(data_dpy, rec, handler, reinterpret_cast<XPointer>(this))) {
          spdlog::error("XRecord setup failed");
          started.set_value(false);
          if (rec) {
            XRecordFreeContext(dpy, rec);
          }
          XCloseDisplay(data_dpy);
          XFree(range);
          XCloseDisplay(dpy);
          return;
        }
        started.set_value(true);
        while (!st.stop_requested()) {
          XRecordProcessReplies(data_dpy);
        }
        XRecordDisableContext(dpy, rec);
        XRecordFreeContext(dpy, rec);
        XCloseDisplay(data_dpy);
        XFree(range);
      } else {
        spdlog::error("XInput2/XRecord unavailable");
        started.set_value(false);
      }
    }
    XCloseDisplay(dpy);
    if (wake_fds_[0] != -1) {
      close(wake_fds_[0]);
      wake_fds_[0] = -1;
    }
    if (wake_fds_[1] != -1) {
      close(wake_fds_[1]);
      wake_fds_[1] = -1;
    }
  }

  KeyCallback callback_;
  std::jthread thread_;
  bool running_{false};
  int wake_fds_[2]{-1, -1};
  static inline AllocRangeFn alloc_range_ = &XRecordAllocRange;
#ifdef LIZARD_TEST
  friend void ::hook::testing::set_xrecord_alloc_range(AllocRangeFn);
#endif
};

} // namespace

std::unique_ptr<KeyboardHook> KeyboardHook::create(KeyCallback callback) {
  return std::make_unique<LinuxKeyboardHook>(std::move(callback));
}

#ifdef LIZARD_TEST
namespace testing {
void set_xrecord_alloc_range(LinuxKeyboardHook::AllocRangeFn fn) {
  LinuxKeyboardHook::alloc_range_ = fn;
}
} // namespace testing
#endif

} // namespace hook
