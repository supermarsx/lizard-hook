#include "../window.hpp"

#ifdef _WIN32
#include "glad/glad.h"
#include <dwmapi.h>
#include <windows.h>
#include <algorithm>
#include <vector>
#pragma comment(lib, "dwmapi.lib")

namespace lizard::platform {

namespace {
HWND g_hwnd = nullptr;

float compute_dpi(HWND hwnd) {
  UINT dpi = GetDpiForWindow(hwnd);
  return dpi / 96.0f;
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  return DefWindowProc(hwnd, msg, wp, lp);
}
} // namespace

Window create_overlay_window(const WindowDesc &desc) {
  Window result{};
  HINSTANCE inst = GetModuleHandle(nullptr);
  WNDCLASSW wc{};
  wc.hInstance = inst;
  wc.lpfnWndProc = wnd_proc;
  wc.lpszClassName = L"LizardOverlay";
  RegisterClassW(&wc);

  DWORD exStyle =
      WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
  int x = desc.x;
  int y = desc.y;
  int width = static_cast<int>(desc.width);
  int height = static_cast<int>(desc.height);
  if (width == 0 || height == 0) {
    x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  }
  g_hwnd = CreateWindowExW(exStyle, wc.lpszClassName, L"", WS_POPUP, x, y, width, height, nullptr,
                           nullptr, inst, nullptr);
  if (!g_hwnd) {
    return result;
  }
  MARGINS margins{-1};
  DwmExtendFrameIntoClientArea(g_hwnd, &margins);
  ShowWindow(g_hwnd, SW_SHOW);

  PIXELFORMATDESCRIPTOR pfd{};
  pfd.nSize = sizeof(pfd);
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.cColorBits = 32;
  HDC dc = GetDC(g_hwnd);
  int pf = ChoosePixelFormat(dc, &pfd);
  SetPixelFormat(dc, pf, &pfd);
  HGLRC rc = wglCreateContext(dc);
  wglMakeCurrent(dc, rc);
  gladLoadGL();

  result.native = g_hwnd;
  result.dpiScale = compute_dpi(g_hwnd);
  SetWindowPos(g_hwnd, nullptr, x, y, static_cast<int>(width * result.dpiScale),
               static_cast<int>(height * result.dpiScale), SWP_NOZORDER | SWP_NOACTIVATE);
  result.glContext = rc;
  result.device = dc;
  return result;
}

void destroy_window(Window &window) {
  if (window.native) {
    wglMakeCurrent(nullptr, nullptr);
    if (window.glContext) {
      wglDeleteContext(window.glContext);
      window.glContext = nullptr;
    }
    if (window.device) {
      ReleaseDC((HWND)window.native, window.device);
      window.device = nullptr;
    }
    DestroyWindow((HWND)window.native);
  }
  window.native = nullptr;
}

void poll_events(Window &window) {
  MSG msg;
  while (PeekMessage(&msg, (HWND)window.native, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}

std::pair<float, float> cursor_pos() {
  POINT p;
  if (!GetCursorPos(&p)) {
    return {0.5f, 0.5f};
  }
  int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
  int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
  int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  float x = 0.0f;
  float y = 0.0f;
  if (vw > 0 && vh > 0) {
    x = static_cast<float>(p.x - vx) / static_cast<float>(vw);
    y = static_cast<float>(p.y - vy) / static_cast<float>(vh);
    y = 1.0f - y;
  }
  x = std::clamp(x, 0.0f, 1.0f);
  y = std::clamp(y, 0.0f, 1.0f);
  return {x, y};
}

bool fullscreen_window_present() {
  struct EnumData {
    bool full = false;
    std::vector<HMONITOR> seen;
  } data;

  EnumWindows(
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto *d = reinterpret_cast<EnumData *>(lparam);
        if (d->full) {
          return FALSE;
        }
        if (hwnd == g_hwnd || !IsWindowVisible(hwnd)) {
          return TRUE;
        }
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
        if (!mon) {
          return TRUE;
        }
        if (std::find(d->seen.begin(), d->seen.end(), mon) != d->seen.end()) {
          return TRUE;
        }
        d->seen.push_back(mon);
        RECT rect{};
        if (!GetWindowRect(hwnd, &rect)) {
          return TRUE;
        }
        MONITORINFO mi{sizeof(mi)};
        if (!GetMonitorInfo(mon, &mi)) {
          return TRUE;
        }
        if (rect.left <= mi.rcMonitor.left && rect.top <= mi.rcMonitor.top &&
            rect.right >= mi.rcMonitor.right && rect.bottom >= mi.rcMonitor.bottom) {
          d->full = true;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&data));

  return data.full;
}

} // namespace lizard::platform

#endif
