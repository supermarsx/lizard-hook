#include "../tray.hpp"

#ifdef _WIN32
#include <shellapi.h>
#include <windows.h>

namespace lizard::platform {

namespace {
TrayState g_state;
TrayCallbacks g_callbacks;
HWND g_hwnd = nullptr;
HMENU g_menu = nullptr;
NOTIFYICONDATAW g_nid{};
constexpr UINT WM_TRAY = WM_APP + 1;

enum MenuId {
  ID_ENABLED = 1,
  ID_MUTE,
  ID_FULLSCREEN,
  ID_FPS,
  ID_CONFIG,
  ID_LOGS,
  ID_QUIT,
};

void update_menu() {
  CheckMenuItem(g_menu, ID_ENABLED,
                MF_BYCOMMAND | (g_state.enabled ? MF_CHECKED : MF_UNCHECKED));
  CheckMenuItem(g_menu, ID_MUTE,
                MF_BYCOMMAND | (g_state.muted ? MF_CHECKED : MF_UNCHECKED));
  CheckMenuItem(g_menu, ID_FULLSCREEN,
                MF_BYCOMMAND |
                    (g_state.fullscreen_pause ? MF_CHECKED : MF_UNCHECKED));
  CheckMenuItem(g_menu, ID_FPS,
                MF_BYCOMMAND | (g_state.show_fps ? MF_CHECKED : MF_UNCHECKED));
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
  case ID_FPS:
    g_state.show_fps = !g_state.show_fps;
    if (g_callbacks.toggle_fps)
      g_callbacks.toggle_fps(g_state.show_fps);
    update_menu();
    break;
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
  }
  return DefWindowProc(hwnd, msg, wParam, lParam);
}

} // namespace

bool init_tray(const TrayState &state, const TrayCallbacks &callbacks) {
  g_state = state;
  g_callbacks = callbacks;

  WNDCLASSW wc{};
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = L"LizardTray";
  RegisterClassW(&wc);
  g_hwnd = CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                         nullptr, wc.hInstance, nullptr);

  g_menu = CreatePopupMenu();
  AppendMenuW(g_menu, MF_STRING, ID_ENABLED, L"Enabled");
  AppendMenuW(g_menu, MF_STRING, ID_MUTE, L"Mute");
  AppendMenuW(g_menu, MF_STRING, ID_FULLSCREEN, L"Pause in Fullscreen");
  AppendMenuW(g_menu, MF_STRING, ID_FPS, L"Show FPS");
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
  g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    lstrcpyW(g_nid.szTip, L"Lizard Hook");
  Shell_NotifyIconW(NIM_ADD, &g_nid);
  return true;
}

void update_tray(const TrayState &state) {
  g_state = state;
  update_menu();
}

void shutdown_tray() {
  Shell_NotifyIconW(NIM_DELETE, &g_nid);
  if (g_menu)
    DestroyMenu(g_menu);
  g_menu = nullptr;
  if (g_hwnd)
    DestroyWindow(g_hwnd);
  g_hwnd = nullptr;
}

} // namespace lizard::platform

#endif
