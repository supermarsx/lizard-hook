#include "hook/keyboard_hook.h"
#include "hook/filter.h"

#include "app/config.h"

// Note: Wayland compositors generally prohibit global keyboard hooks.
// This implementation requires an X11 session.

#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/record.h>
#include <X11/Xatom.h>

#include <cerrno>
#include <future>
#include <algorithm>
#include <sys/select.h>
#include <unistd.h>
#include <thread>
#include <filesystem>
#include <string>
#include <cstdio>

#include <spdlog/spdlog.h>

namespace hook {

#ifdef LIZARD_TEST
namespace testing {
void set_xrecord_alloc_range(XRecordRange *(*)());
void set_process_name_resolver(std::string (*)(Display *));
} // namespace testing
#endif

namespace {

class LinuxKeyboardHook : public KeyboardHook {
public:
  using AllocRangeFn = XRecordRange *(*)();
  LinuxKeyboardHook(KeyCallback cb, const lizard::app::Config &cfg)
      : callback_(std::move(cb)), config_(cfg) {}
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
    display_ = dpy;
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
                std::string proc = process_name_fn_(dpy);
                if (should_deliver_event(config_, false, proc)) {
                  callback_(raw->detail, pressed);
                }
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
            std::string proc = process_name_fn_(self->display_);
            if (should_deliver_event(self->config_, false, proc)) {
              self->callback_(key, pressed);
            }
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
    display_ = nullptr;
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
  const lizard::app::Config &config_;
  Display *display_{nullptr};
  std::jthread thread_;
  bool running_{false};
  int wake_fds_[2]{-1, -1};
  static inline AllocRangeFn alloc_range_ = &XRecordAllocRange;
  using ProcessNameFn = std::string (*)(Display *);
#ifdef LIZARD_TEST
  static inline ProcessNameFn process_name_fn_ = [](Display *) { return std::string(); };
  friend void ::hook::testing::set_xrecord_alloc_range(AllocRangeFn);
  friend void ::hook::testing::set_process_name_resolver(ProcessNameFn);
#else
  static std::string default_process_name(Display *dpy) {
    std::string name;
    if (!dpy) {
      return name;
    }
    Atom active = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", True);
    Atom pid_atom = XInternAtom(dpy, "_NET_WM_PID", True);
    if (active == None || pid_atom == None) {
      return name;
    }
    Atom type;
    int format;
    unsigned long nitems, bytes;
    unsigned char *prop = nullptr;
    if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), active, 0, ~0L, False, AnyPropertyType,
                           &type, &format, &nitems, &bytes, &prop) == Success &&
        prop) {
      Window win = *(Window *)prop;
      XFree(prop);
      prop = nullptr;
      if (XGetWindowProperty(dpy, win, pid_atom, 0, 1, False, XA_CARDINAL, &type, &format, &nitems,
                             &bytes, &prop) == Success &&
          prop) {
        pid_t pid = *(pid_t *)prop;
        XFree(prop);
        char link[64];
        std::snprintf(link, sizeof(link), "/proc/%d/exe", pid);
        std::error_code ec;
        auto p = std::filesystem::read_symlink(link, ec);
        if (!ec) {
          name = p.filename().string();
        }
      }
    }
    return name;
  }
  static inline ProcessNameFn process_name_fn_ = &default_process_name;
#endif
};

} // namespace

std::unique_ptr<KeyboardHook> KeyboardHook::create(KeyCallback callback,
                                                   const lizard::app::Config &cfg) {
  return std::make_unique<LinuxKeyboardHook>(std::move(callback), cfg);
}

#ifdef LIZARD_TEST
namespace testing {
void set_xrecord_alloc_range(LinuxKeyboardHook::AllocRangeFn fn) {
  LinuxKeyboardHook::alloc_range_ = fn;
}
void set_process_name_resolver(LinuxKeyboardHook::ProcessNameFn fn) {
  LinuxKeyboardHook::process_name_fn_ = fn;
}
} // namespace testing
#endif

} // namespace hook
