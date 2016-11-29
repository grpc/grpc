# GRPC global makefile
# This currently builds C and C++ code.
# This file has been automatically generated from a template file.
# Please look at the templates directory instead.
# This file can be regenerated from the template by running
# tools/buildgen/generate_projects.sh

# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.



comma := ,


# Basic platform detection
HOST_SYSTEM = $(shell uname | cut -f 1 -d_)
SYSTEM ?= $(HOST_SYSTEM)
ifeq ($(SYSTEM),MSYS)
SYSTEM = MINGW32
endif
ifeq ($(SYSTEM),MINGW64)
SYSTEM = MINGW32
endif


MAKEFILE_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
ifndef BUILDDIR
BUILDDIR_ABSOLUTE = $(patsubst %/,%,$(dir $(MAKEFILE_PATH)))
else
BUILDDIR_ABSOLUTE = $(abspath $(BUILDDIR))
endif

HAS_GCC = $(shell which gcc > /dev/null 2> /dev/null && echo true || echo false)
HAS_CC = $(shell which cc > /dev/null 2> /dev/null && echo true || echo false)
HAS_CLANG = $(shell which clang > /dev/null 2> /dev/null && echo true || echo false)

ifeq ($(HAS_CC),true)
DEFAULT_CC = cc
DEFAULT_CXX = c++
else
ifeq ($(HAS_GCC),true)
DEFAULT_CC = gcc
DEFAULT_CXX = g++
else
ifeq ($(HAS_CLANG),true)
DEFAULT_CC = clang
DEFAULT_CXX = clang++
else
DEFAULT_CC = no_c_compiler
DEFAULT_CXX = no_c++_compiler
endif
endif
endif


BINDIR = $(BUILDDIR_ABSOLUTE)/bins
OBJDIR = $(BUILDDIR_ABSOLUTE)/objs
LIBDIR = $(BUILDDIR_ABSOLUTE)/libs
GENDIR = $(BUILDDIR_ABSOLUTE)/gens

# Configurations

VALID_CONFIG_opt = 1
CC_opt = $(DEFAULT_CC)
CXX_opt = $(DEFAULT_CXX)
LD_opt = $(DEFAULT_CC)
LDXX_opt = $(DEFAULT_CXX)
CPPFLAGS_opt = -O2
DEFINES_opt = NDEBUG

VALID_CONFIG_asan-trace-cmp = 1
REQUIRE_CUSTOM_LIBRARIES_asan-trace-cmp = 1
CC_asan-trace-cmp = clang
CXX_asan-trace-cmp = clang++
LD_asan-trace-cmp = clang
LDXX_asan-trace-cmp = clang++
CPPFLAGS_asan-trace-cmp = -O0 -fsanitize-coverage=edge -fsanitize-coverage=trace-cmp -fsanitize=address -fno-omit-frame-pointer -Wno-unused-command-line-argument -DGPR_NO_DIRECT_SYSCALLS
LDFLAGS_asan-trace-cmp = -fsanitize=address
DEFINES_asan-trace-cmp += GRPC_TEST_SLOWDOWN_BUILD_FACTOR=3

VALID_CONFIG_dbg = 1
CC_dbg = $(DEFAULT_CC)
CXX_dbg = $(DEFAULT_CXX)
LD_dbg = $(DEFAULT_CC)
LDXX_dbg = $(DEFAULT_CXX)
CPPFLAGS_dbg = -O0
DEFINES_dbg = _DEBUG DEBUG

VALID_CONFIG_easan = 1
REQUIRE_CUSTOM_LIBRARIES_easan = 1
CC_easan = clang
CXX_easan = clang++
LD_easan = clang
LDXX_easan = clang++
CPPFLAGS_easan = -O0 -fsanitize-coverage=edge -fsanitize=address -fno-omit-frame-pointer -Wno-unused-command-line-argument -DGPR_NO_DIRECT_SYSCALLS
LDFLAGS_easan = -fsanitize=address
DEFINES_easan = _DEBUG DEBUG GRPC_EXECUTION_CONTEXT_SANITIZER
DEFINES_easan += GRPC_TEST_SLOWDOWN_BUILD_FACTOR=3

VALID_CONFIG_asan = 1
REQUIRE_CUSTOM_LIBRARIES_asan = 1
CC_asan = clang
CXX_asan = clang++
LD_asan = clang
LDXX_asan = clang++
CPPFLAGS_asan = -O0 -fsanitize-coverage=edge -fsanitize=address -fno-omit-frame-pointer -Wno-unused-command-line-argument -DGPR_NO_DIRECT_SYSCALLS
LDFLAGS_asan = -fsanitize=address
DEFINES_asan += GRPC_TEST_SLOWDOWN_BUILD_FACTOR=3

VALID_CONFIG_msan = 1
REQUIRE_CUSTOM_LIBRARIES_msan = 1
CC_msan = clang
CXX_msan = clang++
LD_msan = clang
LDXX_msan = clang++
CPPFLAGS_msan = -O0 -fsanitize-coverage=edge -fsanitize=memory -fsanitize-memory-track-origins -fno-omit-frame-pointer -DGTEST_HAS_TR1_TUPLE=0 -DGTEST_USE_OWN_TR1_TUPLE=1 -Wno-unused-command-line-argument -fPIE -pie -DGPR_NO_DIRECT_SYSCALLS
LDFLAGS_msan = -fsanitize=memory -DGTEST_HAS_TR1_TUPLE=0 -DGTEST_USE_OWN_TR1_TUPLE=1 -fPIE -pie $(if $(JENKINS_BUILD),-Wl$(comma)-Ttext-segment=0x7e0000000000,)
DEFINES_msan = NDEBUG
DEFINES_msan += GRPC_TEST_SLOWDOWN_BUILD_FACTOR=4

VALID_CONFIG_basicprof = 1
CC_basicprof = $(DEFAULT_CC)
CXX_basicprof = $(DEFAULT_CXX)
LD_basicprof = $(DEFAULT_CC)
LDXX_basicprof = $(DEFAULT_CXX)
CPPFLAGS_basicprof = -O2 -DGRPC_BASIC_PROFILER -DGRPC_TIMERS_RDTSC
DEFINES_basicprof = NDEBUG

VALID_CONFIG_helgrind = 1
CC_helgrind = $(DEFAULT_CC)
CXX_helgrind = $(DEFAULT_CXX)
LD_helgrind = $(DEFAULT_CC)
LDXX_helgrind = $(DEFAULT_CXX)
CPPFLAGS_helgrind = -O0
LDFLAGS_helgrind = -rdynamic
DEFINES_helgrind = _DEBUG DEBUG
DEFINES_helgrind += GRPC_TEST_SLOWDOWN_BUILD_FACTOR=20

VALID_CONFIG_asan-noleaks = 1
REQUIRE_CUSTOM_LIBRARIES_asan-noleaks = 1
CC_asan-noleaks = clang
CXX_asan-noleaks = clang++
LD_asan-noleaks = clang
LDXX_asan-noleaks = clang++
CPPFLAGS_asan-noleaks = -O0 -fsanitize-coverage=edge -fsanitize=address -fno-omit-frame-pointer -Wno-unused-command-line-argument -DGPR_NO_DIRECT_SYSCALLS
LDFLAGS_asan-noleaks = -fsanitize=address
DEFINES_asan-noleaks += GRPC_TEST_SLOWDOWN_BUILD_FACTOR=3

VALID_CONFIG_edbg = 1
CC_edbg = $(DEFAULT_CC)
CXX_edbg = $(DEFAULT_CXX)
LD_edbg = $(DEFAULT_CC)
LDXX_edbg = $(DEFAULT_CXX)
CPPFLAGS_edbg = -O0
DEFINES_edbg = _DEBUG DEBUG GRPC_EXECUTION_CONTEXT_SANITIZER

VALID_CONFIG_ubsan = 1
REQUIRE_CUSTOM_LIBRARIES_ubsan = 1
CC_ubsan = clang
CXX_ubsan = clang++
LD_ubsan = clang
LDXX_ubsan = clang++
CPPFLAGS_ubsan = -O0 -fsanitize-coverage=edge -fsanitize=undefined,unsigned-integer-overflow -fno-omit-frame-pointer -Wno-unused-command-line-argument -Wvarargs
LDFLAGS_ubsan = -fsanitize=undefined,unsigned-integer-overflow
DEFINES_ubsan = NDEBUG
DEFINES_ubsan += GRPC_TEST_SLOWDOWN_BUILD_FACTOR=1.5

VALID_CONFIG_tsan = 1
REQUIRE_CUSTOM_LIBRARIES_tsan = 1
CC_tsan = clang
CXX_tsan = clang++
LD_tsan = clang
LDXX_tsan = clang++
CPPFLAGS_tsan = -O0 -fsanitize=thread -fno-omit-frame-pointer -Wno-unused-command-line-argument -DGPR_NO_DIRECT_SYSCALLS
LDFLAGS_tsan = -fsanitize=thread
DEFINES_tsan = GRPC_TSAN
DEFINES_tsan += GRPC_TEST_SLOWDOWN_BUILD_FACTOR=5

VALID_CONFIG_stapprof = 1
CC_stapprof = $(DEFAULT_CC)
CXX_stapprof = $(DEFAULT_CXX)
LD_stapprof = $(DEFAULT_CC)
LDXX_stapprof = $(DEFAULT_CXX)
CPPFLAGS_stapprof = -O2 -DGRPC_STAP_PROFILER
DEFINES_stapprof = NDEBUG

VALID_CONFIG_mutrace = 1
CC_mutrace = $(DEFAULT_CC)
CXX_mutrace = $(DEFAULT_CXX)
LD_mutrace = $(DEFAULT_CC)
LDXX_mutrace = $(DEFAULT_CXX)
CPPFLAGS_mutrace = -O3 -fno-omit-frame-pointer
LDFLAGS_mutrace = -rdynamic
DEFINES_mutrace = NDEBUG

VALID_CONFIG_memcheck = 1
CC_memcheck = $(DEFAULT_CC)
CXX_memcheck = $(DEFAULT_CXX)
LD_memcheck = $(DEFAULT_CC)
LDXX_memcheck = $(DEFAULT_CXX)
CPPFLAGS_memcheck = -O0
LDFLAGS_memcheck = -rdynamic
DEFINES_memcheck = _DEBUG DEBUG
DEFINES_memcheck += GRPC_TEST_SLOWDOWN_BUILD_FACTOR=10

VALID_CONFIG_etsan = 1
REQUIRE_CUSTOM_LIBRARIES_etsan = 1
CC_etsan = clang
CXX_etsan = clang++
LD_etsan = clang
LDXX_etsan = clang++
CPPFLAGS_etsan = -O0 -fsanitize=thread -fno-omit-frame-pointer -Wno-unused-command-line-argument -DGPR_NO_DIRECT_SYSCALLS
LDFLAGS_etsan = -fsanitize=thread
DEFINES_etsan = _DEBUG DEBUG GRPC_EXECUTION_CONTEXT_SANITIZER
DEFINES_etsan += GRPC_TEST_SLOWDOWN_BUILD_FACTOR=5

VALID_CONFIG_gcov = 1
CC_gcov = gcc
CXX_gcov = g++
LD_gcov = gcc
LDXX_gcov = g++
CPPFLAGS_gcov = -O0 -fprofile-arcs -ftest-coverage -Wno-return-type
LDFLAGS_gcov = -fprofile-arcs -ftest-coverage -rdynamic
DEFINES_gcov = _DEBUG DEBUG GPR_GCOV



# General settings.
# You may want to change these depending on your system.

prefix ?= /usr/local

PROTOC ?= protoc
DTRACE ?= dtrace
CONFIG ?= opt
# Doing X ?= Y is the same as:
# ifeq ($(origin X), undefined)
#  X = Y
# endif
# but some variables, such as CC, CXX, LD or AR, have defaults.
# So instead of using ?= on them, we need to check their origin.
# See:
#  https://www.gnu.org/software/make/manual/html_node/Implicit-Variables.html
#  https://www.gnu.org/software/make/manual/html_node/Flavors.html#index-_003f_003d
#  https://www.gnu.org/software/make/manual/html_node/Origin-Function.html
ifeq ($(origin CC), default)
CC = $(CC_$(CONFIG))
endif
ifeq ($(origin CXX), default)
CXX = $(CXX_$(CONFIG))
endif
ifeq ($(origin LD), default)
LD = $(LD_$(CONFIG))
endif
LDXX ?= $(LDXX_$(CONFIG))
ifeq ($(SYSTEM),Linux)
ifeq ($(origin AR), default)
AR = ar rcs
endif
STRIP ?= strip --strip-unneeded
else
ifeq ($(SYSTEM),Darwin)
ifeq ($(origin AR), default)
AR = libtool -no_warning_for_no_symbols -o
endif
STRIP ?= strip -x
else
ifeq ($(origin AR), default)
AR = ar rcs
endif
STRIP ?= strip
endif
endif
INSTALL ?= install
RM ?= rm -f
PKG_CONFIG ?= pkg-config

ifndef VALID_CONFIG_$(CONFIG)
$(error Invalid CONFIG value '$(CONFIG)')
endif

ifeq ($(SYSTEM),Linux)
TMPOUT = /dev/null
else
TMPOUT = `mktemp /tmp/test-out-XXXXXX`
endif

# Detect if we can use C++11
CXX11_CHECK_CMD = $(CXX) -std=c++11 -o $(TMPOUT) -c test/build/c++11.cc
HAS_CXX11 = $(shell $(CXX11_CHECK_CMD) 2> /dev/null && echo true || echo false)

CHECK_SHADOW_WORKS_CMD = $(CC) -std=c99 -Werror -Wshadow -o $(TMPOUT) -c test/build/shadow.c
HAS_WORKING_SHADOW = $(shell $(CHECK_SHADOW_WORKS_CMD) 2> /dev/null && echo true || echo false)
ifeq ($(HAS_WORKING_SHADOW),true)
W_SHADOW=-Wshadow
NO_W_SHADOW=-Wno-shadow
endif
CHECK_EXTRA_SEMI_WORKS_CMD = $(CC) -std=c99 -Werror -Wextra-semi -o $(TMPOUT) -c test/build/extra-semi.c
HAS_WORKING_EXTRA_SEMI = $(shell $(CHECK_EXTRA_SEMI_WORKS_CMD) 2> /dev/null && echo true || echo false)
ifeq ($(HAS_WORKING_EXTRA_SEMI),true)
W_EXTRA_SEMI=-Wextra-semi
NO_W_EXTRA_SEMI=-Wno-extra-semi
endif
CHECK_NO_SHIFT_NEGATIVE_VALUE_WORKS_CMD = $(CC) -std=c99 -Werror -Wno-shift-negative-value -o $(TMPOUT) -c test/build/no-shift-negative-value.c
HAS_WORKING_NO_SHIFT_NEGATIVE_VALUE = $(shell $(CHECK_NO_SHIFT_NEGATIVE_VALUE_WORKS_CMD) 2> /dev/null && echo true || echo false)
ifeq ($(HAS_WORKING_NO_SHIFT_NEGATIVE_VALUE),true)
W_NO_SHIFT_NEGATIVE_VALUE=-Wno-shift-negative-value
NO_W_NO_SHIFT_NEGATIVE_VALUE=-Wshift-negative-value
endif

# The HOST compiler settings are used to compile the protoc plugins.
# In most cases, you won't have to change anything, but if you are
# cross-compiling, you can override these variables from GNU make's
# command line: make CC=cross-gcc HOST_CC=gcc

HOST_CC ?= $(CC)
HOST_CXX ?= $(CXX)
HOST_LD ?= $(LD)
HOST_LDXX ?= $(LDXX)

CFLAGS += -std=c99 -Wsign-conversion -Wconversion $(W_SHADOW) $(W_EXTRA_SEMI)
ifeq ($(HAS_CXX11),true)
CXXFLAGS += -std=c++11
else
CXXFLAGS += -std=c++0x
endif
CPPFLAGS += -g -Wall -Wextra -Werror -Wno-long-long -Wno-unused-parameter
LDFLAGS += -g

CPPFLAGS += $(CPPFLAGS_$(CONFIG))
CFLAGS += $(CFLAGS_$(CONFIG))
CXXFLAGS += $(CXXFLAGS_$(CONFIG))
DEFINES += $(DEFINES_$(CONFIG)) INSTALL_PREFIX=\"$(prefix)\"
LDFLAGS += $(LDFLAGS_$(CONFIG))

ifneq ($(SYSTEM),MINGW32)
PIC_CPPFLAGS = -fPIC
CPPFLAGS += -fPIC
LDFLAGS += -fPIC
endif

INCLUDES = . include $(GENDIR)
LDFLAGS += -Llibs/$(CONFIG)

ifeq ($(SYSTEM),Darwin)
ifneq ($(wildcard /usr/local/ssl/include),)
INCLUDES += /usr/local/ssl/include
endif
ifneq ($(wildcard /opt/local/include),)
INCLUDES += /opt/local/include
endif
ifneq ($(wildcard /usr/local/include),)
INCLUDES += /usr/local/include
endif
LIBS = m z
ifneq ($(wildcard /usr/local/ssl/lib),)
LDFLAGS += -L/usr/local/ssl/lib
endif
ifneq ($(wildcard /opt/local/lib),)
LDFLAGS += -L/opt/local/lib
endif
ifneq ($(wildcard /usr/local/lib),)
LDFLAGS += -L/usr/local/lib
endif
endif

ifeq ($(SYSTEM),Linux)
LIBS = dl rt m pthread
LDFLAGS += -pthread
endif

ifeq ($(SYSTEM),MINGW32)
LIBS = m pthread ws2_32
LDFLAGS += -pthread
endif

#
# The steps for cross-compiling are as follows:
# First, clone and make install of grpc using the native compilers for the host.
# Also, install protoc (e.g., from a package like apt-get)
# Then clone a fresh grpc for the actual cross-compiled build
# Set the environment variable GRPC_CROSS_COMPILE to true
# Set CC, CXX, LD, LDXX, AR, and STRIP to the cross-compiling binaries
# Also set PROTOBUF_CONFIG_OPTS to indicate cross-compilation to protobuf (e.g.,
#  PROTOBUF_CONFIG_OPTS="--host=arm-linux --with-protoc=/usr/local/bin/protoc" )
# Set HAS_PKG_CONFIG=false
# To build tests, go to third_party/gflags and follow its ccmake instructions
# Make sure that you enable building shared libraries and set your prefix to
# something useful like /usr/local/cross
# You will also need to set GRPC_CROSS_LDOPTS and GRPC_CROSS_AROPTS to hold
# additional required arguments for LD and AR (examples below)
# Then you can do a make from the cross-compiling fresh clone!
#
ifeq ($(GRPC_CROSS_COMPILE),true)
LDFLAGS += $(GRPC_CROSS_LDOPTS) # e.g. -L/usr/local/lib -L/usr/local/cross/lib
AROPTS = $(GRPC_CROSS_AROPTS) # e.g., rc --target=elf32-little
USE_BUILT_PROTOC = false
endif

GTEST_LIB = -Ithird_party/googletest/include -Ithird_party/googletest third_party/googletest/src/gtest-all.cc
GTEST_LIB += -lgflags
ifeq ($(V),1)
E = @:
Q =
else
E = @echo
Q = @
endif

CORE_VERSION = 2.0.0-dev
CPP_VERSION = 1.1.0-dev
CSHARP_VERSION = 1.1.0-dev

CPPFLAGS_NO_ARCH += $(addprefix -I, $(INCLUDES)) $(addprefix -D, $(DEFINES))
CPPFLAGS += $(CPPFLAGS_NO_ARCH) $(ARCH_FLAGS)

LDFLAGS += $(ARCH_FLAGS)
LDLIBS += $(addprefix -l, $(LIBS))
LDLIBSXX += $(addprefix -l, $(LIBSXX))


CFLAGS += $(EXTRA_CFLAGS)
CXXFLAGS += $(EXTRA_CXXFLAGS)
CPPFLAGS += $(EXTRA_CPPFLAGS)
LDFLAGS += $(EXTRA_LDFLAGS)
DEFINES += $(EXTRA_DEFINES)
LDLIBS += $(EXTRA_LDLIBS)

HOST_CPPFLAGS = $(CPPFLAGS)
HOST_CFLAGS = $(CFLAGS)
HOST_CXXFLAGS = $(CXXFLAGS)
HOST_LDFLAGS = $(LDFLAGS)
HOST_LDLIBS = $(LDLIBS)

# These are automatically computed variables.
# There shouldn't be any need to change anything from now on.

-include cache.mk

CACHE_MK =

HAS_PKG_CONFIG ?= $(shell command -v $(PKG_CONFIG) >/dev/null 2>&1 && echo true || echo false)

ifeq ($(HAS_PKG_CONFIG), true)
CACHE_MK += HAS_PKG_CONFIG = true,
endif

CORE_PC_TEMPLATE = prefix=$(prefix),exec_prefix=\$${prefix},includedir=\$${prefix}/include,libdir=\$${exec_prefix}/lib,,Name: $(PC_NAME),Description: $(PC_DESCRIPTION),Version: $(CORE_VERSION),Cflags: -I\$${includedir} $(PC_CFLAGS),Requires.private: $(PC_REQUIRES_PRIVATE),Libs: -L\$${libdir} $(PC_LIB),Libs.private: $(PC_LIBS_PRIVATE)

CPP_PC_TEMPLATE = prefix=$(prefix),exec_prefix=\$${prefix},includedir=\$${prefix}/include,libdir=\$${exec_prefix}/lib,,Name: $(PC_NAME),Description: $(PC_DESCRIPTION),Version: $(CPP_VERSION),Cflags: -I\$${includedir} $(PC_CFLAGS),Requires.private: $(PC_REQUIRES_PRIVATE),Libs: -L\$${libdir} $(PC_LIB),Libs.private: $(PC_LIBS_PRIVATE)

CSHARP_PC_TEMPLATE = prefix=$(prefix),exec_prefix=\$${prefix},includedir=\$${prefix}/include,libdir=\$${exec_prefix}/lib,,Name: $(PC_NAME),Description: $(PC_DESCRIPTION),Version: $(CSHARP_VERSION),Cflags: -I\$${includedir} $(PC_CFLAGS),Requires.private: $(PC_REQUIRES_PRIVATE),Libs: -L\$${libdir} $(PC_LIB),Libs.private: $(PC_LIBS_PRIVATE)

ifeq ($(SYSTEM),MINGW32)
SHARED_EXT_CORE = dll
SHARED_EXT_CPP = dll
SHARED_EXT_CSHARP = dll
SHARED_PREFIX =
SHARED_VERSION_CORE = -2
SHARED_VERSION_CPP = -1
SHARED_VERSION_CSHARP = -1
else ifeq ($(SYSTEM),Darwin)
SHARED_EXT_CORE = dylib
SHARED_EXT_CPP = dylib
SHARED_EXT_CSHARP = dylib
SHARED_PREFIX = lib
SHARED_VERSION_CORE =
SHARED_VERSION_CPP =
SHARED_VERSION_CSHARP =
else
SHARED_EXT_CORE = so.$(CORE_VERSION)
SHARED_EXT_CPP = so.$(CPP_VERSION)
SHARED_EXT_CSHARP = so.$(CSHARP_VERSION)
SHARED_PREFIX = lib
SHARED_VERSION_CORE =
SHARED_VERSION_CPP =
SHARED_VERSION_CSHARP =
endif

ifeq ($(wildcard .git),)
IS_GIT_FOLDER = false
else
IS_GIT_FOLDER = true
endif

ifeq ($(HAS_PKG_CONFIG),true)
OPENSSL_ALPN_CHECK_CMD = $(PKG_CONFIG) --atleast-version=1.0.2 openssl
OPENSSL_NPN_CHECK_CMD = $(PKG_CONFIG) --atleast-version=1.0.1 openssl
ZLIB_CHECK_CMD = $(PKG_CONFIG) --exists zlib
PROTOBUF_CHECK_CMD = $(PKG_CONFIG) --atleast-version=3.0.0 protobuf
else # HAS_PKG_CONFIG

ifeq ($(SYSTEM),MINGW32)
OPENSSL_LIBS = ssl32 eay32
else
OPENSSL_LIBS = ssl crypto
endif

OPENSSL_ALPN_CHECK_CMD = $(CC) $(CPPFLAGS) $(CFLAGS) -o $(TMPOUT) test/build/openssl-alpn.c $(addprefix -l, $(OPENSSL_LIBS)) $(LDFLAGS)
OPENSSL_NPN_CHECK_CMD = $(CC) $(CPPFLAGS) $(CFLAGS) -o $(TMPOUT) test/build/openssl-npn.c $(addprefix -l, $(OPENSSL_LIBS)) $(LDFLAGS)
BORINGSSL_COMPILE_CHECK_CMD = $(CC) $(CPPFLAGS) -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI) -o $(TMPOUT) test/build/boringssl.c $(LDFLAGS)
ZLIB_CHECK_CMD = $(CC) $(CPPFLAGS) $(CFLAGS) -o $(TMPOUT) test/build/zlib.c -lz $(LDFLAGS)
PROTOBUF_CHECK_CMD = $(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $(TMPOUT) test/build/protobuf.cc -lprotobuf $(LDFLAGS)

endif # HAS_PKG_CONFIG

PERFTOOLS_CHECK_CMD = $(CC) $(CPPFLAGS) $(CFLAGS) -o $(TMPOUT) test/build/perftools.c -lprofiler $(LDFLAGS)

PROTOC_CHECK_CMD = which protoc > /dev/null
PROTOC_CHECK_VERSION_CMD = protoc --version | grep -q libprotoc.3
DTRACE_CHECK_CMD = which dtrace > /dev/null
SYSTEMTAP_HEADERS_CHECK_CMD = $(CC) $(CPPFLAGS) $(CFLAGS) -o $(TMPOUT) test/build/systemtap.c $(LDFLAGS)

ifndef REQUIRE_CUSTOM_LIBRARIES_$(CONFIG)
HAS_SYSTEM_PERFTOOLS ?= $(shell $(PERFTOOLS_CHECK_CMD) 2> /dev/null && echo true || echo false)
ifeq ($(HAS_SYSTEM_PERFTOOLS),true)
DEFINES += GRPC_HAVE_PERFTOOLS
LIBS += profiler
CACHE_MK += HAS_SYSTEM_PERFTOOLS = true,
endif
endif

HAS_SYSTEM_PROTOBUF_VERIFY = $(shell $(PROTOBUF_CHECK_CMD) 2> /dev/null && echo true || echo false)
ifndef REQUIRE_CUSTOM_LIBRARIES_$(CONFIG)
HAS_SYSTEM_OPENSSL_ALPN ?= $(shell $(OPENSSL_ALPN_CHECK_CMD) 2> /dev/null && echo true || echo false)
ifeq ($(HAS_SYSTEM_OPENSSL_ALPN),true)
HAS_SYSTEM_OPENSSL_NPN = true
CACHE_MK += HAS_SYSTEM_OPENSSL_ALPN = true,
else
HAS_SYSTEM_OPENSSL_NPN ?= $(shell $(OPENSSL_NPN_CHECK_CMD) 2> /dev/null && echo true || echo false)
endif
ifeq ($(HAS_SYSTEM_OPENSSL_NPN),true)
CACHE_MK += HAS_SYSTEM_OPENSSL_NPN = true,
endif
HAS_SYSTEM_ZLIB ?= $(shell $(ZLIB_CHECK_CMD) 2> /dev/null && echo true || echo false)
ifeq ($(HAS_SYSTEM_ZLIB),true)
CACHE_MK += HAS_SYSTEM_ZLIB = true,
endif
HAS_SYSTEM_PROTOBUF ?= $(HAS_SYSTEM_PROTOBUF_VERIFY)
ifeq ($(HAS_SYSTEM_PROTOBUF),true)
CACHE_MK += HAS_SYSTEM_PROTOBUF = true,
endif
else
# override system libraries if the config requires a custom compiled library
HAS_SYSTEM_OPENSSL_ALPN = false
HAS_SYSTEM_OPENSSL_NPN = false
HAS_SYSTEM_ZLIB = false
HAS_SYSTEM_PROTOBUF = false
endif

HAS_PROTOC ?= $(shell $(PROTOC_CHECK_CMD) 2> /dev/null && echo true || echo false)
ifeq ($(HAS_PROTOC),true)
CACHE_MK += HAS_PROTOC = true,
HAS_VALID_PROTOC ?= $(shell $(PROTOC_CHECK_VERSION_CMD) 2> /dev/null && echo true || echo false)
ifeq ($(HAS_VALID_PROTOC),true)
CACHE_MK += HAS_VALID_PROTOC = true,
endif
else
HAS_VALID_PROTOC = false
endif

# Check for Systemtap (https://sourceware.org/systemtap/), first by making sure <sys/sdt.h> is present
# in the system and secondly by checking for the "dtrace" binary (on Linux, this is part of the Systemtap
# distribution. It's part of the base system on BSD/Solaris machines).
ifndef HAS_SYSTEMTAP
HAS_SYSTEMTAP_HEADERS = $(shell $(SYSTEMTAP_HEADERS_CHECK_CMD) 2> /dev/null && echo true || echo false)
HAS_DTRACE = $(shell $(DTRACE_CHECK_CMD) 2> /dev/null && echo true || echo false)
HAS_SYSTEMTAP = false
ifeq ($(HAS_SYSTEMTAP_HEADERS),true)
ifeq ($(HAS_DTRACE),true)
HAS_SYSTEMTAP = true
endif
endif
endif

ifeq ($(HAS_SYSTEMTAP),true)
CACHE_MK += HAS_SYSTEMTAP = true,
endif

# Note that for testing purposes, one can do:
#   make HAS_EMBEDDED_OPENSSL_ALPN=false
# to emulate the fact we do not have OpenSSL in the third_party folder.
ifneq ($(wildcard third_party/openssl-1.0.2f/libssl.a),)
HAS_EMBEDDED_OPENSSL_ALPN = third_party/openssl-1.0.2f
else ifeq ($(wildcard third_party/boringssl/include/openssl/ssl.h),)
HAS_EMBEDDED_OPENSSL_ALPN = false
else
CAN_COMPILE_EMBEDDED_OPENSSL ?= $(shell $(BORINGSSL_COMPILE_CHECK_CMD) 2> /dev/null && echo true || echo false)
HAS_EMBEDDED_OPENSSL_ALPN = $(CAN_COMPILE_EMBEDDED_OPENSSL)
endif

ifeq ($(wildcard third_party/zlib/zlib.h),)
HAS_EMBEDDED_ZLIB = false
else
HAS_EMBEDDED_ZLIB = true
endif

ifeq ($(wildcard third_party/protobuf/src/google/protobuf/descriptor.pb.h),)
HAS_EMBEDDED_PROTOBUF = false
ifneq ($(HAS_VALID_PROTOC),true)
NO_PROTOC = true
endif
else
HAS_EMBEDDED_PROTOBUF = true
endif

PC_REQUIRES_GRPC =
PC_LIBS_GRPC =

ifeq ($(HAS_SYSTEM_ZLIB),false)
ifeq ($(HAS_EMBEDDED_ZLIB), true)
EMBED_ZLIB ?= true
else
DEP_MISSING += zlib
EMBED_ZLIB ?= broken
endif
else
EMBED_ZLIB ?= false
endif

ifeq ($(EMBED_ZLIB),true)
ZLIB_DEP = $(LIBDIR)/$(CONFIG)/libz.a
ZLIB_MERGE_LIBS = $(LIBDIR)/$(CONFIG)/libz.a
ZLIB_MERGE_OBJS = $(LIBZ_OBJS)
CPPFLAGS += -Ithird_party/zlib
LDFLAGS += -L$(LIBDIR)/$(CONFIG)/zlib
else
ifeq ($(HAS_PKG_CONFIG),true)
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags zlib)
LDFLAGS += $(shell $(PKG_CONFIG) --libs-only-L zlib)
LIBS += $(patsubst -l%,%,$(shell $(PKG_CONFIG) --libs-only-l zlib))
PC_REQUIRES_GRPC += zlib
else
PC_LIBS_GRPC += -lz
LIBS += z
endif
endif

