find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 gtk+-3.0)
if(NOT GTK3_FOUND)
  message(FATAL_ERROR "gtk+-3.0 not found. Install the GTK3 development files (e.g., libgtk-3-dev).")
endif()
pkg_check_modules(APPINDICATOR ayatana-appindicator3-0.1 QUIET)

add_library(lizard_platform_linux window.cpp tray.cpp)

target_include_directories(lizard_platform_linux PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/..
  ${GTK3_INCLUDE_DIRS}
  ${APPINDICATOR_INCLUDE_DIRS}
)

target_link_libraries(lizard_platform_linux PUBLIC
  X11 Xfixes Xext GL glad
  ${GTK3_LIBRARIES}
  ${APPINDICATOR_LIBRARIES}
)

if(APPINDICATOR_FOUND)
  target_compile_definitions(lizard_platform_linux PRIVATE LIZARD_HAVE_APPINDICATOR)
endif()
