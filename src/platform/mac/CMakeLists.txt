add_library(lizard_platform_mac window.mm tray.mm)

    target_include_directories(
        lizard_platform_mac PUBLIC ${CMAKE_CURRENT_LIST_DIR} /..)

        target_link_libraries(lizard_platform_mac PUBLIC "-framework Cocoa"
                                                         "-framework OpenGL")
add_warning_flags(lizard_platform_mac)
