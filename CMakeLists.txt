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

  set(_name_regex "^[ \t]*-[ \t]*name[ \t]*:[ \t]*([ +a-z_A-Z]+)[ \t]*$")

  set(_section_regex "^([a-z_A-Z]+)[ \t]*:[ \t]*$")
  set(_subsection_regex "^[ \t]+([a-z_A-Z]+)[ \t]*:[ \t]*$")

  set(_value_regex "^[ \t]+([+a-z_A-Z]+)[ \t]*:[ \t]*([^ \t$]+)[ \t]*$")
  set(_subvalue_regex "^[ \t]+-[ \t]*([^ \t$]+)[ \t]*$")

  # Parsing

  foreach(_string ${_strings})
    #message("_string:\n${_string}")

    # Name
    string(REGEX MATCH "${_name_regex}"
      _name_string "${_string}")
    if(_name_string)
      string(REGEX REPLACE "${_name_regex}" "\\1" _name "${_name_string}")
      #message("_name: ${_name}")
      set(_name_arg "_${_name}")
      list(APPEND _names ${_name})
      set(gRPC${_section_arg} ${_names} PARENT_SCOPE)
    endif()

    # Section
    string(REGEX MATCH "${_section_regex}"
      _section_string "${_string}")
    if(_section_string)
      string(REGEX REPLACE "${_section_regex}" "\\1" _section "${_section_string}")
      string(TOUPPER "${_section}" _section)
      #message("_section: ${_section}")
      set(_section_arg "_${_section}")
      set(_names)
    endif()

    # Subsection
    string(REGEX MATCH "${_subsection_regex}"
      _subsection_string "${_string}")
    if(_subsection_string)
      string(REGEX REPLACE "${_subsection_regex}" "\\1" _subsection "${_subsection_string}")
      #message("_subsection: ${_subsection}")
      set(_subsection_arg "_${_subsection}")
      set(_subvalues)
    endif()

    # Value
    string(REGEX MATCH "${_value_regex}"
      _value_string "${_string}")
    if(_value_string)
      string(REGEX REPLACE "${_value_regex}" "\\1" _value_name "${_value_string}")
      string(REGEX REPLACE "${_value_regex}" "\\2" _value_value "${_value_string}")
      if(NOT "${_value_value}" STREQUAL "[]")
        if(_name)
          set(gRPC${_section_arg}${_name_arg}_${_value_name} ${_value_value} PARENT_SCOPE)
          #message("gRPC${_section_arg}${_name_arg}_${_value_name} ${_value_value}")
        else()
          set(gRPC${_section_arg}${_name_arg}${_subsection_arg}_${_value_name} ${_value_value} PARENT_SCOPE)
          #message("gRPC${_section_arg}${_name_arg}${_subsection_arg}_${_value_name} ${_value_value}")
        endif()
      endif()
    endif()

    # Subvalue
    string(REGEX MATCH "${_subvalue_regex}"
      _subvalue_string "${_string}")
    if(_subvalue_string)
      string(REGEX REPLACE "${_subvalue_regex}" "\\1" _subvalue "${_subvalue_string}")
      list(APPEND _subvalues ${_subvalue})
      set(gRPC${_section_arg}${_name_arg}${_subsection_arg} ${_subvalues} PARENT_SCOPE)
      #message("gRPC${_section_arg}${_name_arg}${_subsection_arg}: ${_subvalue}")
    endif()

  endforeach()

endfunction()

_parse_build_yaml()

set(gRPC_VERSION_MAJOR ${gRPC_SETTINGS_version_major})
set(gRPC_VERSION_MINOR ${gRPC_SETTINGS_version_minor})
set(gRPC_VERSION_MICRO ${gRPC_SETTINGS_version_micro})
set(gRPC_VERSION_BUILD ${gRPC_SETTINGS_version_build})

set(gRPC_VERSION_PATCH ${gRPC_VERSION_MICRO})
set(gRPC_VERSION_TWEAK ${gRPC_VERSION_BUILD})

set(gRPC_VERSION
  "${gRPC_VERSION_MAJOR}.${gRPC_VERSION_MINOR}.${gRPC_VERSION_MICRO}.${gRPC_VERSION_BUILD}")

