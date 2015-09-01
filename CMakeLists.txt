# Minimum CMake required
cmake_minimum_required(VERSION 2.8)

# CMake policies
cmake_policy(SET CMP0022 NEW)

# Project
project(gRPC C CXX)

# Parse build.json file
function(_parse_build_json)

  # Read file strings
  file(STRINGS "build.json" _strings)

  # Regex
  set(_version_line_regex "^[ \t]*\"([a-z]+)\"[ \t]*:[ \t]*([0-9]+)[ \t]*[,]?[ \t]*$")

  set(_filegroups_regex "^[ \t]*\"filegroups\"[ \t]*:[ \t]*\\[(.*)\\][ \t]*,?[ \t]*$")

  set(_libs_regex "^[ \t]*\"libs\"[ \t]*:[ \t]*\\[(.*)\\][ \t]*,?[ \t]*$")

  set(_targets_regex "^[ \t]*\"targets\"[ \t]*:[ \t]*\\[(.*)\\][ \t]*,?[ \t]*$")

  set(_name_regex "\"name\"[ \t]*:[ \t]*\"([ +a-z_A-Z]+)\"")

  set(_value_regex "\"([ +a-z_A-Z]+)\"[ \t]*:[ \t]*\"?([ +a-z_A-Z]+)\"?")

  set(_file_group_regex "\"([ a-z_A-Z]+)\"[ \t]*:[ \t]*\\[[ \t]*([^]]+)\\]")

  # parsing
  foreach(_string ${_strings})
    #message("_string:\n${_string}")

    # Version
    string(REGEX MATCH "${_version_line_regex}"
      _version_line "${_string}")
    if(_version_line)
      #message("_version_line: ${_version_line}")
      string(REGEX REPLACE "${_version_line_regex}"
        "\\1" _version_name "${_version_line}")
      string(REGEX REPLACE "${_version_line_regex}"
        "\\2" _version_value "${_version_line}")
      string(TOUPPER "${_version_name}" _version_name)
      set(gRPC_VERSION_${_version_name} ${_version_value} PARENT_SCOPE)
    endif()

    # Filegroups
    string(REGEX MATCH "${_filegroups_regex}"
      _filegroups_line "${_string}")
    if(_filegroups_line)
      set(_gRPC_FILEGROUPS)
      #message("_filegroups: ${_filegroups_line}")
      string(REGEX REPLACE "${_filegroups_regex}"
        "\\1" _filegroups "${_filegroups_line}")
      string(REPLACE " " "" _filegroups "${_filegroups}")
      string(REPLACE "\t" "" _filegroups "${_filegroups}")
      string(REPLACE ";" "" _filegroups "${_filegroups}")
      string(REGEX MATCHALL "{([^}]+)}" _filegroups "${_filegroups}")
      #message("_filegroups: ${_filegroups}")
      foreach(_filegroup ${_filegroups})
        #message("_filegroup: ${_filegroup}")
        string(REGEX MATCH "${_name_regex}"
          _filegroup_name "${_filegroup}")
        if(_filegroup_name)
          string(REGEX REPLACE "${_name_regex}"
            "\\1" _filegroup_name "${_filegroup_name}")
          #message("_filegroup_name: ${_filegroup_name}")
          list(APPEND _gRPC_FILEGROUPS ${_filegroup_name})
          # Group
          string(REGEX MATCHALL "${_file_group_regex}"
            _filegroup_groups "${_filegroup}")
          #message("_filegroup_groups: ${_filegroup_groups}")
          foreach(_filegroup_group ${_filegroup_groups})
            #message("_filegroup_group: ${_filegroup_group}")
            string(REGEX REPLACE "${_file_group_regex}"
              "\\1" _filegroup_group_name "${_filegroup_group}")
            string(REGEX REPLACE "${_file_group_regex}"
              "\\2" _filegroup_group_files "${_filegroup_group}")
            string(REPLACE "," ";" _filegroup_group_files "${_filegroup_group_files}")
            string(REPLACE "\"" "" _filegroup_group_files "${_filegroup_group_files}")
            #message("_filegroup_${_filegroup_name}_${_filegroup_group_name}: ${_filegroup_group_files}")
            set(gRPC_FILEGROUP_${_filegroup_name}_${_filegroup_group_name} ${_filegroup_group_files} PARENT_SCOPE)
          endforeach()
        endif()
      endforeach()
      set(gRPC_FILEGROUPS ${_gRPC_FILEGROUPS} PARENT_SCOPE)
    endif()

    # Libraries
    string(REGEX MATCH "${_libs_regex}"
      _libs_line "${_string}")
    if(_libs_line)
      set(_gRPC_LIBS)
      #message("_libs: ${_libs_line}")
      string(REGEX REPLACE "${_libs_regex}"
        "\\1" _libs "${_libs_line}")
      string(REPLACE " " "" _libs "${_libs}")
      string(REPLACE "\t" "" _libs "${_libs}")
      string(REPLACE ";" "" _libs "${_libs}")
      string(REGEX MATCHALL "{([^}]+)}" _libs "${_libs}")
      #message("_libs: ${_libs}")
      foreach(_lib ${_libs})
        #message("_lib: ${_lib}")
        string(REGEX MATCH "${_name_regex}"
          _lib_name "${_lib}")
        if(_lib_name)
          string(REGEX REPLACE "${_name_regex}"
            "\\1" _lib_name "${_lib_name}")
          #message("_lib_name: ${_lib_name}")
          list(APPEND _gRPC_LIBS ${_lib_name})

          # Values
          string(REGEX MATCHALL "${_value_regex}"
            _lib_values "${_lib}")
          #message("_lib_values: ${_lib_values}")
          foreach(_lib_value ${_lib_values})
            string(REGEX REPLACE "${_value_regex}"
              "\\1" _lib_value_name "${_lib_value}")
            string(REGEX REPLACE "${_value_regex}"
              "\\2" _lib_value_value "${_lib_value}")
            #message("_lib_${_lib_name}_${_lib_value_name} : ${_lib_value_value}")
            set(gRPC_LIB_${_lib_name}_${_lib_value_name} "${_lib_value_value}" PARENT_SCOPE)
          endforeach()

          # Groups
          string(REGEX MATCHALL "${_file_group_regex}"
            _lib_groups "${_lib}")
          #message("_lib_groups: ${_lib_groups}")
          foreach(_lib_group ${_lib_groups})
            #message("_lib_group: ${_lib_group}")
            string(REGEX REPLACE "${_file_group_regex}"
              "\\1" _lib_group_name "${_lib_group}")
            string(REGEX REPLACE "${_file_group_regex}"
              "\\2" _lib_group_files "${_lib_group}")
            string(REPLACE "," ";" _lib_group_files "${_lib_group_files}")
            string(REPLACE "\"" "" _lib_group_files "${_lib_group_files}")
            #message("_lib_${_lib_name}_${_lib_group_name}: ${_lib_group_files}")
            set(gRPC_LIB_${_lib_name}_${_lib_group_name} ${_lib_group_files} PARENT_SCOPE)
          endforeach()
        endif()
      endforeach()
      set(gRPC_LIBS ${_gRPC_LIBS} PARENT_SCOPE)
    endif()

    # Targets
    string(REGEX MATCH "${_targets_regex}"
      _targets_line "${_string}")
    if(_targets_line)
      set(_gRPC_TARGETS)
      #message("_targets: ${_targets_line}")
      string(REGEX REPLACE "${_targets_regex}"
        "\\1" _targets "${_targets_line}")
      string(REPLACE " " "" _targets "${_targets}")
      string(REPLACE "\t" "" _targets "${_targets}")
      string(REPLACE ";" "" _targets "${_targets}")
      string(REGEX MATCHALL "{([^}]+)}" _targets "${_targets}")
      #message("_targets: ${_targets}")
      foreach(_target ${_targets})
        #message("_target: ${_target}")
        string(REGEX MATCH "${_name_regex}"
          _target_name "${_target}")
        if(_target_name)
          string(REGEX REPLACE "${_name_regex}"
            "\\1" _target_name "${_target_name}")
          #message("_target_name: ${_target_name}")
          list(APPEND _gRPC_TARGETS ${_target_name})

          # Values
          string(REGEX MATCHALL "${_value_regex}"
            _target_values "${_target}")
          #message("_target_values: ${_target_values}")
          foreach(_target_value ${_target_values})
            string(REGEX REPLACE "${_value_regex}"
              "\\1" _target_value_name "${_target_value}")
            string(REGEX REPLACE "${_value_regex}"
              "\\2" _target_value_value "${_target_value}")
            #message("_target_${_target_name}_${_target_value_name} : ${_target_value_value}")
            set(gRPC_TARGET_${_target_name}_${_target_value_name} "${_target_value_value}" PARENT_SCOPE)
          endforeach()

          # Groups
          string(REGEX MATCHALL "${_file_group_regex}"
            _target_groups "${_target}")
          #message("_target_groups: ${_target_groups}")
          foreach(_target_group ${_target_groups})
            #message("_target_group: ${_target_group}")
            string(REGEX REPLACE "${_file_group_regex}"
              "\\1" _target_group_name "${_target_group}")
            string(REGEX REPLACE "${_file_group_regex}"
              "\\2" _target_group_files "${_target_group}")
            string(REPLACE "," ";" _target_group_files "${_target_group_files}")
            string(REPLACE "\"" "" _target_group_files "${_target_group_files}")
            #message("_target_${_target_name}_${_target_group_name}: ${_target_group_files}")
            set(gRPC_TARGET_${_target_name}_${_target_group_name} ${_target_group_files} PARENT_SCOPE)
          endforeach()
        endif()
      endforeach()
      set(gRPC_TARGETS ${_gRPC_TARGETS} PARENT_SCOPE)
    endif()

  endforeach()
