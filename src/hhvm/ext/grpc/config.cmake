find_path(GRPC_INCLUDE_DIR grpc.h PATH_SUFFIXES grpc)

if (NOT GRPC_INCLUDE_DIR)
  message(FATAL_ERROR "Unable to find grpc.h")
endif()

find_library(GRPC_LIBRARY grpc)
if (NOT GRPC_LIBRARY)
  message(FATAL_ERROR "Unable to find libgrpc")
endif()

set(CMAKE_BUILD_TYPE Debug)

HHVM_EXTENSION(grpc ext_grpc.cpp byte_buffer.h call.h call_credentials.h channel.h channel_credentials.h completion_queue.h server.h server_credentials.h timeval.h version.h byte_buffer.cpp call.cpp call_credentials.cpp channel.cpp channel_credentials.cpp completion_queue.cpp server.cpp server_credentials.cpp timeval.cpp)

HHVM_ADD_INCLUDES(grpc ${GRPC_INCLUDE_DIR})
HHVM_LINK_LIBRARIES(grpc ${GRPC_LIBRARY})

HHVM_SYSTEMLIB(grpc ext_grpc.php)
