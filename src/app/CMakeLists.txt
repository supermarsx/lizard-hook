add_library(lizard_app STATIC config.cpp)

target_include_directories(lizard_app PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(lizard_app PUBLIC nlohmann_json::nlohmann_json lizard_util spdlog::spdlog)

target_include_directories(lizard_app PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(lizard_app PUBLIC nlohmann_json::nlohmann_json)
add_warning_flags(lizard_app)
