add_library(lizard_platform_linux window.cpp)

target_include_directories(lizard_platform_linux PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)

target_link_libraries(lizard_platform_linux PUBLIC X11 Xfixes Xext GL glad)
