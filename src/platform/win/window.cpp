#include "../window.hpp"

#ifdef _WIN32
#include "glad/glad.h"
#include <dwmapi.h>
#include <windows.h>
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
  g_hwnd = CreateWindowExW(exStyle, wc.lpszClassName, L"", WS_POPUP, 0, 0, desc.width, desc.height,
                           nullptr, nullptr, inst, nullptr);
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

} // namespace lizard::platform

#endif
