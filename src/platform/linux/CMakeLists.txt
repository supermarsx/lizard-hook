find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
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
add_warning_flags(lizard_platform_linux)
