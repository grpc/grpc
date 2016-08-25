string(TOLOWER ${CMAKE_SYSTEM_NAME} cares_system_name)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/c-ares)

if(${cares_system_name} MATCHES windows)
  add_definitions(-DCARES_STATICLIB=1)
  add_definitions(-DWIN32_LEAN_AND_MEAN=1)
else()
  include_directories(${CMAKE_CURRENT_SOURCE_DIR}/config_${cares_system_name})
  add_definitions(-DHAVE_CONFIG_H=1)
endif()

file(GLOB lib_sources ../../third_party/c-ares/*.c)
add_library(cares ${lib_sources})
