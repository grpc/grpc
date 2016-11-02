option(gRPC_PROTOBUF_PACKAGE_CONFIG_MODE "Use config mode in FindPackage for protobuf" ON)
option(gRPC_SSL_PACKAGE_CONFIG_MODE "Use config mode in FindPackage for openssl" OFF)
option(gRPC_ZLIB_PACKAGE_CONFIG_MODE "Use config mode in FindPackage for zlib" OFF)

if(gRPC_PROTOBUF_PACKAGE_CONFIG_MODE)
    set(_gRPC_PROTOBUF_PACKAGE_MODE "CONFIG")
endif()

if(gRPC_SSL_PACKAGE_CONFIG_MODE)
    set(_gRPC_SSL_PACKAGE_MODE "CONFIG")
endif()

if(gRPC_ZLIB_PACKAGE_CONFIG_MODE)
    set(_gRPC_ZLIB_PACKAGE_MODE "CONFIG")
endif()
