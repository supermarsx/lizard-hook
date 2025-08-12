add_library(lizard_app STATIC
    config.cpp
)

target_include_directories(lizard_app PUBLIC ${CMAKE_CURRENT_LIST_DIR})
