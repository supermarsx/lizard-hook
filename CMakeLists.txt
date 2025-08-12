cmake_minimum_required(VERSION 3.24)
project(LizardTapper LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Fetch and build glad
FetchContent_Declare(
  glad
  GIT_REPOSITORY https://github.com/Dav1dde/glad.git
  GIT_TAG v0.1.36
)
FetchContent_GetProperties(glad)
if(NOT glad_POPULATED)
  FetchContent_Populate(glad)
endif()
set(GLAD_GEN_DIR ${CMAKE_BINARY_DIR}/glad)
execute_process(
  COMMAND ${Python3_EXECUTABLE} -m glad --profile core --api gl=3.3 --generator c --out-path ${GLAD_GEN_DIR}
  WORKING_DIRECTORY ${glad_SOURCE_DIR}
  RESULT_VARIABLE glad_result
)
if(NOT glad_result EQUAL 0)
  message(FATAL_ERROR "glad generation failed")
endif()
add_library(glad STATIC ${GLAD_GEN_DIR}/src/glad.c)
target_include_directories(glad PUBLIC ${GLAD_GEN_DIR}/include)

# Fetch stb
FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG master
)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
  FetchContent_Populate(stb)
endif()
add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${stb_SOURCE_DIR})

add_subdirectory(src/app)
add_subdirectory(src/hook)
add_subdirectory(src/audio)
add_subdirectory(src/platform)
add_subdirectory(src/overlay)
add_subdirectory(src/util)
