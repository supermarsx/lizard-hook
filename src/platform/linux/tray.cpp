#include "../tray.hpp"

#ifndef __APPLE__
#ifndef _WIN32

#if __has_include(<libayatana-appindicator/app-indicator.h>)
#define LIZARD_HAVE_APPINDICATOR 1
#include <libayatana-appindicator/app-indicator.h>
#elif __has_include(<libappindicator/app-indicator.h>)
#define LIZARD_HAVE_APPINDICATOR 1
#include <libappindicator/app-indicator.h>
#endif

#include <gtk/gtk.h>

namespace lizard::platform {

namespace {
TrayState g_state;
TrayCallbacks g_callbacks;
GtkWidget *g_menu = nullptr;
GtkWidget *g_item_enabled = nullptr;
GtkWidget *g_item_mute = nullptr;
GtkWidget *g_item_fullscreen = nullptr;
GtkWidget *g_item_fps = nullptr;
GtkWidget *g_item_config = nullptr;
GtkWidget *g_item_logs = nullptr;
GtkWidget *g_item_quit = nullptr;
#ifdef LIZARD_HAVE_APPINDICATOR
AppIndicator *g_indicator = nullptr;
#else
GtkStatusIcon *g_status_icon = nullptr;
#endif

void update_menu() {
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(g_item_enabled),
                                 g_state.enabled);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(g_item_mute),
                                 g_state.muted);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(g_item_fullscreen),
                                 g_state.fullscreen_pause);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(g_item_fps),
                                 g_state.show_fps);
}

void on_enabled(GtkWidget *, gpointer) {
  g_state.enabled = !g_state.enabled;
  if (g_callbacks.toggle_enabled)
    g_callbacks.toggle_enabled(g_state.enabled);
  update_menu();
}
void on_mute(GtkWidget *, gpointer) {
  g_state.muted = !g_state.muted;
  if (g_callbacks.toggle_mute)
    g_callbacks.toggle_mute(g_state.muted);
  update_menu();
}
void on_fullscreen(GtkWidget *, gpointer) {
  g_state.fullscreen_pause = !g_state.fullscreen_pause;
  if (g_callbacks.toggle_fullscreen_pause)
    g_callbacks.toggle_fullscreen_pause(g_state.fullscreen_pause);
  update_menu();
}
void on_fps(GtkWidget *, gpointer) {
  g_state.show_fps = !g_state.show_fps;
  if (g_callbacks.toggle_fps)
    g_callbacks.toggle_fps(g_state.show_fps);
  update_menu();
}
void on_config(GtkWidget *, gpointer) {
  if (g_callbacks.open_config)
    g_callbacks.open_config();
}
void on_logs(GtkWidget *, gpointer) {
  if (g_callbacks.open_logs)
    g_callbacks.open_logs();
}
void on_quit(GtkWidget *, gpointer) {
  if (g_callbacks.quit)
    g_callbacks.quit();
}
#ifndef LIZARD_HAVE_APPINDICATOR
void on_popup(GtkStatusIcon *, guint button, guint activate_time, gpointer) {
  gtk_menu_popup_at_pointer(GTK_MENU(g_menu), nullptr);
}
#endif

} // namespace

bool init_tray(const TrayState &state, const TrayCallbacks &callbacks) {
  g_state = state;
  g_callbacks = callbacks;
  int argc = 0;
  char **argv = nullptr;
  gtk_init(&argc, &argv);

  g_menu = gtk_menu_new();
  g_item_enabled = gtk_check_menu_item_new_with_label("Enabled");
  g_signal_connect(g_item_enabled, "activate", G_CALLBACK(on_enabled), nullptr);
  gtk_menu_shell_append(GTK_MENU_SHELL(g_menu), g_item_enabled);
  g_item_mute = gtk_check_menu_item_new_with_label("Mute");
  g_signal_connect(g_item_mute, "activate", G_CALLBACK(on_mute), nullptr);
  gtk_menu_shell_append(GTK_MENU_SHELL(g_menu), g_item_mute);
  g_item_fullscreen = gtk_check_menu_item_new_with_label("Pause in Fullscreen");
  g_signal_connect(g_item_fullscreen, "activate", G_CALLBACK(on_fullscreen),
                   nullptr);
  gtk_menu_shell_append(GTK_MENU_SHELL(g_menu), g_item_fullscreen);
  g_item_fps = gtk_check_menu_item_new_with_label("Show FPS");
  g_signal_connect(g_item_fps, "activate", G_CALLBACK(on_fps), nullptr);
  gtk_menu_shell_append(GTK_MENU_SHELL(g_menu), g_item_fps);
  gtk_menu_shell_append(GTK_MENU_SHELL(g_menu), gtk_separator_menu_item_new());
  g_item_config = gtk_menu_item_new_with_label("Open Config");
  g_signal_connect(g_item_config, "activate", G_CALLBACK(on_config), nullptr);
  gtk_menu_shell_append(GTK_MENU_SHELL(g_menu), g_item_config);
  g_item_logs = gtk_menu_item_new_with_label("Open Logs");
  g_signal_connect(g_item_logs, "activate", G_CALLBACK(on_logs), nullptr);
  gtk_menu_shell_append(GTK_MENU_SHELL(g_menu), g_item_logs);
  gtk_menu_shell_append(GTK_MENU_SHELL(g_menu), gtk_separator_menu_item_new());
  g_item_quit = gtk_menu_item_new_with_label("Quit");
  g_signal_connect(g_item_quit, "activate", G_CALLBACK(on_quit), nullptr);
  gtk_menu_shell_append(GTK_MENU_SHELL(g_menu), g_item_quit);
  gtk_widget_show_all(g_menu);

#ifdef LIZARD_HAVE_APPINDICATOR
  g_indicator = app_indicator_new("lizard", "indicator-messages",
                                  APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
  app_indicator_set_menu(g_indicator, GTK_MENU(g_menu));
  app_indicator_set_status(g_indicator, APP_INDICATOR_STATUS_ACTIVE);
#else
  g_status_icon = gtk_status_icon_new_from_icon_name("indicator-messages");
  g_signal_connect(g_status_icon, "popup-menu", G_CALLBACK(on_popup), nullptr);
  gtk_status_icon_set_visible(g_status_icon, TRUE);
  gtk_status_icon_set_has_tooltip(g_status_icon, TRUE);
  gtk_status_icon_set_tooltip_text(g_status_icon, "Lizard Tapper");
  gtk_status_icon_set_title(g_status_icon, "Lizard Tapper");
  gtk_status_icon_set_name(g_status_icon, "lizard");
  gtk_status_icon_set_from_icon_name(g_status_icon, "indicator-messages");
#endif

  update_menu();
  return true;
}

void update_tray(const TrayState &state) {
  g_state = state;
  update_menu();
}

void shutdown_tray() {
#ifdef LIZARD_HAVE_APPINDICATOR
  if (g_indicator) {
    app_indicator_set_status(g_indicator, APP_INDICATOR_STATUS_PASSIVE);
    g_indicator = nullptr;
  }
#else
  if (g_status_icon) {
    g_object_unref(G_OBJECT(g_status_icon));
    g_status_icon = nullptr;
  }
#endif
  if (g_menu) {
    gtk_widget_destroy(g_menu);
    g_menu = nullptr;
  }
}

} // namespace lizard::platform

#endif
#endif
