#include "../window.hpp"

#include "glad/glad.h"
#include <GL/glx.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>

namespace lizard::platform {

namespace {

Display *g_display = nullptr;
::Window g_root = 0;

float compute_dpi(Display *dpy, ::Window win) {
  int screen = DefaultScreen(dpy);
  int width_px = DisplayWidth(dpy, screen);
  int width_mm = DisplayWidthMM(dpy, screen);
  float dpi = static_cast<float>(width_px) / static_cast<float>(width_mm) * 25.4f;
  return dpi / 96.0f;
}

} // namespace

Window create_overlay_window(const WindowDesc &desc) {
  Window result{};
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
  result.dpiScale = compute_dpi(g_display, win);
  result.glContext = ctx;
  return result;
}

void destroy_window(Window &window) {
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
  if (!g_display || !window.native) {
    return;
  }
  while (XPending(g_display)) {
    XEvent ev;
    XNextEvent(g_display, &ev);
  }
}

} // namespace lizard::platform
