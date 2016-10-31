option(gRPC_CONFIG_MODE_FIND_PACKAGE "Use config mode in FindPackage" OFF)

if(gRPC_CONFIG_MODE_FIND_PACKAGE)
  set(_gRPC_FIND_PACKAGE_MODE "CONFIG")
endif()
