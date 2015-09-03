# Minimum CMake required
cmake_minimum_required(VERSION 2.8)

# CMake policies
cmake_policy(SET CMP0022 NEW)

# Project
project(gRPC C CXX)

# Options
option(gRPC_VERBOSE "Enable to verbose output" OFF)

# Parse build.json file
function(_parse_build_yaml)

  # Read file strings
  file(STRINGS "build.yaml" _strings)

  # Regex

  set(_section_regex "^[ \t]*([a-zA-Z]+)[ \t]*:[ \t]*$")

  set(_version_regex          "^[ \t]*version[ \t]*:[ \t]*{")
  string(APPEND _version_regex "[ \t]*major[ \t]*:[ \t]*([0-9]+)[ \t]*,")
  string(APPEND _version_regex "[ \t]*minor[ \t]*:[ \t]*([0-9]+)[ \t]*,")
  string(APPEND _version_regex "[ \t]*micro[ \t]*:[ \t]*([0-9]+)[ \t]*,")
  string(APPEND _version_regex "[ \t]*build[ \t]*:[ \t]*([0-9]+)[ \t]*")
  string(APPEND _version_regex "}[ \t]*$")

  set(_name_regex "^[ \t]*-[ \t]*name[ \t]*:[ \t]*([ +a-z_A-Z]+)[ \t]*$")

  set(_group_regex "^[ \t]*([a-z_A-Z]+)[ \t]*:[ \t]*\\[([^]]+)\\][ \t]*$")

  set(_value_regex "^[ \t]*([+a-z_A-Z]+)[ \t]*:[ \t]*([+a-z_A-Z]+)[ \t]*$")

  # Parsing
  set(_section "none")
  set(_gRPC_FILEGROUPS)
  set(_gRPC_LIBS)
  set(_gRPC_TARGETS)
  foreach(_string ${_strings})
    #message("_string:\n${_string}")

    # Section
    string(REGEX MATCH "${_section_regex}"
      _section_string "${_string}")
    if(_section_string)
      string(REGEX REPLACE "${_section_regex}" "\\1" _section "${_section_string}")
      string(TOLOWER "${_section}" _section)
    endif()

    # Settings
    if("${_section}" STREQUAL "settings")

      string(REGEX MATCH "${_version_regex}"
        _version_string "${_string}")
      if(_version_string)
        string(REGEX REPLACE "${_version_regex}" "\\1"
          _version_major "${_version_string}")
        set(gRPC_VERSION_MAJOR ${_version_major} PARENT_SCOPE)
        string(REGEX REPLACE "${_version_regex}" "\\2"
          _version_minor "${_version_string}")
        set(gRPC_VERSION_MINOR ${_version_minor} PARENT_SCOPE)
        string(REGEX REPLACE "${_version_regex}" "\\3"
          _version_micro "${_version_string}")
        set(gRPC_VERSION_MICRO ${_version_micro} PARENT_SCOPE)
        string(REGEX REPLACE "${_version_regex}" "\\4"
          _version_build "${_version_string}")
        set(gRPC_VERSION_BUILD ${_version_build} PARENT_SCOPE)
      endif()

    # Filegroups
    elseif("${_section}" STREQUAL "filegroups")

      #message("_string:\n${_string}")
      # Name
      string(REGEX MATCH "${_name_regex}"
        _name_string "${_string}")
      if(_name_string)
        string(REGEX REPLACE "${_name_regex}" "\\1" _name "${_name_string}")
        list(APPEND _gRPC_FILEGROUPS ${_name})
      endif()
      # Groups
      string(REGEX MATCH "${_group_regex}"
        _group_string "${_string}")
      if(_group_string)
        string(REGEX REPLACE "${_group_regex}" "\\1" _group_name "${_group_string}")
        string(REGEX REPLACE "${_group_regex}" "\\2" _group_group "${_group_string}")
        string(REPLACE " " "" _group_group "${_group_group}")
        string(REPLACE "," ";" _group_group "${_group_group}")
        string(REPLACE ";;" ";" _group_group "${_group_group}")
        set(gRPC_FILEGROUP_${_name}_${_group_name} ${_group_group} PARENT_SCOPE)
      endif()

    # Libraries
    elseif("${_section}" STREQUAL "libs")

      # Name
      string(REGEX MATCH "${_name_regex}"
        _name_string "${_string}")
      if(_name_string)
        string(REGEX REPLACE "${_name_regex}" "\\1" _name "${_name_string}")
        list(APPEND _gRPC_LIBS ${_name})
      endif()
      # Values
      string(REGEX MATCH "${_value_regex}"
        _value_string "${_string}")
      if(_value_string)
        string(REGEX REPLACE "${_value_regex}" "\\1" _value_name "${_value_string}")
        string(REGEX REPLACE "${_value_regex}" "\\2" _value_value "${_value_string}")
        set(gRPC_LIB_${_name}_${_value_name} ${_value_value} PARENT_SCOPE)
      endif()
      # Groups
      string(REGEX MATCH "${_group_regex}"
        _group_string "${_string}")
      if(_group_string)
        string(REGEX REPLACE "${_group_regex}" "\\1" _group_name "${_group_string}")
        string(REGEX REPLACE "${_group_regex}" "\\2" _group_group "${_group_string}")
        string(REPLACE " " "" _group_group "${_group_group}")
        string(REPLACE "," ";" _group_group "${_group_group}")
        string(REPLACE ";;" ";" _group_group "${_group_group}")
        set(gRPC_LIB_${_name}_${_group_name} ${_group_group} PARENT_SCOPE)
      endif()

    # Targets
    elseif("${_section}" STREQUAL "targets")

      # Name
      string(REGEX MATCH "${_name_regex}"
        _name_string "${_string}")
      if(_name_string)
        string(REGEX REPLACE "${_name_regex}" "\\1" _name "${_name_string}")
        list(APPEND _gRPC_TARGETS ${_name})
      endif()
      # Values
      string(REGEX MATCH "${_value_regex}"
        _value_string "${_string}")
      if(_value_string)
        string(REGEX REPLACE "${_value_regex}" "\\1" _value_name "${_value_string}")
        string(REGEX REPLACE "${_value_regex}" "\\2" _value_value "${_value_string}")
        set(gRPC_TARGET_${_name}_${_value_name} ${_value_value} PARENT_SCOPE)
      endif()
      # Groups
      string(REGEX MATCH "${_group_regex}"
        _group_string "${_string}")
      if(_group_string)
        string(REGEX REPLACE "${_group_regex}" "\\1" _group_name "${_group_string}")
        string(REGEX REPLACE "${_group_regex}" "\\2" _group_group "${_group_string}")
        string(REPLACE " " "" _group_group "${_group_group}")
        string(REPLACE "," ";" _group_group "${_group_group}")
        string(REPLACE ";;" ";" _group_group "${_group_group}")
        set(gRPC_TARGET_${_name}_${_group_name} ${_group_group} PARENT_SCOPE)
      endif()

    endif()

  endforeach()

  set(gRPC_FILEGROUPS ${_gRPC_FILEGROUPS} PARENT_SCOPE)
  set(gRPC_LIBS ${_gRPC_LIBS} PARENT_SCOPE)
  set(gRPC_TARGETS ${_gRPC_TARGETS} PARENT_SCOPE)

