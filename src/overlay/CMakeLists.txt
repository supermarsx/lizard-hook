add_library(lizard_overlay overlay.cpp)

target_include_directories(lizard_overlay
  PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(lizard_overlay PUBLIC lizard_platform glad stb_image embedded_assets)
add_warning_flags(lizard_overlay)
