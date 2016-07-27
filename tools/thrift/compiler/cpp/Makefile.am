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
#
# Contains some contributions under the Thrift Software License.
# Please see doc/old-thrift-license.txt in the Thrift distribution for
# details.

AM_YFLAGS = -d
LIBS =
BUILT_SOURCES = src/thrifty.cc

bin_PROGRAMS = thrift

noinst_LIBRARIES = libparse.a

thrift_OBJDIR = obj

thrift_SOURCES = src/main.cc \
                 src/generate/t_generator.cc \
                 src/generate/t_generator_registry.h \
                 src/globals.h \
                 src/main.h \
                 src/platform.h \
                 src/logging.h \
                 src/audit/t_audit.cpp \
                 src/audit/t_audit.h \
                 src/parse/t_doc.h \
                 src/parse/t_type.h \
                 src/parse/t_base_type.h \
                 src/parse/t_enum.h \
                 src/parse/t_enum_value.h \
                 src/parse/t_typedef.h \
                 src/parse/t_typedef.cc \
                 src/parse/t_container.h \
                 src/parse/t_list.h \
                 src/parse/t_set.h \
                 src/parse/t_map.h \
                 src/parse/t_struct.h \
                 src/parse/t_field.h \
                 src/parse/t_service.h \
                 src/parse/t_function.h \
                 src/parse/t_program.h \
                 src/parse/t_scope.h \
                 src/parse/t_const.h \
                 src/parse/t_const_value.h \
                 src/parse/parse.cc \
                 src/generate/t_generator.h \
                 src/generate/t_oop_generator.h \
                 src/generate/t_html_generator.h \
                 src/windows/config.h \
                 src/windows/version.h

# Specific client generator source
thrift_SOURCES += src/generate/t_c_glib_generator.cc \
                  src/generate/t_cpp_generator.cc \
                  src/generate/t_java_generator.cc \
                  src/generate/t_json_generator.cc \
                  src/generate/t_as3_generator.cc \
                  src/generate/t_dart_generator.cc \
                  src/generate/t_haxe_generator.cc \
                  src/generate/t_csharp_generator.cc \
                  src/generate/t_py_generator.cc \
                  src/generate/t_rb_generator.cc \
                  src/generate/t_perl_generator.cc \
                  src/generate/t_php_generator.cc \
                  src/generate/t_erl_generator.cc \
                  src/generate/t_cocoa_generator.cc \
                  src/generate/t_swift_generator.cc \
                  src/generate/t_st_generator.cc \
                  src/generate/t_ocaml_generator.cc \
                  src/generate/t_hs_generator.cc \
                  src/generate/t_xsd_generator.cc \
                  src/generate/t_xml_generator.cc \
                  src/generate/t_html_generator.cc \
                  src/generate/t_js_generator.cc \
                  src/generate/t_javame_generator.cc \
                  src/generate/t_delphi_generator.cc \
                  src/generate/t_go_generator.cc \
                  src/generate/t_gv_generator.cc \
                  src/generate/t_d_generator.cc \
                  src/generate/t_lua_generator.cc

thrift_CPPFLAGS = -I$(srcdir)/src
thrift_CXXFLAGS = -Wall -Wextra -pedantic
thrift_LDADD = @LEXLIB@ libparse.a

libparse_a_CPPFLAGS = -I$(srcdir)/src
libparse_a_CXXFLAGS = -Wall -Wno-sign-compare -Wno-unused

libparse_a_SOURCES = src/thrifty.yy \
                     src/thriftl.ll

WINDOWS_DIST = \
             compiler.sln \
             compiler.vcxproj \
             compiler.vcxproj.filters

EXTRA_DIST = \
             coding_standards.md \
             README.md \
             CMakeLists.txt \
             $(WINDOWS_DIST)

clean-local:
	$(RM) thriftl.cc thrifty.cc thrifty.h thrifty.hh version.h windows/version.h

src/main.cc: version.h

style-local:
	$(CPPSTYLE_CMD)
