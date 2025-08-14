cmake_minimum_required(VERSION 3.20)

# -------- clang-format target --------
find_program(CLANG_FORMAT_BIN NAMES clang-format)
if(CLANG_FORMAT_BIN)
  # Соберём список исходников (исключая third_party и build)
  file(GLOB_RECURSE ALL_CXX_SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/app/src/main/*.cpp
    ${CMAKE_SOURCE_DIR}/app/src/main/*.h
  )
  # Создаём custom target для форматирования
  add_custom_target(format
    COMMAND ${CLANG_FORMAT_BIN} -i ${ALL_CXX_SOURCE_FILES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running clang-format on project sources"
  )
endif()
