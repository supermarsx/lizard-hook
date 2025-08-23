#include "../tray.hpp"

#ifdef _WIN32
#include "resource.h"

#include <shellapi.h>
#include <spdlog/spdlog.h>
#include <windows.h>

#include <future>
#include <mutex>
#include <thread>

namespace lizard::platform {

namespace {
TrayState g_state;
TrayCallbacks g_callbacks;
std::mutex g_mutex;
HWND g_hwnd = nullptr;
HMENU g_menu = nullptr;
HMENU g_fps_menu = nullptr;
HMENU g_fps_fixed_menu = nullptr;
NOTIFYICONDATAW g_nid{};
constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_UPDATE = WM_APP + 2;
std::jthread g_thread;

enum MenuId {
  ID_ENABLED = 1,
  ID_MUTE,
  ID_FULLSCREEN,
  ID_FPS_AUTO,
  ID_FPS_FIXED_60,
  ID_FPS_FIXED_75,
  ID_FPS_FIXED_120,
  ID_FPS_FIXED_144,
  ID_FPS_FIXED_165,
  ID_FPS_FIXED_240,
  ID_CONFIG,
  ID_LOGS,
  ID_QUIT,
};

void update_menu() {
  CheckMenuItem(g_menu, ID_ENABLED, MF_BYCOMMAND | (g_state.enabled ? MF_CHECKED : MF_UNCHECKED));
  CheckMenuItem(g_menu, ID_MUTE, MF_BYCOMMAND | (g_state.muted ? MF_CHECKED : MF_UNCHECKED));
  CheckMenuItem(g_menu, ID_FULLSCREEN,
                MF_BYCOMMAND | (g_state.fullscreen_pause ? MF_CHECKED : MF_UNCHECKED));
  CheckMenuItem(g_fps_menu, ID_FPS_AUTO,
                MF_BYCOMMAND | (g_state.fps_mode == FpsMode::Auto ? MF_CHECKED : MF_UNCHECKED));
  UINT fixed_id = 0;
  switch (g_state.fps_fixed) {
  case 60:
    fixed_id = ID_FPS_FIXED_60;
    break;
  case 75:
    fixed_id = ID_FPS_FIXED_75;
    break;
  case 120:
    fixed_id = ID_FPS_FIXED_120;
    break;
  case 144:
    fixed_id = ID_FPS_FIXED_144;
    break;
  case 165:
    fixed_id = ID_FPS_FIXED_165;
    break;
  case 240:
    fixed_id = ID_FPS_FIXED_240;
    break;
  }
  CheckMenuRadioItem(g_fps_fixed_menu, ID_FPS_FIXED_60, ID_FPS_FIXED_240, fixed_id);
}

void on_command(UINT id) {
  switch (id) {
  case ID_ENABLED:
    g_state.enabled = !g_state.enabled;
    if (g_callbacks.toggle_enabled)
      g_callbacks.toggle_enabled(g_state.enabled);
    update_menu();
    break;
  case ID_MUTE:
    g_state.muted = !g_state.muted;
    if (g_callbacks.toggle_mute)
      g_callbacks.toggle_mute(g_state.muted);
    update_menu();
    break;
  case ID_FULLSCREEN:
    g_state.fullscreen_pause = !g_state.fullscreen_pause;
    if (g_callbacks.toggle_fullscreen_pause)
      g_callbacks.toggle_fullscreen_pause(g_state.fullscreen_pause);
    update_menu();
    break;
  case ID_FPS_AUTO:
    g_state.fps_mode = FpsMode::Auto;
    if (g_callbacks.set_fps_mode)
      g_callbacks.set_fps_mode(FpsMode::Auto);
    update_menu();
    break;
  case ID_FPS_FIXED_60:
  case ID_FPS_FIXED_75:
  case ID_FPS_FIXED_120:
  case ID_FPS_FIXED_144:
  case ID_FPS_FIXED_165:
  case ID_FPS_FIXED_240: {
    g_state.fps_mode = FpsMode::Fixed;
    int val = 60;
    if (id == ID_FPS_FIXED_75)
      val = 75;
    else if (id == ID_FPS_FIXED_120)
      val = 120;
    else if (id == ID_FPS_FIXED_144)
      val = 144;
    else if (id == ID_FPS_FIXED_165)
      val = 165;
    else if (id == ID_FPS_FIXED_240)
      val = 240;
    g_state.fps_fixed = val;
    if (g_callbacks.set_fps_mode)
      g_callbacks.set_fps_mode(FpsMode::Fixed);
    if (g_callbacks.set_fps_fixed)
      g_callbacks.set_fps_fixed(val);
    update_menu();
    break;
  }
  case ID_CONFIG:
    if (g_callbacks.open_config)
      g_callbacks.open_config();
    break;
  case ID_LOGS:
    if (g_callbacks.open_logs)
      g_callbacks.open_logs();
    break;
  case ID_QUIT:
    if (g_callbacks.quit)
      g_callbacks.quit();
    break;
  }
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_COMMAND) {
    on_command(LOWORD(wParam));
  } else if (msg == WM_TRAY) {
    if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
      POINT pt;
      GetCursorPos(&pt);
      SetForegroundWindow(hwnd);
      TrackPopupMenu(g_menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    }
  } else if (msg == WM_UPDATE) {
    update_menu();
    return 0;
  } else if (msg == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool init_thread() {
  WNDCLASSW wc{};
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = L"LizardTray";
  if (!RegisterClassW(&wc)) {
    spdlog::error("RegisterClassW failed: {}", GetLastError());
    return false;
  }
  g_hwnd = CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance,
                         nullptr);
  if (!g_hwnd) {
    spdlog::error("CreateWindowW failed: {}", GetLastError());
    return false;
  }

  g_menu = CreatePopupMenu();
  AppendMenuW(g_menu, MF_STRING, ID_ENABLED, L"Enabled");
  AppendMenuW(g_menu, MF_STRING, ID_MUTE, L"Mute");
  AppendMenuW(g_menu, MF_STRING, ID_FULLSCREEN, L"Pause in Fullscreen");

  g_fps_menu = CreatePopupMenu();
  g_fps_fixed_menu = CreatePopupMenu();
  AppendMenuW(g_fps_menu, MF_STRING, ID_FPS_AUTO, L"Auto");
  AppendMenuW(g_fps_fixed_menu, MF_STRING, ID_FPS_FIXED_60, L"60");
  AppendMenuW(g_fps_fixed_menu, MF_STRING, ID_FPS_FIXED_75, L"75");
  AppendMenuW(g_fps_fixed_menu, MF_STRING, ID_FPS_FIXED_120, L"120");
  AppendMenuW(g_fps_fixed_menu, MF_STRING, ID_FPS_FIXED_144, L"144");
  AppendMenuW(g_fps_fixed_menu, MF_STRING, ID_FPS_FIXED_165, L"165");
  AppendMenuW(g_fps_fixed_menu, MF_STRING, ID_FPS_FIXED_240, L"240");
  AppendMenuW(g_fps_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(g_fps_fixed_menu), L"Fixed");
  AppendMenuW(g_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(g_fps_menu), L"FPS");

  AppendMenuW(g_menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(g_menu, MF_STRING, ID_CONFIG, L"Open Config");
  AppendMenuW(g_menu, MF_STRING, ID_LOGS, L"Open Logs");
  AppendMenuW(g_menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(g_menu, MF_STRING, ID_QUIT, L"Quit");
  update_menu();

  g_nid.cbSize = sizeof(g_nid);
  g_nid.hWnd = g_hwnd;
  g_nid.uID = 1;
  g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  g_nid.uCallbackMessage = WM_TRAY;
  g_nid.hIcon = reinterpret_cast<HICON>(LoadImageW(wc.hInstance, MAKEINTRESOURCEW(IDI_LIZARD_TRAY),
                                                   IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
  if (!g_nid.hIcon)
    g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
  lstrcpyW(g_nid.szTip, L"Lizard Hook");
  if (!Shell_NotifyIconW(NIM_ADD, &g_nid)) {
    spdlog::error("Shell_NotifyIconW failed: {}", GetLastError());
    DestroyMenu(g_menu);
    g_menu = nullptr;
    DestroyWindow(g_hwnd);
    g_hwnd = nullptr;
    return false;
  }
  return true;
}

void shutdown_thread() {
  Shell_NotifyIconW(NIM_DELETE, &g_nid);
  if (g_nid.hIcon) {
    DestroyIcon(g_nid.hIcon);
    g_nid.hIcon = nullptr;
  }
  if (g_fps_fixed_menu)
    DestroyMenu(g_fps_fixed_menu);
  g_fps_fixed_menu = nullptr;
  if (g_fps_menu)
    DestroyMenu(g_fps_menu);
  g_fps_menu = nullptr;
  if (g_menu)
    DestroyMenu(g_menu);
  g_menu = nullptr;
  if (g_hwnd)
    DestroyWindow(g_hwnd);
  g_hwnd = nullptr;
}

void tray_thread(std::stop_token st, std::promise<bool> ready) {
  bool ok = init_thread();
  ready.set_value(ok);
  if (!ok)
    return;
  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  shutdown_thread();
}

} // namespace

bool init_tray(const TrayState &state, const TrayCallbacks &callbacks) {
  g_state = state;
  g_callbacks = callbacks;
  std::promise<bool> ready;
  auto fut = ready.get_future();
  g_thread = std::jthread(tray_thread, std::move(ready));
  return fut.get();
}

void update_tray(const TrayState &state) {
  {
    std::scoped_lock lock(g_mutex);
    g_state = state;
  }
  if (g_hwnd)
    PostMessage(g_hwnd, WM_UPDATE, 0, 0);
}

void shutdown_tray() {
  if (g_hwnd)
    PostMessage(g_hwnd, WM_CLOSE, 0, 0);
  if (g_thread.joinable())
    g_thread.join();
}

} // namespace lizard::platform

#endif
