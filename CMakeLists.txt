cmake_minimum_required(VERSION 3.24)
project(LizardTapper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(src/app)
add_subdirectory(src/hook)
add_subdirectory(src/audio)
add_subdirectory(src/overlay)
add_subdirectory(src/util)
add_subdirectory(src/platform)
