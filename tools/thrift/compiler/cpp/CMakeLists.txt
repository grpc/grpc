#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements. See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership. The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License. You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied. See the License for the
# specific language governing permissions and limitations
# under the License.
#

# Windows has a different header
if(MSVC)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/windows/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h)
else()
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h)
endif()

find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)

# Create flex and bison files and build the lib parse static library
BISON_TARGET(thrifty ${CMAKE_CURRENT_SOURCE_DIR}/src/thrifty.yy ${CMAKE_CURRENT_BINARY_DIR}/thrifty.cc)
FLEX_TARGET(thriftl ${CMAKE_CURRENT_SOURCE_DIR}/src/thriftl.ll ${CMAKE_CURRENT_BINARY_DIR}/thriftl.cc)
ADD_FLEX_BISON_DEPENDENCY(thriftl thrifty)

# HACK: Work around the fact that bison crates a .hh file but we need a .h file
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/thrifty.h
                   COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/thrifty.hh ${CMAKE_CURRENT_BINARY_DIR}/thrifty.h
                   DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/thrifty.hh
                   )

set(libparse_SOURCES
    ${CMAKE_CURRENT_BINARY_DIR}/thrifty.cc
    ${CMAKE_CURRENT_BINARY_DIR}/thriftl.cc
    ${CMAKE_CURRENT_BINARY_DIR}/thrifty.h
)

add_library(libparse STATIC ${libparse_SOURCES})

# Create the thrift compiler
set( thrift_SOURCES
    src/main.cc
    src/generate/t_generator.cc
    src/generate/t_generator_registry.h
    src/globals.h
    src/main.h
    src/platform.h
    src/audit/t_audit.cpp
    src/parse/t_doc.h
    src/parse/t_type.h
    src/parse/t_base_type.h
    src/parse/t_enum.h
    src/parse/t_enum_value.h
    src/parse/t_typedef.h
    src/parse/t_typedef.cc
    src/parse/t_container.h
    src/parse/t_list.h
    src/parse/t_set.h
    src/parse/t_map.h
    src/parse/t_struct.h
    src/parse/t_field.h
    src/parse/t_service.h
    src/parse/t_function.h
    src/parse/t_program.h
    src/parse/t_scope.h
    src/parse/t_const.h
    src/parse/t_const_value.h
    src/parse/parse.cc
    src/generate/t_generator.h
    src/generate/t_oop_generator.h
    src/generate/t_html_generator.h
    src/windows/config.h
    version.h
)

# This macro adds an option THRIFT_COMPILER_${NAME}
# that allows enabling or disabling certain languages
macro(THRIFT_ADD_COMPILER name description initial)
    string(TOUPPER "THRIFT_COMPILER_${name}" enabler)
    set(src "src/generate/t_${name}_generator.cc")
    option(${enabler} ${description} ${initial})
    if(${enabler})
        list(APPEND thrift_SOURCES ${src})
    endif()
endmacro()

# The following compiler can be enabled or disabled
THRIFT_ADD_COMPILER(c_glib  "Enable compiler for C with Glib" ON)
THRIFT_ADD_COMPILER(cpp     "Enable compiler for C++" ON)
THRIFT_ADD_COMPILER(java    "Enable compiler for Java"   ON)
THRIFT_ADD_COMPILER(as3     "Enable compiler for ActionScript 3" ON)
THRIFT_ADD_COMPILER(dart    "Enable compiler for Dart" ON)
THRIFT_ADD_COMPILER(haxe    "Enable compiler for Haxe" ON)
THRIFT_ADD_COMPILER(csharp  "Enable compiler for C#" ON)
THRIFT_ADD_COMPILER(py      "Enable compiler for Python 2.0" ON)
THRIFT_ADD_COMPILER(rb      "Enable compiler for Ruby" ON)
THRIFT_ADD_COMPILER(perl    "Enable compiler for Perl" ON)
THRIFT_ADD_COMPILER(php     "Enable compiler for PHP" ON)
THRIFT_ADD_COMPILER(erl     "Enable compiler for Erlang" ON)
THRIFT_ADD_COMPILER(cocoa   "Enable compiler for Cocoa Objective-C" ON)
THRIFT_ADD_COMPILER(swift   "Enable compiler for Cocoa Swift" ON)
THRIFT_ADD_COMPILER(st      "Enable compiler for Smalltalk" ON)
THRIFT_ADD_COMPILER(ocaml   "Enable compiler for OCaml" ON)
THRIFT_ADD_COMPILER(hs      "Enable compiler for Haskell" ON)
THRIFT_ADD_COMPILER(xsd     "Enable compiler for XSD" ON)
THRIFT_ADD_COMPILER(html    "Enable compiler for HTML Documentation" ON)
THRIFT_ADD_COMPILER(js      "Enable compiler for JavaScript" ON)
THRIFT_ADD_COMPILER(json    "Enable compiler for JSON" ON)
THRIFT_ADD_COMPILER(javame  "Enable compiler for Java ME" ON)
THRIFT_ADD_COMPILER(delphi  "Enable compiler for Delphi" ON)
THRIFT_ADD_COMPILER(go      "Enable compiler for Go" ON)
THRIFT_ADD_COMPILER(d       "Enable compiler for D" ON)
THRIFT_ADD_COMPILER(lua     "Enable compiler for Lua" ON)
THRIFT_ADD_COMPILER(gv      "Enable compiler for GraphViz" ON)
THRIFT_ADD_COMPILER(xml     "Enable compiler for XML" ON)

# Thrift is looking for include files in the src directory
# we also add the current binary directory for generated files
include_directories(${CMAKE_CURRENT_BINARY_DIR} src)

add_executable(thrift-compiler ${thrift_SOURCES})
set_target_properties(thrift-compiler PROPERTIES OUTPUT_NAME thrift)

target_link_libraries(thrift-compiler libparse)

install(TARGETS thrift-compiler DESTINATION "${BIN_INSTALL_DIR}")
