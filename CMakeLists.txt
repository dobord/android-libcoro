cmake_minimum_required(VERSION 3.20)

# -------- clang-format target --------
find_program(CLANG_FORMAT_BIN NAMES clang-format)
if(CLANG_FORMAT_BIN)
  # Collect project source files (exclude third_party and build directories)
  file(GLOB_RECURSE ALL_CXX_SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/app/src/main/*.cpp
    ${CMAKE_SOURCE_DIR}/app/src/main/*.h
  )
  # Create custom target to run clang-format in-place
  add_custom_target(format
    COMMAND ${CLANG_FORMAT_BIN} -i ${ALL_CXX_SOURCE_FILES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running clang-format on project sources"
  )
endif()
