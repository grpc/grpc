set(SNAPPY_INSTALL "OFF")

if(NOT SNAPPY_ROOT_DIR)
  set(SNAPPY_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/snappy)
endif()
include_directories("${SNAPPY_ROOT_DIR}")
add_subdirectory(${SNAPPY_ROOT_DIR} third_party/snappy)

set(_gRPC_SNAPPY_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/third_party/snappy")