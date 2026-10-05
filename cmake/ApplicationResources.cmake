if(NOT TARGET PointyPointer)
  return()
endif()
add_custom_target(pointy_resources ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/resources"
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources")
get_property(pointy_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(t IN LISTS pointy_targets)
  get_target_property(kind ${t} TYPE)
  if(kind STREQUAL "EXECUTABLE")
    add_dependencies(${t} pointy_resources)
    # Keep assert-based inherited tests meaningful under Release too.
    if(BUILD_TESTING AND NOT t STREQUAL "PointyPointer")
      if(MSVC)
        target_compile_options(${t} PRIVATE /UNDEBUG)
      else()
        target_compile_options(${t} PRIVATE -UNDEBUG)
      endif()
    endif()
  endif()
endforeach()
if(WIN32)
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/pointypointer.rc"
    "1 ICON \"${PROJECT_SOURCE_DIR}/resources/pointypointer.ico\"\n")
  target_sources(PointyPointer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/pointypointer.rc")
elseif(APPLE)
  add_custom_command(TARGET PointyPointer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/resources"
      "$<TARGET_BUNDLE_CONTENT_DIR:PointyPointer>/Resources/resources")
endif()
