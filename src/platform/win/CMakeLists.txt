add_library(lizard_platform_win window.cpp tray.cpp)

    target_include_directories(
        lizard_platform_win PUBLIC ${CMAKE_CURRENT_LIST_DIR} /..)

        target_link_libraries(lizard_platform_win PUBLIC user32 gdi32 opengl32
                                  dwmapi shell32 glad)