endfunction()

_parse_build_json()

set(gRPC_VERSION
  "${gRPC_VERSION_MAJOR}.${gRPC_VERSION_MINOR}.${gRPC_VERSION_MICRO}.${gRPC_VERSION_BUILD}")

# Remove zookeeper (now not supported)
list(REMOVE_ITEM gRPC_LIBS grpc_zookeeper)
list(REMOVE_ITEM gRPC_TARGETS zookeeper_test)

# Statistics
message(STATUS "Project structure from build.json {")
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
  if("${${_lib_}_secure}" STREQUAL "yes")
    set(_lib_secure_yes TRUE)
  else()
    set(_lib_secure_yes FALSE)
  endif()
  if("${${_lib_}_secure}" STREQUAL "no")
    set(_lib_secure_no TRUE)
  else()
    set(_lib_secure_no FALSE)
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
    message(STATUS "build lib: ${_lib}")
    set(_lib_type "STATIC")
    if(DEFINED ${_lib_}_dll AND "${${_lib_}_dll}" STREQUAL "yes")
      # Now is not work
      #set(_lib_type "SHARED")
      #message(STATUS "${_lib} : dll!")
    endif()
    set(_lib_src ${${_lib_}_src})
    set(_lib_public_headers ${${_lib_}_public_headers})
    if(DEFINED ${_lib_}_filegroups)
      #message(STATUS "${_lib} filegroups: ${${_lib_}_filegroups}")
      foreach(_filegroup ${${_lib_}_filegroups})
        list(APPEND _lib_src ${gRPC_FILEGROUP_${_filegroup}_src})
        list(APPEND _lib_public_headers ${gRPC_FILEGROUP_${_filegroup}_public_headers})
      endforeach()
    endif()
    #message(STATUS "srcs: ${_lib_src}")
    #message(STATUS "hdrs: ${_lib_public_headers}")
    add_library(${_lib} ${_lib_type} ${_lib_src})
    if(DEFINED ${_lib_}_deps)
      #message(STATUS "${_lib} deps: ${${_lib_}_deps}")
      target_link_libraries(${_lib} PUBLIC ${${_lib_}_deps})
    endif()
    if(_lib_build_protoc)
      target_link_libraries(${_lib} PUBLIC ${PROTOBUF_LIBRARIES})
    endif()
    if(_lib_secure_yes OR (_lib_secure_check AND OPENSSL_FOUND))
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
  if(_target_build_protoc
    AND (_target_c OR _target_cxx)
    AND "${${_target_}_secure}" STREQUAL "no")
    set(_target_src ${${_target_}_src})
    #message(STATUS "src: ${_target_src}")
    add_executable(${_target} ${_target_src})
    if(DEFINED ${_target_}_deps)
      #message(STATUS "${_target} deps: ${${_target_}_deps}")
      target_link_libraries(${_target} PRIVATE ${${_target_}_deps})
    endif()
    if(_target_build_protoc)
      target_link_libraries(${_target} PRIVATE ${PROTOBUF_LIBRARIES} ${PROTOBUF_PROTOC_LIBRARIES})
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
