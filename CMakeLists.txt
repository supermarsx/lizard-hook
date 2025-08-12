cmake_minimum_required(VERSION 3.24)
project(LizardHook LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

#Fetch and build glad
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

#Fetch stb
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

#Fetch nlohmann_json
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory(assets)
add_subdirectory(src/app)
add_subdirectory(src/hook)
add_subdirectory(src/audio)
add_subdirectory(src/platform)
add_subdirectory(src/overlay)
add_subdirectory(src/util)

include(CTest)
if(BUILD_TESTING)
  FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
  )
  FetchContent_MakeAvailable(catch2)
  add_subdirectory(src/tests)
endif()

file(GLOB_RECURSE ALL_CXX_SOURCE_FILES CONFIGURE_DEPENDS
  src/*.cpp
  src/*.hpp
  src/*.h
)
file(GLOB_RECURSE ALL_CXX_SOURCE_CPP CONFIGURE_DEPENDS src/*.cpp)

add_custom_target(format
  COMMAND clang-format -i ${ALL_CXX_SOURCE_FILES}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_custom_target(lint
  COMMAND clang-format --dry-run --Werror ${ALL_CXX_SOURCE_FILES}
  COMMAND clang-tidy ${ALL_CXX_SOURCE_CPP} -p ${CMAKE_BINARY_DIR}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
