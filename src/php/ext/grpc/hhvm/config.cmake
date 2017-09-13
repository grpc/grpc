find_path(GRPC_INCLUDE_DIR grpc.h PATH_SUFFIXES grpc)

if (NOT GRPC_INCLUDE_DIR)
  message(FATAL_ERROR "Unable to find grpc.h")
endif()

find_library(GRPC_LIBRARY grpc)
if (NOT GRPC_LIBRARY)
  message(FATAL_ERROR "Unable to find libgrpc")
endif()

set(CMAKE_BUILD_TYPE Debug)

HHVM_EXTENSION(grpc ext_grpc.cpp common.h call.h call_credentials.h channel.h channel_credentials.h completion_queue.h server.h 
               server_credentials.h timeval.h version.h  call.cpp call_credentials.cpp channel.cpp channel_credentials.cpp
               completion_queue.cpp server.cpp server_credentials.cpp timeval.cpp slice.h slice.cpp)

HHVM_ADD_INCLUDES(grpc ${GRPC_INCLUDE_DIR})
HHVM_LINK_LIBRARIES(grpc ${GRPC_LIBRARY})

HHVM_SYSTEMLIB(grpc ext_grpc.php)

if (CMAKE_VERSION VERSION_LESS "3.1")
  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "--std=gnu++11 ${CMAKE_CXX_FLAGS}")
  endif()
else()
  set(CMAKE_CXX_STANDARD 11)
endif()
