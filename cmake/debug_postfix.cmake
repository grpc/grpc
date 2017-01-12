option(gRPC_USE_DEBUG_POSTFIX "Enable use of 'd' as debug postfix" OFF)

if(gRPC_USE_DEBUG_POSTFIX)
    set(CMAKE_DEBUG_POSTFIX "d")
endif()
