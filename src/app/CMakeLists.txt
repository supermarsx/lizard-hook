include(FetchContent)
find_package(Threads REQUIRED)

add_library(lizard_app STATIC config.cpp)

target_include_directories(lizard_app PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(lizard_app PUBLIC nlohmann_json::nlohmann_json lizard_util spdlog::spdlog)

target_include_directories(lizard_app PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(lizard_app PUBLIC nlohmann_json::nlohmann_json)
add_warning_flags(lizard_app)

FetchContent_Declare(
  cxxopts
  GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
  GIT_TAG v3.1.1
)
FetchContent_MakeAvailable(cxxopts)

add_executable(lizard_hook_exe main.cpp)
target_include_directories(lizard_hook_exe PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(lizard_hook_exe PRIVATE Threads::Threads lizard_app lizard_audio lizard_hook lizard_platform glad stb_image embedded_assets spdlog::spdlog cxxopts::cxxopts)
add_warning_flags(lizard_hook_exe)

