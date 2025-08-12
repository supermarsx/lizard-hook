add_library(lizard_platform INTERFACE)

target_include_directories(lizard_platform INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
  add_subdirectory(win)
  target_link_libraries(lizard_platform INTERFACE lizard_platform_win)
elif(APPLE)
  add_subdirectory(mac)
  target_link_libraries(lizard_platform INTERFACE lizard_platform_mac)
else()
  add_subdirectory(linux)
  target_link_libraries(lizard_platform INTERFACE lizard_platform_linux)
endif()
