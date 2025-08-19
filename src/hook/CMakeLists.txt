add_library(lizard_hook STATIC)

# Allow includes like <hook/keyboard_hook.h>
target_include_directories(lizard_hook PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(lizard_hook PUBLIC spdlog::spdlog)

target_sources(lizard_hook PRIVATE filter.cpp)

if (WIN32)
    target_sources(lizard_hook PRIVATE windows/keyboard_hook.cpp)
    target_link_libraries(lizard_hook PRIVATE user32)
elif (APPLE)
    target_sources(lizard_hook PRIVATE mac/keyboard_hook.mm)
    target_link_libraries(lizard_hook PRIVATE "-framework ApplicationServices")
elif (UNIX)
    find_package(X11 REQUIRED)
    target_sources(lizard_hook PRIVATE linux/keyboard_hook.cpp)
    target_link_libraries(lizard_hook PRIVATE X11 Xi Xtst)
endif()

add_warning_flags(lizard_hook)
