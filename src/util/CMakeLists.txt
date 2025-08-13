add_library(lizard_util STATIC log.cpp)

target_include_directories(lizard_util PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(lizard_util PUBLIC spdlog::spdlog)
