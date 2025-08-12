include(FetchContent)

FetchContent_Declare(
  miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG        master
)

FetchContent_Declare(
  drflac
  GIT_REPOSITORY https://github.com/mackron/dr_libs.git
  GIT_TAG        master
)

FetchContent_MakeAvailable(miniaudio drflac)

add_library(lizard_audio STATIC engine.cpp)

target_include_directories(lizard_audio
  PRIVATE
    ${miniaudio_SOURCE_DIR}
    ${drflac_SOURCE_DIR}
)

set(AUDIO_BACKEND "ALSA" CACHE STRING "Audio backend (WASAPI, CoreAudio, ALSA)")
set_property(CACHE AUDIO_BACKEND PROPERTY STRINGS WASAPI CoreAudio ALSA)

if(AUDIO_BACKEND STREQUAL "WASAPI")
  target_compile_definitions(lizard_audio PRIVATE LIZARD_AUDIO_WASAPI)
elseif(AUDIO_BACKEND STREQUAL "CoreAudio")
  target_compile_definitions(lizard_audio PRIVATE LIZARD_AUDIO_COREAUDIO)
elseif(AUDIO_BACKEND STREQUAL "ALSA")
  target_compile_definitions(lizard_audio PRIVATE LIZARD_AUDIO_ALSA)
endif()

# Link platform specific libraries
if(AUDIO_BACKEND STREQUAL "WASAPI")
  target_link_libraries(lizard_audio PRIVATE ole32 uuid)
elif(AUDIO_BACKEND STREQUAL "CoreAudio")
  find_library(COREAUDIO_LIB CoreAudio)
  find_library(AUDIOTOOLBOX_LIB AudioToolbox)
  target_link_libraries(lizard_audio PRIVATE ${COREAUDIO_LIB} ${AUDIOTOOLBOX_LIB})
elif(AUDIO_BACKEND STREQUAL "ALSA")
  target_link_libraries(lizard_audio PRIVATE asound)
endif()