OPENSSL_PKG_CONFIG = false

PC_REQUIRES_SECURE =
PC_LIBS_SECURE =

ifeq ($(HAS_SYSTEM_OPENSSL_ALPN),true)
EMBED_OPENSSL ?= false
NO_SECURE ?= false
else # HAS_SYSTEM_OPENSSL_ALPN=false
ifneq ($(HAS_EMBEDDED_OPENSSL_ALPN),false)
EMBED_OPENSSL ?= $(HAS_EMBEDDED_OPENSSL_ALPN)
NO_SECURE ?= false
else # HAS_EMBEDDED_OPENSSL_ALPN=false
ifeq ($(HAS_SYSTEM_OPENSSL_NPN),true)
EMBED_OPENSSL ?= false
NO_SECURE ?= false
else
NO_SECURE ?= true
endif # HAS_SYSTEM_OPENSSL_NPN=true
endif # HAS_EMBEDDED_OPENSSL_ALPN
endif # HAS_SYSTEM_OPENSSL_ALPN

OPENSSL_DEP :=
OPENSSL_MERGE_LIBS :=
ifeq ($(NO_SECURE),false)
ifeq ($(EMBED_OPENSSL),true)
OPENSSL_DEP += $(LIBDIR)/$(CONFIG)/libboringssl.a
OPENSSL_MERGE_LIBS += $(LIBDIR)/$(CONFIG)/libboringssl.a
OPENSSL_MERGE_OBJS += $(LIBBORINGSSL_OBJS)
# need to prefix these to ensure overriding system libraries
CPPFLAGS := -Ithird_party/boringssl/include $(CPPFLAGS)
else ifneq ($(EMBED_OPENSSL),false)
OPENSSL_DEP += $(EMBED_OPENSSL)/libssl.a $(EMBED_OPENSSL)/libcrypto.a
OPENSSL_MERGE_LIBS += $(EMBED_OPENSSL)/libssl.a $(EMBED_OPENSSL)/libcrypto.a
OPENSSL_MERGE_OBJS += $(wildcard $(EMBED_OPENSSL)/grpc_obj/*.o)
# need to prefix these to ensure overriding system libraries
CPPFLAGS := -I$(EMBED_OPENSSL)/include $(CPPFLAGS)
else # EMBED_OPENSSL=false
ifeq ($(HAS_PKG_CONFIG),true)
OPENSSL_PKG_CONFIG = true
PC_REQUIRES_SECURE = openssl
CPPFLAGS := $(shell $(PKG_CONFIG) --cflags openssl) $(CPPFLAGS)
LDFLAGS_OPENSSL_PKG_CONFIG = $(shell $(PKG_CONFIG) --libs-only-L openssl)
ifeq ($(SYSTEM),Linux)
ifneq ($(LDFLAGS_OPENSSL_PKG_CONFIG),)
LDFLAGS_OPENSSL_PKG_CONFIG += $(shell $(PKG_CONFIG) --libs-only-L openssl | sed s/L/Wl,-rpath,/)
endif # LDFLAGS_OPENSSL_PKG_CONFIG=''
endif # System=Linux
LDFLAGS := $(LDFLAGS_OPENSSL_PKG_CONFIG) $(LDFLAGS)
else # HAS_PKG_CONFIG=false
LIBS_SECURE = $(OPENSSL_LIBS)
endif # HAS_PKG_CONFIG
ifeq ($(HAS_SYSTEM_OPENSSL_NPN),true)
CPPFLAGS += -DTSI_OPENSSL_ALPN_SUPPORT=0
LIBS_SECURE = $(OPENSSL_LIBS)
endif # HAS_SYSTEM_OPENSSL_NPN
PC_LIBS_SECURE = $(addprefix -l, $(LIBS_SECURE))
endif # EMBED_OPENSSL
endif # NO_SECURE

ifeq ($(OPENSSL_PKG_CONFIG),true)
LDLIBS_SECURE += $(shell $(PKG_CONFIG) --libs-only-l openssl)
else
LDLIBS_SECURE += $(addprefix -l, $(LIBS_SECURE))
endif

# grpc .pc file
PC_NAME = gRPC
PC_DESCRIPTION = high performance general RPC framework
PC_CFLAGS =
PC_REQUIRES_PRIVATE = $(PC_REQUIRES_GRPC) $(PC_REQUIRES_SECURE)
PC_LIBS_PRIVATE = $(PC_LIBS_GRPC) $(PC_LIBS_SECURE)
PC_LIB = -lgrpc
GRPC_PC_FILE := $(CORE_PC_TEMPLATE)

# grpc_unsecure .pc file
PC_NAME = gRPC unsecure
PC_DESCRIPTION = high performance general RPC framework without SSL
PC_CFLAGS =
PC_REQUIRES_PRIVATE = $(PC_REQUIRES_GRPC)
PC_LIBS_PRIVATE = $(PC_LIBS_GRPC)
PC_LIB = -lgrpc
GRPC_UNSECURE_PC_FILE := $(CORE_PC_TEMPLATE)

PROTOBUF_PKG_CONFIG = false

PC_REQUIRES_GRPCXX =
PC_LIBS_GRPCXX =

CPPFLAGS := -Ithird_party/googletest/include $(CPPFLAGS)

PROTOC_PLUGINS_ALL = $(BINDIR)/$(CONFIG)/grpc_cpp_plugin $(BINDIR)/$(CONFIG)/grpc_csharp_plugin $(BINDIR)/$(CONFIG)/grpc_node_plugin $(BINDIR)/$(CONFIG)/grpc_objective_c_plugin $(BINDIR)/$(CONFIG)/grpc_php_plugin $(BINDIR)/$(CONFIG)/grpc_python_plugin $(BINDIR)/$(CONFIG)/grpc_ruby_plugin
PROTOC_PLUGINS_DIR = $(BINDIR)/$(CONFIG)

ifeq ($(HAS_SYSTEM_PROTOBUF),true)
ifeq ($(HAS_PKG_CONFIG),true)
PROTOBUF_PKG_CONFIG = true
PC_REQUIRES_GRPCXX = protobuf
CPPFLAGS := $(shell $(PKG_CONFIG) --cflags protobuf) $(CPPFLAGS)
LDFLAGS_PROTOBUF_PKG_CONFIG = $(shell $(PKG_CONFIG) --libs-only-L protobuf)
ifeq ($(SYSTEM),Linux)
ifneq ($(LDFLAGS_PROTOBUF_PKG_CONFIG),)
LDFLAGS_PROTOBUF_PKG_CONFIG += $(shell $(PKG_CONFIG) --libs-only-L protobuf | sed s/L/Wl,-rpath,/)
endif
endif
else
PC_LIBS_GRPCXX = -lprotobuf
endif
PROTOC_PLUGINS = $(PROTOC_PLUGINS_ALL)
else
ifeq ($(HAS_EMBEDDED_PROTOBUF),true)
PROTOBUF_DEP = $(LIBDIR)/$(CONFIG)/protobuf/libprotobuf.a
CPPFLAGS := -Ithird_party/protobuf/src $(CPPFLAGS)
LDFLAGS := -L$(LIBDIR)/$(CONFIG)/protobuf $(LDFLAGS)
ifneq ($(USE_BUILT_PROTOC),false)
PROTOC = $(BINDIR)/$(CONFIG)/protobuf/protoc
PROTOC_PLUGINS = $(PROTOC_PLUGINS_ALL)
else
PROTOC_PLUGINS =
PROTOC_PLUGINS_DIR = $(prefix)/bin
endif
else
NO_PROTOBUF = true
endif
endif

LIBS_PROTOBUF = protobuf
LIBS_PROTOC = protoc protobuf

HOST_LDLIBS_PROTOC += $(addprefix -l, $(LIBS_PROTOC))

ifeq ($(PROTOBUF_PKG_CONFIG),true)
LDLIBS_PROTOBUF += $(shell $(PKG_CONFIG) --libs-only-l protobuf)
else
LDLIBS_PROTOBUF += $(addprefix -l, $(LIBS_PROTOBUF))
endif

# grpc++ .pc file
PC_NAME = gRPC++
PC_DESCRIPTION = C++ wrapper for gRPC
PC_CFLAGS =
PC_REQUIRES_PRIVATE = grpc $(PC_REQUIRES_GRPCXX)
PC_LIBS_PRIVATE = $(PC_LIBS_GRPCXX)
PC_LIB = -lgrpc++
GRPCXX_PC_FILE := $(CPP_PC_TEMPLATE)

# grpc++_unsecure .pc file
PC_NAME = gRPC++ unsecure
PC_DESCRIPTION = C++ wrapper for gRPC without SSL
PC_CFLAGS =
PC_REQUIRES_PRIVATE = grpc_unsecure $(PC_REQUIRES_GRPCXX)
PC_LIBS_PRIVATE = $(PC_LIBS_GRPCXX)
PC_LIB = -lgrpc++
GRPCXX_UNSECURE_PC_FILE := $(CPP_PC_TEMPLATE)

ifeq ($(MAKECMDGOALS),clean)
NO_DEPS = true
endif

.SECONDARY = %.pb.h %.pb.cc

ifeq ($(DEP_MISSING),)
all: static shared plugins
dep_error:
	@echo "You shouldn't see this message - all of your dependencies are correct."
else
all: dep_error git_update stop

dep_error:
	@echo
	@echo "DEPENDENCY ERROR"
	@echo
	@echo "You are missing system dependencies that are essential to build grpc,"
	@echo "and the third_party directory doesn't have them:"
	@echo
	@echo "  $(DEP_MISSING)"
	@echo
	@echo "Installing the development packages for your system will solve"
	@echo "this issue. Please consult INSTALL to get more information."
	@echo
	@echo "If you need information about why these tests failed, run:"
	@echo
	@echo "  make run_dep_checks"
	@echo
endif

git_update:
ifeq ($(IS_GIT_FOLDER),true)
	@echo "Additionally, since you are in a git clone, you can download the"
	@echo "missing dependencies in third_party by running the following command:"
	@echo
	@echo "  git submodule update --init"
	@echo
endif

openssl_dep_error: openssl_dep_message git_update stop

protobuf_dep_error: protobuf_dep_message git_update stop

protoc_dep_error: protoc_dep_message git_update stop

openssl_dep_message:
	@echo
	@echo "DEPENDENCY ERROR"
	@echo
	@echo "The target you are trying to run requires an OpenSSL implementation."
	@echo "Your system doesn't have one, and either the third_party directory"
	@echo "doesn't have it, or your compiler can't build BoringSSL."
	@echo
	@echo "Please consult INSTALL to get more information."
	@echo
	@echo "If you need information about why these tests failed, run:"
	@echo
	@echo "  make run_dep_checks"
	@echo

protobuf_dep_message:
	@echo
	@echo "DEPENDENCY ERROR"
	@echo
	@echo "The target you are trying to run requires protobuf 3.0.0+"
	@echo "Your system doesn't have it, and neither does the third_party directory."
	@echo
	@echo "Please consult INSTALL to get more information."
	@echo
	@echo "If you need information about why these tests failed, run:"
	@echo
	@echo "  make run_dep_checks"
	@echo

protoc_dep_message:
	@echo
	@echo "DEPENDENCY ERROR"
	@echo
	@echo "The target you are trying to run requires protobuf-compiler 3.0.0+"
	@echo "Your system doesn't have it, and neither does the third_party directory."
	@echo
	@echo "Please consult INSTALL to get more information."
	@echo
	@echo "If you need information about why these tests failed, run:"
	@echo
	@echo "  make run_dep_checks"
	@echo

systemtap_dep_error:
	@echo
	@echo "DEPENDENCY ERROR"
	@echo
	@echo "Under the '$(CONFIG)' configutation, the target you are trying "
	@echo "to build requires systemtap 2.7+ (on Linux) or dtrace (on other "
	@echo "platforms such as Solaris and *BSD). "
	@echo
	@echo "Please consult INSTALL to get more information."
	@echo

stop:
	@false

alarm_test: $(BINDIR)/$(CONFIG)/alarm_test
algorithm_test: $(BINDIR)/$(CONFIG)/algorithm_test
alloc_test: $(BINDIR)/$(CONFIG)/alloc_test
alpn_test: $(BINDIR)/$(CONFIG)/alpn_test
api_fuzzer: $(BINDIR)/$(CONFIG)/api_fuzzer
bad_server_response_test: $(BINDIR)/$(CONFIG)/bad_server_response_test
bin_decoder_test: $(BINDIR)/$(CONFIG)/bin_decoder_test
bin_encoder_test: $(BINDIR)/$(CONFIG)/bin_encoder_test
census_context_test: $(BINDIR)/$(CONFIG)/census_context_test
census_resource_test: $(BINDIR)/$(CONFIG)/census_resource_test
census_trace_context_test: $(BINDIR)/$(CONFIG)/census_trace_context_test
channel_create_test: $(BINDIR)/$(CONFIG)/channel_create_test
chttp2_hpack_encoder_test: $(BINDIR)/$(CONFIG)/chttp2_hpack_encoder_test
chttp2_status_conversion_test: $(BINDIR)/$(CONFIG)/chttp2_status_conversion_test
chttp2_stream_map_test: $(BINDIR)/$(CONFIG)/chttp2_stream_map_test
chttp2_varint_test: $(BINDIR)/$(CONFIG)/chttp2_varint_test
client_fuzzer: $(BINDIR)/$(CONFIG)/client_fuzzer
combiner_test: $(BINDIR)/$(CONFIG)/combiner_test
compression_test: $(BINDIR)/$(CONFIG)/compression_test
concurrent_connectivity_test: $(BINDIR)/$(CONFIG)/concurrent_connectivity_test
connection_refused_test: $(BINDIR)/$(CONFIG)/connection_refused_test
dns_resolver_connectivity_test: $(BINDIR)/$(CONFIG)/dns_resolver_connectivity_test
dns_resolver_test: $(BINDIR)/$(CONFIG)/dns_resolver_test
dualstack_socket_test: $(BINDIR)/$(CONFIG)/dualstack_socket_test
endpoint_pair_test: $(BINDIR)/$(CONFIG)/endpoint_pair_test
ev_epoll_linux_test: $(BINDIR)/$(CONFIG)/ev_epoll_linux_test
fd_conservation_posix_test: $(BINDIR)/$(CONFIG)/fd_conservation_posix_test
fd_posix_test: $(BINDIR)/$(CONFIG)/fd_posix_test
fling_client: $(BINDIR)/$(CONFIG)/fling_client
fling_server: $(BINDIR)/$(CONFIG)/fling_server
fling_stream_test: $(BINDIR)/$(CONFIG)/fling_stream_test
fling_test: $(BINDIR)/$(CONFIG)/fling_test
gen_hpack_tables: $(BINDIR)/$(CONFIG)/gen_hpack_tables
gen_legal_metadata_characters: $(BINDIR)/$(CONFIG)/gen_legal_metadata_characters
gen_percent_encoding_tables: $(BINDIR)/$(CONFIG)/gen_percent_encoding_tables
goaway_server_test: $(BINDIR)/$(CONFIG)/goaway_server_test
gpr_avl_test: $(BINDIR)/$(CONFIG)/gpr_avl_test
gpr_backoff_test: $(BINDIR)/$(CONFIG)/gpr_backoff_test
gpr_cmdline_test: $(BINDIR)/$(CONFIG)/gpr_cmdline_test
gpr_cpu_test: $(BINDIR)/$(CONFIG)/gpr_cpu_test
gpr_env_test: $(BINDIR)/$(CONFIG)/gpr_env_test
gpr_histogram_test: $(BINDIR)/$(CONFIG)/gpr_histogram_test
gpr_host_port_test: $(BINDIR)/$(CONFIG)/gpr_host_port_test
gpr_log_test: $(BINDIR)/$(CONFIG)/gpr_log_test
gpr_mpscq_test: $(BINDIR)/$(CONFIG)/gpr_mpscq_test
gpr_stack_lockfree_test: $(BINDIR)/$(CONFIG)/gpr_stack_lockfree_test
gpr_string_test: $(BINDIR)/$(CONFIG)/gpr_string_test
gpr_sync_test: $(BINDIR)/$(CONFIG)/gpr_sync_test
gpr_thd_test: $(BINDIR)/$(CONFIG)/gpr_thd_test
gpr_time_test: $(BINDIR)/$(CONFIG)/gpr_time_test
gpr_tls_test: $(BINDIR)/$(CONFIG)/gpr_tls_test
gpr_useful_test: $(BINDIR)/$(CONFIG)/gpr_useful_test
grpc_auth_context_test: $(BINDIR)/$(CONFIG)/grpc_auth_context_test
grpc_b64_test: $(BINDIR)/$(CONFIG)/grpc_b64_test
grpc_byte_buffer_reader_test: $(BINDIR)/$(CONFIG)/grpc_byte_buffer_reader_test
grpc_channel_args_test: $(BINDIR)/$(CONFIG)/grpc_channel_args_test
grpc_channel_stack_test: $(BINDIR)/$(CONFIG)/grpc_channel_stack_test
grpc_completion_queue_test: $(BINDIR)/$(CONFIG)/grpc_completion_queue_test
grpc_create_jwt: $(BINDIR)/$(CONFIG)/grpc_create_jwt
grpc_credentials_test: $(BINDIR)/$(CONFIG)/grpc_credentials_test
grpc_fetch_oauth2: $(BINDIR)/$(CONFIG)/grpc_fetch_oauth2
grpc_invalid_channel_args_test: $(BINDIR)/$(CONFIG)/grpc_invalid_channel_args_test
grpc_json_token_test: $(BINDIR)/$(CONFIG)/grpc_json_token_test
grpc_jwt_verifier_test: $(BINDIR)/$(CONFIG)/grpc_jwt_verifier_test
grpc_print_google_default_creds_token: $(BINDIR)/$(CONFIG)/grpc_print_google_default_creds_token
grpc_security_connector_test: $(BINDIR)/$(CONFIG)/grpc_security_connector_test
grpc_verify_jwt: $(BINDIR)/$(CONFIG)/grpc_verify_jwt
handshake_client: $(BINDIR)/$(CONFIG)/handshake_client
handshake_server: $(BINDIR)/$(CONFIG)/handshake_server
hpack_parser_fuzzer_test: $(BINDIR)/$(CONFIG)/hpack_parser_fuzzer_test
hpack_parser_test: $(BINDIR)/$(CONFIG)/hpack_parser_test
hpack_table_test: $(BINDIR)/$(CONFIG)/hpack_table_test
http_parser_test: $(BINDIR)/$(CONFIG)/http_parser_test
http_request_fuzzer_test: $(BINDIR)/$(CONFIG)/http_request_fuzzer_test
http_response_fuzzer_test: $(BINDIR)/$(CONFIG)/http_response_fuzzer_test
httpcli_format_request_test: $(BINDIR)/$(CONFIG)/httpcli_format_request_test
httpcli_test: $(BINDIR)/$(CONFIG)/httpcli_test
httpscli_test: $(BINDIR)/$(CONFIG)/httpscli_test
init_test: $(BINDIR)/$(CONFIG)/init_test
internal_api_canary_iomgr_test: $(BINDIR)/$(CONFIG)/internal_api_canary_iomgr_test
internal_api_canary_support_test: $(BINDIR)/$(CONFIG)/internal_api_canary_support_test
internal_api_canary_transport_test: $(BINDIR)/$(CONFIG)/internal_api_canary_transport_test
invalid_call_argument_test: $(BINDIR)/$(CONFIG)/invalid_call_argument_test
json_fuzzer_test: $(BINDIR)/$(CONFIG)/json_fuzzer_test
json_rewrite: $(BINDIR)/$(CONFIG)/json_rewrite
json_rewrite_test: $(BINDIR)/$(CONFIG)/json_rewrite_test
json_stream_error_test: $(BINDIR)/$(CONFIG)/json_stream_error_test
json_test: $(BINDIR)/$(CONFIG)/json_test
lame_client_test: $(BINDIR)/$(CONFIG)/lame_client_test
lb_policies_test: $(BINDIR)/$(CONFIG)/lb_policies_test
load_file_test: $(BINDIR)/$(CONFIG)/load_file_test
low_level_ping_pong_benchmark: $(BINDIR)/$(CONFIG)/low_level_ping_pong_benchmark
message_compress_test: $(BINDIR)/$(CONFIG)/message_compress_test
mlog_test: $(BINDIR)/$(CONFIG)/mlog_test
multiple_server_queues_test: $(BINDIR)/$(CONFIG)/multiple_server_queues_test
murmur_hash_test: $(BINDIR)/$(CONFIG)/murmur_hash_test
nanopb_fuzzer_response_test: $(BINDIR)/$(CONFIG)/nanopb_fuzzer_response_test
nanopb_fuzzer_serverlist_test: $(BINDIR)/$(CONFIG)/nanopb_fuzzer_serverlist_test
no_server_test: $(BINDIR)/$(CONFIG)/no_server_test
percent_decode_fuzzer: $(BINDIR)/$(CONFIG)/percent_decode_fuzzer
percent_encode_fuzzer: $(BINDIR)/$(CONFIG)/percent_encode_fuzzer
percent_encoding_test: $(BINDIR)/$(CONFIG)/percent_encoding_test
resolve_address_test: $(BINDIR)/$(CONFIG)/resolve_address_test
resource_quota_test: $(BINDIR)/$(CONFIG)/resource_quota_test
secure_channel_create_test: $(BINDIR)/$(CONFIG)/secure_channel_create_test
secure_endpoint_test: $(BINDIR)/$(CONFIG)/secure_endpoint_test
sequential_connectivity_test: $(BINDIR)/$(CONFIG)/sequential_connectivity_test
server_chttp2_test: $(BINDIR)/$(CONFIG)/server_chttp2_test
server_fuzzer: $(BINDIR)/$(CONFIG)/server_fuzzer
server_test: $(BINDIR)/$(CONFIG)/server_test
set_initial_connect_string_test: $(BINDIR)/$(CONFIG)/set_initial_connect_string_test
slice_buffer_test: $(BINDIR)/$(CONFIG)/slice_buffer_test
slice_string_helpers_test: $(BINDIR)/$(CONFIG)/slice_string_helpers_test
slice_test: $(BINDIR)/$(CONFIG)/slice_test
sockaddr_resolver_test: $(BINDIR)/$(CONFIG)/sockaddr_resolver_test
sockaddr_utils_test: $(BINDIR)/$(CONFIG)/sockaddr_utils_test
socket_utils_test: $(BINDIR)/$(CONFIG)/socket_utils_test
ssl_server_fuzzer: $(BINDIR)/$(CONFIG)/ssl_server_fuzzer
tcp_client_posix_test: $(BINDIR)/$(CONFIG)/tcp_client_posix_test
tcp_posix_test: $(BINDIR)/$(CONFIG)/tcp_posix_test
tcp_server_posix_test: $(BINDIR)/$(CONFIG)/tcp_server_posix_test
time_averaged_stats_test: $(BINDIR)/$(CONFIG)/time_averaged_stats_test
timeout_encoding_test: $(BINDIR)/$(CONFIG)/timeout_encoding_test
timer_heap_test: $(BINDIR)/$(CONFIG)/timer_heap_test
timer_list_test: $(BINDIR)/$(CONFIG)/timer_list_test
transport_connectivity_state_test: $(BINDIR)/$(CONFIG)/transport_connectivity_state_test
transport_metadata_test: $(BINDIR)/$(CONFIG)/transport_metadata_test
transport_pid_controller_test: $(BINDIR)/$(CONFIG)/transport_pid_controller_test
transport_security_test: $(BINDIR)/$(CONFIG)/transport_security_test
udp_server_test: $(BINDIR)/$(CONFIG)/udp_server_test
uri_fuzzer_test: $(BINDIR)/$(CONFIG)/uri_fuzzer_test
uri_parser_test: $(BINDIR)/$(CONFIG)/uri_parser_test
wakeup_fd_cv_test: $(BINDIR)/$(CONFIG)/wakeup_fd_cv_test
alarm_cpp_test: $(BINDIR)/$(CONFIG)/alarm_cpp_test
async_end2end_test: $(BINDIR)/$(CONFIG)/async_end2end_test
auth_property_iterator_test: $(BINDIR)/$(CONFIG)/auth_property_iterator_test
bm_fullstack: $(BINDIR)/$(CONFIG)/bm_fullstack
channel_arguments_test: $(BINDIR)/$(CONFIG)/channel_arguments_test
channel_filter_test: $(BINDIR)/$(CONFIG)/channel_filter_test
cli_call_test: $(BINDIR)/$(CONFIG)/cli_call_test
client_crash_test: $(BINDIR)/$(CONFIG)/client_crash_test
client_crash_test_server: $(BINDIR)/$(CONFIG)/client_crash_test_server
codegen_test_full: $(BINDIR)/$(CONFIG)/codegen_test_full
codegen_test_minimal: $(BINDIR)/$(CONFIG)/codegen_test_minimal
credentials_test: $(BINDIR)/$(CONFIG)/credentials_test
cxx_byte_buffer_test: $(BINDIR)/$(CONFIG)/cxx_byte_buffer_test
cxx_slice_test: $(BINDIR)/$(CONFIG)/cxx_slice_test
cxx_string_ref_test: $(BINDIR)/$(CONFIG)/cxx_string_ref_test
cxx_time_test: $(BINDIR)/$(CONFIG)/cxx_time_test
end2end_test: $(BINDIR)/$(CONFIG)/end2end_test
filter_end2end_test: $(BINDIR)/$(CONFIG)/filter_end2end_test
generic_end2end_test: $(BINDIR)/$(CONFIG)/generic_end2end_test
golden_file_test: $(BINDIR)/$(CONFIG)/golden_file_test
grpc_cli: $(BINDIR)/$(CONFIG)/grpc_cli
grpc_cpp_plugin: $(BINDIR)/$(CONFIG)/grpc_cpp_plugin
grpc_csharp_plugin: $(BINDIR)/$(CONFIG)/grpc_csharp_plugin
grpc_node_plugin: $(BINDIR)/$(CONFIG)/grpc_node_plugin
grpc_objective_c_plugin: $(BINDIR)/$(CONFIG)/grpc_objective_c_plugin
grpc_php_plugin: $(BINDIR)/$(CONFIG)/grpc_php_plugin
grpc_python_plugin: $(BINDIR)/$(CONFIG)/grpc_python_plugin
grpc_ruby_plugin: $(BINDIR)/$(CONFIG)/grpc_ruby_plugin
grpc_tool_test: $(BINDIR)/$(CONFIG)/grpc_tool_test
grpclb_api_test: $(BINDIR)/$(CONFIG)/grpclb_api_test
grpclb_test: $(BINDIR)/$(CONFIG)/grpclb_test
hybrid_end2end_test: $(BINDIR)/$(CONFIG)/hybrid_end2end_test
interop_client: $(BINDIR)/$(CONFIG)/interop_client
interop_server: $(BINDIR)/$(CONFIG)/interop_server
interop_test: $(BINDIR)/$(CONFIG)/interop_test
json_run_localhost: $(BINDIR)/$(CONFIG)/json_run_localhost
metrics_client: $(BINDIR)/$(CONFIG)/metrics_client
mock_test: $(BINDIR)/$(CONFIG)/mock_test
noop-benchmark: $(BINDIR)/$(CONFIG)/noop-benchmark
proto_server_reflection_test: $(BINDIR)/$(CONFIG)/proto_server_reflection_test
qps_interarrival_test: $(BINDIR)/$(CONFIG)/qps_interarrival_test
qps_json_driver: $(BINDIR)/$(CONFIG)/qps_json_driver
qps_openloop_test: $(BINDIR)/$(CONFIG)/qps_openloop_test
qps_worker: $(BINDIR)/$(CONFIG)/qps_worker
reconnect_interop_client: $(BINDIR)/$(CONFIG)/reconnect_interop_client
reconnect_interop_server: $(BINDIR)/$(CONFIG)/reconnect_interop_server
round_robin_end2end_test: $(BINDIR)/$(CONFIG)/round_robin_end2end_test
secure_auth_context_test: $(BINDIR)/$(CONFIG)/secure_auth_context_test
secure_sync_unary_ping_pong_test: $(BINDIR)/$(CONFIG)/secure_sync_unary_ping_pong_test
server_builder_plugin_test: $(BINDIR)/$(CONFIG)/server_builder_plugin_test
server_context_test_spouse_test: $(BINDIR)/$(CONFIG)/server_context_test_spouse_test
server_crash_test: $(BINDIR)/$(CONFIG)/server_crash_test
server_crash_test_client: $(BINDIR)/$(CONFIG)/server_crash_test_client
shutdown_test: $(BINDIR)/$(CONFIG)/shutdown_test
status_test: $(BINDIR)/$(CONFIG)/status_test
streaming_throughput_test: $(BINDIR)/$(CONFIG)/streaming_throughput_test
stress_test: $(BINDIR)/$(CONFIG)/stress_test
thread_manager_test: $(BINDIR)/$(CONFIG)/thread_manager_test
thread_stress_test: $(BINDIR)/$(CONFIG)/thread_stress_test
public_headers_must_be_c89: $(BINDIR)/$(CONFIG)/public_headers_must_be_c89
boringssl_aes_test: $(BINDIR)/$(CONFIG)/boringssl_aes_test
boringssl_asn1_test: $(BINDIR)/$(CONFIG)/boringssl_asn1_test
boringssl_base64_test: $(BINDIR)/$(CONFIG)/boringssl_base64_test
boringssl_bio_test: $(BINDIR)/$(CONFIG)/boringssl_bio_test
boringssl_bn_test: $(BINDIR)/$(CONFIG)/boringssl_bn_test
boringssl_bytestring_test: $(BINDIR)/$(CONFIG)/boringssl_bytestring_test
boringssl_aead_test: $(BINDIR)/$(CONFIG)/boringssl_aead_test
boringssl_cipher_test: $(BINDIR)/$(CONFIG)/boringssl_cipher_test
boringssl_cmac_test: $(BINDIR)/$(CONFIG)/boringssl_cmac_test
boringssl_constant_time_test: $(BINDIR)/$(CONFIG)/boringssl_constant_time_test
boringssl_ed25519_test: $(BINDIR)/$(CONFIG)/boringssl_ed25519_test
boringssl_x25519_test: $(BINDIR)/$(CONFIG)/boringssl_x25519_test
boringssl_dh_test: $(BINDIR)/$(CONFIG)/boringssl_dh_test
boringssl_digest_test: $(BINDIR)/$(CONFIG)/boringssl_digest_test
boringssl_dsa_test: $(BINDIR)/$(CONFIG)/boringssl_dsa_test
boringssl_ec_test: $(BINDIR)/$(CONFIG)/boringssl_ec_test
boringssl_example_mul: $(BINDIR)/$(CONFIG)/boringssl_example_mul
boringssl_ecdsa_test: $(BINDIR)/$(CONFIG)/boringssl_ecdsa_test
boringssl_err_test: $(BINDIR)/$(CONFIG)/boringssl_err_test
boringssl_evp_extra_test: $(BINDIR)/$(CONFIG)/boringssl_evp_extra_test
boringssl_evp_test: $(BINDIR)/$(CONFIG)/boringssl_evp_test
boringssl_pbkdf_test: $(BINDIR)/$(CONFIG)/boringssl_pbkdf_test
boringssl_hkdf_test: $(BINDIR)/$(CONFIG)/boringssl_hkdf_test
boringssl_hmac_test: $(BINDIR)/$(CONFIG)/boringssl_hmac_test
boringssl_lhash_test: $(BINDIR)/$(CONFIG)/boringssl_lhash_test
boringssl_gcm_test: $(BINDIR)/$(CONFIG)/boringssl_gcm_test
boringssl_pkcs12_test: $(BINDIR)/$(CONFIG)/boringssl_pkcs12_test
boringssl_pkcs8_test: $(BINDIR)/$(CONFIG)/boringssl_pkcs8_test
boringssl_poly1305_test: $(BINDIR)/$(CONFIG)/boringssl_poly1305_test
boringssl_refcount_test: $(BINDIR)/$(CONFIG)/boringssl_refcount_test
boringssl_rsa_test: $(BINDIR)/$(CONFIG)/boringssl_rsa_test
boringssl_thread_test: $(BINDIR)/$(CONFIG)/boringssl_thread_test
boringssl_pkcs7_test: $(BINDIR)/$(CONFIG)/boringssl_pkcs7_test
boringssl_x509_test: $(BINDIR)/$(CONFIG)/boringssl_x509_test
boringssl_tab_test: $(BINDIR)/$(CONFIG)/boringssl_tab_test
boringssl_v3name_test: $(BINDIR)/$(CONFIG)/boringssl_v3name_test
boringssl_pqueue_test: $(BINDIR)/$(CONFIG)/boringssl_pqueue_test
boringssl_ssl_test: $(BINDIR)/$(CONFIG)/boringssl_ssl_test
badreq_bad_client_test: $(BINDIR)/$(CONFIG)/badreq_bad_client_test
connection_prefix_bad_client_test: $(BINDIR)/$(CONFIG)/connection_prefix_bad_client_test
head_of_line_blocking_bad_client_test: $(BINDIR)/$(CONFIG)/head_of_line_blocking_bad_client_test
headers_bad_client_test: $(BINDIR)/$(CONFIG)/headers_bad_client_test
initial_settings_frame_bad_client_test: $(BINDIR)/$(CONFIG)/initial_settings_frame_bad_client_test
large_metadata_bad_client_test: $(BINDIR)/$(CONFIG)/large_metadata_bad_client_test
server_registered_method_bad_client_test: $(BINDIR)/$(CONFIG)/server_registered_method_bad_client_test
simple_request_bad_client_test: $(BINDIR)/$(CONFIG)/simple_request_bad_client_test
unknown_frame_bad_client_test: $(BINDIR)/$(CONFIG)/unknown_frame_bad_client_test
window_overflow_bad_client_test: $(BINDIR)/$(CONFIG)/window_overflow_bad_client_test
bad_ssl_cert_server: $(BINDIR)/$(CONFIG)/bad_ssl_cert_server
bad_ssl_cert_test: $(BINDIR)/$(CONFIG)/bad_ssl_cert_test
h2_census_test: $(BINDIR)/$(CONFIG)/h2_census_test
h2_compress_test: $(BINDIR)/$(CONFIG)/h2_compress_test
h2_fakesec_test: $(BINDIR)/$(CONFIG)/h2_fakesec_test
h2_fd_test: $(BINDIR)/$(CONFIG)/h2_fd_test
h2_full_test: $(BINDIR)/$(CONFIG)/h2_full_test
h2_full+pipe_test: $(BINDIR)/$(CONFIG)/h2_full+pipe_test
h2_full+trace_test: $(BINDIR)/$(CONFIG)/h2_full+trace_test
h2_http_proxy_test: $(BINDIR)/$(CONFIG)/h2_http_proxy_test
h2_load_reporting_test: $(BINDIR)/$(CONFIG)/h2_load_reporting_test
h2_oauth2_test: $(BINDIR)/$(CONFIG)/h2_oauth2_test
h2_proxy_test: $(BINDIR)/$(CONFIG)/h2_proxy_test
h2_sockpair_test: $(BINDIR)/$(CONFIG)/h2_sockpair_test
h2_sockpair+trace_test: $(BINDIR)/$(CONFIG)/h2_sockpair+trace_test
h2_sockpair_1byte_test: $(BINDIR)/$(CONFIG)/h2_sockpair_1byte_test
h2_ssl_test: $(BINDIR)/$(CONFIG)/h2_ssl_test
h2_ssl_cert_test: $(BINDIR)/$(CONFIG)/h2_ssl_cert_test
h2_ssl_proxy_test: $(BINDIR)/$(CONFIG)/h2_ssl_proxy_test
h2_uds_test: $(BINDIR)/$(CONFIG)/h2_uds_test
h2_census_nosec_test: $(BINDIR)/$(CONFIG)/h2_census_nosec_test
h2_compress_nosec_test: $(BINDIR)/$(CONFIG)/h2_compress_nosec_test
h2_fd_nosec_test: $(BINDIR)/$(CONFIG)/h2_fd_nosec_test
h2_full_nosec_test: $(BINDIR)/$(CONFIG)/h2_full_nosec_test
h2_full+pipe_nosec_test: $(BINDIR)/$(CONFIG)/h2_full+pipe_nosec_test
h2_full+trace_nosec_test: $(BINDIR)/$(CONFIG)/h2_full+trace_nosec_test
h2_http_proxy_nosec_test: $(BINDIR)/$(CONFIG)/h2_http_proxy_nosec_test
h2_load_reporting_nosec_test: $(BINDIR)/$(CONFIG)/h2_load_reporting_nosec_test
h2_proxy_nosec_test: $(BINDIR)/$(CONFIG)/h2_proxy_nosec_test
h2_sockpair_nosec_test: $(BINDIR)/$(CONFIG)/h2_sockpair_nosec_test
h2_sockpair+trace_nosec_test: $(BINDIR)/$(CONFIG)/h2_sockpair+trace_nosec_test
h2_sockpair_1byte_nosec_test: $(BINDIR)/$(CONFIG)/h2_sockpair_1byte_nosec_test
h2_uds_nosec_test: $(BINDIR)/$(CONFIG)/h2_uds_nosec_test
api_fuzzer_one_entry: $(BINDIR)/$(CONFIG)/api_fuzzer_one_entry
client_fuzzer_one_entry: $(BINDIR)/$(CONFIG)/client_fuzzer_one_entry
hpack_parser_fuzzer_test_one_entry: $(BINDIR)/$(CONFIG)/hpack_parser_fuzzer_test_one_entry
http_request_fuzzer_test_one_entry: $(BINDIR)/$(CONFIG)/http_request_fuzzer_test_one_entry
http_response_fuzzer_test_one_entry: $(BINDIR)/$(CONFIG)/http_response_fuzzer_test_one_entry
json_fuzzer_test_one_entry: $(BINDIR)/$(CONFIG)/json_fuzzer_test_one_entry
nanopb_fuzzer_response_test_one_entry: $(BINDIR)/$(CONFIG)/nanopb_fuzzer_response_test_one_entry
nanopb_fuzzer_serverlist_test_one_entry: $(BINDIR)/$(CONFIG)/nanopb_fuzzer_serverlist_test_one_entry
percent_decode_fuzzer_one_entry: $(BINDIR)/$(CONFIG)/percent_decode_fuzzer_one_entry
percent_encode_fuzzer_one_entry: $(BINDIR)/$(CONFIG)/percent_encode_fuzzer_one_entry
server_fuzzer_one_entry: $(BINDIR)/$(CONFIG)/server_fuzzer_one_entry
ssl_server_fuzzer_one_entry: $(BINDIR)/$(CONFIG)/ssl_server_fuzzer_one_entry
uri_fuzzer_test_one_entry: $(BINDIR)/$(CONFIG)/uri_fuzzer_test_one_entry

run_dep_checks:
	$(OPENSSL_ALPN_CHECK_CMD) || true
	$(OPENSSL_NPN_CHECK_CMD) || true
	$(ZLIB_CHECK_CMD) || true
	$(PERFTOOLS_CHECK_CMD) || true
	$(PROTOBUF_CHECK_CMD) || true
	$(PROTOC_CHECK_VERSION_CMD) || true

third_party/protobuf/configure:
	$(E) "[AUTOGEN] Preparing protobuf"
	$(Q)(cd third_party/protobuf ; autoreconf -f -i -Wall,no-obsolete)

$(LIBDIR)/$(CONFIG)/protobuf/libprotobuf.a: third_party/protobuf/configure
	$(E) "[MAKE]    Building protobuf"
	$(Q)(cd third_party/protobuf ; CC="$(CC)" CXX="$(CXX)" LDFLAGS="$(LDFLAGS_$(CONFIG)) -g $(PROTOBUF_LDFLAGS_EXTRA)" CPPFLAGS="$(PIC_CPPFLAGS) $(CPPFLAGS_$(CONFIG)) -g $(PROTOBUF_CPPFLAGS_EXTRA)" ./configure --disable-shared --enable-static $(PROTOBUF_CONFIG_OPTS))
	$(Q)$(MAKE) -C third_party/protobuf clean
	$(Q)$(MAKE) -C third_party/protobuf
	$(Q)mkdir -p $(LIBDIR)/$(CONFIG)/protobuf
	$(Q)mkdir -p $(BINDIR)/$(CONFIG)/protobuf
	$(Q)cp third_party/protobuf/src/.libs/libprotoc.a $(LIBDIR)/$(CONFIG)/protobuf
	$(Q)cp third_party/protobuf/src/.libs/libprotobuf.a $(LIBDIR)/$(CONFIG)/protobuf
	$(Q)cp third_party/protobuf/src/protoc $(BINDIR)/$(CONFIG)/protobuf

static: static_c static_cxx

static_c: pc_c pc_c_unsecure cache.mk  $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgrpc_cronet.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a

static_cxx: pc_cxx pc_cxx_unsecure cache.mk  $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc++_cronet.a $(LIBDIR)/$(CONFIG)/libgrpc++_reflection.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a

shared: shared_c shared_cxx

shared_c: pc_c pc_c_unsecure cache.mk $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)
shared_cxx: pc_cxx pc_cxx_unsecure cache.mk $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_unsecure$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)

shared_csharp: shared_c  $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc_csharp_ext$(SHARED_VERSION_CSHARP).$(SHARED_EXT_CSHARP)
grpc_csharp_ext: shared_csharp

plugins: $(PROTOC_PLUGINS)

privatelibs: privatelibs_c privatelibs_cxx

privatelibs_c:  $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libreconnect_server.a $(LIBDIR)/$(CONFIG)/libtest_tcp_server.a $(LIBDIR)/$(CONFIG)/libz.a $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libbad_ssl_test_server.a $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a
pc_c: $(LIBDIR)/$(CONFIG)/pkgconfig/grpc.pc

pc_c_unsecure: $(LIBDIR)/$(CONFIG)/pkgconfig/grpc_unsecure.pc

pc_cxx: $(LIBDIR)/$(CONFIG)/pkgconfig/grpc++.pc

pc_cxx_unsecure: $(LIBDIR)/$(CONFIG)/pkgconfig/grpc++_unsecure.pc

ifeq ($(EMBED_OPENSSL),true)
privatelibs_cxx:  $(LIBDIR)/$(CONFIG)/libgrpc++_proto_reflection_desc_db.a $(LIBDIR)/$(CONFIG)/libgrpc++_test.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_cli_libs.a $(LIBDIR)/$(CONFIG)/libinterop_client_helper.a $(LIBDIR)/$(CONFIG)/libinterop_client_main.a $(LIBDIR)/$(CONFIG)/libinterop_server_helper.a $(LIBDIR)/$(CONFIG)/libinterop_server_lib.a $(LIBDIR)/$(CONFIG)/libinterop_server_main.a $(LIBDIR)/$(CONFIG)/libqps.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl_aes_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_asn1_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_base64_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_bio_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_bn_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_bytestring_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_aead_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_cipher_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_cmac_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_ed25519_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_x25519_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_dh_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_digest_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_ec_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_err_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_evp_extra_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_evp_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_pbkdf_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_hmac_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_pkcs12_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_pkcs8_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_poly1305_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_rsa_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_x509_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_ssl_test_lib.a $(LIBDIR)/$(CONFIG)/libgoogle_benchmark.a
else
privatelibs_cxx:  $(LIBDIR)/$(CONFIG)/libgrpc++_proto_reflection_desc_db.a $(LIBDIR)/$(CONFIG)/libgrpc++_test.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_cli_libs.a $(LIBDIR)/$(CONFIG)/libinterop_client_helper.a $(LIBDIR)/$(CONFIG)/libinterop_client_main.a $(LIBDIR)/$(CONFIG)/libinterop_server_helper.a $(LIBDIR)/$(CONFIG)/libinterop_server_lib.a $(LIBDIR)/$(CONFIG)/libinterop_server_main.a $(LIBDIR)/$(CONFIG)/libqps.a $(LIBDIR)/$(CONFIG)/libgoogle_benchmark.a
endif


buildtests: buildtests_c buildtests_cxx

buildtests_c: privatelibs_c \
  $(BINDIR)/$(CONFIG)/alarm_test \
  $(BINDIR)/$(CONFIG)/algorithm_test \
  $(BINDIR)/$(CONFIG)/alloc_test \
  $(BINDIR)/$(CONFIG)/alpn_test \
  $(BINDIR)/$(CONFIG)/bad_server_response_test \
  $(BINDIR)/$(CONFIG)/bin_decoder_test \
  $(BINDIR)/$(CONFIG)/bin_encoder_test \
  $(BINDIR)/$(CONFIG)/census_context_test \
  $(BINDIR)/$(CONFIG)/census_resource_test \
  $(BINDIR)/$(CONFIG)/census_trace_context_test \
  $(BINDIR)/$(CONFIG)/channel_create_test \
  $(BINDIR)/$(CONFIG)/chttp2_hpack_encoder_test \
  $(BINDIR)/$(CONFIG)/chttp2_status_conversion_test \
  $(BINDIR)/$(CONFIG)/chttp2_stream_map_test \
  $(BINDIR)/$(CONFIG)/chttp2_varint_test \
  $(BINDIR)/$(CONFIG)/combiner_test \
  $(BINDIR)/$(CONFIG)/compression_test \
  $(BINDIR)/$(CONFIG)/concurrent_connectivity_test \
  $(BINDIR)/$(CONFIG)/connection_refused_test \
  $(BINDIR)/$(CONFIG)/dns_resolver_connectivity_test \
  $(BINDIR)/$(CONFIG)/dns_resolver_test \
  $(BINDIR)/$(CONFIG)/dualstack_socket_test \
  $(BINDIR)/$(CONFIG)/endpoint_pair_test \
  $(BINDIR)/$(CONFIG)/ev_epoll_linux_test \
  $(BINDIR)/$(CONFIG)/fd_conservation_posix_test \
  $(BINDIR)/$(CONFIG)/fd_posix_test \
  $(BINDIR)/$(CONFIG)/fling_client \
  $(BINDIR)/$(CONFIG)/fling_server \
  $(BINDIR)/$(CONFIG)/fling_stream_test \
  $(BINDIR)/$(CONFIG)/fling_test \
  $(BINDIR)/$(CONFIG)/goaway_server_test \
  $(BINDIR)/$(CONFIG)/gpr_avl_test \
  $(BINDIR)/$(CONFIG)/gpr_backoff_test \
  $(BINDIR)/$(CONFIG)/gpr_cmdline_test \
  $(BINDIR)/$(CONFIG)/gpr_cpu_test \
  $(BINDIR)/$(CONFIG)/gpr_env_test \
  $(BINDIR)/$(CONFIG)/gpr_histogram_test \
  $(BINDIR)/$(CONFIG)/gpr_host_port_test \
  $(BINDIR)/$(CONFIG)/gpr_log_test \
  $(BINDIR)/$(CONFIG)/gpr_mpscq_test \
  $(BINDIR)/$(CONFIG)/gpr_stack_lockfree_test \
  $(BINDIR)/$(CONFIG)/gpr_string_test \
  $(BINDIR)/$(CONFIG)/gpr_sync_test \
  $(BINDIR)/$(CONFIG)/gpr_thd_test \
  $(BINDIR)/$(CONFIG)/gpr_time_test \
  $(BINDIR)/$(CONFIG)/gpr_tls_test \
  $(BINDIR)/$(CONFIG)/gpr_useful_test \
  $(BINDIR)/$(CONFIG)/grpc_auth_context_test \
  $(BINDIR)/$(CONFIG)/grpc_b64_test \
  $(BINDIR)/$(CONFIG)/grpc_byte_buffer_reader_test \
  $(BINDIR)/$(CONFIG)/grpc_channel_args_test \
  $(BINDIR)/$(CONFIG)/grpc_channel_stack_test \
  $(BINDIR)/$(CONFIG)/grpc_completion_queue_test \
  $(BINDIR)/$(CONFIG)/grpc_credentials_test \
  $(BINDIR)/$(CONFIG)/grpc_fetch_oauth2 \
  $(BINDIR)/$(CONFIG)/grpc_invalid_channel_args_test \
  $(BINDIR)/$(CONFIG)/grpc_json_token_test \
  $(BINDIR)/$(CONFIG)/grpc_jwt_verifier_test \
  $(BINDIR)/$(CONFIG)/grpc_security_connector_test \
  $(BINDIR)/$(CONFIG)/handshake_client \
  $(BINDIR)/$(CONFIG)/handshake_server \
  $(BINDIR)/$(CONFIG)/hpack_parser_test \
  $(BINDIR)/$(CONFIG)/hpack_table_test \
  $(BINDIR)/$(CONFIG)/http_parser_test \
  $(BINDIR)/$(CONFIG)/httpcli_format_request_test \
  $(BINDIR)/$(CONFIG)/httpcli_test \
  $(BINDIR)/$(CONFIG)/httpscli_test \
  $(BINDIR)/$(CONFIG)/init_test \
  $(BINDIR)/$(CONFIG)/internal_api_canary_iomgr_test \
  $(BINDIR)/$(CONFIG)/internal_api_canary_support_test \
  $(BINDIR)/$(CONFIG)/internal_api_canary_transport_test \
  $(BINDIR)/$(CONFIG)/invalid_call_argument_test \
  $(BINDIR)/$(CONFIG)/json_rewrite \
  $(BINDIR)/$(CONFIG)/json_rewrite_test \
  $(BINDIR)/$(CONFIG)/json_stream_error_test \
  $(BINDIR)/$(CONFIG)/json_test \
  $(BINDIR)/$(CONFIG)/lame_client_test \
  $(BINDIR)/$(CONFIG)/lb_policies_test \
  $(BINDIR)/$(CONFIG)/load_file_test \
  $(BINDIR)/$(CONFIG)/message_compress_test \
  $(BINDIR)/$(CONFIG)/mlog_test \
  $(BINDIR)/$(CONFIG)/multiple_server_queues_test \
  $(BINDIR)/$(CONFIG)/murmur_hash_test \
  $(BINDIR)/$(CONFIG)/no_server_test \
  $(BINDIR)/$(CONFIG)/percent_encoding_test \
  $(BINDIR)/$(CONFIG)/resolve_address_test \
  $(BINDIR)/$(CONFIG)/resource_quota_test \
  $(BINDIR)/$(CONFIG)/secure_channel_create_test \
  $(BINDIR)/$(CONFIG)/secure_endpoint_test \
  $(BINDIR)/$(CONFIG)/sequential_connectivity_test \
  $(BINDIR)/$(CONFIG)/server_chttp2_test \
  $(BINDIR)/$(CONFIG)/server_test \
  $(BINDIR)/$(CONFIG)/set_initial_connect_string_test \
  $(BINDIR)/$(CONFIG)/slice_buffer_test \
  $(BINDIR)/$(CONFIG)/slice_string_helpers_test \
  $(BINDIR)/$(CONFIG)/slice_test \
  $(BINDIR)/$(CONFIG)/sockaddr_resolver_test \
  $(BINDIR)/$(CONFIG)/sockaddr_utils_test \
  $(BINDIR)/$(CONFIG)/socket_utils_test \
  $(BINDIR)/$(CONFIG)/tcp_client_posix_test \
  $(BINDIR)/$(CONFIG)/tcp_posix_test \
  $(BINDIR)/$(CONFIG)/tcp_server_posix_test \
  $(BINDIR)/$(CONFIG)/time_averaged_stats_test \
  $(BINDIR)/$(CONFIG)/timeout_encoding_test \
  $(BINDIR)/$(CONFIG)/timer_heap_test \
  $(BINDIR)/$(CONFIG)/timer_list_test \
  $(BINDIR)/$(CONFIG)/transport_connectivity_state_test \
  $(BINDIR)/$(CONFIG)/transport_metadata_test \
  $(BINDIR)/$(CONFIG)/transport_pid_controller_test \
  $(BINDIR)/$(CONFIG)/transport_security_test \
  $(BINDIR)/$(CONFIG)/udp_server_test \
  $(BINDIR)/$(CONFIG)/uri_parser_test \
  $(BINDIR)/$(CONFIG)/wakeup_fd_cv_test \
  $(BINDIR)/$(CONFIG)/public_headers_must_be_c89 \
  $(BINDIR)/$(CONFIG)/badreq_bad_client_test \
  $(BINDIR)/$(CONFIG)/connection_prefix_bad_client_test \
  $(BINDIR)/$(CONFIG)/head_of_line_blocking_bad_client_test \
  $(BINDIR)/$(CONFIG)/headers_bad_client_test \
  $(BINDIR)/$(CONFIG)/initial_settings_frame_bad_client_test \
  $(BINDIR)/$(CONFIG)/large_metadata_bad_client_test \
  $(BINDIR)/$(CONFIG)/server_registered_method_bad_client_test \
  $(BINDIR)/$(CONFIG)/simple_request_bad_client_test \
  $(BINDIR)/$(CONFIG)/unknown_frame_bad_client_test \
  $(BINDIR)/$(CONFIG)/window_overflow_bad_client_test \
  $(BINDIR)/$(CONFIG)/bad_ssl_cert_server \
  $(BINDIR)/$(CONFIG)/bad_ssl_cert_test \
  $(BINDIR)/$(CONFIG)/h2_census_test \
  $(BINDIR)/$(CONFIG)/h2_compress_test \
  $(BINDIR)/$(CONFIG)/h2_fakesec_test \
  $(BINDIR)/$(CONFIG)/h2_fd_test \
  $(BINDIR)/$(CONFIG)/h2_full_test \
  $(BINDIR)/$(CONFIG)/h2_full+pipe_test \
  $(BINDIR)/$(CONFIG)/h2_full+trace_test \
  $(BINDIR)/$(CONFIG)/h2_http_proxy_test \
  $(BINDIR)/$(CONFIG)/h2_load_reporting_test \
  $(BINDIR)/$(CONFIG)/h2_oauth2_test \
  $(BINDIR)/$(CONFIG)/h2_proxy_test \
  $(BINDIR)/$(CONFIG)/h2_sockpair_test \
  $(BINDIR)/$(CONFIG)/h2_sockpair+trace_test \
  $(BINDIR)/$(CONFIG)/h2_sockpair_1byte_test \
  $(BINDIR)/$(CONFIG)/h2_ssl_test \
  $(BINDIR)/$(CONFIG)/h2_ssl_cert_test \
  $(BINDIR)/$(CONFIG)/h2_ssl_proxy_test \
  $(BINDIR)/$(CONFIG)/h2_uds_test \
  $(BINDIR)/$(CONFIG)/h2_census_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_compress_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_fd_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_full_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_full+pipe_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_full+trace_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_http_proxy_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_load_reporting_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_proxy_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_sockpair_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_sockpair+trace_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_sockpair_1byte_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_uds_nosec_test \
  $(BINDIR)/$(CONFIG)/api_fuzzer_one_entry \
  $(BINDIR)/$(CONFIG)/client_fuzzer_one_entry \
  $(BINDIR)/$(CONFIG)/hpack_parser_fuzzer_test_one_entry \
  $(BINDIR)/$(CONFIG)/http_request_fuzzer_test_one_entry \
  $(BINDIR)/$(CONFIG)/http_response_fuzzer_test_one_entry \
  $(BINDIR)/$(CONFIG)/json_fuzzer_test_one_entry \
  $(BINDIR)/$(CONFIG)/nanopb_fuzzer_response_test_one_entry \
  $(BINDIR)/$(CONFIG)/nanopb_fuzzer_serverlist_test_one_entry \
  $(BINDIR)/$(CONFIG)/percent_decode_fuzzer_one_entry \
  $(BINDIR)/$(CONFIG)/percent_encode_fuzzer_one_entry \
  $(BINDIR)/$(CONFIG)/server_fuzzer_one_entry \
  $(BINDIR)/$(CONFIG)/ssl_server_fuzzer_one_entry \
  $(BINDIR)/$(CONFIG)/uri_fuzzer_test_one_entry \


ifeq ($(EMBED_OPENSSL),true)
buildtests_cxx: privatelibs_cxx \
  $(BINDIR)/$(CONFIG)/alarm_cpp_test \
  $(BINDIR)/$(CONFIG)/async_end2end_test \
  $(BINDIR)/$(CONFIG)/auth_property_iterator_test \
  $(BINDIR)/$(CONFIG)/bm_fullstack \
  $(BINDIR)/$(CONFIG)/channel_arguments_test \
  $(BINDIR)/$(CONFIG)/channel_filter_test \
  $(BINDIR)/$(CONFIG)/cli_call_test \
  $(BINDIR)/$(CONFIG)/client_crash_test \
  $(BINDIR)/$(CONFIG)/client_crash_test_server \
  $(BINDIR)/$(CONFIG)/codegen_test_full \
  $(BINDIR)/$(CONFIG)/codegen_test_minimal \
  $(BINDIR)/$(CONFIG)/credentials_test \
  $(BINDIR)/$(CONFIG)/cxx_byte_buffer_test \
  $(BINDIR)/$(CONFIG)/cxx_slice_test \
  $(BINDIR)/$(CONFIG)/cxx_string_ref_test \
  $(BINDIR)/$(CONFIG)/cxx_time_test \
  $(BINDIR)/$(CONFIG)/end2end_test \
  $(BINDIR)/$(CONFIG)/filter_end2end_test \
  $(BINDIR)/$(CONFIG)/generic_end2end_test \
  $(BINDIR)/$(CONFIG)/golden_file_test \
  $(BINDIR)/$(CONFIG)/grpc_cli \
  $(BINDIR)/$(CONFIG)/grpc_tool_test \
  $(BINDIR)/$(CONFIG)/grpclb_api_test \
  $(BINDIR)/$(CONFIG)/grpclb_test \
  $(BINDIR)/$(CONFIG)/hybrid_end2end_test \
  $(BINDIR)/$(CONFIG)/interop_client \
  $(BINDIR)/$(CONFIG)/interop_server \
  $(BINDIR)/$(CONFIG)/interop_test \
  $(BINDIR)/$(CONFIG)/json_run_localhost \
  $(BINDIR)/$(CONFIG)/metrics_client \
  $(BINDIR)/$(CONFIG)/mock_test \
  $(BINDIR)/$(CONFIG)/noop-benchmark \
  $(BINDIR)/$(CONFIG)/proto_server_reflection_test \
  $(BINDIR)/$(CONFIG)/qps_interarrival_test \
  $(BINDIR)/$(CONFIG)/qps_json_driver \
  $(BINDIR)/$(CONFIG)/qps_openloop_test \
  $(BINDIR)/$(CONFIG)/qps_worker \
  $(BINDIR)/$(CONFIG)/reconnect_interop_client \
  $(BINDIR)/$(CONFIG)/reconnect_interop_server \
  $(BINDIR)/$(CONFIG)/round_robin_end2end_test \
  $(BINDIR)/$(CONFIG)/secure_auth_context_test \
  $(BINDIR)/$(CONFIG)/secure_sync_unary_ping_pong_test \
  $(BINDIR)/$(CONFIG)/server_builder_plugin_test \
  $(BINDIR)/$(CONFIG)/server_context_test_spouse_test \
  $(BINDIR)/$(CONFIG)/server_crash_test \
  $(BINDIR)/$(CONFIG)/server_crash_test_client \
  $(BINDIR)/$(CONFIG)/shutdown_test \
  $(BINDIR)/$(CONFIG)/status_test \
  $(BINDIR)/$(CONFIG)/streaming_throughput_test \
  $(BINDIR)/$(CONFIG)/stress_test \
  $(BINDIR)/$(CONFIG)/thread_manager_test \
  $(BINDIR)/$(CONFIG)/thread_stress_test \
  $(BINDIR)/$(CONFIG)/boringssl_aes_test \
  $(BINDIR)/$(CONFIG)/boringssl_asn1_test \
  $(BINDIR)/$(CONFIG)/boringssl_base64_test \
  $(BINDIR)/$(CONFIG)/boringssl_bio_test \
  $(BINDIR)/$(CONFIG)/boringssl_bn_test \
  $(BINDIR)/$(CONFIG)/boringssl_bytestring_test \
  $(BINDIR)/$(CONFIG)/boringssl_aead_test \
  $(BINDIR)/$(CONFIG)/boringssl_cipher_test \
  $(BINDIR)/$(CONFIG)/boringssl_cmac_test \
  $(BINDIR)/$(CONFIG)/boringssl_constant_time_test \
  $(BINDIR)/$(CONFIG)/boringssl_ed25519_test \
  $(BINDIR)/$(CONFIG)/boringssl_x25519_test \
  $(BINDIR)/$(CONFIG)/boringssl_dh_test \
  $(BINDIR)/$(CONFIG)/boringssl_digest_test \
  $(BINDIR)/$(CONFIG)/boringssl_dsa_test \
  $(BINDIR)/$(CONFIG)/boringssl_ec_test \
  $(BINDIR)/$(CONFIG)/boringssl_example_mul \
  $(BINDIR)/$(CONFIG)/boringssl_ecdsa_test \
  $(BINDIR)/$(CONFIG)/boringssl_err_test \
  $(BINDIR)/$(CONFIG)/boringssl_evp_extra_test \
  $(BINDIR)/$(CONFIG)/boringssl_evp_test \
  $(BINDIR)/$(CONFIG)/boringssl_pbkdf_test \
  $(BINDIR)/$(CONFIG)/boringssl_hkdf_test \
  $(BINDIR)/$(CONFIG)/boringssl_hmac_test \
  $(BINDIR)/$(CONFIG)/boringssl_lhash_test \
  $(BINDIR)/$(CONFIG)/boringssl_gcm_test \
  $(BINDIR)/$(CONFIG)/boringssl_pkcs12_test \
  $(BINDIR)/$(CONFIG)/boringssl_pkcs8_test \
  $(BINDIR)/$(CONFIG)/boringssl_poly1305_test \
  $(BINDIR)/$(CONFIG)/boringssl_refcount_test \
  $(BINDIR)/$(CONFIG)/boringssl_rsa_test \
  $(BINDIR)/$(CONFIG)/boringssl_thread_test \
  $(BINDIR)/$(CONFIG)/boringssl_pkcs7_test \
  $(BINDIR)/$(CONFIG)/boringssl_x509_test \
  $(BINDIR)/$(CONFIG)/boringssl_tab_test \
  $(BINDIR)/$(CONFIG)/boringssl_v3name_test \
  $(BINDIR)/$(CONFIG)/boringssl_pqueue_test \
  $(BINDIR)/$(CONFIG)/boringssl_ssl_test \

else
buildtests_cxx: privatelibs_cxx \
  $(BINDIR)/$(CONFIG)/alarm_cpp_test \
  $(BINDIR)/$(CONFIG)/async_end2end_test \
  $(BINDIR)/$(CONFIG)/auth_property_iterator_test \
  $(BINDIR)/$(CONFIG)/bm_fullstack \
  $(BINDIR)/$(CONFIG)/channel_arguments_test \
  $(BINDIR)/$(CONFIG)/channel_filter_test \
  $(BINDIR)/$(CONFIG)/cli_call_test \
  $(BINDIR)/$(CONFIG)/client_crash_test \
  $(BINDIR)/$(CONFIG)/client_crash_test_server \
  $(BINDIR)/$(CONFIG)/codegen_test_full \
  $(BINDIR)/$(CONFIG)/codegen_test_minimal \
  $(BINDIR)/$(CONFIG)/credentials_test \
  $(BINDIR)/$(CONFIG)/cxx_byte_buffer_test \
  $(BINDIR)/$(CONFIG)/cxx_slice_test \
  $(BINDIR)/$(CONFIG)/cxx_string_ref_test \
  $(BINDIR)/$(CONFIG)/cxx_time_test \
  $(BINDIR)/$(CONFIG)/end2end_test \
  $(BINDIR)/$(CONFIG)/filter_end2end_test \
  $(BINDIR)/$(CONFIG)/generic_end2end_test \
  $(BINDIR)/$(CONFIG)/golden_file_test \
  $(BINDIR)/$(CONFIG)/grpc_cli \
  $(BINDIR)/$(CONFIG)/grpc_tool_test \
  $(BINDIR)/$(CONFIG)/grpclb_api_test \
  $(BINDIR)/$(CONFIG)/grpclb_test \
  $(BINDIR)/$(CONFIG)/hybrid_end2end_test \
  $(BINDIR)/$(CONFIG)/interop_client \
  $(BINDIR)/$(CONFIG)/interop_server \
  $(BINDIR)/$(CONFIG)/interop_test \
  $(BINDIR)/$(CONFIG)/json_run_localhost \
  $(BINDIR)/$(CONFIG)/metrics_client \
  $(BINDIR)/$(CONFIG)/mock_test \
  $(BINDIR)/$(CONFIG)/noop-benchmark \
  $(BINDIR)/$(CONFIG)/proto_server_reflection_test \
  $(BINDIR)/$(CONFIG)/qps_interarrival_test \
  $(BINDIR)/$(CONFIG)/qps_json_driver \
  $(BINDIR)/$(CONFIG)/qps_openloop_test \
  $(BINDIR)/$(CONFIG)/qps_worker \
  $(BINDIR)/$(CONFIG)/reconnect_interop_client \
  $(BINDIR)/$(CONFIG)/reconnect_interop_server \
  $(BINDIR)/$(CONFIG)/round_robin_end2end_test \
  $(BINDIR)/$(CONFIG)/secure_auth_context_test \
  $(BINDIR)/$(CONFIG)/secure_sync_unary_ping_pong_test \
  $(BINDIR)/$(CONFIG)/server_builder_plugin_test \
  $(BINDIR)/$(CONFIG)/server_context_test_spouse_test \
  $(BINDIR)/$(CONFIG)/server_crash_test \
  $(BINDIR)/$(CONFIG)/server_crash_test_client \
  $(BINDIR)/$(CONFIG)/shutdown_test \
  $(BINDIR)/$(CONFIG)/status_test \
  $(BINDIR)/$(CONFIG)/streaming_throughput_test \
  $(BINDIR)/$(CONFIG)/stress_test \
  $(BINDIR)/$(CONFIG)/thread_manager_test \
  $(BINDIR)/$(CONFIG)/thread_stress_test \

endif


test: test_c test_cxx

flaky_test: flaky_test_c flaky_test_cxx

test_c: buildtests_c
	$(E) "[RUN]     Testing alarm_test"
	$(Q) $(BINDIR)/$(CONFIG)/alarm_test || ( echo test alarm_test failed ; exit 1 )
	$(E) "[RUN]     Testing algorithm_test"
	$(Q) $(BINDIR)/$(CONFIG)/algorithm_test || ( echo test algorithm_test failed ; exit 1 )
	$(E) "[RUN]     Testing alloc_test"
	$(Q) $(BINDIR)/$(CONFIG)/alloc_test || ( echo test alloc_test failed ; exit 1 )
	$(E) "[RUN]     Testing alpn_test"
	$(Q) $(BINDIR)/$(CONFIG)/alpn_test || ( echo test alpn_test failed ; exit 1 )
	$(E) "[RUN]     Testing bad_server_response_test"
	$(Q) $(BINDIR)/$(CONFIG)/bad_server_response_test || ( echo test bad_server_response_test failed ; exit 1 )
	$(E) "[RUN]     Testing bin_decoder_test"
	$(Q) $(BINDIR)/$(CONFIG)/bin_decoder_test || ( echo test bin_decoder_test failed ; exit 1 )
	$(E) "[RUN]     Testing bin_encoder_test"
	$(Q) $(BINDIR)/$(CONFIG)/bin_encoder_test || ( echo test bin_encoder_test failed ; exit 1 )
	$(E) "[RUN]     Testing census_context_test"
	$(Q) $(BINDIR)/$(CONFIG)/census_context_test || ( echo test census_context_test failed ; exit 1 )
	$(E) "[RUN]     Testing census_resource_test"
	$(Q) $(BINDIR)/$(CONFIG)/census_resource_test || ( echo test census_resource_test failed ; exit 1 )
	$(E) "[RUN]     Testing census_trace_context_test"
	$(Q) $(BINDIR)/$(CONFIG)/census_trace_context_test || ( echo test census_trace_context_test failed ; exit 1 )
	$(E) "[RUN]     Testing channel_create_test"
	$(Q) $(BINDIR)/$(CONFIG)/channel_create_test || ( echo test channel_create_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_hpack_encoder_test"
	$(Q) $(BINDIR)/$(CONFIG)/chttp2_hpack_encoder_test || ( echo test chttp2_hpack_encoder_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_status_conversion_test"
	$(Q) $(BINDIR)/$(CONFIG)/chttp2_status_conversion_test || ( echo test chttp2_status_conversion_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_stream_map_test"
	$(Q) $(BINDIR)/$(CONFIG)/chttp2_stream_map_test || ( echo test chttp2_stream_map_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_varint_test"
	$(Q) $(BINDIR)/$(CONFIG)/chttp2_varint_test || ( echo test chttp2_varint_test failed ; exit 1 )
	$(E) "[RUN]     Testing combiner_test"
	$(Q) $(BINDIR)/$(CONFIG)/combiner_test || ( echo test combiner_test failed ; exit 1 )
	$(E) "[RUN]     Testing compression_test"
	$(Q) $(BINDIR)/$(CONFIG)/compression_test || ( echo test compression_test failed ; exit 1 )
	$(E) "[RUN]     Testing concurrent_connectivity_test"
	$(Q) $(BINDIR)/$(CONFIG)/concurrent_connectivity_test || ( echo test concurrent_connectivity_test failed ; exit 1 )
	$(E) "[RUN]     Testing connection_refused_test"
	$(Q) $(BINDIR)/$(CONFIG)/connection_refused_test || ( echo test connection_refused_test failed ; exit 1 )
	$(E) "[RUN]     Testing dns_resolver_connectivity_test"
	$(Q) $(BINDIR)/$(CONFIG)/dns_resolver_connectivity_test || ( echo test dns_resolver_connectivity_test failed ; exit 1 )
	$(E) "[RUN]     Testing dns_resolver_test"
	$(Q) $(BINDIR)/$(CONFIG)/dns_resolver_test || ( echo test dns_resolver_test failed ; exit 1 )
	$(E) "[RUN]     Testing dualstack_socket_test"
	$(Q) $(BINDIR)/$(CONFIG)/dualstack_socket_test || ( echo test dualstack_socket_test failed ; exit 1 )
	$(E) "[RUN]     Testing endpoint_pair_test"
	$(Q) $(BINDIR)/$(CONFIG)/endpoint_pair_test || ( echo test endpoint_pair_test failed ; exit 1 )
	$(E) "[RUN]     Testing ev_epoll_linux_test"
	$(Q) $(BINDIR)/$(CONFIG)/ev_epoll_linux_test || ( echo test ev_epoll_linux_test failed ; exit 1 )
	$(E) "[RUN]     Testing fd_conservation_posix_test"
	$(Q) $(BINDIR)/$(CONFIG)/fd_conservation_posix_test || ( echo test fd_conservation_posix_test failed ; exit 1 )
	$(E) "[RUN]     Testing fd_posix_test"
	$(Q) $(BINDIR)/$(CONFIG)/fd_posix_test || ( echo test fd_posix_test failed ; exit 1 )
	$(E) "[RUN]     Testing fling_stream_test"
	$(Q) $(BINDIR)/$(CONFIG)/fling_stream_test || ( echo test fling_stream_test failed ; exit 1 )
	$(E) "[RUN]     Testing fling_test"
	$(Q) $(BINDIR)/$(CONFIG)/fling_test || ( echo test fling_test failed ; exit 1 )
	$(E) "[RUN]     Testing goaway_server_test"
	$(Q) $(BINDIR)/$(CONFIG)/goaway_server_test || ( echo test goaway_server_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_avl_test"
	$(Q) $(BINDIR)/$(CONFIG)/gpr_avl_test || ( echo test gpr_avl_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_backoff_test"
	$(Q) $(BINDIR)/$(CONFIG)/gpr_backoff_test || ( echo test gpr_backoff_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_cmdline_test"
	$(Q) $(BINDIR)/$(CONFIG)/gpr_cmdline_test || ( echo test gpr_cmdline_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_cpu_test"
	$(Q) $(BINDIR)/$(CONFIG)/gpr_cpu_test || ( echo test gpr_cpu_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_env_test"
	$(Q) $(BINDIR)/$(CONFIG)/gpr_env_test || ( echo test gpr_env_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_histogram_test"
	$(Q) $(BINDIR)/$(CONFIG)/gpr_histogram_test || ( echo test gpr_histogram_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_host_port_test"
	$(Q) $(BINDIR)/$(CONFIG)/gpr_host_port_test || ( echo test gpr_host_port_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_log_test"
	$(Q) $(BINDIR)/$(CONFIG)/gpr_log_test || ( echo test gpr_log_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_mpscq_test"
	$(Q) $(BINDIR)/$(CONFIG)/gpr_mpscq_test || ( echo test gpr_mpscq_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_stack_lockfree_test"
	$(Q) $(BINDIR)/$(CONFIG)/gpr_stack_lockfree_test || ( echo test gpr_stack_lockfree_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_string_test"
	$(Q) $(BINDIR)/$(CONFIG)/gpr_string_test || ( echo test gpr_string_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_sync_test"
	$(Q) $(BINDIR)/$(CONFIG)/gpr_sync_test || ( echo test gpr_sync_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_thd_test"
	$(Q) $(BINDIR)/$(CONFIG)/gpr_thd_test || ( echo test gpr_thd_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_time_test"
	$(Q) $(BINDIR)/$(CONFIG)/gpr_time_test || ( echo test gpr_time_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_tls_test"
	$(Q) $(BINDIR)/$(CONFIG)/gpr_tls_test || ( echo test gpr_tls_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_useful_test"
	$(Q) $(BINDIR)/$(CONFIG)/gpr_useful_test || ( echo test gpr_useful_test failed ; exit 1 )
	$(E) "[RUN]     Testing grpc_auth_context_test"
	$(Q) $(BINDIR)/$(CONFIG)/grpc_auth_context_test || ( echo test grpc_auth_context_test failed ; exit 1 )
	$(E) "[RUN]     Testing grpc_b64_test"
	$(Q) $(BINDIR)/$(CONFIG)/grpc_b64_test || ( echo test grpc_b64_test failed ; exit 1 )
	$(E) "[RUN]     Testing grpc_byte_buffer_reader_test"
	$(Q) $(BINDIR)/$(CONFIG)/grpc_byte_buffer_reader_test || ( echo test grpc_byte_buffer_reader_test failed ; exit 1 )
	$(E) "[RUN]     Testing grpc_channel_args_test"
	$(Q) $(BINDIR)/$(CONFIG)/grpc_channel_args_test || ( echo test grpc_channel_args_test failed ; exit 1 )
	$(E) "[RUN]     Testing grpc_channel_stack_test"
	$(Q) $(BINDIR)/$(CONFIG)/grpc_channel_stack_test || ( echo test grpc_channel_stack_test failed ; exit 1 )
	$(E) "[RUN]     Testing grpc_completion_queue_test"
	$(Q) $(BINDIR)/$(CONFIG)/grpc_completion_queue_test || ( echo test grpc_completion_queue_test failed ; exit 1 )
	$(E) "[RUN]     Testing grpc_credentials_test"
	$(Q) $(BINDIR)/$(CONFIG)/grpc_credentials_test || ( echo test grpc_credentials_test failed ; exit 1 )
	$(E) "[RUN]     Testing grpc_invalid_channel_args_test"
	$(Q) $(BINDIR)/$(CONFIG)/grpc_invalid_channel_args_test || ( echo test grpc_invalid_channel_args_test failed ; exit 1 )
	$(E) "[RUN]     Testing grpc_json_token_test"
	$(Q) $(BINDIR)/$(CONFIG)/grpc_json_token_test || ( echo test grpc_json_token_test failed ; exit 1 )
	$(E) "[RUN]     Testing grpc_jwt_verifier_test"
	$(Q) $(BINDIR)/$(CONFIG)/grpc_jwt_verifier_test || ( echo test grpc_jwt_verifier_test failed ; exit 1 )
	$(E) "[RUN]     Testing grpc_security_connector_test"
	$(Q) $(BINDIR)/$(CONFIG)/grpc_security_connector_test || ( echo test grpc_security_connector_test failed ; exit 1 )
	$(E) "[RUN]     Testing handshake_client"
	$(Q) $(BINDIR)/$(CONFIG)/handshake_client || ( echo test handshake_client failed ; exit 1 )
	$(E) "[RUN]     Testing handshake_server"
	$(Q) $(BINDIR)/$(CONFIG)/handshake_server || ( echo test handshake_server failed ; exit 1 )
	$(E) "[RUN]     Testing hpack_parser_test"
	$(Q) $(BINDIR)/$(CONFIG)/hpack_parser_test || ( echo test hpack_parser_test failed ; exit 1 )
	$(E) "[RUN]     Testing hpack_table_test"
	$(Q) $(BINDIR)/$(CONFIG)/hpack_table_test || ( echo test hpack_table_test failed ; exit 1 )
	$(E) "[RUN]     Testing http_parser_test"
	$(Q) $(BINDIR)/$(CONFIG)/http_parser_test || ( echo test http_parser_test failed ; exit 1 )
	$(E) "[RUN]     Testing httpcli_format_request_test"
	$(Q) $(BINDIR)/$(CONFIG)/httpcli_format_request_test || ( echo test httpcli_format_request_test failed ; exit 1 )
	$(E) "[RUN]     Testing httpcli_test"
	$(Q) $(BINDIR)/$(CONFIG)/httpcli_test || ( echo test httpcli_test failed ; exit 1 )
	$(E) "[RUN]     Testing httpscli_test"
	$(Q) $(BINDIR)/$(CONFIG)/httpscli_test || ( echo test httpscli_test failed ; exit 1 )
	$(E) "[RUN]     Testing init_test"
	$(Q) $(BINDIR)/$(CONFIG)/init_test || ( echo test init_test failed ; exit 1 )
	$(E) "[RUN]     Testing invalid_call_argument_test"
	$(Q) $(BINDIR)/$(CONFIG)/invalid_call_argument_test || ( echo test invalid_call_argument_test failed ; exit 1 )
	$(E) "[RUN]     Testing json_rewrite_test"
	$(Q) $(BINDIR)/$(CONFIG)/json_rewrite_test || ( echo test json_rewrite_test failed ; exit 1 )
	$(E) "[RUN]     Testing json_stream_error_test"
	$(Q) $(BINDIR)/$(CONFIG)/json_stream_error_test || ( echo test json_stream_error_test failed ; exit 1 )
	$(E) "[RUN]     Testing json_test"
	$(Q) $(BINDIR)/$(CONFIG)/json_test || ( echo test json_test failed ; exit 1 )
	$(E) "[RUN]     Testing lame_client_test"
	$(Q) $(BINDIR)/$(CONFIG)/lame_client_test || ( echo test lame_client_test failed ; exit 1 )
	$(E) "[RUN]     Testing load_file_test"
	$(Q) $(BINDIR)/$(CONFIG)/load_file_test || ( echo test load_file_test failed ; exit 1 )
	$(E) "[RUN]     Testing message_compress_test"
	$(Q) $(BINDIR)/$(CONFIG)/message_compress_test || ( echo test message_compress_test failed ; exit 1 )
	$(E) "[RUN]     Testing multiple_server_queues_test"
	$(Q) $(BINDIR)/$(CONFIG)/multiple_server_queues_test || ( echo test multiple_server_queues_test failed ; exit 1 )
	$(E) "[RUN]     Testing murmur_hash_test"
	$(Q) $(BINDIR)/$(CONFIG)/murmur_hash_test || ( echo test murmur_hash_test failed ; exit 1 )
	$(E) "[RUN]     Testing no_server_test"
	$(Q) $(BINDIR)/$(CONFIG)/no_server_test || ( echo test no_server_test failed ; exit 1 )
	$(E) "[RUN]     Testing percent_encoding_test"
	$(Q) $(BINDIR)/$(CONFIG)/percent_encoding_test || ( echo test percent_encoding_test failed ; exit 1 )
	$(E) "[RUN]     Testing resolve_address_test"
	$(Q) $(BINDIR)/$(CONFIG)/resolve_address_test || ( echo test resolve_address_test failed ; exit 1 )
	$(E) "[RUN]     Testing resource_quota_test"
	$(Q) $(BINDIR)/$(CONFIG)/resource_quota_test || ( echo test resource_quota_test failed ; exit 1 )
	$(E) "[RUN]     Testing secure_channel_create_test"
	$(Q) $(BINDIR)/$(CONFIG)/secure_channel_create_test || ( echo test secure_channel_create_test failed ; exit 1 )
	$(E) "[RUN]     Testing secure_endpoint_test"
	$(Q) $(BINDIR)/$(CONFIG)/secure_endpoint_test || ( echo test secure_endpoint_test failed ; exit 1 )
	$(E) "[RUN]     Testing sequential_connectivity_test"
	$(Q) $(BINDIR)/$(CONFIG)/sequential_connectivity_test || ( echo test sequential_connectivity_test failed ; exit 1 )
	$(E) "[RUN]     Testing server_chttp2_test"
	$(Q) $(BINDIR)/$(CONFIG)/server_chttp2_test || ( echo test server_chttp2_test failed ; exit 1 )
	$(E) "[RUN]     Testing server_test"
	$(Q) $(BINDIR)/$(CONFIG)/server_test || ( echo test server_test failed ; exit 1 )
	$(E) "[RUN]     Testing set_initial_connect_string_test"
	$(Q) $(BINDIR)/$(CONFIG)/set_initial_connect_string_test || ( echo test set_initial_connect_string_test failed ; exit 1 )
	$(E) "[RUN]     Testing slice_buffer_test"
	$(Q) $(BINDIR)/$(CONFIG)/slice_buffer_test || ( echo test slice_buffer_test failed ; exit 1 )
	$(E) "[RUN]     Testing slice_string_helpers_test"
	$(Q) $(BINDIR)/$(CONFIG)/slice_string_helpers_test || ( echo test slice_string_helpers_test failed ; exit 1 )
	$(E) "[RUN]     Testing slice_test"
	$(Q) $(BINDIR)/$(CONFIG)/slice_test || ( echo test slice_test failed ; exit 1 )
	$(E) "[RUN]     Testing sockaddr_resolver_test"
	$(Q) $(BINDIR)/$(CONFIG)/sockaddr_resolver_test || ( echo test sockaddr_resolver_test failed ; exit 1 )
	$(E) "[RUN]     Testing sockaddr_utils_test"
	$(Q) $(BINDIR)/$(CONFIG)/sockaddr_utils_test || ( echo test sockaddr_utils_test failed ; exit 1 )
	$(E) "[RUN]     Testing socket_utils_test"
	$(Q) $(BINDIR)/$(CONFIG)/socket_utils_test || ( echo test socket_utils_test failed ; exit 1 )
	$(E) "[RUN]     Testing tcp_client_posix_test"
	$(Q) $(BINDIR)/$(CONFIG)/tcp_client_posix_test || ( echo test tcp_client_posix_test failed ; exit 1 )
	$(E) "[RUN]     Testing tcp_posix_test"
	$(Q) $(BINDIR)/$(CONFIG)/tcp_posix_test || ( echo test tcp_posix_test failed ; exit 1 )
	$(E) "[RUN]     Testing tcp_server_posix_test"
	$(Q) $(BINDIR)/$(CONFIG)/tcp_server_posix_test || ( echo test tcp_server_posix_test failed ; exit 1 )
	$(E) "[RUN]     Testing time_averaged_stats_test"
	$(Q) $(BINDIR)/$(CONFIG)/time_averaged_stats_test || ( echo test time_averaged_stats_test failed ; exit 1 )
	$(E) "[RUN]     Testing timeout_encoding_test"
	$(Q) $(BINDIR)/$(CONFIG)/timeout_encoding_test || ( echo test timeout_encoding_test failed ; exit 1 )
	$(E) "[RUN]     Testing timer_heap_test"
	$(Q) $(BINDIR)/$(CONFIG)/timer_heap_test || ( echo test timer_heap_test failed ; exit 1 )
	$(E) "[RUN]     Testing timer_list_test"
	$(Q) $(BINDIR)/$(CONFIG)/timer_list_test || ( echo test timer_list_test failed ; exit 1 )
	$(E) "[RUN]     Testing transport_connectivity_state_test"
	$(Q) $(BINDIR)/$(CONFIG)/transport_connectivity_state_test || ( echo test transport_connectivity_state_test failed ; exit 1 )
	$(E) "[RUN]     Testing transport_metadata_test"
	$(Q) $(BINDIR)/$(CONFIG)/transport_metadata_test || ( echo test transport_metadata_test failed ; exit 1 )
	$(E) "[RUN]     Testing transport_pid_controller_test"
	$(Q) $(BINDIR)/$(CONFIG)/transport_pid_controller_test || ( echo test transport_pid_controller_test failed ; exit 1 )
	$(E) "[RUN]     Testing transport_security_test"
	$(Q) $(BINDIR)/$(CONFIG)/transport_security_test || ( echo test transport_security_test failed ; exit 1 )
	$(E) "[RUN]     Testing udp_server_test"
	$(Q) $(BINDIR)/$(CONFIG)/udp_server_test || ( echo test udp_server_test failed ; exit 1 )
	$(E) "[RUN]     Testing uri_parser_test"
	$(Q) $(BINDIR)/$(CONFIG)/uri_parser_test || ( echo test uri_parser_test failed ; exit 1 )
	$(E) "[RUN]     Testing wakeup_fd_cv_test"
	$(Q) $(BINDIR)/$(CONFIG)/wakeup_fd_cv_test || ( echo test wakeup_fd_cv_test failed ; exit 1 )
	$(E) "[RUN]     Testing public_headers_must_be_c89"
	$(Q) $(BINDIR)/$(CONFIG)/public_headers_must_be_c89 || ( echo test public_headers_must_be_c89 failed ; exit 1 )
	$(E) "[RUN]     Testing badreq_bad_client_test"
	$(Q) $(BINDIR)/$(CONFIG)/badreq_bad_client_test || ( echo test badreq_bad_client_test failed ; exit 1 )
	$(E) "[RUN]     Testing connection_prefix_bad_client_test"
	$(Q) $(BINDIR)/$(CONFIG)/connection_prefix_bad_client_test || ( echo test connection_prefix_bad_client_test failed ; exit 1 )
	$(E) "[RUN]     Testing head_of_line_blocking_bad_client_test"
	$(Q) $(BINDIR)/$(CONFIG)/head_of_line_blocking_bad_client_test || ( echo test head_of_line_blocking_bad_client_test failed ; exit 1 )
	$(E) "[RUN]     Testing headers_bad_client_test"
	$(Q) $(BINDIR)/$(CONFIG)/headers_bad_client_test || ( echo test headers_bad_client_test failed ; exit 1 )
	$(E) "[RUN]     Testing initial_settings_frame_bad_client_test"
	$(Q) $(BINDIR)/$(CONFIG)/initial_settings_frame_bad_client_test || ( echo test initial_settings_frame_bad_client_test failed ; exit 1 )
	$(E) "[RUN]     Testing large_metadata_bad_client_test"
	$(Q) $(BINDIR)/$(CONFIG)/large_metadata_bad_client_test || ( echo test large_metadata_bad_client_test failed ; exit 1 )
	$(E) "[RUN]     Testing server_registered_method_bad_client_test"
	$(Q) $(BINDIR)/$(CONFIG)/server_registered_method_bad_client_test || ( echo test server_registered_method_bad_client_test failed ; exit 1 )
	$(E) "[RUN]     Testing simple_request_bad_client_test"
	$(Q) $(BINDIR)/$(CONFIG)/simple_request_bad_client_test || ( echo test simple_request_bad_client_test failed ; exit 1 )
	$(E) "[RUN]     Testing unknown_frame_bad_client_test"
	$(Q) $(BINDIR)/$(CONFIG)/unknown_frame_bad_client_test || ( echo test unknown_frame_bad_client_test failed ; exit 1 )
	$(E) "[RUN]     Testing window_overflow_bad_client_test"
	$(Q) $(BINDIR)/$(CONFIG)/window_overflow_bad_client_test || ( echo test window_overflow_bad_client_test failed ; exit 1 )
	$(E) "[RUN]     Testing bad_ssl_cert_test"
	$(Q) $(BINDIR)/$(CONFIG)/bad_ssl_cert_test || ( echo test bad_ssl_cert_test failed ; exit 1 )


flaky_test_c: buildtests_c
	$(E) "[RUN]     Testing lb_policies_test"
	$(Q) $(BINDIR)/$(CONFIG)/lb_policies_test || ( echo test lb_policies_test failed ; exit 1 )
	$(E) "[RUN]     Testing mlog_test"
	$(Q) $(BINDIR)/$(CONFIG)/mlog_test || ( echo test mlog_test failed ; exit 1 )


test_cxx: buildtests_cxx
	$(E) "[RUN]     Testing alarm_cpp_test"
	$(Q) $(BINDIR)/$(CONFIG)/alarm_cpp_test || ( echo test alarm_cpp_test failed ; exit 1 )
	$(E) "[RUN]     Testing async_end2end_test"
	$(Q) $(BINDIR)/$(CONFIG)/async_end2end_test || ( echo test async_end2end_test failed ; exit 1 )
	$(E) "[RUN]     Testing auth_property_iterator_test"
	$(Q) $(BINDIR)/$(CONFIG)/auth_property_iterator_test || ( echo test auth_property_iterator_test failed ; exit 1 )
	$(E) "[RUN]     Testing bm_fullstack"
	$(Q) $(BINDIR)/$(CONFIG)/bm_fullstack || ( echo test bm_fullstack failed ; exit 1 )
	$(E) "[RUN]     Testing channel_arguments_test"
	$(Q) $(BINDIR)/$(CONFIG)/channel_arguments_test || ( echo test channel_arguments_test failed ; exit 1 )
	$(E) "[RUN]     Testing channel_filter_test"
	$(Q) $(BINDIR)/$(CONFIG)/channel_filter_test || ( echo test channel_filter_test failed ; exit 1 )
	$(E) "[RUN]     Testing cli_call_test"
	$(Q) $(BINDIR)/$(CONFIG)/cli_call_test || ( echo test cli_call_test failed ; exit 1 )
	$(E) "[RUN]     Testing client_crash_test"
	$(Q) $(BINDIR)/$(CONFIG)/client_crash_test || ( echo test client_crash_test failed ; exit 1 )
	$(E) "[RUN]     Testing codegen_test_full"
	$(Q) $(BINDIR)/$(CONFIG)/codegen_test_full || ( echo test codegen_test_full failed ; exit 1 )
	$(E) "[RUN]     Testing codegen_test_minimal"
	$(Q) $(BINDIR)/$(CONFIG)/codegen_test_minimal || ( echo test codegen_test_minimal failed ; exit 1 )
	$(E) "[RUN]     Testing credentials_test"
	$(Q) $(BINDIR)/$(CONFIG)/credentials_test || ( echo test credentials_test failed ; exit 1 )
	$(E) "[RUN]     Testing cxx_byte_buffer_test"
	$(Q) $(BINDIR)/$(CONFIG)/cxx_byte_buffer_test || ( echo test cxx_byte_buffer_test failed ; exit 1 )
	$(E) "[RUN]     Testing cxx_slice_test"
	$(Q) $(BINDIR)/$(CONFIG)/cxx_slice_test || ( echo test cxx_slice_test failed ; exit 1 )
	$(E) "[RUN]     Testing cxx_string_ref_test"
	$(Q) $(BINDIR)/$(CONFIG)/cxx_string_ref_test || ( echo test cxx_string_ref_test failed ; exit 1 )
	$(E) "[RUN]     Testing cxx_time_test"
	$(Q) $(BINDIR)/$(CONFIG)/cxx_time_test || ( echo test cxx_time_test failed ; exit 1 )
	$(E) "[RUN]     Testing end2end_test"
	$(Q) $(BINDIR)/$(CONFIG)/end2end_test || ( echo test end2end_test failed ; exit 1 )
	$(E) "[RUN]     Testing filter_end2end_test"
	$(Q) $(BINDIR)/$(CONFIG)/filter_end2end_test || ( echo test filter_end2end_test failed ; exit 1 )
	$(E) "[RUN]     Testing generic_end2end_test"
	$(Q) $(BINDIR)/$(CONFIG)/generic_end2end_test || ( echo test generic_end2end_test failed ; exit 1 )
	$(E) "[RUN]     Testing golden_file_test"
	$(Q) $(BINDIR)/$(CONFIG)/golden_file_test || ( echo test golden_file_test failed ; exit 1 )
	$(E) "[RUN]     Testing grpc_tool_test"
	$(Q) $(BINDIR)/$(CONFIG)/grpc_tool_test || ( echo test grpc_tool_test failed ; exit 1 )
	$(E) "[RUN]     Testing grpclb_api_test"
	$(Q) $(BINDIR)/$(CONFIG)/grpclb_api_test || ( echo test grpclb_api_test failed ; exit 1 )
	$(E) "[RUN]     Testing grpclb_test"
	$(Q) $(BINDIR)/$(CONFIG)/grpclb_test || ( echo test grpclb_test failed ; exit 1 )
	$(E) "[RUN]     Testing hybrid_end2end_test"
	$(Q) $(BINDIR)/$(CONFIG)/hybrid_end2end_test || ( echo test hybrid_end2end_test failed ; exit 1 )
	$(E) "[RUN]     Testing interop_test"
	$(Q) $(BINDIR)/$(CONFIG)/interop_test || ( echo test interop_test failed ; exit 1 )
	$(E) "[RUN]     Testing mock_test"
	$(Q) $(BINDIR)/$(CONFIG)/mock_test || ( echo test mock_test failed ; exit 1 )
	$(E) "[RUN]     Testing noop-benchmark"
	$(Q) $(BINDIR)/$(CONFIG)/noop-benchmark || ( echo test noop-benchmark failed ; exit 1 )
	$(E) "[RUN]     Testing proto_server_reflection_test"
	$(Q) $(BINDIR)/$(CONFIG)/proto_server_reflection_test || ( echo test proto_server_reflection_test failed ; exit 1 )
	$(E) "[RUN]     Testing qps_openloop_test"
	$(Q) $(BINDIR)/$(CONFIG)/qps_openloop_test || ( echo test qps_openloop_test failed ; exit 1 )
	$(E) "[RUN]     Testing round_robin_end2end_test"
	$(Q) $(BINDIR)/$(CONFIG)/round_robin_end2end_test || ( echo test round_robin_end2end_test failed ; exit 1 )
	$(E) "[RUN]     Testing secure_auth_context_test"
	$(Q) $(BINDIR)/$(CONFIG)/secure_auth_context_test || ( echo test secure_auth_context_test failed ; exit 1 )
	$(E) "[RUN]     Testing secure_sync_unary_ping_pong_test"
	$(Q) $(BINDIR)/$(CONFIG)/secure_sync_unary_ping_pong_test || ( echo test secure_sync_unary_ping_pong_test failed ; exit 1 )
	$(E) "[RUN]     Testing server_builder_plugin_test"
	$(Q) $(BINDIR)/$(CONFIG)/server_builder_plugin_test || ( echo test server_builder_plugin_test failed ; exit 1 )
	$(E) "[RUN]     Testing server_context_test_spouse_test"
	$(Q) $(BINDIR)/$(CONFIG)/server_context_test_spouse_test || ( echo test server_context_test_spouse_test failed ; exit 1 )
	$(E) "[RUN]     Testing server_crash_test"
	$(Q) $(BINDIR)/$(CONFIG)/server_crash_test || ( echo test server_crash_test failed ; exit 1 )
	$(E) "[RUN]     Testing shutdown_test"
	$(Q) $(BINDIR)/$(CONFIG)/shutdown_test || ( echo test shutdown_test failed ; exit 1 )
	$(E) "[RUN]     Testing status_test"
	$(Q) $(BINDIR)/$(CONFIG)/status_test || ( echo test status_test failed ; exit 1 )
	$(E) "[RUN]     Testing streaming_throughput_test"
	$(Q) $(BINDIR)/$(CONFIG)/streaming_throughput_test || ( echo test streaming_throughput_test failed ; exit 1 )
	$(E) "[RUN]     Testing thread_manager_test"
	$(Q) $(BINDIR)/$(CONFIG)/thread_manager_test || ( echo test thread_manager_test failed ; exit 1 )
	$(E) "[RUN]     Testing thread_stress_test"
	$(Q) $(BINDIR)/$(CONFIG)/thread_stress_test || ( echo test thread_stress_test failed ; exit 1 )


flaky_test_cxx: buildtests_cxx


test_python: static_c
	$(E) "[RUN]     Testing python code"
	$(Q) tools/run_tests/run_tests.py -lpython -c$(CONFIG)


tools: tools_c tools_cxx


tools_c: privatelibs_c $(BINDIR)/$(CONFIG)/gen_hpack_tables $(BINDIR)/$(CONFIG)/gen_legal_metadata_characters $(BINDIR)/$(CONFIG)/gen_percent_encoding_tables $(BINDIR)/$(CONFIG)/grpc_create_jwt $(BINDIR)/$(CONFIG)/grpc_print_google_default_creds_token $(BINDIR)/$(CONFIG)/grpc_verify_jwt

tools_cxx: privatelibs_cxx

buildbenchmarks: privatelibs $(BINDIR)/$(CONFIG)/low_level_ping_pong_benchmark

benchmarks: buildbenchmarks

strip: strip-static strip-shared

strip-static: strip-static_c strip-static_cxx

strip-shared: strip-shared_c strip-shared_cxx


# TODO(nnoble): the strip target is stripping in-place, instead
# of copying files in a temporary folder.
# This prevents proper debugging after running make install.

strip-static_c: static_c
ifeq ($(CONFIG),opt)
	$(E) "[STRIP]   Stripping libgpr.a"
	$(Q) $(STRIP) $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[STRIP]   Stripping libgrpc.a"
	$(Q) $(STRIP) $(LIBDIR)/$(CONFIG)/libgrpc.a
	$(E) "[STRIP]   Stripping libgrpc_cronet.a"
	$(Q) $(STRIP) $(LIBDIR)/$(CONFIG)/libgrpc_cronet.a
	$(E) "[STRIP]   Stripping libgrpc_unsecure.a"
	$(Q) $(STRIP) $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a
endif

strip-static_cxx: static_cxx
ifeq ($(CONFIG),opt)
	$(E) "[STRIP]   Stripping libgrpc++.a"
	$(Q) $(STRIP) $(LIBDIR)/$(CONFIG)/libgrpc++.a
	$(E) "[STRIP]   Stripping libgrpc++_cronet.a"
	$(Q) $(STRIP) $(LIBDIR)/$(CONFIG)/libgrpc++_cronet.a
	$(E) "[STRIP]   Stripping libgrpc++_reflection.a"
	$(Q) $(STRIP) $(LIBDIR)/$(CONFIG)/libgrpc++_reflection.a
	$(E) "[STRIP]   Stripping libgrpc++_unsecure.a"
	$(Q) $(STRIP) $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a
endif

strip-shared_c: shared_c
ifeq ($(CONFIG),opt)
	$(E) "[STRIP]   Stripping $(SHARED_PREFIX)gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)"
	$(Q) $(STRIP) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)
	$(E) "[STRIP]   Stripping $(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)"
	$(Q) $(STRIP) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)
	$(E) "[STRIP]   Stripping $(SHARED_PREFIX)grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)"
	$(Q) $(STRIP) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)
	$(E) "[STRIP]   Stripping $(SHARED_PREFIX)grpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)"
	$(Q) $(STRIP) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)
endif

strip-shared_cxx: shared_cxx
ifeq ($(CONFIG),opt)
	$(E) "[STRIP]   Stripping $(SHARED_PREFIX)grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)"
	$(Q) $(STRIP) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)
	$(E) "[STRIP]   Stripping $(SHARED_PREFIX)grpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)"
	$(Q) $(STRIP) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)
	$(E) "[STRIP]   Stripping $(SHARED_PREFIX)grpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)"
	$(Q) $(STRIP) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)
	$(E) "[STRIP]   Stripping $(SHARED_PREFIX)grpc++_unsecure$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)"
	$(Q) $(STRIP) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_unsecure$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)
endif

strip-shared_csharp: shared_csharp
ifeq ($(CONFIG),opt)
	$(E) "[STRIP]   Stripping $(SHARED_PREFIX)grpc_csharp_ext$(SHARED_VERSION_CSHARP).$(SHARED_EXT_CSHARP)"
	$(Q) $(STRIP) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc_csharp_ext$(SHARED_VERSION_CSHARP).$(SHARED_EXT_CSHARP)
endif

cache.mk::
	$(E) "[MAKE]    Generating $@"
	$(Q) echo "$(CACHE_MK)" | tr , '\n' >$@

$(LIBDIR)/$(CONFIG)/pkgconfig/grpc.pc:
	$(E) "[MAKE]    Generating $@"
	$(Q) mkdir -p $(@D)
	$(Q) echo "$(GRPC_PC_FILE)" | tr , '\n' >$@

$(LIBDIR)/$(CONFIG)/pkgconfig/grpc_unsecure.pc:
	$(E) "[MAKE]    Generating $@"
	$(Q) mkdir -p $(@D)
	$(Q) echo "$(GRPC_UNSECURE_PC_FILE)" | tr , '\n' >$@

$(LIBDIR)/$(CONFIG)/pkgconfig/grpc++.pc:
	$(E) "[MAKE]    Generating $@"
	$(Q) mkdir -p $(@D)
	$(Q) echo "$(GRPCXX_PC_FILE)" | tr , '\n' >$@

$(LIBDIR)/$(CONFIG)/pkgconfig/grpc++_unsecure.pc:
	$(E) "[MAKE]    Generating $@"
	$(Q) mkdir -p $(@D)
	$(Q) echo "$(GRPCXX_UNSECURE_PC_FILE)" | tr , '\n' >$@

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/lb/v1/load_balancer.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/lb/v1/load_balancer.grpc.pb.cc: protoc_dep_error
else
$(GENDIR)/src/proto/grpc/lb/v1/load_balancer.pb.cc: src/proto/grpc/lb/v1/load_balancer.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/lb/v1/load_balancer.grpc.pb.cc: src/proto/grpc/lb/v1/load_balancer.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc: protoc_dep_error
else
$(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.pb.cc: src/proto/grpc/reflection/v1alpha/reflection.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc: src/proto/grpc/reflection/v1alpha/reflection.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/compiler_test.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/compiler_test.grpc.pb.cc: protoc_dep_error
else
$(GENDIR)/src/proto/grpc/testing/compiler_test.pb.cc: src/proto/grpc/testing/compiler_test.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/compiler_test.grpc.pb.cc: src/proto/grpc/testing/compiler_test.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/control.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/control.grpc.pb.cc: protoc_dep_error
else
$(GENDIR)/src/proto/grpc/testing/control.pb.cc: src/proto/grpc/testing/control.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) $(GENDIR)/src/proto/grpc/testing/payloads.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.pb.cc
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/control.grpc.pb.cc: src/proto/grpc/testing/control.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) $(GENDIR)/src/proto/grpc/testing/payloads.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc: protoc_dep_error
else
$(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc: src/proto/grpc/testing/duplicate/echo_duplicate.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc: src/proto/grpc/testing/duplicate/echo_duplicate.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/echo.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc: protoc_dep_error
else
$(GENDIR)/src/proto/grpc/testing/echo.pb.cc: src/proto/grpc/testing/echo.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc: src/proto/grpc/testing/echo.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc: protoc_dep_error
else
$(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc: src/proto/grpc/testing/echo_messages.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc: src/proto/grpc/testing/echo_messages.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/empty.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/empty.grpc.pb.cc: protoc_dep_error
else
$(GENDIR)/src/proto/grpc/testing/empty.pb.cc: src/proto/grpc/testing/empty.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/empty.grpc.pb.cc: src/proto/grpc/testing/empty.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/messages.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc: protoc_dep_error
else
$(GENDIR)/src/proto/grpc/testing/messages.pb.cc: src/proto/grpc/testing/messages.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc: src/proto/grpc/testing/messages.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/metrics.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/metrics.grpc.pb.cc: protoc_dep_error
else
$(GENDIR)/src/proto/grpc/testing/metrics.pb.cc: src/proto/grpc/testing/metrics.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/metrics.grpc.pb.cc: src/proto/grpc/testing/metrics.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/payloads.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/payloads.grpc.pb.cc: protoc_dep_error
else
$(GENDIR)/src/proto/grpc/testing/payloads.pb.cc: src/proto/grpc/testing/payloads.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/payloads.grpc.pb.cc: src/proto/grpc/testing/payloads.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/services.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/services.grpc.pb.cc: protoc_dep_error
else
$(GENDIR)/src/proto/grpc/testing/services.pb.cc: src/proto/grpc/testing/services.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/control.pb.cc
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/services.grpc.pb.cc: src/proto/grpc/testing/services.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/control.pb.cc $(GENDIR)/src/proto/grpc/testing/control.grpc.pb.cc
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/stats.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc: protoc_dep_error
else
$(GENDIR)/src/proto/grpc/testing/stats.pb.cc: src/proto/grpc/testing/stats.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc: src/proto/grpc/testing/stats.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/test.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/test.grpc.pb.cc: protoc_dep_error
else
$(GENDIR)/src/proto/grpc/testing/test.pb.cc: src/proto/grpc/testing/test.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) $(GENDIR)/src/proto/grpc/testing/empty.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.pb.cc
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/test.grpc.pb.cc: src/proto/grpc/testing/test.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) $(GENDIR)/src/proto/grpc/testing/empty.pb.cc $(GENDIR)/src/proto/grpc/testing/empty.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin $<
endif


ifeq ($(CONFIG),stapprof)
src/core/profiling/stap_timers.c: $(GENDIR)/src/core/profiling/stap_probes.h
ifeq ($(HAS_SYSTEMTAP),true)
$(GENDIR)/src/core/profiling/stap_probes.h: src/core/profiling/stap_probes.d
	$(E) "[DTRACE]  Compiling $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(DTRACE) -C -h -s $< -o $@
else
$(GENDIR)/src/core/profiling/stap_probes.h: systemtap_dep_error stop
endif
endif

$(OBJDIR)/$(CONFIG)/%.o : %.c
	$(E) "[C]       Compiling $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MF $(addsuffix .dep, $(basename $@)) -c -o $@ $<

$(OBJDIR)/$(CONFIG)/%.o : $(GENDIR)/%.pb.cc
	$(E) "[CXX]     Compiling $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MF $(addsuffix .dep, $(basename $@)) -c -o $@ $<

$(OBJDIR)/$(CONFIG)/src/compiler/%.o : src/compiler/%.cc
	$(E) "[HOSTCXX] Compiling $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(HOST_CXX) $(HOST_CXXFLAGS) $(HOST_CPPFLAGS) -MMD -MF $(addsuffix .dep, $(basename $@)) -c -o $@ $<

$(OBJDIR)/$(CONFIG)/%.o : %.cc
	$(E) "[CXX]     Compiling $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MF $(addsuffix .dep, $(basename $@)) -c -o $@ $<

install: install_c install_cxx install-plugins install-certs

install_c: install-headers_c install-static_c install-shared_c

install_cxx: install-headers_cxx install-static_cxx install-shared_cxx

install_csharp: install-shared_csharp install_c

install_grpc_csharp_ext: install_csharp

install-headers: install-headers_c install-headers_cxx

install-headers_c:
	$(E) "[INSTALL] Installing public C headers"
	$(Q) $(foreach h, $(PUBLIC_HEADERS_C), $(INSTALL) -d $(prefix)/$(dir $(h)) && ) exit 0 || exit 1
	$(Q) $(foreach h, $(PUBLIC_HEADERS_C), $(INSTALL) $(h) $(prefix)/$(h) && ) exit 0 || exit 1

install-headers_cxx:
	$(E) "[INSTALL] Installing public C++ headers"
	$(Q) $(foreach h, $(PUBLIC_HEADERS_CXX), $(INSTALL) -d $(prefix)/$(dir $(h)) && ) exit 0 || exit 1
	$(Q) $(foreach h, $(PUBLIC_HEADERS_CXX), $(INSTALL) $(h) $(prefix)/$(h) && ) exit 0 || exit 1

install-static: install-static_c install-static_cxx

install-static_c: static_c strip-static_c install-pkg-config_c
	$(E) "[INSTALL] Installing libgpr.a"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgpr.a $(prefix)/lib/libgpr.a
	$(E) "[INSTALL] Installing libgrpc.a"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc.a $(prefix)/lib/libgrpc.a
	$(E) "[INSTALL] Installing libgrpc_cronet.a"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc_cronet.a $(prefix)/lib/libgrpc_cronet.a
	$(E) "[INSTALL] Installing libgrpc_unsecure.a"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(prefix)/lib/libgrpc_unsecure.a

install-static_cxx: static_cxx strip-static_cxx install-pkg-config_cxx
	$(E) "[INSTALL] Installing libgrpc++.a"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc++.a $(prefix)/lib/libgrpc++.a
	$(E) "[INSTALL] Installing libgrpc++_cronet.a"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc++_cronet.a $(prefix)/lib/libgrpc++_cronet.a
	$(E) "[INSTALL] Installing libgrpc++_reflection.a"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc++_reflection.a $(prefix)/lib/libgrpc++_reflection.a
	$(E) "[INSTALL] Installing libgrpc++_unsecure.a"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(prefix)/lib/libgrpc++_unsecure.a



install-shared_c: shared_c strip-shared_c install-pkg-config_c
	$(E) "[INSTALL] Installing $(SHARED_PREFIX)gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/$(SHARED_PREFIX)gpr$(SHARED_VERSION).$(SHARED_EXT_CORE)
ifeq ($(SYSTEM),MINGW32)
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgpr-imp.a $(prefix)/lib/libgpr-imp.a
else ifneq ($(SYSTEM),Darwin)
	$(Q) ln -sf $(SHARED_PREFIX)gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/libgpr.so.2
	$(Q) ln -sf $(SHARED_PREFIX)gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/libgpr.so
endif
	$(E) "[INSTALL] Installing $(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/$(SHARED_PREFIX)grpc$(SHARED_VERSION).$(SHARED_EXT_CORE)
ifeq ($(SYSTEM),MINGW32)
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc-imp.a $(prefix)/lib/libgrpc-imp.a
else ifneq ($(SYSTEM),Darwin)
	$(Q) ln -sf $(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/libgrpc.so.2
	$(Q) ln -sf $(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/libgrpc.so
endif
	$(E) "[INSTALL] Installing $(SHARED_PREFIX)grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/$(SHARED_PREFIX)grpc_cronet$(SHARED_VERSION).$(SHARED_EXT_CORE)
ifeq ($(SYSTEM),MINGW32)
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc_cronet-imp.a $(prefix)/lib/libgrpc_cronet-imp.a
else ifneq ($(SYSTEM),Darwin)
	$(Q) ln -sf $(SHARED_PREFIX)grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/libgrpc_cronet.so.2
	$(Q) ln -sf $(SHARED_PREFIX)grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/libgrpc_cronet.so
endif
	$(E) "[INSTALL] Installing $(SHARED_PREFIX)grpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/$(SHARED_PREFIX)grpc_unsecure$(SHARED_VERSION).$(SHARED_EXT_CORE)
ifeq ($(SYSTEM),MINGW32)
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc_unsecure-imp.a $(prefix)/lib/libgrpc_unsecure-imp.a
else ifneq ($(SYSTEM),Darwin)
	$(Q) ln -sf $(SHARED_PREFIX)grpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/libgrpc_unsecure.so.2
	$(Q) ln -sf $(SHARED_PREFIX)grpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/libgrpc_unsecure.so
endif
ifneq ($(SYSTEM),MINGW32)
ifneq ($(SYSTEM),Darwin)
	$(Q) ldconfig || true
endif
endif


install-shared_cxx: shared_cxx strip-shared_cxx install-shared_c install-pkg-config_cxx
	$(E) "[INSTALL] Installing $(SHARED_PREFIX)grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/$(SHARED_PREFIX)grpc++$(SHARED_VERSION).$(SHARED_EXT_CPP)
ifeq ($(SYSTEM),MINGW32)
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc++-imp.a $(prefix)/lib/libgrpc++-imp.a
else ifneq ($(SYSTEM),Darwin)
	$(Q) ln -sf $(SHARED_PREFIX)grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/libgrpc++.so.2
	$(Q) ln -sf $(SHARED_PREFIX)grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/libgrpc++.so
endif
	$(E) "[INSTALL] Installing $(SHARED_PREFIX)grpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/$(SHARED_PREFIX)grpc++_cronet$(SHARED_VERSION).$(SHARED_EXT_CPP)
ifeq ($(SYSTEM),MINGW32)
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc++_cronet-imp.a $(prefix)/lib/libgrpc++_cronet-imp.a
else ifneq ($(SYSTEM),Darwin)
	$(Q) ln -sf $(SHARED_PREFIX)grpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/libgrpc++_cronet.so.2
	$(Q) ln -sf $(SHARED_PREFIX)grpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/libgrpc++_cronet.so
endif
	$(E) "[INSTALL] Installing $(SHARED_PREFIX)grpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/$(SHARED_PREFIX)grpc++_reflection$(SHARED_VERSION).$(SHARED_EXT_CPP)
ifeq ($(SYSTEM),MINGW32)
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc++_reflection-imp.a $(prefix)/lib/libgrpc++_reflection-imp.a
else ifneq ($(SYSTEM),Darwin)
	$(Q) ln -sf $(SHARED_PREFIX)grpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/libgrpc++_reflection.so.2
	$(Q) ln -sf $(SHARED_PREFIX)grpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/libgrpc++_reflection.so
endif
	$(E) "[INSTALL] Installing $(SHARED_PREFIX)grpc++_unsecure$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_unsecure$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/$(SHARED_PREFIX)grpc++_unsecure$(SHARED_VERSION).$(SHARED_EXT_CPP)
ifeq ($(SYSTEM),MINGW32)
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure-imp.a $(prefix)/lib/libgrpc++_unsecure-imp.a
else ifneq ($(SYSTEM),Darwin)
	$(Q) ln -sf $(SHARED_PREFIX)grpc++_unsecure$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/libgrpc++_unsecure.so.2
	$(Q) ln -sf $(SHARED_PREFIX)grpc++_unsecure$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/libgrpc++_unsecure.so
endif
ifneq ($(SYSTEM),MINGW32)
ifneq ($(SYSTEM),Darwin)
	$(Q) ldconfig || true
endif
endif


install-shared_csharp: shared_csharp strip-shared_csharp
	$(E) "[INSTALL] Installing $(SHARED_PREFIX)grpc_csharp_ext$(SHARED_VERSION_CSHARP).$(SHARED_EXT_CSHARP)"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc_csharp_ext$(SHARED_VERSION_CSHARP).$(SHARED_EXT_CSHARP) $(prefix)/lib/$(SHARED_PREFIX)grpc_csharp_ext$(SHARED_VERSION).$(SHARED_EXT_CSHARP)
ifeq ($(SYSTEM),MINGW32)
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc_csharp_ext-imp.a $(prefix)/lib/libgrpc_csharp_ext-imp.a
else ifneq ($(SYSTEM),Darwin)
	$(Q) ln -sf $(SHARED_PREFIX)grpc_csharp_ext$(SHARED_VERSION_CSHARP).$(SHARED_EXT_CSHARP) $(prefix)/lib/libgrpc_csharp_ext.so.2
	$(Q) ln -sf $(SHARED_PREFIX)grpc_csharp_ext$(SHARED_VERSION_CSHARP).$(SHARED_EXT_CSHARP) $(prefix)/lib/libgrpc_csharp_ext.so
endif
ifneq ($(SYSTEM),MINGW32)
ifneq ($(SYSTEM),Darwin)
	$(Q) ldconfig || true
endif
endif


install-plugins: $(PROTOC_PLUGINS)
ifeq ($(SYSTEM),MINGW32)
	$(Q) false
else
	$(E) "[INSTALL] Installing grpc protoc plugins"
	$(Q) $(INSTALL) -d $(prefix)/bin
	$(Q) $(INSTALL) $(BINDIR)/$(CONFIG)/grpc_cpp_plugin $(prefix)/bin/grpc_cpp_plugin
	$(Q) $(INSTALL) -d $(prefix)/bin
	$(Q) $(INSTALL) $(BINDIR)/$(CONFIG)/grpc_csharp_plugin $(prefix)/bin/grpc_csharp_plugin
	$(Q) $(INSTALL) -d $(prefix)/bin
	$(Q) $(INSTALL) $(BINDIR)/$(CONFIG)/grpc_node_plugin $(prefix)/bin/grpc_node_plugin
	$(Q) $(INSTALL) -d $(prefix)/bin
	$(Q) $(INSTALL) $(BINDIR)/$(CONFIG)/grpc_objective_c_plugin $(prefix)/bin/grpc_objective_c_plugin
	$(Q) $(INSTALL) -d $(prefix)/bin
	$(Q) $(INSTALL) $(BINDIR)/$(CONFIG)/grpc_php_plugin $(prefix)/bin/grpc_php_plugin
	$(Q) $(INSTALL) -d $(prefix)/bin
	$(Q) $(INSTALL) $(BINDIR)/$(CONFIG)/grpc_python_plugin $(prefix)/bin/grpc_python_plugin
	$(Q) $(INSTALL) -d $(prefix)/bin
	$(Q) $(INSTALL) $(BINDIR)/$(CONFIG)/grpc_ruby_plugin $(prefix)/bin/grpc_ruby_plugin
endif

install-pkg-config_c: pc_c pc_c_unsecure
	$(E) "[INSTALL] Installing C pkg-config files"
	$(Q) $(INSTALL) -d $(prefix)/lib/pkgconfig
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/pkgconfig/grpc.pc $(prefix)/lib/pkgconfig/grpc.pc
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/pkgconfig/grpc_unsecure.pc $(prefix)/lib/pkgconfig/grpc_unsecure.pc

install-pkg-config_cxx: pc_cxx pc_cxx_unsecure
	$(E) "[INSTALL] Installing C++ pkg-config files"
	$(Q) $(INSTALL) -d $(prefix)/lib/pkgconfig
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/pkgconfig/grpc++.pc $(prefix)/lib/pkgconfig/grpc++.pc
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/pkgconfig/grpc++_unsecure.pc $(prefix)/lib/pkgconfig/grpc++_unsecure.pc

install-certs: etc/roots.pem
	$(E) "[INSTALL] Installing root certificates"
	$(Q) $(INSTALL) -d $(prefix)/share/grpc
	$(Q) $(INSTALL) etc/roots.pem $(prefix)/share/grpc/roots.pem

clean:
	$(E) "[CLEAN]   Cleaning build directories."
	$(Q) $(RM) -rf $(OBJDIR) $(LIBDIR) $(BINDIR) $(GENDIR) cache.mk


# The various libraries


LIBGPR_SRC = \
    src/core/lib/profiling/basic_timers.c \
    src/core/lib/profiling/stap_timers.c \
    src/core/lib/support/alloc.c \
    src/core/lib/support/avl.c \
    src/core/lib/support/backoff.c \
    src/core/lib/support/cmdline.c \
    src/core/lib/support/cpu_iphone.c \
    src/core/lib/support/cpu_linux.c \
    src/core/lib/support/cpu_posix.c \
    src/core/lib/support/cpu_windows.c \
    src/core/lib/support/env_linux.c \
    src/core/lib/support/env_posix.c \
    src/core/lib/support/env_windows.c \
    src/core/lib/support/histogram.c \
    src/core/lib/support/host_port.c \
    src/core/lib/support/log.c \
    src/core/lib/support/log_android.c \
    src/core/lib/support/log_linux.c \
    src/core/lib/support/log_posix.c \
    src/core/lib/support/log_windows.c \
    src/core/lib/support/mpscq.c \
    src/core/lib/support/murmur_hash.c \
    src/core/lib/support/stack_lockfree.c \
    src/core/lib/support/string.c \
    src/core/lib/support/string_posix.c \
    src/core/lib/support/string_util_windows.c \
    src/core/lib/support/string_windows.c \
    src/core/lib/support/subprocess_posix.c \
    src/core/lib/support/subprocess_windows.c \
    src/core/lib/support/sync.c \
    src/core/lib/support/sync_posix.c \
    src/core/lib/support/sync_windows.c \
    src/core/lib/support/thd.c \
    src/core/lib/support/thd_posix.c \
    src/core/lib/support/thd_windows.c \
    src/core/lib/support/time.c \
    src/core/lib/support/time_posix.c \
    src/core/lib/support/time_precise.c \
    src/core/lib/support/time_windows.c \
    src/core/lib/support/tls_pthread.c \
    src/core/lib/support/tmpfile_msys.c \
    src/core/lib/support/tmpfile_posix.c \
    src/core/lib/support/tmpfile_windows.c \
    src/core/lib/support/wrap_memcpy.c \

PUBLIC_HEADERS_C += \
    include/grpc/support/alloc.h \
    include/grpc/support/atm.h \
    include/grpc/support/atm_gcc_atomic.h \
    include/grpc/support/atm_gcc_sync.h \
    include/grpc/support/atm_windows.h \
    include/grpc/support/avl.h \
    include/grpc/support/cmdline.h \
    include/grpc/support/cpu.h \
    include/grpc/support/histogram.h \
    include/grpc/support/host_port.h \
    include/grpc/support/log.h \
    include/grpc/support/log_windows.h \
    include/grpc/support/port_platform.h \
    include/grpc/support/string_util.h \
    include/grpc/support/subprocess.h \
    include/grpc/support/sync.h \
    include/grpc/support/sync_generic.h \
    include/grpc/support/sync_posix.h \
    include/grpc/support/sync_windows.h \
    include/grpc/support/thd.h \
    include/grpc/support/time.h \
    include/grpc/support/tls.h \
    include/grpc/support/tls_gcc.h \
    include/grpc/support/tls_msvc.h \
    include/grpc/support/tls_pthread.h \
    include/grpc/support/useful.h \
    include/grpc/impl/codegen/atm.h \
    include/grpc/impl/codegen/atm_gcc_atomic.h \
    include/grpc/impl/codegen/atm_gcc_sync.h \
    include/grpc/impl/codegen/atm_windows.h \
    include/grpc/impl/codegen/gpr_types.h \
    include/grpc/impl/codegen/port_platform.h \
    include/grpc/impl/codegen/slice.h \
    include/grpc/impl/codegen/sync.h \
    include/grpc/impl/codegen/sync_generic.h \
    include/grpc/impl/codegen/sync_posix.h \
    include/grpc/impl/codegen/sync_windows.h \

LIBGPR_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGPR_SRC))))


$(LIBDIR)/$(CONFIG)/libgpr.a: $(ZLIB_DEP)  $(LIBGPR_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgpr.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBGPR_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgpr.a
endif



ifeq ($(SYSTEM),MINGW32)
$(LIBDIR)/$(CONFIG)/gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): $(LIBGPR_OBJS)  $(ZLIB_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared gpr.def -Wl,--output-def=$(LIBDIR)/$(CONFIG)/gpr$(SHARED_VERSION_CORE).def -Wl,--out-implib=$(LIBDIR)/$(CONFIG)/libgpr$(SHARED_VERSION_CORE)-dll.a -o $(LIBDIR)/$(CONFIG)/gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGPR_OBJS) $(LDLIBS) $(ZLIB_MERGE_LIBS)
else
$(LIBDIR)/$(CONFIG)/libgpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): $(LIBGPR_OBJS)  $(ZLIB_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
ifeq ($(SYSTEM),Darwin)
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -install_name $(SHARED_PREFIX)gpr$(SHARED_VERSION).$(SHARED_EXT_CORE) -dynamiclib -o $(LIBDIR)/$(CONFIG)/libgpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGPR_OBJS) $(LDLIBS) $(ZLIB_MERGE_LIBS)
else
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,-soname,libgpr.so.2 -o $(LIBDIR)/$(CONFIG)/libgpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGPR_OBJS) $(LDLIBS) $(ZLIB_MERGE_LIBS)
	$(Q) ln -sf $(SHARED_PREFIX)gpr$(SHARED_VERSION).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/libgpr$(SHARED_VERSION_CORE).so.2
	$(Q) ln -sf $(SHARED_PREFIX)gpr$(SHARED_VERSION).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/libgpr$(SHARED_VERSION_CORE).so
endif
endif

ifneq ($(NO_DEPS),true)
-include $(LIBGPR_OBJS:.o=.dep)
endif


LIBGPR_TEST_UTIL_SRC = \
    test/core/util/test_config.c \

PUBLIC_HEADERS_C += \

LIBGPR_TEST_UTIL_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGPR_TEST_UTIL_SRC))))


$(LIBDIR)/$(CONFIG)/libgpr_test_util.a: $(ZLIB_DEP)  $(LIBGPR_TEST_UTIL_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgpr_test_util.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBGPR_TEST_UTIL_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgpr_test_util.a
endif




ifneq ($(NO_DEPS),true)
-include $(LIBGPR_TEST_UTIL_OBJS:.o=.dep)
endif


LIBGRPC_SRC = \
    src/core/lib/surface/init.c \
    src/core/lib/channel/channel_args.c \
    src/core/lib/channel/channel_stack.c \
    src/core/lib/channel/channel_stack_builder.c \
    src/core/lib/channel/compress_filter.c \
    src/core/lib/channel/connected_channel.c \
    src/core/lib/channel/deadline_filter.c \
    src/core/lib/channel/handshaker.c \
    src/core/lib/channel/http_client_filter.c \
    src/core/lib/channel/http_server_filter.c \
    src/core/lib/channel/message_size_filter.c \
    src/core/lib/compression/compression.c \
    src/core/lib/compression/message_compress.c \
    src/core/lib/debug/trace.c \
    src/core/lib/http/format_request.c \
    src/core/lib/http/httpcli.c \
    src/core/lib/http/parser.c \
    src/core/lib/iomgr/closure.c \
    src/core/lib/iomgr/combiner.c \
    src/core/lib/iomgr/endpoint.c \
    src/core/lib/iomgr/endpoint_pair_posix.c \
    src/core/lib/iomgr/endpoint_pair_uv.c \
    src/core/lib/iomgr/endpoint_pair_windows.c \
    src/core/lib/iomgr/error.c \
    src/core/lib/iomgr/ev_epoll_linux.c \
    src/core/lib/iomgr/ev_poll_posix.c \
    src/core/lib/iomgr/ev_posix.c \
    src/core/lib/iomgr/exec_ctx.c \
    src/core/lib/iomgr/executor.c \
    src/core/lib/iomgr/iocp_windows.c \
    src/core/lib/iomgr/iomgr.c \
    src/core/lib/iomgr/iomgr_posix.c \
    src/core/lib/iomgr/iomgr_uv.c \
    src/core/lib/iomgr/iomgr_windows.c \
    src/core/lib/iomgr/load_file.c \
    src/core/lib/iomgr/network_status_tracker.c \
    src/core/lib/iomgr/polling_entity.c \
    src/core/lib/iomgr/pollset_set_uv.c \
    src/core/lib/iomgr/pollset_set_windows.c \
    src/core/lib/iomgr/pollset_uv.c \
    src/core/lib/iomgr/pollset_windows.c \
    src/core/lib/iomgr/resolve_address_posix.c \
    src/core/lib/iomgr/resolve_address_uv.c \
    src/core/lib/iomgr/resolve_address_windows.c \
    src/core/lib/iomgr/resource_quota.c \
    src/core/lib/iomgr/sockaddr_utils.c \
    src/core/lib/iomgr/socket_mutator.c \
    src/core/lib/iomgr/socket_utils_common_posix.c \
    src/core/lib/iomgr/socket_utils_linux.c \
    src/core/lib/iomgr/socket_utils_posix.c \
    src/core/lib/iomgr/socket_utils_uv.c \
    src/core/lib/iomgr/socket_utils_windows.c \
    src/core/lib/iomgr/socket_windows.c \
    src/core/lib/iomgr/tcp_client_posix.c \
    src/core/lib/iomgr/tcp_client_uv.c \
    src/core/lib/iomgr/tcp_client_windows.c \
    src/core/lib/iomgr/tcp_posix.c \
    src/core/lib/iomgr/tcp_server_posix.c \
    src/core/lib/iomgr/tcp_server_uv.c \
    src/core/lib/iomgr/tcp_server_windows.c \
    src/core/lib/iomgr/tcp_uv.c \
    src/core/lib/iomgr/tcp_windows.c \
    src/core/lib/iomgr/time_averaged_stats.c \
    src/core/lib/iomgr/timer_generic.c \
    src/core/lib/iomgr/timer_heap.c \
    src/core/lib/iomgr/timer_uv.c \
    src/core/lib/iomgr/udp_server.c \
    src/core/lib/iomgr/unix_sockets_posix.c \
    src/core/lib/iomgr/unix_sockets_posix_noop.c \
    src/core/lib/iomgr/wakeup_fd_cv.c \
    src/core/lib/iomgr/wakeup_fd_eventfd.c \
    src/core/lib/iomgr/wakeup_fd_nospecial.c \
    src/core/lib/iomgr/wakeup_fd_pipe.c \
    src/core/lib/iomgr/wakeup_fd_posix.c \
    src/core/lib/iomgr/workqueue_uv.c \
    src/core/lib/iomgr/workqueue_windows.c \
    src/core/lib/json/json.c \
    src/core/lib/json/json_reader.c \
    src/core/lib/json/json_string.c \
    src/core/lib/json/json_writer.c \
    src/core/lib/slice/percent_encoding.c \
    src/core/lib/slice/slice.c \
    src/core/lib/slice/slice_buffer.c \
    src/core/lib/slice/slice_string_helpers.c \
    src/core/lib/surface/alarm.c \
    src/core/lib/surface/api_trace.c \
    src/core/lib/surface/byte_buffer.c \
    src/core/lib/surface/byte_buffer_reader.c \
    src/core/lib/surface/call.c \
    src/core/lib/surface/call_details.c \
    src/core/lib/surface/call_log_batch.c \
    src/core/lib/surface/channel.c \
    src/core/lib/surface/channel_init.c \
    src/core/lib/surface/channel_ping.c \
    src/core/lib/surface/channel_stack_type.c \
    src/core/lib/surface/completion_queue.c \
    src/core/lib/surface/event_string.c \
    src/core/lib/surface/lame_client.c \
    src/core/lib/surface/metadata_array.c \
    src/core/lib/surface/server.c \
    src/core/lib/surface/validate_metadata.c \
    src/core/lib/surface/version.c \
    src/core/lib/transport/byte_stream.c \
    src/core/lib/transport/connectivity_state.c \
    src/core/lib/transport/mdstr_hash_table.c \
    src/core/lib/transport/metadata.c \
    src/core/lib/transport/metadata_batch.c \
    src/core/lib/transport/pid_controller.c \
    src/core/lib/transport/service_config.c \
    src/core/lib/transport/static_metadata.c \
    src/core/lib/transport/timeout_encoding.c \
    src/core/lib/transport/transport.c \
    src/core/lib/transport/transport_op_string.c \
    src/core/ext/transport/chttp2/server/secure/server_secure_chttp2.c \
    src/core/ext/transport/chttp2/transport/bin_decoder.c \
    src/core/ext/transport/chttp2/transport/bin_encoder.c \
    src/core/ext/transport/chttp2/transport/chttp2_plugin.c \
    src/core/ext/transport/chttp2/transport/chttp2_transport.c \
    src/core/ext/transport/chttp2/transport/frame_data.c \
    src/core/ext/transport/chttp2/transport/frame_goaway.c \
    src/core/ext/transport/chttp2/transport/frame_ping.c \
    src/core/ext/transport/chttp2/transport/frame_rst_stream.c \
    src/core/ext/transport/chttp2/transport/frame_settings.c \
    src/core/ext/transport/chttp2/transport/frame_window_update.c \
    src/core/ext/transport/chttp2/transport/hpack_encoder.c \
    src/core/ext/transport/chttp2/transport/hpack_parser.c \
    src/core/ext/transport/chttp2/transport/hpack_table.c \
    src/core/ext/transport/chttp2/transport/huffsyms.c \
    src/core/ext/transport/chttp2/transport/incoming_metadata.c \
    src/core/ext/transport/chttp2/transport/parsing.c \
    src/core/ext/transport/chttp2/transport/status_conversion.c \
    src/core/ext/transport/chttp2/transport/stream_lists.c \
    src/core/ext/transport/chttp2/transport/stream_map.c \
    src/core/ext/transport/chttp2/transport/varint.c \
    src/core/ext/transport/chttp2/transport/writing.c \
    src/core/ext/transport/chttp2/alpn/alpn.c \
    src/core/lib/http/httpcli_security_connector.c \
    src/core/lib/security/context/security_context.c \
    src/core/lib/security/credentials/composite/composite_credentials.c \
    src/core/lib/security/credentials/credentials.c \
    src/core/lib/security/credentials/credentials_metadata.c \
    src/core/lib/security/credentials/fake/fake_credentials.c \
    src/core/lib/security/credentials/google_default/credentials_generic.c \
    src/core/lib/security/credentials/google_default/google_default_credentials.c \
    src/core/lib/security/credentials/iam/iam_credentials.c \
    src/core/lib/security/credentials/jwt/json_token.c \
    src/core/lib/security/credentials/jwt/jwt_credentials.c \
    src/core/lib/security/credentials/jwt/jwt_verifier.c \
    src/core/lib/security/credentials/oauth2/oauth2_credentials.c \
    src/core/lib/security/credentials/plugin/plugin_credentials.c \
    src/core/lib/security/credentials/ssl/ssl_credentials.c \
    src/core/lib/security/transport/client_auth_filter.c \
    src/core/lib/security/transport/secure_endpoint.c \
    src/core/lib/security/transport/security_connector.c \
    src/core/lib/security/transport/security_handshaker.c \
    src/core/lib/security/transport/server_auth_filter.c \
    src/core/lib/security/transport/tsi_error.c \
    src/core/lib/security/util/b64.c \
    src/core/lib/security/util/json_util.c \
    src/core/lib/surface/init_secure.c \
    src/core/lib/tsi/fake_transport_security.c \
    src/core/lib/tsi/ssl_transport_security.c \
    src/core/lib/tsi/transport_security.c \
    src/core/ext/transport/chttp2/client/secure/secure_channel_create.c \
    src/core/ext/client_channel/channel_connectivity.c \
    src/core/ext/client_channel/client_channel.c \
    src/core/ext/client_channel/client_channel_factory.c \
    src/core/ext/client_channel/client_channel_plugin.c \
    src/core/ext/client_channel/connector.c \
    src/core/ext/client_channel/default_initial_connect_string.c \
    src/core/ext/client_channel/http_connect_handshaker.c \
    src/core/ext/client_channel/initial_connect_string.c \
    src/core/ext/client_channel/lb_policy.c \
    src/core/ext/client_channel/lb_policy_factory.c \
    src/core/ext/client_channel/lb_policy_registry.c \
    src/core/ext/client_channel/parse_address.c \
    src/core/ext/client_channel/resolver.c \
    src/core/ext/client_channel/resolver_factory.c \
    src/core/ext/client_channel/resolver_registry.c \
    src/core/ext/client_channel/subchannel.c \
    src/core/ext/client_channel/subchannel_index.c \
    src/core/ext/client_channel/uri_parser.c \
    src/core/ext/transport/chttp2/client/chttp2_connector.c \
    src/core/ext/transport/chttp2/server/insecure/server_chttp2.c \
    src/core/ext/transport/chttp2/server/insecure/server_chttp2_posix.c \
    src/core/ext/transport/chttp2/client/insecure/channel_create.c \
    src/core/ext/transport/chttp2/client/insecure/channel_create_posix.c \
    src/core/ext/lb_policy/grpclb/grpclb.c \
    src/core/ext/lb_policy/grpclb/load_balancer_api.c \
    src/core/ext/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.c \
    third_party/nanopb/pb_common.c \
    third_party/nanopb/pb_decode.c \
    third_party/nanopb/pb_encode.c \
    src/core/ext/lb_policy/pick_first/pick_first.c \
    src/core/ext/lb_policy/round_robin/round_robin.c \
    src/core/ext/resolver/dns/native/dns_resolver.c \
    src/core/ext/resolver/sockaddr/sockaddr_resolver.c \
    src/core/ext/load_reporting/load_reporting.c \
    src/core/ext/load_reporting/load_reporting_filter.c \
    src/core/ext/census/base_resources.c \
    src/core/ext/census/context.c \
    src/core/ext/census/gen/census.pb.c \
    src/core/ext/census/gen/trace_context.pb.c \
    src/core/ext/census/grpc_context.c \
    src/core/ext/census/grpc_filter.c \
    src/core/ext/census/grpc_plugin.c \
    src/core/ext/census/initialize.c \
    src/core/ext/census/mlog.c \
    src/core/ext/census/operation.c \
    src/core/ext/census/placeholders.c \
    src/core/ext/census/resource.c \
    src/core/ext/census/trace_context.c \
    src/core/ext/census/tracing.c \
    src/core/plugin_registry/grpc_plugin_registry.c \

PUBLIC_HEADERS_C += \
    include/grpc/byte_buffer.h \
    include/grpc/byte_buffer_reader.h \
    include/grpc/compression.h \
    include/grpc/grpc.h \
    include/grpc/grpc_posix.h \
    include/grpc/grpc_security_constants.h \
    include/grpc/slice.h \
    include/grpc/slice_buffer.h \
    include/grpc/status.h \
    include/grpc/impl/codegen/byte_buffer_reader.h \
    include/grpc/impl/codegen/compression_types.h \
    include/grpc/impl/codegen/connectivity_state.h \
    include/grpc/impl/codegen/grpc_types.h \
    include/grpc/impl/codegen/propagation_bits.h \
    include/grpc/impl/codegen/status.h \
    include/grpc/impl/codegen/atm.h \
    include/grpc/impl/codegen/atm_gcc_atomic.h \
    include/grpc/impl/codegen/atm_gcc_sync.h \
    include/grpc/impl/codegen/atm_windows.h \
    include/grpc/impl/codegen/gpr_types.h \
    include/grpc/impl/codegen/port_platform.h \
    include/grpc/impl/codegen/slice.h \
    include/grpc/impl/codegen/sync.h \
    include/grpc/impl/codegen/sync_generic.h \
    include/grpc/impl/codegen/sync_posix.h \
    include/grpc/impl/codegen/sync_windows.h \
    include/grpc/grpc_security.h \
    include/grpc/census.h \

LIBGRPC_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libgrpc.a: openssl_dep_error

$(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): openssl_dep_error

else


$(LIBDIR)/$(CONFIG)/libgrpc.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(LIBGRPC_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS)  $(OPENSSL_MERGE_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBGRPC_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS)  $(OPENSSL_MERGE_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc.a
endif



ifeq ($(SYSTEM),MINGW32)
$(LIBDIR)/$(CONFIG)/grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): $(LIBGRPC_OBJS)  $(ZLIB_DEP) $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc_transport_chttp2.a $(LIBDIR)/$(CONFIG)/libgrpc_base.a $(OPENSSL_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared grpc.def -Wl,--output-def=$(LIBDIR)/$(CONFIG)/grpc$(SHARED_VERSION_CORE).def -Wl,--out-implib=$(LIBDIR)/$(CONFIG)/libgrpc$(SHARED_VERSION_CORE)-dll.a -o $(LIBDIR)/$(CONFIG)/grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGRPC_OBJS) $(LDLIBS) $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc_transport_chttp2.a $(LIBDIR)/$(CONFIG)/libgrpc_base.a $(OPENSSL_MERGE_LIBS) $(LDLIBS_SECURE) $(ZLIB_MERGE_LIBS)
else
$(LIBDIR)/$(CONFIG)/libgrpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): $(LIBGRPC_OBJS)  $(ZLIB_DEP) $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc_transport_chttp2.a $(LIBDIR)/$(CONFIG)/libgrpc_base.a $(OPENSSL_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
ifeq ($(SYSTEM),Darwin)
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -install_name $(SHARED_PREFIX)grpc$(SHARED_VERSION).$(SHARED_EXT_CORE) -dynamiclib -o $(LIBDIR)/$(CONFIG)/libgrpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGRPC_OBJS) $(LDLIBS) $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc_transport_chttp2.a $(LIBDIR)/$(CONFIG)/libgrpc_base.a $(OPENSSL_MERGE_LIBS) $(LDLIBS_SECURE) $(ZLIB_MERGE_LIBS)
else
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,-soname,libgrpc.so.2 -o $(LIBDIR)/$(CONFIG)/libgrpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGRPC_OBJS) $(LDLIBS) $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc_transport_chttp2.a $(LIBDIR)/$(CONFIG)/libgrpc_base.a $(OPENSSL_MERGE_LIBS) $(LDLIBS_SECURE) $(ZLIB_MERGE_LIBS)
	$(Q) ln -sf $(SHARED_PREFIX)grpc$(SHARED_VERSION).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/libgrpc$(SHARED_VERSION_CORE).so.2
	$(Q) ln -sf $(SHARED_PREFIX)grpc$(SHARED_VERSION).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/libgrpc$(SHARED_VERSION_CORE).so
endif
endif

endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBGRPC_OBJS:.o=.dep)
endif
endif


LIBGRPC_CRONET_SRC = \
    src/core/lib/surface/init.c \
    src/core/lib/channel/channel_args.c \
    src/core/lib/channel/channel_stack.c \
    src/core/lib/channel/channel_stack_builder.c \
    src/core/lib/channel/compress_filter.c \
    src/core/lib/channel/connected_channel.c \
    src/core/lib/channel/deadline_filter.c \
    src/core/lib/channel/handshaker.c \
    src/core/lib/channel/http_client_filter.c \
    src/core/lib/channel/http_server_filter.c \
    src/core/lib/channel/message_size_filter.c \
    src/core/lib/compression/compression.c \
    src/core/lib/compression/message_compress.c \
    src/core/lib/debug/trace.c \
    src/core/lib/http/format_request.c \
    src/core/lib/http/httpcli.c \
    src/core/lib/http/parser.c \
    src/core/lib/iomgr/closure.c \
    src/core/lib/iomgr/combiner.c \
    src/core/lib/iomgr/endpoint.c \
    src/core/lib/iomgr/endpoint_pair_posix.c \
    src/core/lib/iomgr/endpoint_pair_uv.c \
    src/core/lib/iomgr/endpoint_pair_windows.c \
    src/core/lib/iomgr/error.c \
    src/core/lib/iomgr/ev_epoll_linux.c \
    src/core/lib/iomgr/ev_poll_posix.c \
    src/core/lib/iomgr/ev_posix.c \
    src/core/lib/iomgr/exec_ctx.c \
    src/core/lib/iomgr/executor.c \
    src/core/lib/iomgr/iocp_windows.c \
    src/core/lib/iomgr/iomgr.c \
    src/core/lib/iomgr/iomgr_posix.c \
    src/core/lib/iomgr/iomgr_uv.c \
    src/core/lib/iomgr/iomgr_windows.c \
    src/core/lib/iomgr/load_file.c \
    src/core/lib/iomgr/network_status_tracker.c \
    src/core/lib/iomgr/polling_entity.c \
    src/core/lib/iomgr/pollset_set_uv.c \
    src/core/lib/iomgr/pollset_set_windows.c \
    src/core/lib/iomgr/pollset_uv.c \
    src/core/lib/iomgr/pollset_windows.c \
    src/core/lib/iomgr/resolve_address_posix.c \
    src/core/lib/iomgr/resolve_address_uv.c \
    src/core/lib/iomgr/resolve_address_windows.c \
    src/core/lib/iomgr/resource_quota.c \
    src/core/lib/iomgr/sockaddr_utils.c \
    src/core/lib/iomgr/socket_mutator.c \
    src/core/lib/iomgr/socket_utils_common_posix.c \
    src/core/lib/iomgr/socket_utils_linux.c \
    src/core/lib/iomgr/socket_utils_posix.c \
    src/core/lib/iomgr/socket_utils_uv.c \
    src/core/lib/iomgr/socket_utils_windows.c \
    src/core/lib/iomgr/socket_windows.c \
    src/core/lib/iomgr/tcp_client_posix.c \
    src/core/lib/iomgr/tcp_client_uv.c \
    src/core/lib/iomgr/tcp_client_windows.c \
    src/core/lib/iomgr/tcp_posix.c \
    src/core/lib/iomgr/tcp_server_posix.c \
    src/core/lib/iomgr/tcp_server_uv.c \
    src/core/lib/iomgr/tcp_server_windows.c \
    src/core/lib/iomgr/tcp_uv.c \
    src/core/lib/iomgr/tcp_windows.c \
    src/core/lib/iomgr/time_averaged_stats.c \
    src/core/lib/iomgr/timer_generic.c \
    src/core/lib/iomgr/timer_heap.c \
    src/core/lib/iomgr/timer_uv.c \
    src/core/lib/iomgr/udp_server.c \
    src/core/lib/iomgr/unix_sockets_posix.c \
    src/core/lib/iomgr/unix_sockets_posix_noop.c \
    src/core/lib/iomgr/wakeup_fd_cv.c \
    src/core/lib/iomgr/wakeup_fd_eventfd.c \
    src/core/lib/iomgr/wakeup_fd_nospecial.c \
    src/core/lib/iomgr/wakeup_fd_pipe.c \
    src/core/lib/iomgr/wakeup_fd_posix.c \
    src/core/lib/iomgr/workqueue_uv.c \
    src/core/lib/iomgr/workqueue_windows.c \
    src/core/lib/json/json.c \
    src/core/lib/json/json_reader.c \
    src/core/lib/json/json_string.c \
    src/core/lib/json/json_writer.c \
    src/core/lib/slice/percent_encoding.c \
    src/core/lib/slice/slice.c \
    src/core/lib/slice/slice_buffer.c \
    src/core/lib/slice/slice_string_helpers.c \
    src/core/lib/surface/alarm.c \
    src/core/lib/surface/api_trace.c \
    src/core/lib/surface/byte_buffer.c \
    src/core/lib/surface/byte_buffer_reader.c \
    src/core/lib/surface/call.c \
    src/core/lib/surface/call_details.c \
    src/core/lib/surface/call_log_batch.c \
    src/core/lib/surface/channel.c \
    src/core/lib/surface/channel_init.c \
    src/core/lib/surface/channel_ping.c \
    src/core/lib/surface/channel_stack_type.c \
    src/core/lib/surface/completion_queue.c \
    src/core/lib/surface/event_string.c \
    src/core/lib/surface/lame_client.c \
    src/core/lib/surface/metadata_array.c \
    src/core/lib/surface/server.c \
    src/core/lib/surface/validate_metadata.c \
    src/core/lib/surface/version.c \
    src/core/lib/transport/byte_stream.c \
    src/core/lib/transport/connectivity_state.c \
    src/core/lib/transport/mdstr_hash_table.c \
    src/core/lib/transport/metadata.c \
    src/core/lib/transport/metadata_batch.c \
    src/core/lib/transport/pid_controller.c \
    src/core/lib/transport/service_config.c \
    src/core/lib/transport/static_metadata.c \
    src/core/lib/transport/timeout_encoding.c \
    src/core/lib/transport/transport.c \
    src/core/lib/transport/transport_op_string.c \
    src/core/ext/transport/cronet/client/secure/cronet_channel_create.c \
    src/core/ext/transport/cronet/transport/cronet_api_dummy.c \
    src/core/ext/transport/cronet/transport/cronet_transport.c \
    src/core/ext/transport/chttp2/client/secure/secure_channel_create.c \
    src/core/ext/transport/chttp2/transport/bin_decoder.c \
    src/core/ext/transport/chttp2/transport/bin_encoder.c \
    src/core/ext/transport/chttp2/transport/chttp2_plugin.c \
    src/core/ext/transport/chttp2/transport/chttp2_transport.c \
    src/core/ext/transport/chttp2/transport/frame_data.c \
    src/core/ext/transport/chttp2/transport/frame_goaway.c \
    src/core/ext/transport/chttp2/transport/frame_ping.c \
    src/core/ext/transport/chttp2/transport/frame_rst_stream.c \
    src/core/ext/transport/chttp2/transport/frame_settings.c \
    src/core/ext/transport/chttp2/transport/frame_window_update.c \
    src/core/ext/transport/chttp2/transport/hpack_encoder.c \
    src/core/ext/transport/chttp2/transport/hpack_parser.c \
    src/core/ext/transport/chttp2/transport/hpack_table.c \
    src/core/ext/transport/chttp2/transport/huffsyms.c \
    src/core/ext/transport/chttp2/transport/incoming_metadata.c \
    src/core/ext/transport/chttp2/transport/parsing.c \
    src/core/ext/transport/chttp2/transport/status_conversion.c \
    src/core/ext/transport/chttp2/transport/stream_lists.c \
    src/core/ext/transport/chttp2/transport/stream_map.c \
    src/core/ext/transport/chttp2/transport/varint.c \
    src/core/ext/transport/chttp2/transport/writing.c \
    src/core/ext/transport/chttp2/alpn/alpn.c \
    src/core/ext/client_channel/channel_connectivity.c \
    src/core/ext/client_channel/client_channel.c \
    src/core/ext/client_channel/client_channel_factory.c \
    src/core/ext/client_channel/client_channel_plugin.c \
    src/core/ext/client_channel/connector.c \
    src/core/ext/client_channel/default_initial_connect_string.c \
    src/core/ext/client_channel/http_connect_handshaker.c \
    src/core/ext/client_channel/initial_connect_string.c \
    src/core/ext/client_channel/lb_policy.c \
    src/core/ext/client_channel/lb_policy_factory.c \
    src/core/ext/client_channel/lb_policy_registry.c \
    src/core/ext/client_channel/parse_address.c \
    src/core/ext/client_channel/resolver.c \
    src/core/ext/client_channel/resolver_factory.c \
    src/core/ext/client_channel/resolver_registry.c \
    src/core/ext/client_channel/subchannel.c \
    src/core/ext/client_channel/subchannel_index.c \
    src/core/ext/client_channel/uri_parser.c \
    src/core/lib/http/httpcli_security_connector.c \
    src/core/lib/security/context/security_context.c \
    src/core/lib/security/credentials/composite/composite_credentials.c \
    src/core/lib/security/credentials/credentials.c \
    src/core/lib/security/credentials/credentials_metadata.c \
    src/core/lib/security/credentials/fake/fake_credentials.c \
    src/core/lib/security/credentials/google_default/credentials_generic.c \
    src/core/lib/security/credentials/google_default/google_default_credentials.c \
    src/core/lib/security/credentials/iam/iam_credentials.c \
    src/core/lib/security/credentials/jwt/json_token.c \
    src/core/lib/security/credentials/jwt/jwt_credentials.c \
    src/core/lib/security/credentials/jwt/jwt_verifier.c \
    src/core/lib/security/credentials/oauth2/oauth2_credentials.c \
    src/core/lib/security/credentials/plugin/plugin_credentials.c \
    src/core/lib/security/credentials/ssl/ssl_credentials.c \
    src/core/lib/security/transport/client_auth_filter.c \
    src/core/lib/security/transport/secure_endpoint.c \
    src/core/lib/security/transport/security_connector.c \
    src/core/lib/security/transport/security_handshaker.c \
    src/core/lib/security/transport/server_auth_filter.c \
    src/core/lib/security/transport/tsi_error.c \
    src/core/lib/security/util/b64.c \
    src/core/lib/security/util/json_util.c \
    src/core/lib/surface/init_secure.c \
    src/core/lib/tsi/fake_transport_security.c \
    src/core/lib/tsi/ssl_transport_security.c \
    src/core/lib/tsi/transport_security.c \
    src/core/ext/transport/chttp2/client/chttp2_connector.c \
    src/core/plugin_registry/grpc_cronet_plugin_registry.c \

PUBLIC_HEADERS_C += \
    include/grpc/byte_buffer.h \
    include/grpc/byte_buffer_reader.h \
    include/grpc/compression.h \
    include/grpc/grpc.h \
    include/grpc/grpc_posix.h \
    include/grpc/grpc_security_constants.h \
    include/grpc/slice.h \
    include/grpc/slice_buffer.h \
    include/grpc/status.h \
    include/grpc/impl/codegen/byte_buffer_reader.h \
    include/grpc/impl/codegen/compression_types.h \
    include/grpc/impl/codegen/connectivity_state.h \
    include/grpc/impl/codegen/grpc_types.h \
    include/grpc/impl/codegen/propagation_bits.h \
    include/grpc/impl/codegen/status.h \
    include/grpc/impl/codegen/atm.h \
    include/grpc/impl/codegen/atm_gcc_atomic.h \
    include/grpc/impl/codegen/atm_gcc_sync.h \
    include/grpc/impl/codegen/atm_windows.h \
    include/grpc/impl/codegen/gpr_types.h \
    include/grpc/impl/codegen/port_platform.h \
    include/grpc/impl/codegen/slice.h \
    include/grpc/impl/codegen/sync.h \
    include/grpc/impl/codegen/sync_generic.h \
    include/grpc/impl/codegen/sync_posix.h \
    include/grpc/impl/codegen/sync_windows.h \
    include/grpc/grpc_cronet.h \
    include/grpc/grpc_security.h \

LIBGRPC_CRONET_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC_CRONET_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libgrpc_cronet.a: openssl_dep_error

$(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): openssl_dep_error

else


$(LIBDIR)/$(CONFIG)/libgrpc_cronet.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(LIBGRPC_CRONET_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS)  $(OPENSSL_MERGE_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc_cronet.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc_cronet.a $(LIBGRPC_CRONET_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS)  $(OPENSSL_MERGE_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc_cronet.a
endif



ifeq ($(SYSTEM),MINGW32)
$(LIBDIR)/$(CONFIG)/grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): $(LIBGRPC_CRONET_OBJS)  $(ZLIB_DEP) $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc_transport_chttp2.a $(LIBDIR)/$(CONFIG)/libgrpc_base.a $(OPENSSL_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared grpc_cronet.def -Wl,--output-def=$(LIBDIR)/$(CONFIG)/grpc_cronet$(SHARED_VERSION_CORE).def -Wl,--out-implib=$(LIBDIR)/$(CONFIG)/libgrpc_cronet$(SHARED_VERSION_CORE)-dll.a -o $(LIBDIR)/$(CONFIG)/grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGRPC_CRONET_OBJS) $(LDLIBS) $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc_transport_chttp2.a $(LIBDIR)/$(CONFIG)/libgrpc_base.a $(OPENSSL_MERGE_LIBS) $(LDLIBS_SECURE) $(ZLIB_MERGE_LIBS)
else
$(LIBDIR)/$(CONFIG)/libgrpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): $(LIBGRPC_CRONET_OBJS)  $(ZLIB_DEP) $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc_transport_chttp2.a $(LIBDIR)/$(CONFIG)/libgrpc_base.a $(OPENSSL_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
ifeq ($(SYSTEM),Darwin)
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -install_name $(SHARED_PREFIX)grpc_cronet$(SHARED_VERSION).$(SHARED_EXT_CORE) -dynamiclib -o $(LIBDIR)/$(CONFIG)/libgrpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGRPC_CRONET_OBJS) $(LDLIBS) $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc_transport_chttp2.a $(LIBDIR)/$(CONFIG)/libgrpc_base.a $(OPENSSL_MERGE_LIBS) $(LDLIBS_SECURE) $(ZLIB_MERGE_LIBS)
else
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,-soname,libgrpc_cronet.so.2 -o $(LIBDIR)/$(CONFIG)/libgrpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGRPC_CRONET_OBJS) $(LDLIBS) $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc_transport_chttp2.a $(LIBDIR)/$(CONFIG)/libgrpc_base.a $(OPENSSL_MERGE_LIBS) $(LDLIBS_SECURE) $(ZLIB_MERGE_LIBS)
	$(Q) ln -sf $(SHARED_PREFIX)grpc_cronet$(SHARED_VERSION).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/libgrpc_cronet$(SHARED_VERSION_CORE).so.2
	$(Q) ln -sf $(SHARED_PREFIX)grpc_cronet$(SHARED_VERSION).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/libgrpc_cronet$(SHARED_VERSION_CORE).so
endif
endif

endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBGRPC_CRONET_OBJS:.o=.dep)
endif
endif


LIBGRPC_TEST_UTIL_SRC = \
    test/core/end2end/data/client_certs.c \
    test/core/end2end/data/server1_cert.c \
    test/core/end2end/data/server1_key.c \
    test/core/end2end/data/test_root_cert.c \
    test/core/security/oauth2_utils.c \
    test/core/end2end/cq_verifier.c \
    test/core/end2end/fake_resolver.c \
    test/core/end2end/fixtures/http_proxy.c \
    test/core/end2end/fixtures/proxy.c \
    test/core/iomgr/endpoint_tests.c \
    test/core/util/grpc_profiler.c \
    test/core/util/memory_counters.c \
    test/core/util/mock_endpoint.c \
    test/core/util/parse_hexstring.c \
    test/core/util/passthru_endpoint.c \
    test/core/util/port_posix.c \
    test/core/util/port_server_client.c \
    test/core/util/port_uv.c \
    test/core/util/port_windows.c \
    test/core/util/slice_splitter.c \
    src/core/lib/channel/channel_args.c \
    src/core/lib/channel/channel_stack.c \
    src/core/lib/channel/channel_stack_builder.c \
    src/core/lib/channel/compress_filter.c \
    src/core/lib/channel/connected_channel.c \
    src/core/lib/channel/deadline_filter.c \
    src/core/lib/channel/handshaker.c \
    src/core/lib/channel/http_client_filter.c \
    src/core/lib/channel/http_server_filter.c \
    src/core/lib/channel/message_size_filter.c \
    src/core/lib/compression/compression.c \
    src/core/lib/compression/message_compress.c \
    src/core/lib/debug/trace.c \
    src/core/lib/http/format_request.c \
    src/core/lib/http/httpcli.c \
    src/core/lib/http/parser.c \
    src/core/lib/iomgr/closure.c \
    src/core/lib/iomgr/combiner.c \
    src/core/lib/iomgr/endpoint.c \
    src/core/lib/iomgr/endpoint_pair_posix.c \
    src/core/lib/iomgr/endpoint_pair_uv.c \
    src/core/lib/iomgr/endpoint_pair_windows.c \
    src/core/lib/iomgr/error.c \
    src/core/lib/iomgr/ev_epoll_linux.c \
    src/core/lib/iomgr/ev_poll_posix.c \
    src/core/lib/iomgr/ev_posix.c \
    src/core/lib/iomgr/exec_ctx.c \
    src/core/lib/iomgr/executor.c \
    src/core/lib/iomgr/iocp_windows.c \
    src/core/lib/iomgr/iomgr.c \
    src/core/lib/iomgr/iomgr_posix.c \
    src/core/lib/iomgr/iomgr_uv.c \
    src/core/lib/iomgr/iomgr_windows.c \
    src/core/lib/iomgr/load_file.c \
    src/core/lib/iomgr/network_status_tracker.c \
    src/core/lib/iomgr/polling_entity.c \
    src/core/lib/iomgr/pollset_set_uv.c \
    src/core/lib/iomgr/pollset_set_windows.c \
    src/core/lib/iomgr/pollset_uv.c \
    src/core/lib/iomgr/pollset_windows.c \
    src/core/lib/iomgr/resolve_address_posix.c \
    src/core/lib/iomgr/resolve_address_uv.c \
    src/core/lib/iomgr/resolve_address_windows.c \
    src/core/lib/iomgr/resource_quota.c \
    src/core/lib/iomgr/sockaddr_utils.c \
    src/core/lib/iomgr/socket_mutator.c \
    src/core/lib/iomgr/socket_utils_common_posix.c \
    src/core/lib/iomgr/socket_utils_linux.c \
    src/core/lib/iomgr/socket_utils_posix.c \
    src/core/lib/iomgr/socket_utils_uv.c \
    src/core/lib/iomgr/socket_utils_windows.c \
    src/core/lib/iomgr/socket_windows.c \
    src/core/lib/iomgr/tcp_client_posix.c \
    src/core/lib/iomgr/tcp_client_uv.c \
    src/core/lib/iomgr/tcp_client_windows.c \
    src/core/lib/iomgr/tcp_posix.c \
    src/core/lib/iomgr/tcp_server_posix.c \
    src/core/lib/iomgr/tcp_server_uv.c \
    src/core/lib/iomgr/tcp_server_windows.c \
    src/core/lib/iomgr/tcp_uv.c \
    src/core/lib/iomgr/tcp_windows.c \
    src/core/lib/iomgr/time_averaged_stats.c \
    src/core/lib/iomgr/timer_generic.c \
    src/core/lib/iomgr/timer_heap.c \
    src/core/lib/iomgr/timer_uv.c \
    src/core/lib/iomgr/udp_server.c \
    src/core/lib/iomgr/unix_sockets_posix.c \
    src/core/lib/iomgr/unix_sockets_posix_noop.c \
    src/core/lib/iomgr/wakeup_fd_cv.c \
    src/core/lib/iomgr/wakeup_fd_eventfd.c \
    src/core/lib/iomgr/wakeup_fd_nospecial.c \
    src/core/lib/iomgr/wakeup_fd_pipe.c \
    src/core/lib/iomgr/wakeup_fd_posix.c \
    src/core/lib/iomgr/workqueue_uv.c \
    src/core/lib/iomgr/workqueue_windows.c \
    src/core/lib/json/json.c \
    src/core/lib/json/json_reader.c \
    src/core/lib/json/json_string.c \
    src/core/lib/json/json_writer.c \
    src/core/lib/slice/percent_encoding.c \
    src/core/lib/slice/slice.c \
    src/core/lib/slice/slice_buffer.c \
    src/core/lib/slice/slice_string_helpers.c \
    src/core/lib/surface/alarm.c \
    src/core/lib/surface/api_trace.c \
    src/core/lib/surface/byte_buffer.c \
    src/core/lib/surface/byte_buffer_reader.c \
    src/core/lib/surface/call.c \
    src/core/lib/surface/call_details.c \
    src/core/lib/surface/call_log_batch.c \
    src/core/lib/surface/channel.c \
    src/core/lib/surface/channel_init.c \
    src/core/lib/surface/channel_ping.c \
    src/core/lib/surface/channel_stack_type.c \
    src/core/lib/surface/completion_queue.c \
    src/core/lib/surface/event_string.c \
    src/core/lib/surface/lame_client.c \
    src/core/lib/surface/metadata_array.c \
    src/core/lib/surface/server.c \
    src/core/lib/surface/validate_metadata.c \
    src/core/lib/surface/version.c \
    src/core/lib/transport/byte_stream.c \
    src/core/lib/transport/connectivity_state.c \
    src/core/lib/transport/mdstr_hash_table.c \
    src/core/lib/transport/metadata.c \
    src/core/lib/transport/metadata_batch.c \
    src/core/lib/transport/pid_controller.c \
    src/core/lib/transport/service_config.c \
    src/core/lib/transport/static_metadata.c \
    src/core/lib/transport/timeout_encoding.c \
    src/core/lib/transport/transport.c \
    src/core/lib/transport/transport_op_string.c \

PUBLIC_HEADERS_C += \
    include/grpc/byte_buffer.h \
    include/grpc/byte_buffer_reader.h \
    include/grpc/compression.h \
    include/grpc/grpc.h \
    include/grpc/grpc_posix.h \
    include/grpc/grpc_security_constants.h \
    include/grpc/slice.h \
    include/grpc/slice_buffer.h \
    include/grpc/status.h \
    include/grpc/impl/codegen/byte_buffer_reader.h \
    include/grpc/impl/codegen/compression_types.h \
    include/grpc/impl/codegen/connectivity_state.h \
    include/grpc/impl/codegen/grpc_types.h \
    include/grpc/impl/codegen/propagation_bits.h \
    include/grpc/impl/codegen/status.h \
    include/grpc/impl/codegen/atm.h \
    include/grpc/impl/codegen/atm_gcc_atomic.h \
    include/grpc/impl/codegen/atm_gcc_sync.h \
    include/grpc/impl/codegen/atm_windows.h \
    include/grpc/impl/codegen/gpr_types.h \
    include/grpc/impl/codegen/port_platform.h \
    include/grpc/impl/codegen/slice.h \
    include/grpc/impl/codegen/sync.h \
    include/grpc/impl/codegen/sync_generic.h \
    include/grpc/impl/codegen/sync_posix.h \
    include/grpc/impl/codegen/sync_windows.h \

LIBGRPC_TEST_UTIL_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC_TEST_UTIL_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libgrpc_test_util.a: openssl_dep_error


else


$(LIBDIR)/$(CONFIG)/libgrpc_test_util.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(LIBGRPC_TEST_UTIL_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBGRPC_TEST_UTIL_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a
endif




endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBGRPC_TEST_UTIL_OBJS:.o=.dep)
endif
endif


LIBGRPC_TEST_UTIL_UNSECURE_SRC = \
    test/core/end2end/cq_verifier.c \
    test/core/end2end/fake_resolver.c \
    test/core/end2end/fixtures/http_proxy.c \
    test/core/end2end/fixtures/proxy.c \
    test/core/iomgr/endpoint_tests.c \
    test/core/util/grpc_profiler.c \
    test/core/util/memory_counters.c \
    test/core/util/mock_endpoint.c \
    test/core/util/parse_hexstring.c \
    test/core/util/passthru_endpoint.c \
    test/core/util/port_posix.c \
    test/core/util/port_server_client.c \
    test/core/util/port_uv.c \
    test/core/util/port_windows.c \
    test/core/util/slice_splitter.c \

PUBLIC_HEADERS_C += \

LIBGRPC_TEST_UTIL_UNSECURE_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC_TEST_UTIL_UNSECURE_SRC))))


$(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a: $(ZLIB_DEP)  $(LIBGRPC_TEST_UTIL_UNSECURE_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBGRPC_TEST_UTIL_UNSECURE_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a
endif




ifneq ($(NO_DEPS),true)
-include $(LIBGRPC_TEST_UTIL_UNSECURE_OBJS:.o=.dep)
endif


LIBGRPC_UNSECURE_SRC = \
    src/core/lib/surface/init.c \
    src/core/lib/surface/init_unsecure.c \
    src/core/lib/channel/channel_args.c \
    src/core/lib/channel/channel_stack.c \
    src/core/lib/channel/channel_stack_builder.c \
    src/core/lib/channel/compress_filter.c \
    src/core/lib/channel/connected_channel.c \
    src/core/lib/channel/deadline_filter.c \
    src/core/lib/channel/handshaker.c \
    src/core/lib/channel/http_client_filter.c \
    src/core/lib/channel/http_server_filter.c \
    src/core/lib/channel/message_size_filter.c \
    src/core/lib/compression/compression.c \
    src/core/lib/compression/message_compress.c \
    src/core/lib/debug/trace.c \
    src/core/lib/http/format_request.c \
    src/core/lib/http/httpcli.c \
    src/core/lib/http/parser.c \
    src/core/lib/iomgr/closure.c \
    src/core/lib/iomgr/combiner.c \
    src/core/lib/iomgr/endpoint.c \
    src/core/lib/iomgr/endpoint_pair_posix.c \
    src/core/lib/iomgr/endpoint_pair_uv.c \
    src/core/lib/iomgr/endpoint_pair_windows.c \
    src/core/lib/iomgr/error.c \
    src/core/lib/iomgr/ev_epoll_linux.c \
    src/core/lib/iomgr/ev_poll_posix.c \
    src/core/lib/iomgr/ev_posix.c \
    src/core/lib/iomgr/exec_ctx.c \
    src/core/lib/iomgr/executor.c \
    src/core/lib/iomgr/iocp_windows.c \
    src/core/lib/iomgr/iomgr.c \
    src/core/lib/iomgr/iomgr_posix.c \
    src/core/lib/iomgr/iomgr_uv.c \
    src/core/lib/iomgr/iomgr_windows.c \
    src/core/lib/iomgr/load_file.c \
    src/core/lib/iomgr/network_status_tracker.c \
    src/core/lib/iomgr/polling_entity.c \
    src/core/lib/iomgr/pollset_set_uv.c \
    src/core/lib/iomgr/pollset_set_windows.c \
    src/core/lib/iomgr/pollset_uv.c \
    src/core/lib/iomgr/pollset_windows.c \
    src/core/lib/iomgr/resolve_address_posix.c \
    src/core/lib/iomgr/resolve_address_uv.c \
    src/core/lib/iomgr/resolve_address_windows.c \
    src/core/lib/iomgr/resource_quota.c \
    src/core/lib/iomgr/sockaddr_utils.c \
    src/core/lib/iomgr/socket_mutator.c \
    src/core/lib/iomgr/socket_utils_common_posix.c \
    src/core/lib/iomgr/socket_utils_linux.c \
    src/core/lib/iomgr/socket_utils_posix.c \
    src/core/lib/iomgr/socket_utils_uv.c \
    src/core/lib/iomgr/socket_utils_windows.c \
    src/core/lib/iomgr/socket_windows.c \
    src/core/lib/iomgr/tcp_client_posix.c \
    src/core/lib/iomgr/tcp_client_uv.c \
    src/core/lib/iomgr/tcp_client_windows.c \
    src/core/lib/iomgr/tcp_posix.c \
    src/core/lib/iomgr/tcp_server_posix.c \
    src/core/lib/iomgr/tcp_server_uv.c \
    src/core/lib/iomgr/tcp_server_windows.c \
    src/core/lib/iomgr/tcp_uv.c \
    src/core/lib/iomgr/tcp_windows.c \
    src/core/lib/iomgr/time_averaged_stats.c \
    src/core/lib/iomgr/timer_generic.c \
    src/core/lib/iomgr/timer_heap.c \
    src/core/lib/iomgr/timer_uv.c \
    src/core/lib/iomgr/udp_server.c \
    src/core/lib/iomgr/unix_sockets_posix.c \
    src/core/lib/iomgr/unix_sockets_posix_noop.c \
    src/core/lib/iomgr/wakeup_fd_cv.c \
    src/core/lib/iomgr/wakeup_fd_eventfd.c \
    src/core/lib/iomgr/wakeup_fd_nospecial.c \
    src/core/lib/iomgr/wakeup_fd_pipe.c \
    src/core/lib/iomgr/wakeup_fd_posix.c \
    src/core/lib/iomgr/workqueue_uv.c \
    src/core/lib/iomgr/workqueue_windows.c \
    src/core/lib/json/json.c \
    src/core/lib/json/json_reader.c \
    src/core/lib/json/json_string.c \
    src/core/lib/json/json_writer.c \
    src/core/lib/slice/percent_encoding.c \
    src/core/lib/slice/slice.c \
    src/core/lib/slice/slice_buffer.c \
    src/core/lib/slice/slice_string_helpers.c \
    src/core/lib/surface/alarm.c \
    src/core/lib/surface/api_trace.c \
    src/core/lib/surface/byte_buffer.c \
    src/core/lib/surface/byte_buffer_reader.c \
    src/core/lib/surface/call.c \
    src/core/lib/surface/call_details.c \
    src/core/lib/surface/call_log_batch.c \
    src/core/lib/surface/channel.c \
    src/core/lib/surface/channel_init.c \
    src/core/lib/surface/channel_ping.c \
    src/core/lib/surface/channel_stack_type.c \
    src/core/lib/surface/completion_queue.c \
    src/core/lib/surface/event_string.c \
    src/core/lib/surface/lame_client.c \
    src/core/lib/surface/metadata_array.c \
    src/core/lib/surface/server.c \
    src/core/lib/surface/validate_metadata.c \
    src/core/lib/surface/version.c \
    src/core/lib/transport/byte_stream.c \
    src/core/lib/transport/connectivity_state.c \
    src/core/lib/transport/mdstr_hash_table.c \
    src/core/lib/transport/metadata.c \
    src/core/lib/transport/metadata_batch.c \
    src/core/lib/transport/pid_controller.c \
    src/core/lib/transport/service_config.c \
    src/core/lib/transport/static_metadata.c \
    src/core/lib/transport/timeout_encoding.c \
    src/core/lib/transport/transport.c \
    src/core/lib/transport/transport_op_string.c \
    src/core/ext/transport/chttp2/server/insecure/server_chttp2.c \
    src/core/ext/transport/chttp2/server/insecure/server_chttp2_posix.c \
    src/core/ext/transport/chttp2/transport/bin_decoder.c \
    src/core/ext/transport/chttp2/transport/bin_encoder.c \
    src/core/ext/transport/chttp2/transport/chttp2_plugin.c \
    src/core/ext/transport/chttp2/transport/chttp2_transport.c \
    src/core/ext/transport/chttp2/transport/frame_data.c \
    src/core/ext/transport/chttp2/transport/frame_goaway.c \
    src/core/ext/transport/chttp2/transport/frame_ping.c \
    src/core/ext/transport/chttp2/transport/frame_rst_stream.c \
    src/core/ext/transport/chttp2/transport/frame_settings.c \
    src/core/ext/transport/chttp2/transport/frame_window_update.c \
    src/core/ext/transport/chttp2/transport/hpack_encoder.c \
    src/core/ext/transport/chttp2/transport/hpack_parser.c \
    src/core/ext/transport/chttp2/transport/hpack_table.c \
    src/core/ext/transport/chttp2/transport/huffsyms.c \
    src/core/ext/transport/chttp2/transport/incoming_metadata.c \
    src/core/ext/transport/chttp2/transport/parsing.c \
    src/core/ext/transport/chttp2/transport/status_conversion.c \
    src/core/ext/transport/chttp2/transport/stream_lists.c \
    src/core/ext/transport/chttp2/transport/stream_map.c \
    src/core/ext/transport/chttp2/transport/varint.c \
    src/core/ext/transport/chttp2/transport/writing.c \
    src/core/ext/transport/chttp2/alpn/alpn.c \
    src/core/ext/transport/chttp2/client/insecure/channel_create.c \
    src/core/ext/transport/chttp2/client/insecure/channel_create_posix.c \
    src/core/ext/client_channel/channel_connectivity.c \
    src/core/ext/client_channel/client_channel.c \
    src/core/ext/client_channel/client_channel_factory.c \
    src/core/ext/client_channel/client_channel_plugin.c \
    src/core/ext/client_channel/connector.c \
    src/core/ext/client_channel/default_initial_connect_string.c \
    src/core/ext/client_channel/http_connect_handshaker.c \
    src/core/ext/client_channel/initial_connect_string.c \
    src/core/ext/client_channel/lb_policy.c \
    src/core/ext/client_channel/lb_policy_factory.c \
    src/core/ext/client_channel/lb_policy_registry.c \
    src/core/ext/client_channel/parse_address.c \
    src/core/ext/client_channel/resolver.c \
    src/core/ext/client_channel/resolver_factory.c \
    src/core/ext/client_channel/resolver_registry.c \
    src/core/ext/client_channel/subchannel.c \
    src/core/ext/client_channel/subchannel_index.c \
    src/core/ext/client_channel/uri_parser.c \
    src/core/ext/transport/chttp2/client/chttp2_connector.c \
    src/core/ext/resolver/dns/native/dns_resolver.c \
    src/core/ext/resolver/sockaddr/sockaddr_resolver.c \
    src/core/ext/load_reporting/load_reporting.c \
    src/core/ext/load_reporting/load_reporting_filter.c \
    src/core/ext/lb_policy/grpclb/grpclb.c \
    src/core/ext/lb_policy/grpclb/load_balancer_api.c \
    src/core/ext/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.c \
    third_party/nanopb/pb_common.c \
    third_party/nanopb/pb_decode.c \
    third_party/nanopb/pb_encode.c \
    src/core/ext/lb_policy/pick_first/pick_first.c \
    src/core/ext/lb_policy/round_robin/round_robin.c \
    src/core/ext/census/base_resources.c \
    src/core/ext/census/context.c \
    src/core/ext/census/gen/census.pb.c \
    src/core/ext/census/gen/trace_context.pb.c \
    src/core/ext/census/grpc_context.c \
    src/core/ext/census/grpc_filter.c \
    src/core/ext/census/grpc_plugin.c \
    src/core/ext/census/initialize.c \
    src/core/ext/census/mlog.c \
    src/core/ext/census/operation.c \
    src/core/ext/census/placeholders.c \
    src/core/ext/census/resource.c \
    src/core/ext/census/trace_context.c \
    src/core/ext/census/tracing.c \
    src/core/plugin_registry/grpc_unsecure_plugin_registry.c \

PUBLIC_HEADERS_C += \
    include/grpc/byte_buffer.h \
    include/grpc/byte_buffer_reader.h \
    include/grpc/compression.h \
    include/grpc/grpc.h \
    include/grpc/grpc_posix.h \
    include/grpc/grpc_security_constants.h \
    include/grpc/slice.h \
    include/grpc/slice_buffer.h \
    include/grpc/status.h \
    include/grpc/impl/codegen/byte_buffer_reader.h \
    include/grpc/impl/codegen/compression_types.h \
    include/grpc/impl/codegen/connectivity_state.h \
    include/grpc/impl/codegen/grpc_types.h \
    include/grpc/impl/codegen/propagation_bits.h \
    include/grpc/impl/codegen/status.h \
    include/grpc/impl/codegen/atm.h \
    include/grpc/impl/codegen/atm_gcc_atomic.h \
    include/grpc/impl/codegen/atm_gcc_sync.h \
    include/grpc/impl/codegen/atm_windows.h \
    include/grpc/impl/codegen/gpr_types.h \
    include/grpc/impl/codegen/port_platform.h \
    include/grpc/impl/codegen/slice.h \
    include/grpc/impl/codegen/sync.h \
    include/grpc/impl/codegen/sync_generic.h \
    include/grpc/impl/codegen/sync_posix.h \
    include/grpc/impl/codegen/sync_windows.h \
    include/grpc/census.h \

LIBGRPC_UNSECURE_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC_UNSECURE_SRC))))


$(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a: $(ZLIB_DEP)  $(LIBGRPC_UNSECURE_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBGRPC_UNSECURE_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a
endif



ifeq ($(SYSTEM),MINGW32)
$(LIBDIR)/$(CONFIG)/grpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): $(LIBGRPC_UNSECURE_OBJS)  $(ZLIB_DEP) $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc_transport_chttp2.a $(LIBDIR)/$(CONFIG)/libgrpc_base.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared grpc_unsecure.def -Wl,--output-def=$(LIBDIR)/$(CONFIG)/grpc_unsecure$(SHARED_VERSION_CORE).def -Wl,--out-implib=$(LIBDIR)/$(CONFIG)/libgrpc_unsecure$(SHARED_VERSION_CORE)-dll.a -o $(LIBDIR)/$(CONFIG)/grpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGRPC_UNSECURE_OBJS) $(LDLIBS) $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc_transport_chttp2.a $(LIBDIR)/$(CONFIG)/libgrpc_base.a $(ZLIB_MERGE_LIBS)
else
$(LIBDIR)/$(CONFIG)/libgrpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): $(LIBGRPC_UNSECURE_OBJS)  $(ZLIB_DEP) $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc_transport_chttp2.a $(LIBDIR)/$(CONFIG)/libgrpc_base.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
ifeq ($(SYSTEM),Darwin)
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -install_name $(SHARED_PREFIX)grpc_unsecure$(SHARED_VERSION).$(SHARED_EXT_CORE) -dynamiclib -o $(LIBDIR)/$(CONFIG)/libgrpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGRPC_UNSECURE_OBJS) $(LDLIBS) $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc_transport_chttp2.a $(LIBDIR)/$(CONFIG)/libgrpc_base.a $(ZLIB_MERGE_LIBS)
else
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,-soname,libgrpc_unsecure.so.2 -o $(LIBDIR)/$(CONFIG)/libgrpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGRPC_UNSECURE_OBJS) $(LDLIBS) $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc_transport_chttp2.a $(LIBDIR)/$(CONFIG)/libgrpc_base.a $(ZLIB_MERGE_LIBS)
	$(Q) ln -sf $(SHARED_PREFIX)grpc_unsecure$(SHARED_VERSION).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/libgrpc_unsecure$(SHARED_VERSION_CORE).so.2
	$(Q) ln -sf $(SHARED_PREFIX)grpc_unsecure$(SHARED_VERSION).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/libgrpc_unsecure$(SHARED_VERSION_CORE).so
endif
endif

ifneq ($(NO_DEPS),true)
-include $(LIBGRPC_UNSECURE_OBJS:.o=.dep)
endif


LIBRECONNECT_SERVER_SRC = \
    test/core/util/reconnect_server.c \

PUBLIC_HEADERS_C += \

LIBRECONNECT_SERVER_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBRECONNECT_SERVER_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libreconnect_server.a: openssl_dep_error


else


$(LIBDIR)/$(CONFIG)/libreconnect_server.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(LIBRECONNECT_SERVER_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libreconnect_server.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libreconnect_server.a $(LIBRECONNECT_SERVER_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libreconnect_server.a
endif




endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBRECONNECT_SERVER_OBJS:.o=.dep)
endif
endif


LIBTEST_TCP_SERVER_SRC = \
    test/core/util/test_tcp_server.c \

PUBLIC_HEADERS_C += \

LIBTEST_TCP_SERVER_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBTEST_TCP_SERVER_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libtest_tcp_server.a: openssl_dep_error


else


$(LIBDIR)/$(CONFIG)/libtest_tcp_server.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(LIBTEST_TCP_SERVER_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libtest_tcp_server.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libtest_tcp_server.a $(LIBTEST_TCP_SERVER_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libtest_tcp_server.a
endif




endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBTEST_TCP_SERVER_OBJS:.o=.dep)
endif
endif


LIBGRPC++_SRC = \
    src/cpp/client/insecure_credentials.cc \
    src/cpp/client/secure_credentials.cc \
    src/cpp/common/auth_property_iterator.cc \
    src/cpp/common/secure_auth_context.cc \
    src/cpp/common/secure_channel_arguments.cc \
    src/cpp/common/secure_create_auth_context.cc \
    src/cpp/server/insecure_server_credentials.cc \
    src/cpp/server/secure_server_credentials.cc \
    src/cpp/client/channel_cc.cc \
    src/cpp/client/client_context.cc \
    src/cpp/client/create_channel.cc \
    src/cpp/client/create_channel_internal.cc \
    src/cpp/client/create_channel_posix.cc \
    src/cpp/client/credentials_cc.cc \
    src/cpp/client/generic_stub.cc \
    src/cpp/common/channel_arguments.cc \
    src/cpp/common/channel_filter.cc \
    src/cpp/common/completion_queue_cc.cc \
    src/cpp/common/core_codegen.cc \
    src/cpp/common/resource_quota_cc.cc \
    src/cpp/common/rpc_method.cc \
    src/cpp/common/version_cc.cc \
    src/cpp/server/async_generic_service.cc \
    src/cpp/server/create_default_thread_pool.cc \
    src/cpp/server/dynamic_thread_pool.cc \
    src/cpp/server/server_builder.cc \
    src/cpp/server/server_cc.cc \
    src/cpp/server/server_context.cc \
    src/cpp/server/server_credentials.cc \
    src/cpp/server/server_posix.cc \
    src/cpp/thread_manager/thread_manager.cc \
    src/cpp/util/byte_buffer_cc.cc \
    src/cpp/util/slice_cc.cc \
    src/cpp/util/status.cc \
    src/cpp/util/string_ref.cc \
    src/cpp/util/time_cc.cc \
    src/cpp/codegen/codegen_init.cc \

PUBLIC_HEADERS_CXX += \
    include/grpc++/alarm.h \
    include/grpc++/channel.h \
    include/grpc++/client_context.h \
    include/grpc++/completion_queue.h \
    include/grpc++/create_channel.h \
    include/grpc++/create_channel_posix.h \
    include/grpc++/generic/async_generic_service.h \
    include/grpc++/generic/generic_stub.h \
    include/grpc++/grpc++.h \
    include/grpc++/impl/call.h \
    include/grpc++/impl/client_unary_call.h \
    include/grpc++/impl/codegen/core_codegen.h \
    include/grpc++/impl/grpc_library.h \
    include/grpc++/impl/method_handler_impl.h \
    include/grpc++/impl/rpc_method.h \
    include/grpc++/impl/rpc_service_method.h \
    include/grpc++/impl/serialization_traits.h \
    include/grpc++/impl/server_builder_option.h \
    include/grpc++/impl/server_builder_plugin.h \
    include/grpc++/impl/server_initializer.h \
    include/grpc++/impl/service_type.h \
    include/grpc++/resource_quota.h \
    include/grpc++/security/auth_context.h \
    include/grpc++/security/auth_metadata_processor.h \
    include/grpc++/security/credentials.h \
    include/grpc++/security/server_credentials.h \
    include/grpc++/server.h \
    include/grpc++/server_builder.h \
    include/grpc++/server_context.h \
    include/grpc++/server_posix.h \
    include/grpc++/support/async_stream.h \
    include/grpc++/support/async_unary_call.h \
    include/grpc++/support/byte_buffer.h \
    include/grpc++/support/channel_arguments.h \
    include/grpc++/support/config.h \
    include/grpc++/support/slice.h \
    include/grpc++/support/status.h \
    include/grpc++/support/status_code_enum.h \
    include/grpc++/support/string_ref.h \
    include/grpc++/support/stub_options.h \
    include/grpc++/support/sync_stream.h \
    include/grpc++/support/time.h \
    include/grpc++/impl/codegen/async_stream.h \
    include/grpc++/impl/codegen/async_unary_call.h \
    include/grpc++/impl/codegen/call.h \
    include/grpc++/impl/codegen/call_hook.h \
    include/grpc++/impl/codegen/channel_interface.h \
    include/grpc++/impl/codegen/client_context.h \
    include/grpc++/impl/codegen/client_unary_call.h \
    include/grpc++/impl/codegen/completion_queue.h \
    include/grpc++/impl/codegen/completion_queue_tag.h \
    include/grpc++/impl/codegen/config.h \
    include/grpc++/impl/codegen/core_codegen_interface.h \
    include/grpc++/impl/codegen/create_auth_context.h \
    include/grpc++/impl/codegen/grpc_library.h \
    include/grpc++/impl/codegen/method_handler_impl.h \
    include/grpc++/impl/codegen/rpc_method.h \
    include/grpc++/impl/codegen/rpc_service_method.h \
    include/grpc++/impl/codegen/security/auth_context.h \
    include/grpc++/impl/codegen/serialization_traits.h \
    include/grpc++/impl/codegen/server_context.h \
    include/grpc++/impl/codegen/server_interface.h \
    include/grpc++/impl/codegen/service_type.h \
    include/grpc++/impl/codegen/status.h \
    include/grpc++/impl/codegen/status_code_enum.h \
    include/grpc++/impl/codegen/status_helper.h \
    include/grpc++/impl/codegen/string_ref.h \
    include/grpc++/impl/codegen/stub_options.h \
    include/grpc++/impl/codegen/sync_stream.h \
    include/grpc++/impl/codegen/time.h \
    include/grpc/impl/codegen/byte_buffer_reader.h \
    include/grpc/impl/codegen/compression_types.h \
    include/grpc/impl/codegen/connectivity_state.h \
    include/grpc/impl/codegen/grpc_types.h \
    include/grpc/impl/codegen/propagation_bits.h \
    include/grpc/impl/codegen/status.h \
    include/grpc/impl/codegen/atm.h \
    include/grpc/impl/codegen/atm_gcc_atomic.h \
    include/grpc/impl/codegen/atm_gcc_sync.h \
    include/grpc/impl/codegen/atm_windows.h \
    include/grpc/impl/codegen/gpr_types.h \
    include/grpc/impl/codegen/port_platform.h \
    include/grpc/impl/codegen/slice.h \
    include/grpc/impl/codegen/sync.h \
    include/grpc/impl/codegen/sync_generic.h \
    include/grpc/impl/codegen/sync_posix.h \
    include/grpc/impl/codegen/sync_windows.h \

LIBGRPC++_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC++_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libgrpc++.a: openssl_dep_error

$(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP): openssl_dep_error

else

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libgrpc++.a: protobuf_dep_error

$(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP): protobuf_dep_error

else

$(LIBDIR)/$(CONFIG)/libgrpc++.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(PROTOBUF_DEP) $(LIBGRPC++_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc++.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBGRPC++_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc++.a
endif



ifeq ($(SYSTEM),MINGW32)
$(LIBDIR)/$(CONFIG)/grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP): $(LIBGRPC++_OBJS)  $(ZLIB_DEP) $(PROTOBUF_DEP) $(LIBDIR)/$(CONFIG)/grpc.$(SHARED_EXT_CORE) $(OPENSSL_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared grpc++.def -Wl,--output-def=$(LIBDIR)/$(CONFIG)/grpc++$(SHARED_VERSION_CPP).def -Wl,--out-implib=$(LIBDIR)/$(CONFIG)/libgrpc++$(SHARED_VERSION_CPP)-dll.a -o $(LIBDIR)/$(CONFIG)/grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBGRPC++_OBJS) $(LDLIBS) $(ZLIB_MERGE_LIBS) $(LDLIBSXX) $(LDLIBS_PROTOBUF) -lgrpc-imp
else
$(LIBDIR)/$(CONFIG)/libgrpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP): $(LIBGRPC++_OBJS)  $(ZLIB_DEP) $(PROTOBUF_DEP) $(LIBDIR)/$(CONFIG)/libgrpc.$(SHARED_EXT_CORE) $(OPENSSL_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
ifeq ($(SYSTEM),Darwin)
	$(Q) $(LDXX) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -install_name $(SHARED_PREFIX)grpc++$(SHARED_VERSION).$(SHARED_EXT_CPP) -dynamiclib -o $(LIBDIR)/$(CONFIG)/libgrpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBGRPC++_OBJS) $(LDLIBS) $(ZLIB_MERGE_LIBS) $(LDLIBSXX) $(LDLIBS_PROTOBUF) -lgrpc
else
	$(Q) $(LDXX) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,-soname,libgrpc++.so.2 -o $(LIBDIR)/$(CONFIG)/libgrpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBGRPC++_OBJS) $(LDLIBS) $(ZLIB_MERGE_LIBS) $(LDLIBSXX) $(LDLIBS_PROTOBUF) -lgrpc
	$(Q) ln -sf $(SHARED_PREFIX)grpc++$(SHARED_VERSION).$(SHARED_EXT_CPP) $(LIBDIR)/$(CONFIG)/libgrpc++$(SHARED_VERSION_CPP).so.1
	$(Q) ln -sf $(SHARED_PREFIX)grpc++$(SHARED_VERSION).$(SHARED_EXT_CPP) $(LIBDIR)/$(CONFIG)/libgrpc++$(SHARED_VERSION_CPP).so
endif
endif

endif

endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBGRPC++_OBJS:.o=.dep)
endif
endif


LIBGRPC++_CRONET_SRC = \
    src/cpp/client/cronet_credentials.cc \
    src/cpp/client/insecure_credentials.cc \
    src/cpp/common/insecure_create_auth_context.cc \
    src/cpp/server/insecure_server_credentials.cc \
    src/cpp/client/channel_cc.cc \
    src/cpp/client/client_context.cc \
    src/cpp/client/create_channel.cc \
    src/cpp/client/create_channel_internal.cc \
    src/cpp/client/create_channel_posix.cc \
    src/cpp/client/credentials_cc.cc \
    src/cpp/client/generic_stub.cc \
    src/cpp/common/channel_arguments.cc \
    src/cpp/common/channel_filter.cc \
    src/cpp/common/completion_queue_cc.cc \
    src/cpp/common/core_codegen.cc \
    src/cpp/common/resource_quota_cc.cc \
    src/cpp/common/rpc_method.cc \
    src/cpp/common/version_cc.cc \
    src/cpp/server/async_generic_service.cc \
    src/cpp/server/create_default_thread_pool.cc \
    src/cpp/server/dynamic_thread_pool.cc \
    src/cpp/server/server_builder.cc \
    src/cpp/server/server_cc.cc \
    src/cpp/server/server_context.cc \
    src/cpp/server/server_credentials.cc \
    src/cpp/server/server_posix.cc \
    src/cpp/thread_manager/thread_manager.cc \
    src/cpp/util/byte_buffer_cc.cc \
    src/cpp/util/slice_cc.cc \
    src/cpp/util/status.cc \
    src/cpp/util/string_ref.cc \
    src/cpp/util/time_cc.cc \
    src/cpp/codegen/codegen_init.cc \
    src/core/ext/transport/chttp2/client/insecure/channel_create.c \
    src/core/ext/transport/chttp2/client/insecure/channel_create_posix.c \
    src/core/ext/transport/chttp2/transport/bin_decoder.c \
    src/core/ext/transport/chttp2/transport/bin_encoder.c \
    src/core/ext/transport/chttp2/transport/chttp2_plugin.c \
    src/core/ext/transport/chttp2/transport/chttp2_transport.c \
    src/core/ext/transport/chttp2/transport/frame_data.c \
    src/core/ext/transport/chttp2/transport/frame_goaway.c \
    src/core/ext/transport/chttp2/transport/frame_ping.c \
    src/core/ext/transport/chttp2/transport/frame_rst_stream.c \
    src/core/ext/transport/chttp2/transport/frame_settings.c \
    src/core/ext/transport/chttp2/transport/frame_window_update.c \
    src/core/ext/transport/chttp2/transport/hpack_encoder.c \
    src/core/ext/transport/chttp2/transport/hpack_parser.c \
    src/core/ext/transport/chttp2/transport/hpack_table.c \
    src/core/ext/transport/chttp2/transport/huffsyms.c \
    src/core/ext/transport/chttp2/transport/incoming_metadata.c \
    src/core/ext/transport/chttp2/transport/parsing.c \
    src/core/ext/transport/chttp2/transport/status_conversion.c \
    src/core/ext/transport/chttp2/transport/stream_lists.c \
    src/core/ext/transport/chttp2/transport/stream_map.c \
    src/core/ext/transport/chttp2/transport/varint.c \
    src/core/ext/transport/chttp2/transport/writing.c \
    src/core/lib/channel/channel_args.c \
    src/core/lib/channel/channel_stack.c \
    src/core/lib/channel/channel_stack_builder.c \
    src/core/lib/channel/compress_filter.c \
    src/core/lib/channel/connected_channel.c \
    src/core/lib/channel/deadline_filter.c \
    src/core/lib/channel/handshaker.c \
    src/core/lib/channel/http_client_filter.c \
    src/core/lib/channel/http_server_filter.c \
    src/core/lib/channel/message_size_filter.c \
    src/core/lib/compression/compression.c \
    src/core/lib/compression/message_compress.c \
    src/core/lib/debug/trace.c \
    src/core/lib/http/format_request.c \
    src/core/lib/http/httpcli.c \
    src/core/lib/http/parser.c \
    src/core/lib/iomgr/closure.c \
    src/core/lib/iomgr/combiner.c \
    src/core/lib/iomgr/endpoint.c \
    src/core/lib/iomgr/endpoint_pair_posix.c \
    src/core/lib/iomgr/endpoint_pair_uv.c \
    src/core/lib/iomgr/endpoint_pair_windows.c \
    src/core/lib/iomgr/error.c \
    src/core/lib/iomgr/ev_epoll_linux.c \
    src/core/lib/iomgr/ev_poll_posix.c \
    src/core/lib/iomgr/ev_posix.c \
    src/core/lib/iomgr/exec_ctx.c \
    src/core/lib/iomgr/executor.c \
    src/core/lib/iomgr/iocp_windows.c \
    src/core/lib/iomgr/iomgr.c \
    src/core/lib/iomgr/iomgr_posix.c \
    src/core/lib/iomgr/iomgr_uv.c \
    src/core/lib/iomgr/iomgr_windows.c \
    src/core/lib/iomgr/load_file.c \
    src/core/lib/iomgr/network_status_tracker.c \
    src/core/lib/iomgr/polling_entity.c \
    src/core/lib/iomgr/pollset_set_uv.c \
    src/core/lib/iomgr/pollset_set_windows.c \
    src/core/lib/iomgr/pollset_uv.c \
    src/core/lib/iomgr/pollset_windows.c \
    src/core/lib/iomgr/resolve_address_posix.c \
    src/core/lib/iomgr/resolve_address_uv.c \
    src/core/lib/iomgr/resolve_address_windows.c \
    src/core/lib/iomgr/resource_quota.c \
    src/core/lib/iomgr/sockaddr_utils.c \
    src/core/lib/iomgr/socket_mutator.c \
    src/core/lib/iomgr/socket_utils_common_posix.c \
    src/core/lib/iomgr/socket_utils_linux.c \
    src/core/lib/iomgr/socket_utils_posix.c \
    src/core/lib/iomgr/socket_utils_uv.c \
    src/core/lib/iomgr/socket_utils_windows.c \
    src/core/lib/iomgr/socket_windows.c \
    src/core/lib/iomgr/tcp_client_posix.c \
    src/core/lib/iomgr/tcp_client_uv.c \
    src/core/lib/iomgr/tcp_client_windows.c \
    src/core/lib/iomgr/tcp_posix.c \
    src/core/lib/iomgr/tcp_server_posix.c \
    src/core/lib/iomgr/tcp_server_uv.c \
    src/core/lib/iomgr/tcp_server_windows.c \
    src/core/lib/iomgr/tcp_uv.c \
    src/core/lib/iomgr/tcp_windows.c \
    src/core/lib/iomgr/time_averaged_stats.c \
    src/core/lib/iomgr/timer_generic.c \
    src/core/lib/iomgr/timer_heap.c \
    src/core/lib/iomgr/timer_uv.c \
    src/core/lib/iomgr/udp_server.c \
    src/core/lib/iomgr/unix_sockets_posix.c \
    src/core/lib/iomgr/unix_sockets_posix_noop.c \
    src/core/lib/iomgr/wakeup_fd_cv.c \
    src/core/lib/iomgr/wakeup_fd_eventfd.c \
    src/core/lib/iomgr/wakeup_fd_nospecial.c \
    src/core/lib/iomgr/wakeup_fd_pipe.c \
    src/core/lib/iomgr/wakeup_fd_posix.c \
    src/core/lib/iomgr/workqueue_uv.c \
    src/core/lib/iomgr/workqueue_windows.c \
    src/core/lib/json/json.c \
    src/core/lib/json/json_reader.c \
    src/core/lib/json/json_string.c \
    src/core/lib/json/json_writer.c \
    src/core/lib/slice/percent_encoding.c \
    src/core/lib/slice/slice.c \
    src/core/lib/slice/slice_buffer.c \
    src/core/lib/slice/slice_string_helpers.c \
    src/core/lib/surface/alarm.c \
    src/core/lib/surface/api_trace.c \
    src/core/lib/surface/byte_buffer.c \
    src/core/lib/surface/byte_buffer_reader.c \
    src/core/lib/surface/call.c \
    src/core/lib/surface/call_details.c \
    src/core/lib/surface/call_log_batch.c \
    src/core/lib/surface/channel.c \
    src/core/lib/surface/channel_init.c \
    src/core/lib/surface/channel_ping.c \
    src/core/lib/surface/channel_stack_type.c \
    src/core/lib/surface/completion_queue.c \
    src/core/lib/surface/event_string.c \
    src/core/lib/surface/lame_client.c \
    src/core/lib/surface/metadata_array.c \
    src/core/lib/surface/server.c \
    src/core/lib/surface/validate_metadata.c \
    src/core/lib/surface/version.c \
    src/core/lib/transport/byte_stream.c \
    src/core/lib/transport/connectivity_state.c \
    src/core/lib/transport/mdstr_hash_table.c \
    src/core/lib/transport/metadata.c \
    src/core/lib/transport/metadata_batch.c \
    src/core/lib/transport/pid_controller.c \
    src/core/lib/transport/service_config.c \
    src/core/lib/transport/static_metadata.c \
    src/core/lib/transport/timeout_encoding.c \
    src/core/lib/transport/transport.c \
    src/core/lib/transport/transport_op_string.c \
    src/core/ext/transport/chttp2/alpn/alpn.c \
    src/core/ext/client_channel/channel_connectivity.c \
    src/core/ext/client_channel/client_channel.c \
    src/core/ext/client_channel/client_channel_factory.c \
    src/core/ext/client_channel/client_channel_plugin.c \
    src/core/ext/client_channel/connector.c \
    src/core/ext/client_channel/default_initial_connect_string.c \
    src/core/ext/client_channel/http_connect_handshaker.c \
    src/core/ext/client_channel/initial_connect_string.c \
    src/core/ext/client_channel/lb_policy.c \
    src/core/ext/client_channel/lb_policy_factory.c \
    src/core/ext/client_channel/lb_policy_registry.c \
    src/core/ext/client_channel/parse_address.c \
    src/core/ext/client_channel/resolver.c \
    src/core/ext/client_channel/resolver_factory.c \
    src/core/ext/client_channel/resolver_registry.c \
    src/core/ext/client_channel/subchannel.c \
    src/core/ext/client_channel/subchannel_index.c \
    src/core/ext/client_channel/uri_parser.c \
    src/core/ext/transport/chttp2/client/chttp2_connector.c \
    src/core/ext/transport/chttp2/server/insecure/server_chttp2.c \
    src/core/ext/transport/chttp2/server/insecure/server_chttp2_posix.c \
    src/core/ext/census/base_resources.c \
    src/core/ext/census/context.c \
    src/core/ext/census/gen/census.pb.c \
    src/core/ext/census/gen/trace_context.pb.c \
    src/core/ext/census/grpc_context.c \
    src/core/ext/census/grpc_filter.c \
    src/core/ext/census/grpc_plugin.c \
    src/core/ext/census/initialize.c \
    src/core/ext/census/mlog.c \
    src/core/ext/census/operation.c \
    src/core/ext/census/placeholders.c \
    src/core/ext/census/resource.c \
    src/core/ext/census/trace_context.c \
    src/core/ext/census/tracing.c \
    third_party/nanopb/pb_common.c \
    third_party/nanopb/pb_decode.c \
    third_party/nanopb/pb_encode.c \

PUBLIC_HEADERS_CXX += \
    include/grpc++/alarm.h \
    include/grpc++/channel.h \
    include/grpc++/client_context.h \
    include/grpc++/completion_queue.h \
    include/grpc++/create_channel.h \
    include/grpc++/create_channel_posix.h \
    include/grpc++/generic/async_generic_service.h \
    include/grpc++/generic/generic_stub.h \
    include/grpc++/grpc++.h \
    include/grpc++/impl/call.h \
    include/grpc++/impl/client_unary_call.h \
    include/grpc++/impl/codegen/core_codegen.h \
    include/grpc++/impl/grpc_library.h \
    include/grpc++/impl/method_handler_impl.h \
    include/grpc++/impl/rpc_method.h \
    include/grpc++/impl/rpc_service_method.h \
    include/grpc++/impl/serialization_traits.h \
    include/grpc++/impl/server_builder_option.h \
    include/grpc++/impl/server_builder_plugin.h \
    include/grpc++/impl/server_initializer.h \
    include/grpc++/impl/service_type.h \
    include/grpc++/resource_quota.h \
    include/grpc++/security/auth_context.h \
    include/grpc++/security/auth_metadata_processor.h \
    include/grpc++/security/credentials.h \
    include/grpc++/security/server_credentials.h \
    include/grpc++/server.h \
    include/grpc++/server_builder.h \
    include/grpc++/server_context.h \
    include/grpc++/server_posix.h \
    include/grpc++/support/async_stream.h \
    include/grpc++/support/async_unary_call.h \
    include/grpc++/support/byte_buffer.h \
    include/grpc++/support/channel_arguments.h \
    include/grpc++/support/config.h \
    include/grpc++/support/slice.h \
    include/grpc++/support/status.h \
    include/grpc++/support/status_code_enum.h \
    include/grpc++/support/string_ref.h \
    include/grpc++/support/stub_options.h \
    include/grpc++/support/sync_stream.h \
    include/grpc++/support/time.h \
    include/grpc++/impl/codegen/async_stream.h \
    include/grpc++/impl/codegen/async_unary_call.h \
    include/grpc++/impl/codegen/call.h \
    include/grpc++/impl/codegen/call_hook.h \
    include/grpc++/impl/codegen/channel_interface.h \
    include/grpc++/impl/codegen/client_context.h \
    include/grpc++/impl/codegen/client_unary_call.h \
    include/grpc++/impl/codegen/completion_queue.h \
    include/grpc++/impl/codegen/completion_queue_tag.h \
    include/grpc++/impl/codegen/config.h \
    include/grpc++/impl/codegen/core_codegen_interface.h \
    include/grpc++/impl/codegen/create_auth_context.h \
    include/grpc++/impl/codegen/grpc_library.h \
    include/grpc++/impl/codegen/method_handler_impl.h \
    include/grpc++/impl/codegen/rpc_method.h \
    include/grpc++/impl/codegen/rpc_service_method.h \
    include/grpc++/impl/codegen/security/auth_context.h \
    include/grpc++/impl/codegen/serialization_traits.h \
    include/grpc++/impl/codegen/server_context.h \
    include/grpc++/impl/codegen/server_interface.h \
    include/grpc++/impl/codegen/service_type.h \
    include/grpc++/impl/codegen/status.h \
    include/grpc++/impl/codegen/status_code_enum.h \
    include/grpc++/impl/codegen/status_helper.h \
    include/grpc++/impl/codegen/string_ref.h \
    include/grpc++/impl/codegen/stub_options.h \
    include/grpc++/impl/codegen/sync_stream.h \
    include/grpc++/impl/codegen/time.h \
    include/grpc/impl/codegen/byte_buffer_reader.h \
    include/grpc/impl/codegen/compression_types.h \
    include/grpc/impl/codegen/connectivity_state.h \
    include/grpc/impl/codegen/grpc_types.h \
    include/grpc/impl/codegen/propagation_bits.h \
    include/grpc/impl/codegen/status.h \
    include/grpc/impl/codegen/atm.h \
    include/grpc/impl/codegen/atm_gcc_atomic.h \
    include/grpc/impl/codegen/atm_gcc_sync.h \
    include/grpc/impl/codegen/atm_windows.h \
    include/grpc/impl/codegen/gpr_types.h \
    include/grpc/impl/codegen/port_platform.h \
    include/grpc/impl/codegen/slice.h \
    include/grpc/impl/codegen/sync.h \
    include/grpc/impl/codegen/sync_generic.h \
    include/grpc/impl/codegen/sync_posix.h \
    include/grpc/impl/codegen/sync_windows.h \
    include/grpc/byte_buffer.h \
    include/grpc/byte_buffer_reader.h \
    include/grpc/compression.h \
    include/grpc/grpc.h \
    include/grpc/grpc_posix.h \
    include/grpc/grpc_security_constants.h \
    include/grpc/slice.h \
    include/grpc/slice_buffer.h \
    include/grpc/status.h \
    include/grpc/census.h \

LIBGRPC++_CRONET_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC++_CRONET_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libgrpc++_cronet.a: openssl_dep_error

$(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP): openssl_dep_error

else

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libgrpc++_cronet.a: protobuf_dep_error

$(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP): protobuf_dep_error

else

$(LIBDIR)/$(CONFIG)/libgrpc++_cronet.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(PROTOBUF_DEP) $(LIBGRPC++_CRONET_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS)  $(OPENSSL_MERGE_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc++_cronet.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc++_cronet.a $(LIBGRPC++_CRONET_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS)  $(OPENSSL_MERGE_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc++_cronet.a
endif

