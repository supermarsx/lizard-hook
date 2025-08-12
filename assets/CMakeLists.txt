set(ASSETS
    lizard-regular.png
    lizard-processed-clean-no-meta.flac
)

set(GENERATED_SOURCES)
foreach(asset ${ASSETS})
  get_filename_component(fname ${asset} NAME)
  string(REPLACE "-" "_" var ${fname})
  string(REPLACE "." "_" var ${var})
  set(out ${CMAKE_CURRENT_BINARY_DIR}/${var}.cpp)
  add_custom_command(
    OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/${asset} -DOUTPUT=${out} -DVAR=${var} -P ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/embed_resource.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${asset} ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/embed_resource.cmake
    COMMENT "Embedding ${asset}"
  )
  list(APPEND GENERATED_SOURCES ${out})
endforeach()

add_library(embedded_assets STATIC ${GENERATED_SOURCES})

target_include_directories(embedded_assets PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)