endfunction()

_parse_build_yaml()

set(gRPC_VERSION
  "${gRPC_VERSION_MAJOR}.${gRPC_VERSION_MINOR}.${gRPC_VERSION_MICRO}.${gRPC_VERSION_BUILD}")

# Remove zookeeper (now not supported)
list(REMOVE_ITEM gRPC_LIBS grpc_zookeeper)
list(REMOVE_ITEM gRPC_TARGETS zookeeper_test)

# Statistics
if(gRPC_VERBOSE)
  message(STATUS "Project structure from build.yaml {")
  message(STATUS "  gRPC_VERSION ${gRPC_VERSION}")
  message(STATUS "  gRPC_FILEGROUPS {")
  foreach(gRPC_FILEGROUP ${gRPC_FILEGROUPS})
  message(STATUS "    ${gRPC_FILEGROUP}")
  endforeach()
  message(STATUS "  }")
  message(STATUS "  gRPC_LIBS {")
  foreach(gRPC_LIB ${gRPC_LIBS})
  message(STATUS "    ${gRPC_LIB} (${gRPC_LIB_${gRPC_LIB}_language}) - ${gRPC_LIB_${gRPC_LIB}_build}")
  endforeach()
  message(STATUS "  }")
  message(STATUS "  gRPC_TARGETS {")
  foreach(gRPC_TARGET ${gRPC_TARGETS})
  message(STATUS "    ${gRPC_TARGET} (${gRPC_TARGET_${gRPC_TARGET}_language}) - ${gRPC_TARGET_${gRPC_TARGET}_build}")
  endforeach()
  message(STATUS "  }")
  message(STATUS "}")
