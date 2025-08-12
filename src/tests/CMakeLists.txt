add_executable(config_tests config_tests.cpp)
target_link_libraries(config_tests PRIVATE lizard_app Catch2::Catch2WithMain)
target_include_directories(config_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME config COMMAND config_tests)