# Remove zookeeper (now not supported)
#list(REMOVE_ITEM gRPC_LIBS grpc_zookeeper)
#list(REMOVE_ITEM gRPC_TARGETS zookeeper_test)

# Statistics
if(gRPC_VERBOSE)
  message(STATUS "gRPC project structure from build.yaml {")
  message(STATUS "  gRPC_VERSION ${gRPC_VERSION}")
  message(STATUS "  gRPC_FILEGROUPS {")
  foreach(gRPC_FILEGROUP ${gRPC_FILEGROUPS})
  message(STATUS "    ${gRPC_FILEGROUP}")
  endforeach()
  message(STATUS "  }")
  message(STATUS "  gRPC_LIBS {")
  foreach(gRPC_LIB ${gRPC_LIBS})
  if(DEFINED gRPC_LIBS_${gRPC_LIB}_platforms)
    set(_platforms " (${gRPC_LIBS_${gRPC_LIB}_platforms})")
  else()
    set(_platforms)
  endif()
  message(STATUS "    ${gRPC_LIB} (${gRPC_LIBS_${gRPC_LIB}_language}) - ${gRPC_LIBS_${gRPC_LIB}_build}${_platforms}")
  endforeach()
  message(STATUS "  }")
  message(STATUS "  gRPC_TARGETS {")
  foreach(gRPC_TARGET ${gRPC_TARGETS})
  if(DEFINED gRPC_TARGETS_${gRPC_TARGET}_platforms)
    set(_platforms " (${gRPC_TARGETS_${gRPC_TARGET}_platforms})")
  else()
    set(_platforms)
  endif()
  message(STATUS "    ${gRPC_TARGET} (${gRPC_TARGETS_${gRPC_TARGET}_language}) - ${gRPC_TARGETS_${gRPC_TARGET}_build}${_platforms}")
  endforeach()
  message(STATUS "  }")
  message(STATUS "}")
endif()

#return()

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

function(_check_platforms PREFIX VALUE)
  if(DEFINED ${PREFIX}_platforms)
    set(_result FALSE)
    foreach(_platform ${${PREFIX}_platforms})
      if("${_platform}" STREQUAL "mac")
         if("${CMAKE_SYSTEM_NAME}" STREQUAL "Darwin")
           set(_result TRUE)
         endif()
      elseif("${_platform}" STREQUAL "linux")
         if("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
           set(_result TRUE)
         endif()
      elseif("${_platform}" STREQUAL "posix")
         # Check this
      endif()
    endforeach()
  else()
    set(_result TRUE)
  endif()
  set(${VALUE} ${_result} PARENT_SCOPE)
endfunction()

# Libraries (now only ALL and non secure libraries)
foreach(gRPC_LIB
  ${gRPC_LIBS})
  set(_lib ${gRPC_LIB})
  set(_lib_ gRPC_LIBS_${_lib})
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
  _check_platforms(${_lib_} _lib_platform)
  #message("${_lib_}_platform: ${_lib_platform}")
  if(_lib_platform
    AND (_lib_c OR _lib_cxx)
    AND (_lib_build_all OR _lib_build_protoc))
    set(_lib_type "STATIC")
    if(DEFINED ${_lib_}_dll AND "${${_lib_}_dll}" STREQUAL "yes")
      # Now is not work
      #set(_lib_type "SHARED")
    endif()
    set(_lib_src ${${_lib_}_src})
    set(_lib_public_headers ${${_lib_}_public_headers})
    if(DEFINED ${_lib_}_filegroups)
      foreach(_filegroup ${${_lib_}_filegroups})
        list(APPEND _lib_src ${gRPC_FILEGROUPS_${_filegroup}_src})
        list(APPEND _lib_public_headers ${gRPC_FILEGROUPS_${_filegroup}_public_headers})
      endforeach()
    endif()
    add_library(${_lib} ${_lib_type} ${_lib_src})
    if(MSVC)
      # Check this
      target_compile_options(${_lib} PUBLIC -D_WIN32_WINNT=0x0600)
    endif()
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
      #message("_hdr: ${_hdr}")
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
  set(_target_ gRPC_TARGETS_${_target})
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
  _check_platforms(${_target_} _target_platform)
  #message("${_target_}_platform: ${_target_platform}")
  if(_target_platform
    AND (_target_c OR _target_cxx)
    AND _target_build_protoc)
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
