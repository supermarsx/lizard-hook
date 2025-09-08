#include "glad/glad.h"
#include "../window.hpp"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xrandr.h>
#include <vector>
#include <mutex>

namespace lizard::platform {

namespace {

Display *g_display = nullptr;
::Window g_root = 0;
::Window g_overlay = 0;
std::mutex g_display_mutex;
std::once_flag g_xlib_init_once;

float compute_dpi(Display *dpy) {
  int screen = DefaultScreen(dpy);
  int width_px = DisplayWidth(dpy, screen);
  int width_mm = DisplayWidthMM(dpy, screen);
  float dpi = static_cast<float>(width_px) / static_cast<float>(width_mm) * 25.4f;
  return dpi / 96.0f;
}

} // namespace

Window create_overlay_window(const WindowDesc &desc) {
  std::call_once(g_xlib_init_once, []() { XInitThreads(); });
  Window result{};
  std::lock_guard<std::mutex> lock(g_display_mutex);
  g_display = XOpenDisplay(nullptr);
  if (!g_display) {
    return result;
  }
  int screen = DefaultScreen(g_display);
  g_root = RootWindow(g_display, screen);

  XSetWindowAttributes attrs{};
  attrs.override_redirect = True;
  attrs.event_mask = StructureNotifyMask;
  attrs.background_pixel = 0;

  ::Window win = XCreateWindow(g_display, g_root, desc.x, desc.y, desc.width, desc.height, 0,
                               CopyFromParent, InputOutput, CopyFromParent,
                               CWOverrideRedirect | CWEventMask | CWBackPixel, &attrs);
  g_overlay = win;

  XMapRaised(g_display, win);

  // Click-through using shape extension
  XserverRegion region = XFixesCreateRegion(g_display, nullptr, 0);
  XFixesSetWindowShapeRegion(g_display, win, ShapeInput, 0, 0, region);
  XFixesDestroyRegion(g_display, region);

  GLXFBConfig fb = nullptr;
  int nfb = 0;
  int attrsList[] = {GLX_RENDER_TYPE,
                     GLX_RGBA_BIT,
                     GLX_DRAWABLE_TYPE,
                     GLX_WINDOW_BIT,
                     GLX_DOUBLEBUFFER,
                     True,
                     GLX_RED_SIZE,
                     8,
                     GLX_GREEN_SIZE,
                     8,
                     GLX_BLUE_SIZE,
                     8,
                     GLX_ALPHA_SIZE,
                     8,
                     None};
  GLXFBConfig *configs = glXChooseFBConfig(g_display, screen, attrsList, &nfb);
  if (configs && nfb > 0) {
    fb = configs[0];
    XFree(configs);
  }
  using CreateContext = GLXContext (*)(Display *, GLXFBConfig, GLXContext, Bool, const int *);
  auto createContext =
      (CreateContext)glXGetProcAddress((const GLubyte *)"glXCreateContextAttribsARB");
  int ctxAttr[] = {GLX_CONTEXT_MAJOR_VERSION_ARB,
                   3,
                   GLX_CONTEXT_MINOR_VERSION_ARB,
                   3,
                   GLX_CONTEXT_PROFILE_MASK_ARB,
                   GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                   None};
  GLXContext ctx = nullptr;
  if (createContext) {
    ctx = createContext(g_display, fb, nullptr, True, ctxAttr);
  }
  glXMakeCurrent(g_display, win, ctx);
  gladLoadGL();

  result.native = (void *)win;
  result.dpiScale = compute_dpi(g_display);
  result.glContext = ctx;
  return result;
}

void destroy_window(Window &window) {
  std::lock_guard<std::mutex> lock(g_display_mutex);
  if (g_display && window.native) {
    glXMakeCurrent(g_display, None, nullptr);
    if (window.glContext) {
      glXDestroyContext(g_display, window.glContext);
      window.glContext = nullptr;
    }
    XDestroyWindow(g_display, (::Window)window.native);
    XCloseDisplay(g_display);
    g_display = nullptr;
  }
  window.native = nullptr;
}

void poll_events(Window &window) {
  std::lock_guard<std::mutex> lock(g_display_mutex);
  if (!g_display || !window.native) {
    return;
  }
  while (XPending(g_display)) {
    XEvent ev;
    XNextEvent(g_display, &ev);
  }
}

std::pair<float, float> cursor_pos() {
  std::lock_guard<std::mutex> lock(g_display_mutex);
  if (!g_display) {
    return {0.5f, 0.5f};
  }
  ::Window root_return, child;
  int root_x = 0, root_y = 0;
  int win_x = 0, win_y = 0;
  unsigned int mask = 0;
  if (!XQueryPointer(g_display, g_root, &root_return, &child, &root_x, &root_y, &win_x, &win_y,
                     &mask)) {
    return {0.5f, 0.5f};
  }
  int screen = DefaultScreen(g_display);
  float w = static_cast<float>(DisplayWidth(g_display, screen));
  float h = static_cast<float>(DisplayHeight(g_display, screen));
  float x = w > 0.0f ? static_cast<float>(root_x) / w : 0.0f;
  float y = h > 0.0f ? static_cast<float>(root_y) / h : 0.0f;
  return {x, y};
}

bool fullscreen_window_present() {
  std::lock_guard<std::mutex> lock(g_display_mutex);
  if (!g_display) {
    return false;
  }
  Atom listAtom = XInternAtom(g_display, "_NET_CLIENT_LIST_STACKING", False);
  Atom type;
  int format;
  unsigned long nitems, bytes;
  unsigned char *data = nullptr;
  if (XGetWindowProperty(g_display, g_root, listAtom, 0, ~0L, False, XA_WINDOW, &type, &format,
                         &nitems, &bytes, &data) != Success ||
      !data) {
    if (data)
      XFree(data);
    return false;
  }
  std::vector<::Window> stack(reinterpret_cast<::Window *>(data),
                              reinterpret_cast<::Window *>(data) + nitems);
  XFree(data);

  int nmon = 0;
  XRRMonitorInfo *mons = XRRGetMonitors(g_display, g_root, True, &nmon);
  struct Mon {
    int x, y, w, h;
  };
  std::vector<Mon> monitors;
  std::vector<bool> seen;
  if (mons && nmon > 0) {
    for (int i = 0; i < nmon; ++i) {
      monitors.push_back({mons[i].x, mons[i].y, mons[i].width, mons[i].height});
    }
    seen.resize(monitors.size(), false);
  } else {
    int screen = DefaultScreen(g_display);
    monitors.push_back({0, 0, DisplayWidth(g_display, screen), DisplayHeight(g_display, screen)});
    seen.resize(1, false);
  }
  if (mons)
    XRRFreeMonitors(mons);

  bool full = false;
  for (auto it = stack.rbegin(); it != stack.rend() && !full; ++it) {
    ::Window win = *it;
    if (win == g_overlay) {
      continue;
    }
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(g_display, win, &attrs) || attrs.map_state != IsViewable) {
      continue;
    }
    int wx = 0, wy = 0;
    ::Window child;
    XTranslateCoordinates(g_display, win, g_root, 0, 0, &wx, &wy, &child);
    for (std::size_t i = 0; i < monitors.size(); ++i) {
      if (seen[i]) {
        continue;
      }
      auto &m = monitors[i];
      if (wx <= m.x && wy <= m.y && wx + attrs.width >= m.x + m.w &&
          wy + attrs.height >= m.y + m.h) {
        full = true;
        break;
      }
      if (wx < m.x + m.w && wx + attrs.width > m.x && wy < m.y + m.h && wy + attrs.height > m.y) {
        seen[i] = true;
      }
    }
  }
  return full;
}

} // namespace lizard::platform