endif()

# Find ZLIB
find_package(ZLIB)

if(TARGET ZLIB::ZLIB)
  set(ZLIB_LIBRARIES ZLIB::ZLIB)
endif()

# Find Protobuf
find_package(Protobuf CONFIG)

if(Protobuf_FOUND)
  if(TARGET libprotobuf)
    set(PROTOBUF_LIBRARIES libprotobuf)
  endif()
  if(TARGET libprotoc)
    set(PROTOBUF_PROTOC_LIBRARIES libprotoc)
  endif()
else()
  find_package(Protobuf MODULE)
endif()

# Find OpenSSL
find_package(OpenSSL)

if(TARGET OpenSSL::SSL AND TARGET OpenSSL::Crypto)
  set(OPENSSL_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
endif()

# Include
include_directories("." "include"
  ${PROTOBUF_INCLUDE_DIRS}
  ${ZLIB_INCLUDE_DIRS}
  ${OPENSSL_INCLUDE_DIR})

# Standard paths
include(GNUInstallDirs)

# Manual build gpr
#add_library(gpr ${gRPC_LIB_gpr_src})

# Libraries (now only ALL and non secure libraries)
foreach(gRPC_LIB
  ${gRPC_LIBS})
  set(_lib ${gRPC_LIB})
  set(_lib_ gRPC_LIB_${_lib})
  if("${${_lib_}_language}" STREQUAL "c")
    set(_lib_c TRUE)
  else()
    set(_lib_c FALSE)
  endif()
  if("${${_lib_}_language}" STREQUAL "c++")
    set(_lib_cxx TRUE)
  else()
    set(_lib_cxx FALSE)
  endif()
  if("${${_lib_}_build}" STREQUAL "all")
    set(_lib_build_all TRUE)
  else()
    set(_lib_build_all FALSE)
  endif()
  if("${${_lib_}_build}" STREQUAL "protoc")
    set(_lib_build_protoc TRUE)
  else()
    set(_lib_build_protoc FALSE)
  endif()
  if("${${_lib_}_secure}" STREQUAL "true")
    set(_lib_secure_true TRUE)
  else()
    set(_lib_secure_true FALSE)
  endif()
  if("${${_lib_}_secure}" STREQUAL "false")
    set(_lib_secure_false TRUE)
  else()
    set(_lib_secure_false FALSE)
  endif()
  if("${${_lib_}_secure}" STREQUAL "check")
    set(_lib_secure_check TRUE)
  else()
    set(_lib_secure_check FALSE)
  endif()
  if(DEFINED ${_lib_}_baselib AND "${${_lib_}_baselib}" STREQUAL "true")
    set(_lib_baselib TRUE)
  else()
    set(_lib_baselib FALSE)
  endif()
  if((_lib_c OR _lib_cxx) AND (_lib_build_all OR _lib_build_protoc))
    set(_lib_type "STATIC")
    if(DEFINED ${_lib_}_dll AND "${${_lib_}_dll}" STREQUAL "yes")
      # Now is not work
      #set(_lib_type "SHARED")
    endif()
    set(_lib_src ${${_lib_}_src})
    set(_lib_public_headers ${${_lib_}_public_headers})
    if(DEFINED ${_lib_}_filegroups)
      foreach(_filegroup ${${_lib_}_filegroups})
        list(APPEND _lib_src ${gRPC_FILEGROUP_${_filegroup}_src})
        list(APPEND _lib_public_headers ${gRPC_FILEGROUP_${_filegroup}_public_headers})
      endforeach()
    endif()
    add_library(${_lib} ${_lib_type} ${_lib_src})
    if(DEFINED ${_lib_}_deps)
      target_link_libraries(${_lib} PUBLIC ${${_lib_}_deps})
    endif()
    if(_lib_build_protoc)
      target_link_libraries(${_lib} PUBLIC ${PROTOBUF_LIBRARIES})
    endif()
    if(_lib_secure_true OR (_lib_secure_check AND OPENSSL_FOUND))
      target_link_libraries(${_lib} PRIVATE ${OPENSSL_LIBRARIES})
    endif()
    if(_lib_baselib)
      if(WIN32)
        target_link_libraries(${_lib} PRIVATE wsock32 ws2_32)
      endif()
      if(_lib_c)
        target_link_libraries(${_lib} PRIVATE ${ZLIB_LIBRARIES})
      endif()
    endif()
    # Install
    install(TARGETS ${_lib} EXPORT grpc-targets
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT grpc-${_lib}
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT grpc-${_lib}
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT grpc-${_lib})
    set_property(TARGET ${_lib}
      PROPERTY INTERFACE_INCLUDE_DIRECTORIES
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    foreach(_hdr ${_lib_public_headers})
      get_filename_component(_hdr_name "${_hdr}" NAME)
      get_filename_component(_hdr_path "${_hdr}" PATH)
      string(REPLACE "include/" "${CMAKE_INSTALL_INCLUDEDIR}/"
        _hdr_path "${_hdr_path}")
      install(FILES "${_hdr}"
        DESTINATION "${_hdr_path}"
        COMPONENT grpc-${_lib}-headers)
    endforeach()
  endif()
endforeach()

# Targets (now only protoc targets)
foreach(gRPC_TARGET
  ${gRPC_TARGETS}
  )
  set(_target ${gRPC_TARGET})
  set(_target_ gRPC_TARGET_${_target})
  if("${${_target_}_language}" STREQUAL "c")
    set(_target_c TRUE)
  else()
    set(_target_c FALSE)
  endif()
  if("${${_target_}_language}" STREQUAL "c++")
    set(_target_cxx TRUE)
  else()
    set(_target_cxx FALSE)
  endif()
  if("${${_target_}_build}" STREQUAL "protoc")
    set(_target_build_protoc TRUE)
  else()
    set(_target_build_protoc FALSE)
  endif()
  if("${${_target_}_secure}" STREQUAL "true")
    set(_target_secure_true TRUE)
  else()
    set(_target_secure_true FALSE)
  endif()
  if("${${_target_}_secure}" STREQUAL "false")
    set(_target_secure_false TRUE)
  else()
    set(_target_secure_false FALSE)
  endif()
  if((_target_c OR _target_cxx) AND _target_build_protoc)
    set(_target_src ${${_target_}_src})
    add_executable(${_target} ${_target_src})
    if(DEFINED ${_target_}_deps)
      target_link_libraries(${_target} PRIVATE ${${_target_}_deps})
    endif()
    if(_target_build_protoc)
      target_link_libraries(${_target} PRIVATE ${PROTOBUF_LIBRARIES} ${PROTOBUF_PROTOC_LIBRARIES})
    endif()
    if(_target_secure_true OR (_target_secure_check AND OPENSSL_FOUND))
      target_link_libraries(${_target} PRIVATE ${OPENSSL_LIBRARIES})
    endif()
    # Install
    install(TARGETS ${_target} EXPORT grpc-targets
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT grpc-${_target})
  endif()
endforeach()

# Export configuration

install(EXPORT grpc-targets
  DESTINATION "lib/cmake/grpc"
  NAMESPACE "gRPC::"
  COMPONENT grpc-export)

configure_file("templates/cmake/grpc-config.cmake.in"
  grpc-config.cmake @ONLY)
configure_file("templates/cmake/grpc-config-version.cmake.in"
  grpc-config-version.cmake @ONLY)

install(FILES
  "${gRPC_BINARY_DIR}/grpc-config.cmake"
  "${gRPC_BINARY_DIR}/grpc-config-version.cmake"
  DESTINATION "lib/cmake/grpc"
  COMPONENT grpc-export)
