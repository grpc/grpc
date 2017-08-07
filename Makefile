# GRPC global makefile
# This currently builds C and C++ code.
# This file has been automatically generated from a template file.
# Please look at the templates directory instead.
# This file can be regenerated from the template by running
# tools/buildgen/generate_projects.sh

# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.



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
CXXFLAGS_opt = -fno-exceptions
CPPFLAGS_opt = -O2
DEFINES_opt = NDEBUG

VALID_CONFIG_asan-trace-cmp = 1
REQUIRE_CUSTOM_LIBRARIES_asan-trace-cmp = 1
CC_asan-trace-cmp = clang
CXX_asan-trace-cmp = clang++
LD_asan-trace-cmp = clang++
LDXX_asan-trace-cmp = clang++
CPPFLAGS_asan-trace-cmp = -O0 -fsanitize-coverage=edge -fsanitize-coverage=trace-cmp -fsanitize=address -fno-omit-frame-pointer -Wno-unused-command-line-argument -DGPR_NO_DIRECT_SYSCALLS
LDFLAGS_asan-trace-cmp = -fsanitize=address

VALID_CONFIG_dbg = 1
CC_dbg = $(DEFAULT_CC)
CXX_dbg = $(DEFAULT_CXX)
LD_dbg = $(DEFAULT_CC)
LDXX_dbg = $(DEFAULT_CXX)
CXXFLAGS_dbg = -fno-exceptions
CPPFLAGS_dbg = -O0
DEFINES_dbg = _DEBUG DEBUG

VALID_CONFIG_asan = 1
REQUIRE_CUSTOM_LIBRARIES_asan = 1
CC_asan = clang
CXX_asan = clang++
LD_asan = clang++
LDXX_asan = clang++
CPPFLAGS_asan = -O0 -fsanitize-coverage=edge -fsanitize=address -fno-omit-frame-pointer -Wno-unused-command-line-argument -DGPR_NO_DIRECT_SYSCALLS
LDFLAGS_asan = -fsanitize=address

VALID_CONFIG_msan = 1
REQUIRE_CUSTOM_LIBRARIES_msan = 1
CC_msan = clang
CXX_msan = clang++
LD_msan = clang++
LDXX_msan = clang++
CPPFLAGS_msan = -O0 -fsanitize-coverage=edge -fsanitize=memory -fsanitize-memory-track-origins -fno-omit-frame-pointer -DGTEST_HAS_TR1_TUPLE=0 -DGTEST_USE_OWN_TR1_TUPLE=1 -Wno-unused-command-line-argument -fPIE -pie -DGPR_NO_DIRECT_SYSCALLS
LDFLAGS_msan = -fsanitize=memory -DGTEST_HAS_TR1_TUPLE=0 -DGTEST_USE_OWN_TR1_TUPLE=1 -fPIE -pie $(if $(JENKINS_BUILD),-Wl$(comma)-Ttext-segment=0x7e0000000000,)
DEFINES_msan = NDEBUG

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

VALID_CONFIG_asan-noleaks = 1
REQUIRE_CUSTOM_LIBRARIES_asan-noleaks = 1
CC_asan-noleaks = clang
CXX_asan-noleaks = clang++
LD_asan-noleaks = clang++
LDXX_asan-noleaks = clang++
CPPFLAGS_asan-noleaks = -O0 -fsanitize-coverage=edge -fsanitize=address -fno-omit-frame-pointer -Wno-unused-command-line-argument -DGPR_NO_DIRECT_SYSCALLS
LDFLAGS_asan-noleaks = -fsanitize=address

VALID_CONFIG_c++-compat = 1
CC_c++-compat = $(DEFAULT_CC)
CXX_c++-compat = $(DEFAULT_CXX)
LD_c++-compat = $(DEFAULT_CC)
LDXX_c++-compat = $(DEFAULT_CXX)
CFLAGS_c++-compat = -Wc++-compat
CPPFLAGS_c++-compat = -O0
DEFINES_c++-compat = _DEBUG DEBUG

VALID_CONFIG_ubsan = 1
REQUIRE_CUSTOM_LIBRARIES_ubsan = 1
CC_ubsan = clang
CXX_ubsan = clang++
LD_ubsan = clang++
LDXX_ubsan = clang++
CPPFLAGS_ubsan = -O0 -fsanitize-coverage=edge -fsanitize=undefined -fno-omit-frame-pointer -Wno-unused-command-line-argument -Wvarargs
LDFLAGS_ubsan = -fsanitize=undefined,unsigned-integer-overflow
DEFINES_ubsan = NDEBUG GRPC_UBSAN

VALID_CONFIG_tsan = 1
REQUIRE_CUSTOM_LIBRARIES_tsan = 1
CC_tsan = clang
CXX_tsan = clang++
LD_tsan = clang++
LDXX_tsan = clang++
CPPFLAGS_tsan = -O0 -fsanitize=thread -fno-omit-frame-pointer -Wno-unused-command-line-argument -DGPR_NO_DIRECT_SYSCALLS
LDFLAGS_tsan = -fsanitize=thread
DEFINES_tsan = GRPC_TSAN

VALID_CONFIG_stapprof = 1
CC_stapprof = $(DEFAULT_CC)
CXX_stapprof = $(DEFAULT_CXX)
LD_stapprof = $(DEFAULT_CC)
LDXX_stapprof = $(DEFAULT_CXX)
CPPFLAGS_stapprof = -O2 -DGRPC_STAP_PROFILER
DEFINES_stapprof = NDEBUG

VALID_CONFIG_gcov = 1
CC_gcov = gcc
CXX_gcov = g++
LD_gcov = gcc
LDXX_gcov = g++
CPPFLAGS_gcov = -O0 -fprofile-arcs -ftest-coverage -Wno-return-type
LDFLAGS_gcov = -fprofile-arcs -ftest-coverage -rdynamic
DEFINES_gcov = _DEBUG DEBUG GPR_GCOV

VALID_CONFIG_memcheck = 1
CC_memcheck = $(DEFAULT_CC)
CXX_memcheck = $(DEFAULT_CXX)
LD_memcheck = $(DEFAULT_CC)
LDXX_memcheck = $(DEFAULT_CXX)
CPPFLAGS_memcheck = -O0
LDFLAGS_memcheck = -rdynamic
DEFINES_memcheck = _DEBUG DEBUG

VALID_CONFIG_lto = 1
CC_lto = $(DEFAULT_CC)
CXX_lto = $(DEFAULT_CXX)
LD_lto = $(DEFAULT_CC)
LDXX_lto = $(DEFAULT_CXX)
CPPFLAGS_lto = -O2
DEFINES_lto = NDEBUG

VALID_CONFIG_mutrace = 1
CC_mutrace = $(DEFAULT_CC)
CXX_mutrace = $(DEFAULT_CXX)
LD_mutrace = $(DEFAULT_CC)
LDXX_mutrace = $(DEFAULT_CXX)
CPPFLAGS_mutrace = -O3 -fno-omit-frame-pointer
LDFLAGS_mutrace = -rdynamic
DEFINES_mutrace = NDEBUG

VALID_CONFIG_counters = 1
CC_counters = $(DEFAULT_CC)
CXX_counters = $(DEFAULT_CXX)
LD_counters = $(DEFAULT_CC)
LDXX_counters = $(DEFAULT_CXX)
CPPFLAGS_counters = -O2 -DGPR_LOW_LEVEL_COUNTERS
DEFINES_counters = NDEBUG



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
ifeq ($(SYSTEM),MINGW32)
ifeq ($(origin AR), default)
AR = ar rcs
endif
STRIP ?= strip --strip-unneeded
else
ifeq ($(origin AR), default)
AR = ar rcs
endif
STRIP ?= strip
endif
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
CXXFLAGS += -std=c++11
ifeq ($(SYSTEM),Darwin)
CXXFLAGS += -stdlib=libc++
endif
CPPFLAGS += -g -Wall -Wextra -Werror -Wno-long-long -Wno-unused-parameter -DOSATOMIC_USE_INLINED=1
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

GTEST_LIB = -Ithird_party/googletest/googletest/include -Ithird_party/googletest/googletest third_party/googletest/googletest/src/gtest-all.cc -Ithird_party/googletest/googlemock/include -Ithird_party/googletest/googlemock third_party/googletest/googlemock/src/gmock-all.cc
GTEST_LIB += -lgflags
ifeq ($(V),1)
E = @:
Q =
else
E = @echo
Q = @
endif

CORE_VERSION = 4.0.0-dev
CPP_VERSION = 1.5.0-dev
CSHARP_VERSION = 1.5.0-dev

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
EXECUTABLE_SUFFIX = .exe
SHARED_EXT_CORE = dll
SHARED_EXT_CPP = dll
SHARED_EXT_CSHARP = dll
SHARED_PREFIX =
SHARED_VERSION_CORE = -4
SHARED_VERSION_CPP = -1
SHARED_VERSION_CSHARP = -1
else ifeq ($(SYSTEM),Darwin)
EXECUTABLE_SUFFIX =
SHARED_EXT_CORE = dylib
SHARED_EXT_CPP = dylib
SHARED_EXT_CSHARP = dylib
SHARED_PREFIX = lib
SHARED_VERSION_CORE =
SHARED_VERSION_CPP =
SHARED_VERSION_CSHARP =
else
EXECUTABLE_SUFFIX =
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
CARES_CHECK_CMD = $(PKG_CONFIG) --atleast-version=1.11.0 libcares
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
CARES_CHECK_CMD = $(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $(TMPOUT) test/build/c-ares.c -lcares $(LDFLAGS)

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
HAS_SYSTEM_CARES ?=  $(shell $(CARES_CHECK_CMD) 2> /dev/null && echo true || echo false)
ifeq ($(HAS_SYSTEM_CARES),true)
CACHE_MK += HAS_SYSTEM_CARES = true,
endif
else
# override system libraries if the config requires a custom compiled library
HAS_SYSTEM_OPENSSL_ALPN = false
HAS_SYSTEM_OPENSSL_NPN = false
HAS_SYSTEM_ZLIB = false
HAS_SYSTEM_PROTOBUF = false
HAS_SYSTEM_CARES = false
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

ifeq ($(wildcard third_party/cares/cares/ares.h),)
HAS_EMBEDDED_CARES = false
else
HAS_EMBEDDED_CARES = true
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

CARES_PKG_CONFIG = false

ifeq ($(HAS_SYSTEM_CARES),false)
ifeq ($(HAS_EMBEDDED_CARES), true)
EMBED_CARES ?= true
else
DEP_MISSING += cares
EMBED_CARES ?= broken
endif
else
EMBED_CARES ?= false
endif

ifeq ($(EMBED_CARES),true)
CARES_DEP = $(LIBDIR)/$(CONFIG)/libares.a
CARES_MERGE_OBJS = $(LIBARES_OBJS)
CARES_MERGE_LIBS = $(LIBDIR)/$(CONFIG)/libares.a
CPPFLAGS := -Ithird_party/cares -Ithird_party/cares/cares $(CPPFLAGS)
LDFLAGS := -L$(LIBDIR)/$(CONFIG)/c-ares $(LDFLAGS)
else
ifeq ($(HAS_PKG_CONFIG),true)
PC_REQUIRES_GRPC += libcares
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags libcares)
LDFLAGS += $(shell $(PKG_CONFIG) --libs-only-L libcares)
LIBS += $(patsubst -l%,%,$(shell $(PKG_CONFIG) --libs-only-l libcares))
else
PC_LIBS_GRPC += -lcares
LIBS += cares
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

CPPFLAGS := -Ithird_party/googletest/googletest/include -Ithird_party/googletest/googlemock/include $(CPPFLAGS)

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
arena_test: $(BINDIR)/$(CONFIG)/arena_test
bad_server_response_test: $(BINDIR)/$(CONFIG)/bad_server_response_test
bdp_estimator_test: $(BINDIR)/$(CONFIG)/bdp_estimator_test
bin_decoder_test: $(BINDIR)/$(CONFIG)/bin_decoder_test
bin_encoder_test: $(BINDIR)/$(CONFIG)/bin_encoder_test
byte_stream_test: $(BINDIR)/$(CONFIG)/byte_stream_test
census_context_test: $(BINDIR)/$(CONFIG)/census_context_test
census_intrusive_hash_map_test: $(BINDIR)/$(CONFIG)/census_intrusive_hash_map_test
census_resource_test: $(BINDIR)/$(CONFIG)/census_resource_test
census_trace_context_test: $(BINDIR)/$(CONFIG)/census_trace_context_test
channel_create_test: $(BINDIR)/$(CONFIG)/channel_create_test
check_epollexclusive: $(BINDIR)/$(CONFIG)/check_epollexclusive
chttp2_hpack_encoder_test: $(BINDIR)/$(CONFIG)/chttp2_hpack_encoder_test
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
error_test: $(BINDIR)/$(CONFIG)/error_test
ev_epollsig_linux_test: $(BINDIR)/$(CONFIG)/ev_epollsig_linux_test
fake_resolver_test: $(BINDIR)/$(CONFIG)/fake_resolver_test
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
gpr_spinlock_test: $(BINDIR)/$(CONFIG)/gpr_spinlock_test
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
grpc_completion_queue_threading_test: $(BINDIR)/$(CONFIG)/grpc_completion_queue_threading_test
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
memory_profile_client: $(BINDIR)/$(CONFIG)/memory_profile_client
memory_profile_server: $(BINDIR)/$(CONFIG)/memory_profile_server
memory_profile_test: $(BINDIR)/$(CONFIG)/memory_profile_test
message_compress_test: $(BINDIR)/$(CONFIG)/message_compress_test
minimal_stack_is_minimal_test: $(BINDIR)/$(CONFIG)/minimal_stack_is_minimal_test
mlog_test: $(BINDIR)/$(CONFIG)/mlog_test
multiple_server_queues_test: $(BINDIR)/$(CONFIG)/multiple_server_queues_test
murmur_hash_test: $(BINDIR)/$(CONFIG)/murmur_hash_test
nanopb_fuzzer_response_test: $(BINDIR)/$(CONFIG)/nanopb_fuzzer_response_test
nanopb_fuzzer_serverlist_test: $(BINDIR)/$(CONFIG)/nanopb_fuzzer_serverlist_test
no_server_test: $(BINDIR)/$(CONFIG)/no_server_test
num_external_connectivity_watchers_test: $(BINDIR)/$(CONFIG)/num_external_connectivity_watchers_test
parse_address_test: $(BINDIR)/$(CONFIG)/parse_address_test
percent_decode_fuzzer: $(BINDIR)/$(CONFIG)/percent_decode_fuzzer
percent_encode_fuzzer: $(BINDIR)/$(CONFIG)/percent_encode_fuzzer
percent_encoding_test: $(BINDIR)/$(CONFIG)/percent_encoding_test
pollset_set_test: $(BINDIR)/$(CONFIG)/pollset_set_test
resolve_address_posix_test: $(BINDIR)/$(CONFIG)/resolve_address_posix_test
resolve_address_test: $(BINDIR)/$(CONFIG)/resolve_address_test
resource_quota_test: $(BINDIR)/$(CONFIG)/resource_quota_test
secure_channel_create_test: $(BINDIR)/$(CONFIG)/secure_channel_create_test
secure_endpoint_test: $(BINDIR)/$(CONFIG)/secure_endpoint_test
sequential_connectivity_test: $(BINDIR)/$(CONFIG)/sequential_connectivity_test
server_chttp2_test: $(BINDIR)/$(CONFIG)/server_chttp2_test
server_fuzzer: $(BINDIR)/$(CONFIG)/server_fuzzer
server_test: $(BINDIR)/$(CONFIG)/server_test
slice_buffer_test: $(BINDIR)/$(CONFIG)/slice_buffer_test
slice_hash_table_test: $(BINDIR)/$(CONFIG)/slice_hash_table_test
slice_string_helpers_test: $(BINDIR)/$(CONFIG)/slice_string_helpers_test
slice_test: $(BINDIR)/$(CONFIG)/slice_test
sockaddr_resolver_test: $(BINDIR)/$(CONFIG)/sockaddr_resolver_test
sockaddr_utils_test: $(BINDIR)/$(CONFIG)/sockaddr_utils_test
socket_utils_test: $(BINDIR)/$(CONFIG)/socket_utils_test
ssl_server_fuzzer: $(BINDIR)/$(CONFIG)/ssl_server_fuzzer
status_conversion_test: $(BINDIR)/$(CONFIG)/status_conversion_test
stream_compression_test: $(BINDIR)/$(CONFIG)/stream_compression_test
stream_owned_slice_test: $(BINDIR)/$(CONFIG)/stream_owned_slice_test
tcp_client_posix_test: $(BINDIR)/$(CONFIG)/tcp_client_posix_test
tcp_client_uv_test: $(BINDIR)/$(CONFIG)/tcp_client_uv_test
tcp_posix_test: $(BINDIR)/$(CONFIG)/tcp_posix_test
tcp_server_posix_test: $(BINDIR)/$(CONFIG)/tcp_server_posix_test
tcp_server_uv_test: $(BINDIR)/$(CONFIG)/tcp_server_uv_test
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
bm_arena: $(BINDIR)/$(CONFIG)/bm_arena
bm_call_create: $(BINDIR)/$(CONFIG)/bm_call_create
bm_chttp2_hpack: $(BINDIR)/$(CONFIG)/bm_chttp2_hpack
bm_chttp2_transport: $(BINDIR)/$(CONFIG)/bm_chttp2_transport
bm_closure: $(BINDIR)/$(CONFIG)/bm_closure
bm_cq: $(BINDIR)/$(CONFIG)/bm_cq
bm_cq_multiple_threads: $(BINDIR)/$(CONFIG)/bm_cq_multiple_threads
bm_error: $(BINDIR)/$(CONFIG)/bm_error
bm_fullstack_streaming_ping_pong: $(BINDIR)/$(CONFIG)/bm_fullstack_streaming_ping_pong
bm_fullstack_streaming_pump: $(BINDIR)/$(CONFIG)/bm_fullstack_streaming_pump
bm_fullstack_trickle: $(BINDIR)/$(CONFIG)/bm_fullstack_trickle
bm_fullstack_unary_ping_pong: $(BINDIR)/$(CONFIG)/bm_fullstack_unary_ping_pong
bm_metadata: $(BINDIR)/$(CONFIG)/bm_metadata
bm_pollset: $(BINDIR)/$(CONFIG)/bm_pollset
channel_arguments_test: $(BINDIR)/$(CONFIG)/channel_arguments_test
channel_filter_test: $(BINDIR)/$(CONFIG)/channel_filter_test
cli_call_test: $(BINDIR)/$(CONFIG)/cli_call_test
client_crash_test: $(BINDIR)/$(CONFIG)/client_crash_test
client_crash_test_server: $(BINDIR)/$(CONFIG)/client_crash_test_server
client_lb_end2end_test: $(BINDIR)/$(CONFIG)/client_lb_end2end_test
codegen_test_full: $(BINDIR)/$(CONFIG)/codegen_test_full
codegen_test_minimal: $(BINDIR)/$(CONFIG)/codegen_test_minimal
credentials_test: $(BINDIR)/$(CONFIG)/credentials_test
cxx_byte_buffer_test: $(BINDIR)/$(CONFIG)/cxx_byte_buffer_test
cxx_slice_test: $(BINDIR)/$(CONFIG)/cxx_slice_test
cxx_string_ref_test: $(BINDIR)/$(CONFIG)/cxx_string_ref_test
cxx_time_test: $(BINDIR)/$(CONFIG)/cxx_time_test
end2end_test: $(BINDIR)/$(CONFIG)/end2end_test
error_details_test: $(BINDIR)/$(CONFIG)/error_details_test
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
grpclb_end2end_test: $(BINDIR)/$(CONFIG)/grpclb_end2end_test
grpclb_test: $(BINDIR)/$(CONFIG)/grpclb_test
health_service_end2end_test: $(BINDIR)/$(CONFIG)/health_service_end2end_test
http2_client: $(BINDIR)/$(CONFIG)/http2_client
hybrid_end2end_test: $(BINDIR)/$(CONFIG)/hybrid_end2end_test
interop_client: $(BINDIR)/$(CONFIG)/interop_client
interop_server: $(BINDIR)/$(CONFIG)/interop_server
interop_test: $(BINDIR)/$(CONFIG)/interop_test
json_run_localhost: $(BINDIR)/$(CONFIG)/json_run_localhost
memory_test: $(BINDIR)/$(CONFIG)/memory_test
metrics_client: $(BINDIR)/$(CONFIG)/metrics_client
mock_test: $(BINDIR)/$(CONFIG)/mock_test
noop-benchmark: $(BINDIR)/$(CONFIG)/noop-benchmark
proto_server_reflection_test: $(BINDIR)/$(CONFIG)/proto_server_reflection_test
proto_utils_test: $(BINDIR)/$(CONFIG)/proto_utils_test
qps_interarrival_test: $(BINDIR)/$(CONFIG)/qps_interarrival_test
qps_json_driver: $(BINDIR)/$(CONFIG)/qps_json_driver
qps_openloop_test: $(BINDIR)/$(CONFIG)/qps_openloop_test
qps_worker: $(BINDIR)/$(CONFIG)/qps_worker
reconnect_interop_client: $(BINDIR)/$(CONFIG)/reconnect_interop_client
reconnect_interop_server: $(BINDIR)/$(CONFIG)/reconnect_interop_server
secure_auth_context_test: $(BINDIR)/$(CONFIG)/secure_auth_context_test
secure_sync_unary_ping_pong_test: $(BINDIR)/$(CONFIG)/secure_sync_unary_ping_pong_test
server_builder_plugin_test: $(BINDIR)/$(CONFIG)/server_builder_plugin_test
server_builder_test: $(BINDIR)/$(CONFIG)/server_builder_test
server_context_test_spouse_test: $(BINDIR)/$(CONFIG)/server_context_test_spouse_test
server_crash_test: $(BINDIR)/$(CONFIG)/server_crash_test
server_crash_test_client: $(BINDIR)/$(CONFIG)/server_crash_test_client
server_request_call_test: $(BINDIR)/$(CONFIG)/server_request_call_test
shutdown_test: $(BINDIR)/$(CONFIG)/shutdown_test
status_test: $(BINDIR)/$(CONFIG)/status_test
streaming_throughput_test: $(BINDIR)/$(CONFIG)/streaming_throughput_test
stress_test: $(BINDIR)/$(CONFIG)/stress_test
thread_manager_test: $(BINDIR)/$(CONFIG)/thread_manager_test
thread_stress_test: $(BINDIR)/$(CONFIG)/thread_stress_test
writes_per_rpc_test: $(BINDIR)/$(CONFIG)/writes_per_rpc_test
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
boringssl_spake25519_test: $(BINDIR)/$(CONFIG)/boringssl_spake25519_test
boringssl_x25519_test: $(BINDIR)/$(CONFIG)/boringssl_x25519_test
boringssl_digest_test: $(BINDIR)/$(CONFIG)/boringssl_digest_test
boringssl_example_mul: $(BINDIR)/$(CONFIG)/boringssl_example_mul
boringssl_p256-x86_64_test: $(BINDIR)/$(CONFIG)/boringssl_p256-x86_64_test
boringssl_ecdh_test: $(BINDIR)/$(CONFIG)/boringssl_ecdh_test
boringssl_ecdsa_sign_test: $(BINDIR)/$(CONFIG)/boringssl_ecdsa_sign_test
boringssl_ecdsa_test: $(BINDIR)/$(CONFIG)/boringssl_ecdsa_test
boringssl_ecdsa_verify_test: $(BINDIR)/$(CONFIG)/boringssl_ecdsa_verify_test
boringssl_evp_extra_test: $(BINDIR)/$(CONFIG)/boringssl_evp_extra_test
boringssl_evp_test: $(BINDIR)/$(CONFIG)/boringssl_evp_test
boringssl_pbkdf_test: $(BINDIR)/$(CONFIG)/boringssl_pbkdf_test
boringssl_hkdf_test: $(BINDIR)/$(CONFIG)/boringssl_hkdf_test
boringssl_hmac_test: $(BINDIR)/$(CONFIG)/boringssl_hmac_test
boringssl_lhash_test: $(BINDIR)/$(CONFIG)/boringssl_lhash_test
boringssl_gcm_test: $(BINDIR)/$(CONFIG)/boringssl_gcm_test
boringssl_obj_test: $(BINDIR)/$(CONFIG)/boringssl_obj_test
boringssl_pkcs12_test: $(BINDIR)/$(CONFIG)/boringssl_pkcs12_test
boringssl_pkcs8_test: $(BINDIR)/$(CONFIG)/boringssl_pkcs8_test
boringssl_poly1305_test: $(BINDIR)/$(CONFIG)/boringssl_poly1305_test
boringssl_pool_test: $(BINDIR)/$(CONFIG)/boringssl_pool_test
boringssl_refcount_test: $(BINDIR)/$(CONFIG)/boringssl_refcount_test
boringssl_thread_test: $(BINDIR)/$(CONFIG)/boringssl_thread_test
boringssl_pkcs7_test: $(BINDIR)/$(CONFIG)/boringssl_pkcs7_test
boringssl_x509_test: $(BINDIR)/$(CONFIG)/boringssl_x509_test
boringssl_tab_test: $(BINDIR)/$(CONFIG)/boringssl_tab_test
boringssl_v3name_test: $(BINDIR)/$(CONFIG)/boringssl_v3name_test
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
h2_full+workarounds_test: $(BINDIR)/$(CONFIG)/h2_full+workarounds_test
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
inproc_test: $(BINDIR)/$(CONFIG)/inproc_test
h2_census_nosec_test: $(BINDIR)/$(CONFIG)/h2_census_nosec_test
h2_compress_nosec_test: $(BINDIR)/$(CONFIG)/h2_compress_nosec_test
h2_fd_nosec_test: $(BINDIR)/$(CONFIG)/h2_fd_nosec_test
h2_full_nosec_test: $(BINDIR)/$(CONFIG)/h2_full_nosec_test
h2_full+pipe_nosec_test: $(BINDIR)/$(CONFIG)/h2_full+pipe_nosec_test
h2_full+trace_nosec_test: $(BINDIR)/$(CONFIG)/h2_full+trace_nosec_test
h2_full+workarounds_nosec_test: $(BINDIR)/$(CONFIG)/h2_full+workarounds_nosec_test
h2_http_proxy_nosec_test: $(BINDIR)/$(CONFIG)/h2_http_proxy_nosec_test
h2_load_reporting_nosec_test: $(BINDIR)/$(CONFIG)/h2_load_reporting_nosec_test
h2_proxy_nosec_test: $(BINDIR)/$(CONFIG)/h2_proxy_nosec_test
h2_sockpair_nosec_test: $(BINDIR)/$(CONFIG)/h2_sockpair_nosec_test
h2_sockpair+trace_nosec_test: $(BINDIR)/$(CONFIG)/h2_sockpair+trace_nosec_test
h2_sockpair_1byte_nosec_test: $(BINDIR)/$(CONFIG)/h2_sockpair_1byte_nosec_test
h2_uds_nosec_test: $(BINDIR)/$(CONFIG)/h2_uds_nosec_test
inproc_nosec_test: $(BINDIR)/$(CONFIG)/inproc_nosec_test
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
	$(CARES_CHECK_CMD) || true

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

static_cxx: pc_cxx pc_cxx_unsecure cache.mk  $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc++_cronet.a $(LIBDIR)/$(CONFIG)/libgrpc++_error_details.a $(LIBDIR)/$(CONFIG)/libgrpc++_reflection.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a

shared: shared_c shared_cxx

shared_c: pc_c pc_c_unsecure cache.mk $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)
shared_cxx: pc_cxx pc_cxx_unsecure cache.mk $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_error_details$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_unsecure$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)

shared_csharp: shared_c  $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc_csharp_ext$(SHARED_VERSION_CSHARP).$(SHARED_EXT_CSHARP)
grpc_csharp_ext: shared_csharp

plugins: $(PROTOC_PLUGINS)

privatelibs: privatelibs_c privatelibs_cxx

privatelibs_c:  $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libreconnect_server.a $(LIBDIR)/$(CONFIG)/libtest_tcp_server.a $(LIBDIR)/$(CONFIG)/libz.a $(LIBDIR)/$(CONFIG)/libares.a $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libbad_ssl_test_server.a $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a
pc_c: $(LIBDIR)/$(CONFIG)/pkgconfig/grpc.pc

pc_c_unsecure: $(LIBDIR)/$(CONFIG)/pkgconfig/grpc_unsecure.pc

pc_cxx: $(LIBDIR)/$(CONFIG)/pkgconfig/grpc++.pc

pc_cxx_unsecure: $(LIBDIR)/$(CONFIG)/pkgconfig/grpc++_unsecure.pc

ifeq ($(EMBED_OPENSSL),true)
privatelibs_cxx:  $(LIBDIR)/$(CONFIG)/libgrpc++_proto_reflection_desc_db.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_cli_libs.a $(LIBDIR)/$(CONFIG)/libhttp2_client_main.a $(LIBDIR)/$(CONFIG)/libinterop_client_helper.a $(LIBDIR)/$(CONFIG)/libinterop_client_main.a $(LIBDIR)/$(CONFIG)/libinterop_server_helper.a $(LIBDIR)/$(CONFIG)/libinterop_server_lib.a $(LIBDIR)/$(CONFIG)/libinterop_server_main.a $(LIBDIR)/$(CONFIG)/libqps.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl_aes_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_asn1_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_base64_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_bio_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_bn_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_bytestring_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_aead_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_cipher_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_cmac_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_constant_time_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_ed25519_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_spake25519_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_x25519_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_digest_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_p256-x86_64_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_ecdh_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_sign_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_verify_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_evp_extra_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_evp_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_pbkdf_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_hkdf_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_hmac_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_lhash_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_gcm_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_obj_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_pkcs12_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_pkcs8_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_poly1305_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_pool_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_refcount_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_x509_test_lib.a $(LIBDIR)/$(CONFIG)/libbenchmark.a
else
privatelibs_cxx:  $(LIBDIR)/$(CONFIG)/libgrpc++_proto_reflection_desc_db.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_cli_libs.a $(LIBDIR)/$(CONFIG)/libhttp2_client_main.a $(LIBDIR)/$(CONFIG)/libinterop_client_helper.a $(LIBDIR)/$(CONFIG)/libinterop_client_main.a $(LIBDIR)/$(CONFIG)/libinterop_server_helper.a $(LIBDIR)/$(CONFIG)/libinterop_server_lib.a $(LIBDIR)/$(CONFIG)/libinterop_server_main.a $(LIBDIR)/$(CONFIG)/libqps.a $(LIBDIR)/$(CONFIG)/libbenchmark.a
endif


buildtests: buildtests_c buildtests_cxx

buildtests_c: privatelibs_c \
  $(BINDIR)/$(CONFIG)/alarm_test \
  $(BINDIR)/$(CONFIG)/algorithm_test \
  $(BINDIR)/$(CONFIG)/alloc_test \
  $(BINDIR)/$(CONFIG)/alpn_test \
  $(BINDIR)/$(CONFIG)/arena_test \
  $(BINDIR)/$(CONFIG)/bad_server_response_test \
  $(BINDIR)/$(CONFIG)/bdp_estimator_test \
  $(BINDIR)/$(CONFIG)/bin_decoder_test \
  $(BINDIR)/$(CONFIG)/bin_encoder_test \
  $(BINDIR)/$(CONFIG)/byte_stream_test \
  $(BINDIR)/$(CONFIG)/census_context_test \
  $(BINDIR)/$(CONFIG)/census_intrusive_hash_map_test \
  $(BINDIR)/$(CONFIG)/census_resource_test \
  $(BINDIR)/$(CONFIG)/census_trace_context_test \
  $(BINDIR)/$(CONFIG)/channel_create_test \
  $(BINDIR)/$(CONFIG)/chttp2_hpack_encoder_test \
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
  $(BINDIR)/$(CONFIG)/error_test \
  $(BINDIR)/$(CONFIG)/ev_epollsig_linux_test \
  $(BINDIR)/$(CONFIG)/fake_resolver_test \
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
  $(BINDIR)/$(CONFIG)/gpr_spinlock_test \
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
  $(BINDIR)/$(CONFIG)/grpc_completion_queue_threading_test \
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
  $(BINDIR)/$(CONFIG)/invalid_call_argument_test \
  $(BINDIR)/$(CONFIG)/json_rewrite \
  $(BINDIR)/$(CONFIG)/json_rewrite_test \
  $(BINDIR)/$(CONFIG)/json_stream_error_test \
  $(BINDIR)/$(CONFIG)/json_test \
  $(BINDIR)/$(CONFIG)/lame_client_test \
  $(BINDIR)/$(CONFIG)/lb_policies_test \
  $(BINDIR)/$(CONFIG)/load_file_test \
  $(BINDIR)/$(CONFIG)/memory_profile_client \
  $(BINDIR)/$(CONFIG)/memory_profile_server \
  $(BINDIR)/$(CONFIG)/memory_profile_test \
  $(BINDIR)/$(CONFIG)/message_compress_test \
  $(BINDIR)/$(CONFIG)/minimal_stack_is_minimal_test \
  $(BINDIR)/$(CONFIG)/mlog_test \
  $(BINDIR)/$(CONFIG)/multiple_server_queues_test \
  $(BINDIR)/$(CONFIG)/murmur_hash_test \
  $(BINDIR)/$(CONFIG)/no_server_test \
  $(BINDIR)/$(CONFIG)/num_external_connectivity_watchers_test \
  $(BINDIR)/$(CONFIG)/parse_address_test \
  $(BINDIR)/$(CONFIG)/percent_encoding_test \
  $(BINDIR)/$(CONFIG)/pollset_set_test \
  $(BINDIR)/$(CONFIG)/resolve_address_posix_test \
  $(BINDIR)/$(CONFIG)/resolve_address_test \
  $(BINDIR)/$(CONFIG)/resource_quota_test \
  $(BINDIR)/$(CONFIG)/secure_channel_create_test \
  $(BINDIR)/$(CONFIG)/secure_endpoint_test \
  $(BINDIR)/$(CONFIG)/sequential_connectivity_test \
  $(BINDIR)/$(CONFIG)/server_chttp2_test \
  $(BINDIR)/$(CONFIG)/server_test \
  $(BINDIR)/$(CONFIG)/slice_buffer_test \
  $(BINDIR)/$(CONFIG)/slice_hash_table_test \
  $(BINDIR)/$(CONFIG)/slice_string_helpers_test \
  $(BINDIR)/$(CONFIG)/slice_test \
  $(BINDIR)/$(CONFIG)/sockaddr_resolver_test \
  $(BINDIR)/$(CONFIG)/sockaddr_utils_test \
  $(BINDIR)/$(CONFIG)/socket_utils_test \
  $(BINDIR)/$(CONFIG)/status_conversion_test \
  $(BINDIR)/$(CONFIG)/stream_compression_test \
  $(BINDIR)/$(CONFIG)/stream_owned_slice_test \
  $(BINDIR)/$(CONFIG)/tcp_client_posix_test \
  $(BINDIR)/$(CONFIG)/tcp_client_uv_test \
  $(BINDIR)/$(CONFIG)/tcp_posix_test \
  $(BINDIR)/$(CONFIG)/tcp_server_posix_test \
  $(BINDIR)/$(CONFIG)/tcp_server_uv_test \
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
  $(BINDIR)/$(CONFIG)/h2_full+workarounds_test \
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
  $(BINDIR)/$(CONFIG)/inproc_test \
  $(BINDIR)/$(CONFIG)/h2_census_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_compress_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_fd_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_full_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_full+pipe_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_full+trace_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_full+workarounds_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_http_proxy_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_load_reporting_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_proxy_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_sockpair_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_sockpair+trace_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_sockpair_1byte_nosec_test \
  $(BINDIR)/$(CONFIG)/h2_uds_nosec_test \
  $(BINDIR)/$(CONFIG)/inproc_nosec_test \
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
  $(BINDIR)/$(CONFIG)/bm_arena \
  $(BINDIR)/$(CONFIG)/bm_call_create \
  $(BINDIR)/$(CONFIG)/bm_chttp2_hpack \
  $(BINDIR)/$(CONFIG)/bm_chttp2_transport \
  $(BINDIR)/$(CONFIG)/bm_closure \
  $(BINDIR)/$(CONFIG)/bm_cq \
  $(BINDIR)/$(CONFIG)/bm_cq_multiple_threads \
  $(BINDIR)/$(CONFIG)/bm_error \
  $(BINDIR)/$(CONFIG)/bm_fullstack_streaming_ping_pong \
  $(BINDIR)/$(CONFIG)/bm_fullstack_streaming_pump \
  $(BINDIR)/$(CONFIG)/bm_fullstack_trickle \
  $(BINDIR)/$(CONFIG)/bm_fullstack_unary_ping_pong \
  $(BINDIR)/$(CONFIG)/bm_metadata \
  $(BINDIR)/$(CONFIG)/bm_pollset \
  $(BINDIR)/$(CONFIG)/channel_arguments_test \
  $(BINDIR)/$(CONFIG)/channel_filter_test \
  $(BINDIR)/$(CONFIG)/cli_call_test \
  $(BINDIR)/$(CONFIG)/client_crash_test \
  $(BINDIR)/$(CONFIG)/client_crash_test_server \
  $(BINDIR)/$(CONFIG)/client_lb_end2end_test \
  $(BINDIR)/$(CONFIG)/codegen_test_full \
  $(BINDIR)/$(CONFIG)/codegen_test_minimal \
  $(BINDIR)/$(CONFIG)/credentials_test \
  $(BINDIR)/$(CONFIG)/cxx_byte_buffer_test \
  $(BINDIR)/$(CONFIG)/cxx_slice_test \
  $(BINDIR)/$(CONFIG)/cxx_string_ref_test \
  $(BINDIR)/$(CONFIG)/cxx_time_test \
  $(BINDIR)/$(CONFIG)/end2end_test \
  $(BINDIR)/$(CONFIG)/error_details_test \
  $(BINDIR)/$(CONFIG)/filter_end2end_test \
  $(BINDIR)/$(CONFIG)/generic_end2end_test \
  $(BINDIR)/$(CONFIG)/golden_file_test \
  $(BINDIR)/$(CONFIG)/grpc_cli \
  $(BINDIR)/$(CONFIG)/grpc_tool_test \
  $(BINDIR)/$(CONFIG)/grpclb_api_test \
  $(BINDIR)/$(CONFIG)/grpclb_end2end_test \
  $(BINDIR)/$(CONFIG)/grpclb_test \
  $(BINDIR)/$(CONFIG)/health_service_end2end_test \
  $(BINDIR)/$(CONFIG)/http2_client \
  $(BINDIR)/$(CONFIG)/hybrid_end2end_test \
  $(BINDIR)/$(CONFIG)/interop_client \
  $(BINDIR)/$(CONFIG)/interop_server \
  $(BINDIR)/$(CONFIG)/interop_test \
  $(BINDIR)/$(CONFIG)/json_run_localhost \
  $(BINDIR)/$(CONFIG)/memory_test \
  $(BINDIR)/$(CONFIG)/metrics_client \
  $(BINDIR)/$(CONFIG)/mock_test \
  $(BINDIR)/$(CONFIG)/noop-benchmark \
  $(BINDIR)/$(CONFIG)/proto_server_reflection_test \
  $(BINDIR)/$(CONFIG)/proto_utils_test \
  $(BINDIR)/$(CONFIG)/qps_interarrival_test \
  $(BINDIR)/$(CONFIG)/qps_json_driver \
  $(BINDIR)/$(CONFIG)/qps_openloop_test \
  $(BINDIR)/$(CONFIG)/qps_worker \
  $(BINDIR)/$(CONFIG)/reconnect_interop_client \
  $(BINDIR)/$(CONFIG)/reconnect_interop_server \
  $(BINDIR)/$(CONFIG)/secure_auth_context_test \
  $(BINDIR)/$(CONFIG)/secure_sync_unary_ping_pong_test \
  $(BINDIR)/$(CONFIG)/server_builder_plugin_test \
  $(BINDIR)/$(CONFIG)/server_builder_test \
  $(BINDIR)/$(CONFIG)/server_context_test_spouse_test \
  $(BINDIR)/$(CONFIG)/server_crash_test \
  $(BINDIR)/$(CONFIG)/server_crash_test_client \
  $(BINDIR)/$(CONFIG)/server_request_call_test \
  $(BINDIR)/$(CONFIG)/shutdown_test \
  $(BINDIR)/$(CONFIG)/status_test \
  $(BINDIR)/$(CONFIG)/streaming_throughput_test \
  $(BINDIR)/$(CONFIG)/stress_test \
  $(BINDIR)/$(CONFIG)/thread_manager_test \
  $(BINDIR)/$(CONFIG)/thread_stress_test \
  $(BINDIR)/$(CONFIG)/writes_per_rpc_test \
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
  $(BINDIR)/$(CONFIG)/boringssl_spake25519_test \
  $(BINDIR)/$(CONFIG)/boringssl_x25519_test \
  $(BINDIR)/$(CONFIG)/boringssl_digest_test \
  $(BINDIR)/$(CONFIG)/boringssl_example_mul \
  $(BINDIR)/$(CONFIG)/boringssl_p256-x86_64_test \
  $(BINDIR)/$(CONFIG)/boringssl_ecdh_test \
  $(BINDIR)/$(CONFIG)/boringssl_ecdsa_sign_test \
  $(BINDIR)/$(CONFIG)/boringssl_ecdsa_test \
  $(BINDIR)/$(CONFIG)/boringssl_ecdsa_verify_test \
  $(BINDIR)/$(CONFIG)/boringssl_evp_extra_test \
  $(BINDIR)/$(CONFIG)/boringssl_evp_test \
  $(BINDIR)/$(CONFIG)/boringssl_pbkdf_test \
  $(BINDIR)/$(CONFIG)/boringssl_hkdf_test \
  $(BINDIR)/$(CONFIG)/boringssl_hmac_test \
  $(BINDIR)/$(CONFIG)/boringssl_lhash_test \
  $(BINDIR)/$(CONFIG)/boringssl_gcm_test \
  $(BINDIR)/$(CONFIG)/boringssl_obj_test \
  $(BINDIR)/$(CONFIG)/boringssl_pkcs12_test \
  $(BINDIR)/$(CONFIG)/boringssl_pkcs8_test \
  $(BINDIR)/$(CONFIG)/boringssl_poly1305_test \
  $(BINDIR)/$(CONFIG)/boringssl_pool_test \
  $(BINDIR)/$(CONFIG)/boringssl_refcount_test \
  $(BINDIR)/$(CONFIG)/boringssl_thread_test \
  $(BINDIR)/$(CONFIG)/boringssl_pkcs7_test \
  $(BINDIR)/$(CONFIG)/boringssl_x509_test \
  $(BINDIR)/$(CONFIG)/boringssl_tab_test \
  $(BINDIR)/$(CONFIG)/boringssl_v3name_test \

else
buildtests_cxx: privatelibs_cxx \
  $(BINDIR)/$(CONFIG)/alarm_cpp_test \
  $(BINDIR)/$(CONFIG)/async_end2end_test \
  $(BINDIR)/$(CONFIG)/auth_property_iterator_test \
  $(BINDIR)/$(CONFIG)/bm_arena \
  $(BINDIR)/$(CONFIG)/bm_call_create \
  $(BINDIR)/$(CONFIG)/bm_chttp2_hpack \
  $(BINDIR)/$(CONFIG)/bm_chttp2_transport \
  $(BINDIR)/$(CONFIG)/bm_closure \
  $(BINDIR)/$(CONFIG)/bm_cq \
  $(BINDIR)/$(CONFIG)/bm_cq_multiple_threads \
  $(BINDIR)/$(CONFIG)/bm_error \
  $(BINDIR)/$(CONFIG)/bm_fullstack_streaming_ping_pong \
  $(BINDIR)/$(CONFIG)/bm_fullstack_streaming_pump \
  $(BINDIR)/$(CONFIG)/bm_fullstack_trickle \
  $(BINDIR)/$(CONFIG)/bm_fullstack_unary_ping_pong \
  $(BINDIR)/$(CONFIG)/bm_metadata \
  $(BINDIR)/$(CONFIG)/bm_pollset \
  $(BINDIR)/$(CONFIG)/channel_arguments_test \
  $(BINDIR)/$(CONFIG)/channel_filter_test \
  $(BINDIR)/$(CONFIG)/cli_call_test \
  $(BINDIR)/$(CONFIG)/client_crash_test \
  $(BINDIR)/$(CONFIG)/client_crash_test_server \
  $(BINDIR)/$(CONFIG)/client_lb_end2end_test \
  $(BINDIR)/$(CONFIG)/codegen_test_full \
  $(BINDIR)/$(CONFIG)/codegen_test_minimal \
  $(BINDIR)/$(CONFIG)/credentials_test \
  $(BINDIR)/$(CONFIG)/cxx_byte_buffer_test \
  $(BINDIR)/$(CONFIG)/cxx_slice_test \
  $(BINDIR)/$(CONFIG)/cxx_string_ref_test \
  $(BINDIR)/$(CONFIG)/cxx_time_test \
  $(BINDIR)/$(CONFIG)/end2end_test \
  $(BINDIR)/$(CONFIG)/error_details_test \
  $(BINDIR)/$(CONFIG)/filter_end2end_test \
  $(BINDIR)/$(CONFIG)/generic_end2end_test \
  $(BINDIR)/$(CONFIG)/golden_file_test \
  $(BINDIR)/$(CONFIG)/grpc_cli \
  $(BINDIR)/$(CONFIG)/grpc_tool_test \
  $(BINDIR)/$(CONFIG)/grpclb_api_test \
  $(BINDIR)/$(CONFIG)/grpclb_end2end_test \
  $(BINDIR)/$(CONFIG)/grpclb_test \
  $(BINDIR)/$(CONFIG)/health_service_end2end_test \
  $(BINDIR)/$(CONFIG)/http2_client \
  $(BINDIR)/$(CONFIG)/hybrid_end2end_test \
  $(BINDIR)/$(CONFIG)/interop_client \
  $(BINDIR)/$(CONFIG)/interop_server \
  $(BINDIR)/$(CONFIG)/interop_test \
  $(BINDIR)/$(CONFIG)/json_run_localhost \
  $(BINDIR)/$(CONFIG)/memory_test \
  $(BINDIR)/$(CONFIG)/metrics_client \
  $(BINDIR)/$(CONFIG)/mock_test \
  $(BINDIR)/$(CONFIG)/noop-benchmark \
  $(BINDIR)/$(CONFIG)/proto_server_reflection_test \
  $(BINDIR)/$(CONFIG)/proto_utils_test \
  $(BINDIR)/$(CONFIG)/qps_interarrival_test \
  $(BINDIR)/$(CONFIG)/qps_json_driver \
  $(BINDIR)/$(CONFIG)/qps_openloop_test \
  $(BINDIR)/$(CONFIG)/qps_worker \
  $(BINDIR)/$(CONFIG)/reconnect_interop_client \
  $(BINDIR)/$(CONFIG)/reconnect_interop_server \
  $(BINDIR)/$(CONFIG)/secure_auth_context_test \
  $(BINDIR)/$(CONFIG)/secure_sync_unary_ping_pong_test \
  $(BINDIR)/$(CONFIG)/server_builder_plugin_test \
  $(BINDIR)/$(CONFIG)/server_builder_test \
  $(BINDIR)/$(CONFIG)/server_context_test_spouse_test \
  $(BINDIR)/$(CONFIG)/server_crash_test \
  $(BINDIR)/$(CONFIG)/server_crash_test_client \
  $(BINDIR)/$(CONFIG)/server_request_call_test \
  $(BINDIR)/$(CONFIG)/shutdown_test \
  $(BINDIR)/$(CONFIG)/status_test \
  $(BINDIR)/$(CONFIG)/streaming_throughput_test \
  $(BINDIR)/$(CONFIG)/stress_test \
  $(BINDIR)/$(CONFIG)/thread_manager_test \
  $(BINDIR)/$(CONFIG)/thread_stress_test \
  $(BINDIR)/$(CONFIG)/writes_per_rpc_test \

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
	$(E) "[RUN]     Testing arena_test"
	$(Q) $(BINDIR)/$(CONFIG)/arena_test || ( echo test arena_test failed ; exit 1 )
	$(E) "[RUN]     Testing bad_server_response_test"
	$(Q) $(BINDIR)/$(CONFIG)/bad_server_response_test || ( echo test bad_server_response_test failed ; exit 1 )
	$(E) "[RUN]     Testing bdp_estimator_test"
	$(Q) $(BINDIR)/$(CONFIG)/bdp_estimator_test || ( echo test bdp_estimator_test failed ; exit 1 )
	$(E) "[RUN]     Testing bin_decoder_test"
	$(Q) $(BINDIR)/$(CONFIG)/bin_decoder_test || ( echo test bin_decoder_test failed ; exit 1 )
	$(E) "[RUN]     Testing bin_encoder_test"
	$(Q) $(BINDIR)/$(CONFIG)/bin_encoder_test || ( echo test bin_encoder_test failed ; exit 1 )
	$(E) "[RUN]     Testing byte_stream_test"
	$(Q) $(BINDIR)/$(CONFIG)/byte_stream_test || ( echo test byte_stream_test failed ; exit 1 )
	$(E) "[RUN]     Testing census_context_test"
	$(Q) $(BINDIR)/$(CONFIG)/census_context_test || ( echo test census_context_test failed ; exit 1 )
	$(E) "[RUN]     Testing census_intrusive_hash_map_test"
	$(Q) $(BINDIR)/$(CONFIG)/census_intrusive_hash_map_test || ( echo test census_intrusive_hash_map_test failed ; exit 1 )
	$(E) "[RUN]     Testing census_resource_test"
	$(Q) $(BINDIR)/$(CONFIG)/census_resource_test || ( echo test census_resource_test failed ; exit 1 )
	$(E) "[RUN]     Testing census_trace_context_test"
	$(Q) $(BINDIR)/$(CONFIG)/census_trace_context_test || ( echo test census_trace_context_test failed ; exit 1 )
	$(E) "[RUN]     Testing channel_create_test"
	$(Q) $(BINDIR)/$(CONFIG)/channel_create_test || ( echo test channel_create_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_hpack_encoder_test"
	$(Q) $(BINDIR)/$(CONFIG)/chttp2_hpack_encoder_test || ( echo test chttp2_hpack_encoder_test failed ; exit 1 )
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
	$(E) "[RUN]     Testing error_test"
	$(Q) $(BINDIR)/$(CONFIG)/error_test || ( echo test error_test failed ; exit 1 )
	$(E) "[RUN]     Testing ev_epollsig_linux_test"
	$(Q) $(BINDIR)/$(CONFIG)/ev_epollsig_linux_test || ( echo test ev_epollsig_linux_test failed ; exit 1 )
	$(E) "[RUN]     Testing fake_resolver_test"
	$(Q) $(BINDIR)/$(CONFIG)/fake_resolver_test || ( echo test fake_resolver_test failed ; exit 1 )
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
	$(E) "[RUN]     Testing gpr_spinlock_test"
	$(Q) $(BINDIR)/$(CONFIG)/gpr_spinlock_test || ( echo test gpr_spinlock_test failed ; exit 1 )
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
	$(E) "[RUN]     Testing grpc_completion_queue_threading_test"
	$(Q) $(BINDIR)/$(CONFIG)/grpc_completion_queue_threading_test || ( echo test grpc_completion_queue_threading_test failed ; exit 1 )
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
	$(E) "[RUN]     Testing memory_profile_test"
	$(Q) $(BINDIR)/$(CONFIG)/memory_profile_test || ( echo test memory_profile_test failed ; exit 1 )
	$(E) "[RUN]     Testing message_compress_test"
	$(Q) $(BINDIR)/$(CONFIG)/message_compress_test || ( echo test message_compress_test failed ; exit 1 )
	$(E) "[RUN]     Testing minimal_stack_is_minimal_test"
	$(Q) $(BINDIR)/$(CONFIG)/minimal_stack_is_minimal_test || ( echo test minimal_stack_is_minimal_test failed ; exit 1 )
	$(E) "[RUN]     Testing multiple_server_queues_test"
	$(Q) $(BINDIR)/$(CONFIG)/multiple_server_queues_test || ( echo test multiple_server_queues_test failed ; exit 1 )
	$(E) "[RUN]     Testing murmur_hash_test"
	$(Q) $(BINDIR)/$(CONFIG)/murmur_hash_test || ( echo test murmur_hash_test failed ; exit 1 )
	$(E) "[RUN]     Testing no_server_test"
	$(Q) $(BINDIR)/$(CONFIG)/no_server_test || ( echo test no_server_test failed ; exit 1 )
	$(E) "[RUN]     Testing num_external_connectivity_watchers_test"
	$(Q) $(BINDIR)/$(CONFIG)/num_external_connectivity_watchers_test || ( echo test num_external_connectivity_watchers_test failed ; exit 1 )
	$(E) "[RUN]     Testing parse_address_test"
	$(Q) $(BINDIR)/$(CONFIG)/parse_address_test || ( echo test parse_address_test failed ; exit 1 )
	$(E) "[RUN]     Testing percent_encoding_test"
	$(Q) $(BINDIR)/$(CONFIG)/percent_encoding_test || ( echo test percent_encoding_test failed ; exit 1 )
	$(E) "[RUN]     Testing pollset_set_test"
	$(Q) $(BINDIR)/$(CONFIG)/pollset_set_test || ( echo test pollset_set_test failed ; exit 1 )
	$(E) "[RUN]     Testing resolve_address_posix_test"
	$(Q) $(BINDIR)/$(CONFIG)/resolve_address_posix_test || ( echo test resolve_address_posix_test failed ; exit 1 )
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
	$(E) "[RUN]     Testing slice_buffer_test"
	$(Q) $(BINDIR)/$(CONFIG)/slice_buffer_test || ( echo test slice_buffer_test failed ; exit 1 )
	$(E) "[RUN]     Testing slice_hash_table_test"
	$(Q) $(BINDIR)/$(CONFIG)/slice_hash_table_test || ( echo test slice_hash_table_test failed ; exit 1 )
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
	$(E) "[RUN]     Testing status_conversion_test"
	$(Q) $(BINDIR)/$(CONFIG)/status_conversion_test || ( echo test status_conversion_test failed ; exit 1 )
	$(E) "[RUN]     Testing stream_compression_test"
	$(Q) $(BINDIR)/$(CONFIG)/stream_compression_test || ( echo test stream_compression_test failed ; exit 1 )
	$(E) "[RUN]     Testing stream_owned_slice_test"
	$(Q) $(BINDIR)/$(CONFIG)/stream_owned_slice_test || ( echo test stream_owned_slice_test failed ; exit 1 )
	$(E) "[RUN]     Testing tcp_client_posix_test"
	$(Q) $(BINDIR)/$(CONFIG)/tcp_client_posix_test || ( echo test tcp_client_posix_test failed ; exit 1 )
	$(E) "[RUN]     Testing tcp_client_uv_test"
	$(Q) $(BINDIR)/$(CONFIG)/tcp_client_uv_test || ( echo test tcp_client_uv_test failed ; exit 1 )
	$(E) "[RUN]     Testing tcp_posix_test"
	$(Q) $(BINDIR)/$(CONFIG)/tcp_posix_test || ( echo test tcp_posix_test failed ; exit 1 )
	$(E) "[RUN]     Testing tcp_server_posix_test"
	$(Q) $(BINDIR)/$(CONFIG)/tcp_server_posix_test || ( echo test tcp_server_posix_test failed ; exit 1 )
	$(E) "[RUN]     Testing tcp_server_uv_test"
	$(Q) $(BINDIR)/$(CONFIG)/tcp_server_uv_test || ( echo test tcp_server_uv_test failed ; exit 1 )
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
	$(E) "[RUN]     Testing mlog_test"
	$(Q) $(BINDIR)/$(CONFIG)/mlog_test || ( echo test mlog_test failed ; exit 1 )


test_cxx: buildtests_cxx
	$(E) "[RUN]     Testing alarm_cpp_test"
	$(Q) $(BINDIR)/$(CONFIG)/alarm_cpp_test || ( echo test alarm_cpp_test failed ; exit 1 )
	$(E) "[RUN]     Testing async_end2end_test"
	$(Q) $(BINDIR)/$(CONFIG)/async_end2end_test || ( echo test async_end2end_test failed ; exit 1 )
	$(E) "[RUN]     Testing auth_property_iterator_test"
	$(Q) $(BINDIR)/$(CONFIG)/auth_property_iterator_test || ( echo test auth_property_iterator_test failed ; exit 1 )
	$(E) "[RUN]     Testing bm_arena"
	$(Q) $(BINDIR)/$(CONFIG)/bm_arena || ( echo test bm_arena failed ; exit 1 )
	$(E) "[RUN]     Testing bm_call_create"
	$(Q) $(BINDIR)/$(CONFIG)/bm_call_create || ( echo test bm_call_create failed ; exit 1 )
	$(E) "[RUN]     Testing bm_chttp2_hpack"
	$(Q) $(BINDIR)/$(CONFIG)/bm_chttp2_hpack || ( echo test bm_chttp2_hpack failed ; exit 1 )
	$(E) "[RUN]     Testing bm_chttp2_transport"
	$(Q) $(BINDIR)/$(CONFIG)/bm_chttp2_transport || ( echo test bm_chttp2_transport failed ; exit 1 )
	$(E) "[RUN]     Testing bm_closure"
	$(Q) $(BINDIR)/$(CONFIG)/bm_closure || ( echo test bm_closure failed ; exit 1 )
	$(E) "[RUN]     Testing bm_cq"
	$(Q) $(BINDIR)/$(CONFIG)/bm_cq || ( echo test bm_cq failed ; exit 1 )
	$(E) "[RUN]     Testing bm_cq_multiple_threads"
	$(Q) $(BINDIR)/$(CONFIG)/bm_cq_multiple_threads || ( echo test bm_cq_multiple_threads failed ; exit 1 )
	$(E) "[RUN]     Testing bm_error"
	$(Q) $(BINDIR)/$(CONFIG)/bm_error || ( echo test bm_error failed ; exit 1 )
	$(E) "[RUN]     Testing bm_fullstack_streaming_ping_pong"
	$(Q) $(BINDIR)/$(CONFIG)/bm_fullstack_streaming_ping_pong || ( echo test bm_fullstack_streaming_ping_pong failed ; exit 1 )
	$(E) "[RUN]     Testing bm_fullstack_streaming_pump"
	$(Q) $(BINDIR)/$(CONFIG)/bm_fullstack_streaming_pump || ( echo test bm_fullstack_streaming_pump failed ; exit 1 )
	$(E) "[RUN]     Testing bm_fullstack_trickle"
	$(Q) $(BINDIR)/$(CONFIG)/bm_fullstack_trickle || ( echo test bm_fullstack_trickle failed ; exit 1 )
	$(E) "[RUN]     Testing bm_fullstack_unary_ping_pong"
	$(Q) $(BINDIR)/$(CONFIG)/bm_fullstack_unary_ping_pong || ( echo test bm_fullstack_unary_ping_pong failed ; exit 1 )
	$(E) "[RUN]     Testing bm_metadata"
	$(Q) $(BINDIR)/$(CONFIG)/bm_metadata || ( echo test bm_metadata failed ; exit 1 )
	$(E) "[RUN]     Testing bm_pollset"
	$(Q) $(BINDIR)/$(CONFIG)/bm_pollset || ( echo test bm_pollset failed ; exit 1 )
	$(E) "[RUN]     Testing channel_arguments_test"
	$(Q) $(BINDIR)/$(CONFIG)/channel_arguments_test || ( echo test channel_arguments_test failed ; exit 1 )
	$(E) "[RUN]     Testing channel_filter_test"
	$(Q) $(BINDIR)/$(CONFIG)/channel_filter_test || ( echo test channel_filter_test failed ; exit 1 )
	$(E) "[RUN]     Testing cli_call_test"
	$(Q) $(BINDIR)/$(CONFIG)/cli_call_test || ( echo test cli_call_test failed ; exit 1 )
	$(E) "[RUN]     Testing client_crash_test"
	$(Q) $(BINDIR)/$(CONFIG)/client_crash_test || ( echo test client_crash_test failed ; exit 1 )
	$(E) "[RUN]     Testing client_lb_end2end_test"
	$(Q) $(BINDIR)/$(CONFIG)/client_lb_end2end_test || ( echo test client_lb_end2end_test failed ; exit 1 )
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
	$(E) "[RUN]     Testing error_details_test"
	$(Q) $(BINDIR)/$(CONFIG)/error_details_test || ( echo test error_details_test failed ; exit 1 )
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
	$(E) "[RUN]     Testing grpclb_end2end_test"
	$(Q) $(BINDIR)/$(CONFIG)/grpclb_end2end_test || ( echo test grpclb_end2end_test failed ; exit 1 )
	$(E) "[RUN]     Testing grpclb_test"
	$(Q) $(BINDIR)/$(CONFIG)/grpclb_test || ( echo test grpclb_test failed ; exit 1 )
	$(E) "[RUN]     Testing health_service_end2end_test"
	$(Q) $(BINDIR)/$(CONFIG)/health_service_end2end_test || ( echo test health_service_end2end_test failed ; exit 1 )
	$(E) "[RUN]     Testing interop_test"
	$(Q) $(BINDIR)/$(CONFIG)/interop_test || ( echo test interop_test failed ; exit 1 )
	$(E) "[RUN]     Testing memory_test"
	$(Q) $(BINDIR)/$(CONFIG)/memory_test || ( echo test memory_test failed ; exit 1 )
	$(E) "[RUN]     Testing mock_test"
	$(Q) $(BINDIR)/$(CONFIG)/mock_test || ( echo test mock_test failed ; exit 1 )
	$(E) "[RUN]     Testing noop-benchmark"
	$(Q) $(BINDIR)/$(CONFIG)/noop-benchmark || ( echo test noop-benchmark failed ; exit 1 )
	$(E) "[RUN]     Testing proto_server_reflection_test"
	$(Q) $(BINDIR)/$(CONFIG)/proto_server_reflection_test || ( echo test proto_server_reflection_test failed ; exit 1 )
	$(E) "[RUN]     Testing proto_utils_test"
	$(Q) $(BINDIR)/$(CONFIG)/proto_utils_test || ( echo test proto_utils_test failed ; exit 1 )
	$(E) "[RUN]     Testing qps_openloop_test"
	$(Q) $(BINDIR)/$(CONFIG)/qps_openloop_test || ( echo test qps_openloop_test failed ; exit 1 )
	$(E) "[RUN]     Testing secure_auth_context_test"
	$(Q) $(BINDIR)/$(CONFIG)/secure_auth_context_test || ( echo test secure_auth_context_test failed ; exit 1 )
	$(E) "[RUN]     Testing secure_sync_unary_ping_pong_test"
	$(Q) $(BINDIR)/$(CONFIG)/secure_sync_unary_ping_pong_test || ( echo test secure_sync_unary_ping_pong_test failed ; exit 1 )
	$(E) "[RUN]     Testing server_builder_plugin_test"
	$(Q) $(BINDIR)/$(CONFIG)/server_builder_plugin_test || ( echo test server_builder_plugin_test failed ; exit 1 )
	$(E) "[RUN]     Testing server_builder_test"
	$(Q) $(BINDIR)/$(CONFIG)/server_builder_test || ( echo test server_builder_test failed ; exit 1 )
	$(E) "[RUN]     Testing server_context_test_spouse_test"
	$(Q) $(BINDIR)/$(CONFIG)/server_context_test_spouse_test || ( echo test server_context_test_spouse_test failed ; exit 1 )
	$(E) "[RUN]     Testing server_crash_test"
	$(Q) $(BINDIR)/$(CONFIG)/server_crash_test || ( echo test server_crash_test failed ; exit 1 )
	$(E) "[RUN]     Testing server_request_call_test"
	$(Q) $(BINDIR)/$(CONFIG)/server_request_call_test || ( echo test server_request_call_test failed ; exit 1 )
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
	$(E) "[RUN]     Testing writes_per_rpc_test"
	$(Q) $(BINDIR)/$(CONFIG)/writes_per_rpc_test || ( echo test writes_per_rpc_test failed ; exit 1 )


flaky_test_cxx: buildtests_cxx
	$(E) "[RUN]     Testing hybrid_end2end_test"
	$(Q) $(BINDIR)/$(CONFIG)/hybrid_end2end_test || ( echo test hybrid_end2end_test failed ; exit 1 )


test_python: static_c
	$(E) "[RUN]     Testing python code"
	$(Q) tools/run_tests/run_tests.py -lpython -c$(CONFIG)


tools: tools_c tools_cxx


tools_c: privatelibs_c $(BINDIR)/$(CONFIG)/check_epollexclusive $(BINDIR)/$(CONFIG)/gen_hpack_tables $(BINDIR)/$(CONFIG)/gen_legal_metadata_characters $(BINDIR)/$(CONFIG)/gen_percent_encoding_tables $(BINDIR)/$(CONFIG)/grpc_create_jwt $(BINDIR)/$(CONFIG)/grpc_print_google_default_creds_token $(BINDIR)/$(CONFIG)/grpc_verify_jwt

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
	$(E) "[STRIP]   Stripping libgrpc++_error_details.a"
	$(Q) $(STRIP) $(LIBDIR)/$(CONFIG)/libgrpc++_error_details.a
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
	$(E) "[STRIP]   Stripping $(SHARED_PREFIX)grpc++_error_details$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)"
	$(Q) $(STRIP) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_error_details$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)
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
$(GENDIR)/src/proto/grpc/health/v1/health.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/health/v1/health.grpc.pb.cc: protoc_dep_error
else

$(GENDIR)/src/proto/grpc/health/v1/health.pb.cc: src/proto/grpc/health/v1/health.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/health/v1/health.grpc.pb.cc: src/proto/grpc/health/v1/health.proto $(GENDIR)/src/proto/grpc/health/v1/health.pb.cc $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin$(EXECUTABLE_SUFFIX) $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/lb/v1/load_balancer.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/lb/v1/load_balancer.grpc.pb.cc: protoc_dep_error
else

$(GENDIR)/src/proto/grpc/lb/v1/load_balancer.pb.cc: src/proto/grpc/lb/v1/load_balancer.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/lb/v1/load_balancer.grpc.pb.cc: src/proto/grpc/lb/v1/load_balancer.proto $(GENDIR)/src/proto/grpc/lb/v1/load_balancer.pb.cc $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin$(EXECUTABLE_SUFFIX) $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc: protoc_dep_error
else

$(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.pb.cc: src/proto/grpc/reflection/v1alpha/reflection.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc: src/proto/grpc/reflection/v1alpha/reflection.proto $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.pb.cc $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin$(EXECUTABLE_SUFFIX) $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/status/status.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/status/status.grpc.pb.cc: protoc_dep_error
else

$(GENDIR)/src/proto/grpc/status/status.pb.cc: src/proto/grpc/status/status.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/status/status.grpc.pb.cc: src/proto/grpc/status/status.proto $(GENDIR)/src/proto/grpc/status/status.pb.cc $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin$(EXECUTABLE_SUFFIX) $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/compiler_test.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/compiler_test.grpc.pb.cc: protoc_dep_error
else


$(GENDIR)/src/proto/grpc/testing/compiler_test.pb.cc: src/proto/grpc/testing/compiler_test.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/compiler_test.grpc.pb.cc: src/proto/grpc/testing/compiler_test.proto $(GENDIR)/src/proto/grpc/testing/compiler_test.pb.cc $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=generate_mock_code=true:$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin$(EXECUTABLE_SUFFIX) $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/control.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/control.grpc.pb.cc: protoc_dep_error
else

$(GENDIR)/src/proto/grpc/testing/control.pb.cc: src/proto/grpc/testing/control.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) $(GENDIR)/src/proto/grpc/testing/payloads.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.pb.cc
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/control.grpc.pb.cc: src/proto/grpc/testing/control.proto $(GENDIR)/src/proto/grpc/testing/control.pb.cc $(PROTOBUF_DEP) $(PROTOC_PLUGINS) $(GENDIR)/src/proto/grpc/testing/payloads.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin$(EXECUTABLE_SUFFIX) $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc: protoc_dep_error
else

$(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc: src/proto/grpc/testing/duplicate/echo_duplicate.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc: src/proto/grpc/testing/duplicate/echo_duplicate.proto $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc $(PROTOBUF_DEP) $(PROTOC_PLUGINS) $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin$(EXECUTABLE_SUFFIX) $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/echo.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc: protoc_dep_error
else


$(GENDIR)/src/proto/grpc/testing/echo.pb.cc: src/proto/grpc/testing/echo.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc: src/proto/grpc/testing/echo.proto $(GENDIR)/src/proto/grpc/testing/echo.pb.cc $(PROTOBUF_DEP) $(PROTOC_PLUGINS) $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=generate_mock_code=true:$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin$(EXECUTABLE_SUFFIX) $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc: protoc_dep_error
else

$(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc: src/proto/grpc/testing/echo_messages.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc: src/proto/grpc/testing/echo_messages.proto $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin$(EXECUTABLE_SUFFIX) $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/empty.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/empty.grpc.pb.cc: protoc_dep_error
else

$(GENDIR)/src/proto/grpc/testing/empty.pb.cc: src/proto/grpc/testing/empty.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/empty.grpc.pb.cc: src/proto/grpc/testing/empty.proto $(GENDIR)/src/proto/grpc/testing/empty.pb.cc $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin$(EXECUTABLE_SUFFIX) $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/messages.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc: protoc_dep_error
else

$(GENDIR)/src/proto/grpc/testing/messages.pb.cc: src/proto/grpc/testing/messages.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc: src/proto/grpc/testing/messages.proto $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin$(EXECUTABLE_SUFFIX) $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/metrics.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/metrics.grpc.pb.cc: protoc_dep_error
else

$(GENDIR)/src/proto/grpc/testing/metrics.pb.cc: src/proto/grpc/testing/metrics.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/metrics.grpc.pb.cc: src/proto/grpc/testing/metrics.proto $(GENDIR)/src/proto/grpc/testing/metrics.pb.cc $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin$(EXECUTABLE_SUFFIX) $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/payloads.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/payloads.grpc.pb.cc: protoc_dep_error
else

$(GENDIR)/src/proto/grpc/testing/payloads.pb.cc: src/proto/grpc/testing/payloads.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/payloads.grpc.pb.cc: src/proto/grpc/testing/payloads.proto $(GENDIR)/src/proto/grpc/testing/payloads.pb.cc $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin$(EXECUTABLE_SUFFIX) $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/services.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/services.grpc.pb.cc: protoc_dep_error
else

$(GENDIR)/src/proto/grpc/testing/services.pb.cc: src/proto/grpc/testing/services.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/control.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.pb.cc
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/services.grpc.pb.cc: src/proto/grpc/testing/services.proto $(GENDIR)/src/proto/grpc/testing/services.pb.cc $(PROTOBUF_DEP) $(PROTOC_PLUGINS) $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/control.pb.cc $(GENDIR)/src/proto/grpc/testing/control.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin$(EXECUTABLE_SUFFIX) $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/stats.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc: protoc_dep_error
else

$(GENDIR)/src/proto/grpc/testing/stats.pb.cc: src/proto/grpc/testing/stats.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc: src/proto/grpc/testing/stats.proto $(GENDIR)/src/proto/grpc/testing/stats.pb.cc $(PROTOBUF_DEP) $(PROTOC_PLUGINS) 
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin$(EXECUTABLE_SUFFIX) $<
endif

ifeq ($(NO_PROTOC),true)
$(GENDIR)/src/proto/grpc/testing/test.pb.cc: protoc_dep_error
$(GENDIR)/src/proto/grpc/testing/test.grpc.pb.cc: protoc_dep_error
else

$(GENDIR)/src/proto/grpc/testing/test.pb.cc: src/proto/grpc/testing/test.proto $(PROTOBUF_DEP) $(PROTOC_PLUGINS) $(GENDIR)/src/proto/grpc/testing/empty.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.pb.cc
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --cpp_out=$(GENDIR) $<

$(GENDIR)/src/proto/grpc/testing/test.grpc.pb.cc: src/proto/grpc/testing/test.proto $(GENDIR)/src/proto/grpc/testing/test.pb.cc $(PROTOBUF_DEP) $(PROTOC_PLUGINS) $(GENDIR)/src/proto/grpc/testing/empty.pb.cc $(GENDIR)/src/proto/grpc/testing/empty.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc
	$(E) "[GRPC]    Generating gRPC's protobuf service CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) -Ithird_party/protobuf/src -I. --grpc_out=$(GENDIR) --plugin=protoc-gen-grpc=$(PROTOC_PLUGINS_DIR)/grpc_cpp_plugin$(EXECUTABLE_SUFFIX) $<
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
	$(E) "[INSTALL] Installing libgrpc++_error_details.a"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc++_error_details.a $(prefix)/lib/libgrpc++_error_details.a
	$(E) "[INSTALL] Installing libgrpc++_reflection.a"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc++_reflection.a $(prefix)/lib/libgrpc++_reflection.a
	$(E) "[INSTALL] Installing libgrpc++_unsecure.a"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(prefix)/lib/libgrpc++_unsecure.a



install-shared_c: shared_c strip-shared_c install-pkg-config_c
	$(E) "[INSTALL] Installing $(SHARED_PREFIX)gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/$(SHARED_PREFIX)gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)
ifeq ($(SYSTEM),MINGW32)
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgpr$(SHARED_VERSION_CORE)-dll.a $(prefix)/lib/libgpr.a
else ifneq ($(SYSTEM),Darwin)
	$(Q) ln -sf $(SHARED_PREFIX)gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/libgpr.so.4
	$(Q) ln -sf $(SHARED_PREFIX)gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/libgpr.so
endif
	$(E) "[INSTALL] Installing $(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/$(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)
ifeq ($(SYSTEM),MINGW32)
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc$(SHARED_VERSION_CORE)-dll.a $(prefix)/lib/libgrpc.a
else ifneq ($(SYSTEM),Darwin)
	$(Q) ln -sf $(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/libgrpc.so.4
	$(Q) ln -sf $(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/libgrpc.so
endif
	$(E) "[INSTALL] Installing $(SHARED_PREFIX)grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/$(SHARED_PREFIX)grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)
ifeq ($(SYSTEM),MINGW32)
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc_cronet$(SHARED_VERSION_CORE)-dll.a $(prefix)/lib/libgrpc_cronet.a
else ifneq ($(SYSTEM),Darwin)
	$(Q) ln -sf $(SHARED_PREFIX)grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/libgrpc_cronet.so.4
	$(Q) ln -sf $(SHARED_PREFIX)grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/libgrpc_cronet.so
endif
	$(E) "[INSTALL] Installing $(SHARED_PREFIX)grpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/$(SHARED_PREFIX)grpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)
ifeq ($(SYSTEM),MINGW32)
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc_unsecure$(SHARED_VERSION_CORE)-dll.a $(prefix)/lib/libgrpc_unsecure.a
else ifneq ($(SYSTEM),Darwin)
	$(Q) ln -sf $(SHARED_PREFIX)grpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(prefix)/lib/libgrpc_unsecure.so.4
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
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/$(SHARED_PREFIX)grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)
ifeq ($(SYSTEM),MINGW32)
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc++$(SHARED_VERSION_CPP)-dll.a $(prefix)/lib/libgrpc++.a
else ifneq ($(SYSTEM),Darwin)
	$(Q) ln -sf $(SHARED_PREFIX)grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/libgrpc++.so.4
	$(Q) ln -sf $(SHARED_PREFIX)grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/libgrpc++.so
endif
	$(E) "[INSTALL] Installing $(SHARED_PREFIX)grpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/$(SHARED_PREFIX)grpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)
ifeq ($(SYSTEM),MINGW32)
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc++_cronet$(SHARED_VERSION_CPP)-dll.a $(prefix)/lib/libgrpc++_cronet.a
else ifneq ($(SYSTEM),Darwin)
	$(Q) ln -sf $(SHARED_PREFIX)grpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/libgrpc++_cronet.so.4
	$(Q) ln -sf $(SHARED_PREFIX)grpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/libgrpc++_cronet.so
endif
	$(E) "[INSTALL] Installing $(SHARED_PREFIX)grpc++_error_details$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_error_details$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/$(SHARED_PREFIX)grpc++_error_details$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)
ifeq ($(SYSTEM),MINGW32)
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc++_error_details$(SHARED_VERSION_CPP)-dll.a $(prefix)/lib/libgrpc++_error_details.a
else ifneq ($(SYSTEM),Darwin)
	$(Q) ln -sf $(SHARED_PREFIX)grpc++_error_details$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/libgrpc++_error_details.so.4
	$(Q) ln -sf $(SHARED_PREFIX)grpc++_error_details$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/libgrpc++_error_details.so
endif
	$(E) "[INSTALL] Installing $(SHARED_PREFIX)grpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/$(SHARED_PREFIX)grpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)
ifeq ($(SYSTEM),MINGW32)
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc++_reflection$(SHARED_VERSION_CPP)-dll.a $(prefix)/lib/libgrpc++_reflection.a
else ifneq ($(SYSTEM),Darwin)
	$(Q) ln -sf $(SHARED_PREFIX)grpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/libgrpc++_reflection.so.4
	$(Q) ln -sf $(SHARED_PREFIX)grpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/libgrpc++_reflection.so
endif
	$(E) "[INSTALL] Installing $(SHARED_PREFIX)grpc++_unsecure$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)"
	$(Q) $(INSTALL) -d $(prefix)/lib
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_unsecure$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/$(SHARED_PREFIX)grpc++_unsecure$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP)
ifeq ($(SYSTEM),MINGW32)
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure$(SHARED_VERSION_CPP)-dll.a $(prefix)/lib/libgrpc++_unsecure.a
else ifneq ($(SYSTEM),Darwin)
	$(Q) ln -sf $(SHARED_PREFIX)grpc++_unsecure$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(prefix)/lib/libgrpc++_unsecure.so.4
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
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc_csharp_ext$(SHARED_VERSION_CSHARP).$(SHARED_EXT_CSHARP) $(prefix)/lib/$(SHARED_PREFIX)grpc_csharp_ext$(SHARED_VERSION_CSHARP).$(SHARED_EXT_CSHARP)
ifeq ($(SYSTEM),MINGW32)
	$(Q) $(INSTALL) $(LIBDIR)/$(CONFIG)/libgrpc_csharp_ext$(SHARED_VERSION_CSHARP)-dll.a $(prefix)/lib/libgrpc_csharp_ext.a
else ifneq ($(SYSTEM),Darwin)
	$(Q) ln -sf $(SHARED_PREFIX)grpc_csharp_ext$(SHARED_VERSION_CSHARP).$(SHARED_EXT_CSHARP) $(prefix)/lib/libgrpc_csharp_ext.so.4
	$(Q) ln -sf $(SHARED_PREFIX)grpc_csharp_ext$(SHARED_VERSION_CSHARP).$(SHARED_EXT_CSHARP) $(prefix)/lib/libgrpc_csharp_ext.so
endif
ifneq ($(SYSTEM),MINGW32)
ifneq ($(SYSTEM),Darwin)
	$(Q) ldconfig || true
endif
endif


install-plugins: $(PROTOC_PLUGINS)
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
    src/core/lib/support/arena.c \
    src/core/lib/support/atm.c \
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
    include/grpc/impl/codegen/gpr_slice.h \
    include/grpc/impl/codegen/gpr_types.h \
    include/grpc/impl/codegen/port_platform.h \
    include/grpc/impl/codegen/sync.h \
    include/grpc/impl/codegen/sync_generic.h \
    include/grpc/impl/codegen/sync_posix.h \
    include/grpc/impl/codegen/sync_windows.h \

LIBGPR_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGPR_SRC))))


$(LIBDIR)/$(CONFIG)/libgpr.a: $(ZLIB_DEP) $(CARES_DEP)  $(LIBGPR_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgpr.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBGPR_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgpr.a
endif



ifeq ($(SYSTEM),MINGW32)
$(LIBDIR)/$(CONFIG)/gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): $(LIBGPR_OBJS)  $(ZLIB_DEP) $(CARES_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,--output-def=$(LIBDIR)/$(CONFIG)/gpr$(SHARED_VERSION_CORE).def -Wl,--out-implib=$(LIBDIR)/$(CONFIG)/libgpr$(SHARED_VERSION_CORE)-dll.a -o $(LIBDIR)/$(CONFIG)/gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGPR_OBJS) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBS)
else
$(LIBDIR)/$(CONFIG)/libgpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): $(LIBGPR_OBJS)  $(ZLIB_DEP) $(CARES_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
ifeq ($(SYSTEM),Darwin)
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -install_name $(SHARED_PREFIX)gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) -dynamiclib -o $(LIBDIR)/$(CONFIG)/libgpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGPR_OBJS) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBS)
else
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,-soname,libgpr.so.4 -o $(LIBDIR)/$(CONFIG)/libgpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGPR_OBJS) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBS)
	$(Q) ln -sf $(SHARED_PREFIX)gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/libgpr$(SHARED_VERSION_CORE).so.4
	$(Q) ln -sf $(SHARED_PREFIX)gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/libgpr$(SHARED_VERSION_CORE).so
endif
endif

ifneq ($(NO_DEPS),true)
-include $(LIBGPR_OBJS:.o=.dep)
endif


LIBGPR_TEST_UTIL_SRC = \
    test/core/util/test_config.c \

PUBLIC_HEADERS_C += \

LIBGPR_TEST_UTIL_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGPR_TEST_UTIL_SRC))))


$(LIBDIR)/$(CONFIG)/libgpr_test_util.a: $(ZLIB_DEP) $(CARES_DEP)  $(LIBGPR_TEST_UTIL_OBJS) 
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
    src/core/lib/channel/connected_channel.c \
    src/core/lib/channel/handshaker.c \
    src/core/lib/channel/handshaker_factory.c \
    src/core/lib/channel/handshaker_registry.c \
    src/core/lib/compression/compression.c \
    src/core/lib/compression/message_compress.c \
    src/core/lib/compression/stream_compression.c \
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
    src/core/lib/iomgr/ev_epoll1_linux.c \
    src/core/lib/iomgr/ev_epoll_limited_pollers_linux.c \
    src/core/lib/iomgr/ev_epoll_thread_pool_linux.c \
    src/core/lib/iomgr/ev_epollex_linux.c \
    src/core/lib/iomgr/ev_epollsig_linux.c \
    src/core/lib/iomgr/ev_poll_posix.c \
    src/core/lib/iomgr/ev_posix.c \
    src/core/lib/iomgr/ev_windows.c \
    src/core/lib/iomgr/exec_ctx.c \
    src/core/lib/iomgr/executor.c \
    src/core/lib/iomgr/gethostname_fallback.c \
    src/core/lib/iomgr/gethostname_host_name_max.c \
    src/core/lib/iomgr/gethostname_sysconf.c \
    src/core/lib/iomgr/iocp_windows.c \
    src/core/lib/iomgr/iomgr.c \
    src/core/lib/iomgr/iomgr_posix.c \
    src/core/lib/iomgr/iomgr_uv.c \
    src/core/lib/iomgr/iomgr_windows.c \
    src/core/lib/iomgr/is_epollexclusive_available.c \
    src/core/lib/iomgr/load_file.c \
    src/core/lib/iomgr/lockfree_event.c \
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
    src/core/lib/iomgr/socket_factory_posix.c \
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
    src/core/lib/iomgr/tcp_server_utils_posix_common.c \
    src/core/lib/iomgr/tcp_server_utils_posix_ifaddrs.c \
    src/core/lib/iomgr/tcp_server_utils_posix_noifaddrs.c \
    src/core/lib/iomgr/tcp_server_uv.c \
    src/core/lib/iomgr/tcp_server_windows.c \
    src/core/lib/iomgr/tcp_uv.c \
    src/core/lib/iomgr/tcp_windows.c \
    src/core/lib/iomgr/time_averaged_stats.c \
    src/core/lib/iomgr/timer_generic.c \
    src/core/lib/iomgr/timer_heap.c \
    src/core/lib/iomgr/timer_manager.c \
    src/core/lib/iomgr/timer_uv.c \
    src/core/lib/iomgr/udp_server.c \
    src/core/lib/iomgr/unix_sockets_posix.c \
    src/core/lib/iomgr/unix_sockets_posix_noop.c \
    src/core/lib/iomgr/wakeup_fd_cv.c \
    src/core/lib/iomgr/wakeup_fd_eventfd.c \
    src/core/lib/iomgr/wakeup_fd_nospecial.c \
    src/core/lib/iomgr/wakeup_fd_pipe.c \
    src/core/lib/iomgr/wakeup_fd_posix.c \
    src/core/lib/json/json.c \
    src/core/lib/json/json_reader.c \
    src/core/lib/json/json_string.c \
    src/core/lib/json/json_writer.c \
    src/core/lib/slice/b64.c \
    src/core/lib/slice/percent_encoding.c \
    src/core/lib/slice/slice.c \
    src/core/lib/slice/slice_buffer.c \
    src/core/lib/slice/slice_hash_table.c \
    src/core/lib/slice/slice_intern.c \
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
    src/core/lib/surface/completion_queue_factory.c \
    src/core/lib/surface/event_string.c \
    src/core/lib/surface/lame_client.cc \
    src/core/lib/surface/metadata_array.c \
    src/core/lib/surface/server.c \
    src/core/lib/surface/validate_metadata.c \
    src/core/lib/surface/version.c \
    src/core/lib/transport/bdp_estimator.c \
    src/core/lib/transport/byte_stream.c \
    src/core/lib/transport/connectivity_state.c \
    src/core/lib/transport/error_utils.c \
    src/core/lib/transport/metadata.c \
    src/core/lib/transport/metadata_batch.c \
    src/core/lib/transport/pid_controller.c \
    src/core/lib/transport/service_config.c \
    src/core/lib/transport/static_metadata.c \
    src/core/lib/transport/status_conversion.c \
    src/core/lib/transport/timeout_encoding.c \
    src/core/lib/transport/transport.c \
    src/core/lib/transport/transport_op_string.c \
    src/core/lib/debug/trace.c \
    src/core/ext/transport/chttp2/server/secure/server_secure_chttp2.c \
    src/core/ext/transport/chttp2/transport/bin_decoder.c \
    src/core/ext/transport/chttp2/transport/bin_encoder.c \
    src/core/ext/transport/chttp2/transport/chttp2_plugin.c \
    src/core/ext/transport/chttp2/transport/chttp2_transport.c \
    src/core/ext/transport/chttp2/transport/flow_control.c \
    src/core/ext/transport/chttp2/transport/frame_data.c \
    src/core/ext/transport/chttp2/transport/frame_goaway.c \
    src/core/ext/transport/chttp2/transport/frame_ping.c \
    src/core/ext/transport/chttp2/transport/frame_rst_stream.c \
    src/core/ext/transport/chttp2/transport/frame_settings.c \
    src/core/ext/transport/chttp2/transport/frame_window_update.c \
    src/core/ext/transport/chttp2/transport/hpack_encoder.c \
    src/core/ext/transport/chttp2/transport/hpack_parser.c \
    src/core/ext/transport/chttp2/transport/hpack_table.c \
    src/core/ext/transport/chttp2/transport/http2_settings.c \
    src/core/ext/transport/chttp2/transport/huffsyms.c \
    src/core/ext/transport/chttp2/transport/incoming_metadata.c \
    src/core/ext/transport/chttp2/transport/parsing.c \
    src/core/ext/transport/chttp2/transport/stream_lists.c \
    src/core/ext/transport/chttp2/transport/stream_map.c \
    src/core/ext/transport/chttp2/transport/varint.c \
    src/core/ext/transport/chttp2/transport/writing.c \
    src/core/ext/transport/chttp2/alpn/alpn.c \
    src/core/ext/filters/http/client/http_client_filter.c \
    src/core/ext/filters/http/http_filters_plugin.c \
    src/core/ext/filters/http/message_compress/message_compress_filter.c \
    src/core/ext/filters/http/server/http_server_filter.c \
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
    src/core/lib/security/transport/lb_targets_info.c \
    src/core/lib/security/transport/secure_endpoint.c \
    src/core/lib/security/transport/security_connector.c \
    src/core/lib/security/transport/security_handshaker.c \
    src/core/lib/security/transport/server_auth_filter.c \
    src/core/lib/security/transport/tsi_error.c \
    src/core/lib/security/util/json_util.c \
    src/core/lib/surface/init_secure.c \
    src/core/tsi/fake_transport_security.c \
    src/core/tsi/gts_transport_security.c \
    src/core/tsi/ssl_transport_security.c \
    src/core/tsi/transport_security.c \
    src/core/tsi/transport_security_adapter.c \
    src/core/tsi/transport_security_grpc.c \
    src/core/ext/transport/chttp2/server/chttp2_server.c \
    src/core/ext/transport/chttp2/client/secure/secure_channel_create.c \
    src/core/ext/filters/client_channel/channel_connectivity.c \
    src/core/ext/filters/client_channel/client_channel.c \
    src/core/ext/filters/client_channel/client_channel_factory.c \
    src/core/ext/filters/client_channel/client_channel_plugin.c \
    src/core/ext/filters/client_channel/connector.c \
    src/core/ext/filters/client_channel/http_connect_handshaker.c \
    src/core/ext/filters/client_channel/http_proxy.c \
    src/core/ext/filters/client_channel/lb_policy.c \
    src/core/ext/filters/client_channel/lb_policy_factory.c \
    src/core/ext/filters/client_channel/lb_policy_registry.c \
    src/core/ext/filters/client_channel/parse_address.c \
    src/core/ext/filters/client_channel/proxy_mapper.c \
    src/core/ext/filters/client_channel/proxy_mapper_registry.c \
    src/core/ext/filters/client_channel/resolver.c \
    src/core/ext/filters/client_channel/resolver_factory.c \
    src/core/ext/filters/client_channel/resolver_registry.c \
    src/core/ext/filters/client_channel/retry_throttle.c \
    src/core/ext/filters/client_channel/subchannel.c \
    src/core/ext/filters/client_channel/subchannel_index.c \
    src/core/ext/filters/client_channel/uri_parser.c \
    src/core/ext/filters/deadline/deadline_filter.c \
    src/core/ext/transport/chttp2/client/chttp2_connector.c \
    src/core/ext/transport/chttp2/server/insecure/server_chttp2.c \
    src/core/ext/transport/chttp2/server/insecure/server_chttp2_posix.c \
    src/core/ext/transport/chttp2/client/insecure/channel_create.c \
    src/core/ext/transport/chttp2/client/insecure/channel_create_posix.c \
    src/core/ext/transport/inproc/inproc_plugin.c \
    src/core/ext/transport/inproc/inproc_transport.c \
    src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.c \
    src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.c \
    src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel_secure.c \
    src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.c \
    src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.c \
    src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.c \
    third_party/nanopb/pb_common.c \
    third_party/nanopb/pb_decode.c \
    third_party/nanopb/pb_encode.c \
    src/core/ext/filters/client_channel/resolver/fake/fake_resolver.c \
    src/core/ext/filters/client_channel/lb_policy/pick_first/pick_first.c \
    src/core/ext/filters/client_channel/lb_policy/round_robin/round_robin.c \
    src/core/ext/filters/client_channel/resolver/dns/c_ares/dns_resolver_ares.c \
    src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_posix.c \
    src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.c \
    src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_fallback.c \
    src/core/ext/filters/client_channel/resolver/dns/native/dns_resolver.c \
    src/core/ext/filters/client_channel/resolver/sockaddr/sockaddr_resolver.c \
    src/core/ext/filters/load_reporting/load_reporting.c \
    src/core/ext/filters/load_reporting/load_reporting_filter.c \
    src/core/ext/census/base_resources.c \
    src/core/ext/census/context.c \
    src/core/ext/census/gen/census.pb.c \
    src/core/ext/census/gen/trace_context.pb.c \
    src/core/ext/census/grpc_context.c \
    src/core/ext/census/grpc_filter.c \
    src/core/ext/census/grpc_plugin.c \
    src/core/ext/census/initialize.c \
    src/core/ext/census/intrusive_hash_map.c \
    src/core/ext/census/mlog.c \
    src/core/ext/census/operation.c \
    src/core/ext/census/placeholders.c \
    src/core/ext/census/resource.c \
    src/core/ext/census/trace_context.c \
    src/core/ext/census/tracing.c \
    src/core/ext/filters/max_age/max_age_filter.c \
    src/core/ext/filters/message_size/message_size_filter.c \
    src/core/ext/filters/workarounds/workaround_cronet_compression_filter.c \
    src/core/ext/filters/workarounds/workaround_utils.c \
    src/core/plugin_registry/grpc_plugin_registry.c \

PUBLIC_HEADERS_C += \
    include/grpc/impl/codegen/byte_buffer_reader.h \
    include/grpc/impl/codegen/compression_types.h \
    include/grpc/impl/codegen/connectivity_state.h \
    include/grpc/impl/codegen/exec_ctx_fwd.h \
    include/grpc/impl/codegen/grpc_types.h \
    include/grpc/impl/codegen/propagation_bits.h \
    include/grpc/impl/codegen/slice.h \
    include/grpc/impl/codegen/status.h \
    include/grpc/impl/codegen/atm.h \
    include/grpc/impl/codegen/atm_gcc_atomic.h \
    include/grpc/impl/codegen/atm_gcc_sync.h \
    include/grpc/impl/codegen/atm_windows.h \
    include/grpc/impl/codegen/gpr_slice.h \
    include/grpc/impl/codegen/gpr_types.h \
    include/grpc/impl/codegen/port_platform.h \
    include/grpc/impl/codegen/sync.h \
    include/grpc/impl/codegen/sync_generic.h \
    include/grpc/impl/codegen/sync_posix.h \
    include/grpc/impl/codegen/sync_windows.h \
    include/grpc/grpc_security.h \
    include/grpc/byte_buffer.h \
    include/grpc/byte_buffer_reader.h \
    include/grpc/compression.h \
    include/grpc/grpc.h \
    include/grpc/grpc_posix.h \
    include/grpc/grpc_security_constants.h \
    include/grpc/load_reporting.h \
    include/grpc/slice.h \
    include/grpc/slice_buffer.h \
    include/grpc/status.h \
    include/grpc/support/workaround_list.h \
    include/grpc/census.h \

LIBGRPC_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libgrpc.a: openssl_dep_error

$(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): openssl_dep_error

else


$(LIBDIR)/$(CONFIG)/libgrpc.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(LIBGRPC_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS)  $(CARES_MERGE_OBJS)  $(OPENSSL_MERGE_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBGRPC_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS)  $(CARES_MERGE_OBJS)  $(OPENSSL_MERGE_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc.a
endif



ifeq ($(SYSTEM),MINGW32)
$(LIBDIR)/$(CONFIG)/grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): $(LIBGRPC_OBJS)  $(ZLIB_DEP) $(CARES_DEP) $(LIBDIR)/$(CONFIG)/libgpr.a $(OPENSSL_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,--output-def=$(LIBDIR)/$(CONFIG)/grpc$(SHARED_VERSION_CORE).def -Wl,--out-implib=$(LIBDIR)/$(CONFIG)/libgrpc$(SHARED_VERSION_CORE)-dll.a -o $(LIBDIR)/$(CONFIG)/grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGRPC_OBJS) $(LIBDIR)/$(CONFIG)/libgpr.a $(OPENSSL_MERGE_LIBS) $(LDLIBS_SECURE) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBS)
else
$(LIBDIR)/$(CONFIG)/libgrpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): $(LIBGRPC_OBJS)  $(ZLIB_DEP) $(CARES_DEP) $(LIBDIR)/$(CONFIG)/libgpr.a $(OPENSSL_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
ifeq ($(SYSTEM),Darwin)
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -install_name $(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) -dynamiclib -o $(LIBDIR)/$(CONFIG)/libgrpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGRPC_OBJS) $(LIBDIR)/$(CONFIG)/libgpr.a $(OPENSSL_MERGE_LIBS) $(LDLIBS_SECURE) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBS)
else
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,-soname,libgrpc.so.4 -o $(LIBDIR)/$(CONFIG)/libgrpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGRPC_OBJS) $(LIBDIR)/$(CONFIG)/libgpr.a $(OPENSSL_MERGE_LIBS) $(LDLIBS_SECURE) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBS)
	$(Q) ln -sf $(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/libgrpc$(SHARED_VERSION_CORE).so.4
	$(Q) ln -sf $(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/libgrpc$(SHARED_VERSION_CORE).so
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
    src/core/lib/channel/connected_channel.c \
    src/core/lib/channel/handshaker.c \
    src/core/lib/channel/handshaker_factory.c \
    src/core/lib/channel/handshaker_registry.c \
    src/core/lib/compression/compression.c \
    src/core/lib/compression/message_compress.c \
    src/core/lib/compression/stream_compression.c \
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
    src/core/lib/iomgr/ev_epoll1_linux.c \
    src/core/lib/iomgr/ev_epoll_limited_pollers_linux.c \
    src/core/lib/iomgr/ev_epoll_thread_pool_linux.c \
    src/core/lib/iomgr/ev_epollex_linux.c \
    src/core/lib/iomgr/ev_epollsig_linux.c \
    src/core/lib/iomgr/ev_poll_posix.c \
    src/core/lib/iomgr/ev_posix.c \
    src/core/lib/iomgr/ev_windows.c \
    src/core/lib/iomgr/exec_ctx.c \
    src/core/lib/iomgr/executor.c \
    src/core/lib/iomgr/gethostname_fallback.c \
    src/core/lib/iomgr/gethostname_host_name_max.c \
    src/core/lib/iomgr/gethostname_sysconf.c \
    src/core/lib/iomgr/iocp_windows.c \
    src/core/lib/iomgr/iomgr.c \
    src/core/lib/iomgr/iomgr_posix.c \
    src/core/lib/iomgr/iomgr_uv.c \
    src/core/lib/iomgr/iomgr_windows.c \
    src/core/lib/iomgr/is_epollexclusive_available.c \
    src/core/lib/iomgr/load_file.c \
    src/core/lib/iomgr/lockfree_event.c \
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
    src/core/lib/iomgr/socket_factory_posix.c \
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
    src/core/lib/iomgr/tcp_server_utils_posix_common.c \
    src/core/lib/iomgr/tcp_server_utils_posix_ifaddrs.c \
    src/core/lib/iomgr/tcp_server_utils_posix_noifaddrs.c \
    src/core/lib/iomgr/tcp_server_uv.c \
    src/core/lib/iomgr/tcp_server_windows.c \
    src/core/lib/iomgr/tcp_uv.c \
    src/core/lib/iomgr/tcp_windows.c \
    src/core/lib/iomgr/time_averaged_stats.c \
    src/core/lib/iomgr/timer_generic.c \
    src/core/lib/iomgr/timer_heap.c \
    src/core/lib/iomgr/timer_manager.c \
    src/core/lib/iomgr/timer_uv.c \
    src/core/lib/iomgr/udp_server.c \
    src/core/lib/iomgr/unix_sockets_posix.c \
    src/core/lib/iomgr/unix_sockets_posix_noop.c \
    src/core/lib/iomgr/wakeup_fd_cv.c \
    src/core/lib/iomgr/wakeup_fd_eventfd.c \
    src/core/lib/iomgr/wakeup_fd_nospecial.c \
    src/core/lib/iomgr/wakeup_fd_pipe.c \
    src/core/lib/iomgr/wakeup_fd_posix.c \
    src/core/lib/json/json.c \
    src/core/lib/json/json_reader.c \
    src/core/lib/json/json_string.c \
    src/core/lib/json/json_writer.c \
    src/core/lib/slice/b64.c \
    src/core/lib/slice/percent_encoding.c \
    src/core/lib/slice/slice.c \
    src/core/lib/slice/slice_buffer.c \
    src/core/lib/slice/slice_hash_table.c \
    src/core/lib/slice/slice_intern.c \
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
    src/core/lib/surface/completion_queue_factory.c \
    src/core/lib/surface/event_string.c \
    src/core/lib/surface/lame_client.cc \
    src/core/lib/surface/metadata_array.c \
    src/core/lib/surface/server.c \
    src/core/lib/surface/validate_metadata.c \
    src/core/lib/surface/version.c \
    src/core/lib/transport/bdp_estimator.c \
    src/core/lib/transport/byte_stream.c \
    src/core/lib/transport/connectivity_state.c \
    src/core/lib/transport/error_utils.c \
    src/core/lib/transport/metadata.c \
    src/core/lib/transport/metadata_batch.c \
    src/core/lib/transport/pid_controller.c \
    src/core/lib/transport/service_config.c \
    src/core/lib/transport/static_metadata.c \
    src/core/lib/transport/status_conversion.c \
    src/core/lib/transport/timeout_encoding.c \
    src/core/lib/transport/transport.c \
    src/core/lib/transport/transport_op_string.c \
    src/core/lib/debug/trace.c \
    src/core/ext/transport/cronet/client/secure/cronet_channel_create.c \
    src/core/ext/transport/cronet/transport/cronet_api_dummy.c \
    src/core/ext/transport/cronet/transport/cronet_transport.c \
    src/core/ext/transport/chttp2/client/secure/secure_channel_create.c \
    src/core/ext/transport/chttp2/transport/bin_decoder.c \
    src/core/ext/transport/chttp2/transport/bin_encoder.c \
    src/core/ext/transport/chttp2/transport/chttp2_plugin.c \
    src/core/ext/transport/chttp2/transport/chttp2_transport.c \
    src/core/ext/transport/chttp2/transport/flow_control.c \
    src/core/ext/transport/chttp2/transport/frame_data.c \
    src/core/ext/transport/chttp2/transport/frame_goaway.c \
    src/core/ext/transport/chttp2/transport/frame_ping.c \
    src/core/ext/transport/chttp2/transport/frame_rst_stream.c \
    src/core/ext/transport/chttp2/transport/frame_settings.c \
    src/core/ext/transport/chttp2/transport/frame_window_update.c \
    src/core/ext/transport/chttp2/transport/hpack_encoder.c \
    src/core/ext/transport/chttp2/transport/hpack_parser.c \
    src/core/ext/transport/chttp2/transport/hpack_table.c \
    src/core/ext/transport/chttp2/transport/http2_settings.c \
    src/core/ext/transport/chttp2/transport/huffsyms.c \
    src/core/ext/transport/chttp2/transport/incoming_metadata.c \
    src/core/ext/transport/chttp2/transport/parsing.c \
    src/core/ext/transport/chttp2/transport/stream_lists.c \
    src/core/ext/transport/chttp2/transport/stream_map.c \
    src/core/ext/transport/chttp2/transport/varint.c \
    src/core/ext/transport/chttp2/transport/writing.c \
    src/core/ext/transport/chttp2/alpn/alpn.c \
    src/core/ext/filters/http/client/http_client_filter.c \
    src/core/ext/filters/http/http_filters_plugin.c \
    src/core/ext/filters/http/message_compress/message_compress_filter.c \
    src/core/ext/filters/http/server/http_server_filter.c \
    src/core/ext/filters/client_channel/channel_connectivity.c \
    src/core/ext/filters/client_channel/client_channel.c \
    src/core/ext/filters/client_channel/client_channel_factory.c \
    src/core/ext/filters/client_channel/client_channel_plugin.c \
    src/core/ext/filters/client_channel/connector.c \
    src/core/ext/filters/client_channel/http_connect_handshaker.c \
    src/core/ext/filters/client_channel/http_proxy.c \
    src/core/ext/filters/client_channel/lb_policy.c \
    src/core/ext/filters/client_channel/lb_policy_factory.c \
    src/core/ext/filters/client_channel/lb_policy_registry.c \
    src/core/ext/filters/client_channel/parse_address.c \
    src/core/ext/filters/client_channel/proxy_mapper.c \
    src/core/ext/filters/client_channel/proxy_mapper_registry.c \
    src/core/ext/filters/client_channel/resolver.c \
    src/core/ext/filters/client_channel/resolver_factory.c \
    src/core/ext/filters/client_channel/resolver_registry.c \
    src/core/ext/filters/client_channel/retry_throttle.c \
    src/core/ext/filters/client_channel/subchannel.c \
    src/core/ext/filters/client_channel/subchannel_index.c \
    src/core/ext/filters/client_channel/uri_parser.c \
    src/core/ext/filters/deadline/deadline_filter.c \
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
    src/core/lib/security/transport/lb_targets_info.c \
    src/core/lib/security/transport/secure_endpoint.c \
    src/core/lib/security/transport/security_connector.c \
    src/core/lib/security/transport/security_handshaker.c \
    src/core/lib/security/transport/server_auth_filter.c \
    src/core/lib/security/transport/tsi_error.c \
    src/core/lib/security/util/json_util.c \
    src/core/lib/surface/init_secure.c \
    src/core/tsi/fake_transport_security.c \
    src/core/tsi/gts_transport_security.c \
    src/core/tsi/ssl_transport_security.c \
    src/core/tsi/transport_security.c \
    src/core/tsi/transport_security_adapter.c \
    src/core/tsi/transport_security_grpc.c \
    src/core/ext/transport/chttp2/client/chttp2_connector.c \
    src/core/ext/filters/load_reporting/load_reporting.c \
    src/core/ext/filters/load_reporting/load_reporting_filter.c \
    src/core/plugin_registry/grpc_cronet_plugin_registry.c \

PUBLIC_HEADERS_C += \
    include/grpc/impl/codegen/byte_buffer_reader.h \
    include/grpc/impl/codegen/compression_types.h \
    include/grpc/impl/codegen/connectivity_state.h \
    include/grpc/impl/codegen/exec_ctx_fwd.h \
    include/grpc/impl/codegen/grpc_types.h \
    include/grpc/impl/codegen/propagation_bits.h \
    include/grpc/impl/codegen/slice.h \
    include/grpc/impl/codegen/status.h \
    include/grpc/impl/codegen/atm.h \
    include/grpc/impl/codegen/atm_gcc_atomic.h \
    include/grpc/impl/codegen/atm_gcc_sync.h \
    include/grpc/impl/codegen/atm_windows.h \
    include/grpc/impl/codegen/gpr_slice.h \
    include/grpc/impl/codegen/gpr_types.h \
    include/grpc/impl/codegen/port_platform.h \
    include/grpc/impl/codegen/sync.h \
    include/grpc/impl/codegen/sync_generic.h \
    include/grpc/impl/codegen/sync_posix.h \
    include/grpc/impl/codegen/sync_windows.h \
    include/grpc/grpc_cronet.h \
    include/grpc/grpc_security.h \
    include/grpc/grpc_security_constants.h \

LIBGRPC_CRONET_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC_CRONET_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libgrpc_cronet.a: openssl_dep_error

$(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): openssl_dep_error

else


$(LIBDIR)/$(CONFIG)/libgrpc_cronet.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(LIBGRPC_CRONET_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS)  $(CARES_MERGE_OBJS)  $(OPENSSL_MERGE_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc_cronet.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc_cronet.a $(LIBGRPC_CRONET_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS)  $(CARES_MERGE_OBJS)  $(OPENSSL_MERGE_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc_cronet.a
endif



ifeq ($(SYSTEM),MINGW32)
$(LIBDIR)/$(CONFIG)/grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): $(LIBGRPC_CRONET_OBJS)  $(ZLIB_DEP) $(CARES_DEP) $(LIBDIR)/$(CONFIG)/libgpr.a $(OPENSSL_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,--output-def=$(LIBDIR)/$(CONFIG)/grpc_cronet$(SHARED_VERSION_CORE).def -Wl,--out-implib=$(LIBDIR)/$(CONFIG)/libgrpc_cronet$(SHARED_VERSION_CORE)-dll.a -o $(LIBDIR)/$(CONFIG)/grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGRPC_CRONET_OBJS) $(LIBDIR)/$(CONFIG)/libgpr.a $(OPENSSL_MERGE_LIBS) $(LDLIBS_SECURE) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBS)
else
$(LIBDIR)/$(CONFIG)/libgrpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): $(LIBGRPC_CRONET_OBJS)  $(ZLIB_DEP) $(CARES_DEP) $(LIBDIR)/$(CONFIG)/libgpr.a $(OPENSSL_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
ifeq ($(SYSTEM),Darwin)
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -install_name $(SHARED_PREFIX)grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) -dynamiclib -o $(LIBDIR)/$(CONFIG)/libgrpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGRPC_CRONET_OBJS) $(LIBDIR)/$(CONFIG)/libgpr.a $(OPENSSL_MERGE_LIBS) $(LDLIBS_SECURE) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBS)
else
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,-soname,libgrpc_cronet.so.4 -o $(LIBDIR)/$(CONFIG)/libgrpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGRPC_CRONET_OBJS) $(LIBDIR)/$(CONFIG)/libgpr.a $(OPENSSL_MERGE_LIBS) $(LDLIBS_SECURE) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBS)
	$(Q) ln -sf $(SHARED_PREFIX)grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/libgrpc_cronet$(SHARED_VERSION_CORE).so.4
	$(Q) ln -sf $(SHARED_PREFIX)grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/libgrpc_cronet$(SHARED_VERSION_CORE).so
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
    src/core/ext/filters/client_channel/resolver/fake/fake_resolver.c \
    test/core/end2end/cq_verifier.c \
    test/core/end2end/fixtures/http_proxy_fixture.c \
    test/core/end2end/fixtures/proxy.c \
    test/core/iomgr/endpoint_tests.c \
    test/core/util/debugger_macros.c \
    test/core/util/grpc_profiler.c \
    test/core/util/memory_counters.c \
    test/core/util/mock_endpoint.c \
    test/core/util/parse_hexstring.c \
    test/core/util/passthru_endpoint.c \
    test/core/util/port.c \
    test/core/util/port_server_client.c \
    test/core/util/slice_splitter.c \
    test/core/util/trickle_endpoint.c \
    src/core/lib/channel/channel_args.c \
    src/core/lib/channel/channel_stack.c \
    src/core/lib/channel/channel_stack_builder.c \
    src/core/lib/channel/connected_channel.c \
    src/core/lib/channel/handshaker.c \
    src/core/lib/channel/handshaker_factory.c \
    src/core/lib/channel/handshaker_registry.c \
    src/core/lib/compression/compression.c \
    src/core/lib/compression/message_compress.c \
    src/core/lib/compression/stream_compression.c \
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
    src/core/lib/iomgr/ev_epoll1_linux.c \
    src/core/lib/iomgr/ev_epoll_limited_pollers_linux.c \
    src/core/lib/iomgr/ev_epoll_thread_pool_linux.c \
    src/core/lib/iomgr/ev_epollex_linux.c \
    src/core/lib/iomgr/ev_epollsig_linux.c \
    src/core/lib/iomgr/ev_poll_posix.c \
    src/core/lib/iomgr/ev_posix.c \
    src/core/lib/iomgr/ev_windows.c \
    src/core/lib/iomgr/exec_ctx.c \
    src/core/lib/iomgr/executor.c \
    src/core/lib/iomgr/gethostname_fallback.c \
    src/core/lib/iomgr/gethostname_host_name_max.c \
    src/core/lib/iomgr/gethostname_sysconf.c \
    src/core/lib/iomgr/iocp_windows.c \
    src/core/lib/iomgr/iomgr.c \
    src/core/lib/iomgr/iomgr_posix.c \
    src/core/lib/iomgr/iomgr_uv.c \
    src/core/lib/iomgr/iomgr_windows.c \
    src/core/lib/iomgr/is_epollexclusive_available.c \
    src/core/lib/iomgr/load_file.c \
    src/core/lib/iomgr/lockfree_event.c \
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
    src/core/lib/iomgr/socket_factory_posix.c \
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
    src/core/lib/iomgr/tcp_server_utils_posix_common.c \
    src/core/lib/iomgr/tcp_server_utils_posix_ifaddrs.c \
    src/core/lib/iomgr/tcp_server_utils_posix_noifaddrs.c \
    src/core/lib/iomgr/tcp_server_uv.c \
    src/core/lib/iomgr/tcp_server_windows.c \
    src/core/lib/iomgr/tcp_uv.c \
    src/core/lib/iomgr/tcp_windows.c \
    src/core/lib/iomgr/time_averaged_stats.c \
    src/core/lib/iomgr/timer_generic.c \
    src/core/lib/iomgr/timer_heap.c \
    src/core/lib/iomgr/timer_manager.c \
    src/core/lib/iomgr/timer_uv.c \
    src/core/lib/iomgr/udp_server.c \
    src/core/lib/iomgr/unix_sockets_posix.c \
    src/core/lib/iomgr/unix_sockets_posix_noop.c \
    src/core/lib/iomgr/wakeup_fd_cv.c \
    src/core/lib/iomgr/wakeup_fd_eventfd.c \
    src/core/lib/iomgr/wakeup_fd_nospecial.c \
    src/core/lib/iomgr/wakeup_fd_pipe.c \
    src/core/lib/iomgr/wakeup_fd_posix.c \
    src/core/lib/json/json.c \
    src/core/lib/json/json_reader.c \
    src/core/lib/json/json_string.c \
    src/core/lib/json/json_writer.c \
    src/core/lib/slice/b64.c \
    src/core/lib/slice/percent_encoding.c \
    src/core/lib/slice/slice.c \
    src/core/lib/slice/slice_buffer.c \
    src/core/lib/slice/slice_hash_table.c \
    src/core/lib/slice/slice_intern.c \
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
    src/core/lib/surface/completion_queue_factory.c \
    src/core/lib/surface/event_string.c \
    src/core/lib/surface/lame_client.cc \
    src/core/lib/surface/metadata_array.c \
    src/core/lib/surface/server.c \
    src/core/lib/surface/validate_metadata.c \
    src/core/lib/surface/version.c \
    src/core/lib/transport/bdp_estimator.c \
    src/core/lib/transport/byte_stream.c \
    src/core/lib/transport/connectivity_state.c \
    src/core/lib/transport/error_utils.c \
    src/core/lib/transport/metadata.c \
    src/core/lib/transport/metadata_batch.c \
    src/core/lib/transport/pid_controller.c \
    src/core/lib/transport/service_config.c \
    src/core/lib/transport/static_metadata.c \
    src/core/lib/transport/status_conversion.c \
    src/core/lib/transport/timeout_encoding.c \
    src/core/lib/transport/transport.c \
    src/core/lib/transport/transport_op_string.c \
    src/core/lib/debug/trace.c \
    src/core/ext/filters/client_channel/channel_connectivity.c \
    src/core/ext/filters/client_channel/client_channel.c \
    src/core/ext/filters/client_channel/client_channel_factory.c \
    src/core/ext/filters/client_channel/client_channel_plugin.c \
    src/core/ext/filters/client_channel/connector.c \
    src/core/ext/filters/client_channel/http_connect_handshaker.c \
    src/core/ext/filters/client_channel/http_proxy.c \
    src/core/ext/filters/client_channel/lb_policy.c \
    src/core/ext/filters/client_channel/lb_policy_factory.c \
    src/core/ext/filters/client_channel/lb_policy_registry.c \
    src/core/ext/filters/client_channel/parse_address.c \
    src/core/ext/filters/client_channel/proxy_mapper.c \
    src/core/ext/filters/client_channel/proxy_mapper_registry.c \
    src/core/ext/filters/client_channel/resolver.c \
    src/core/ext/filters/client_channel/resolver_factory.c \
    src/core/ext/filters/client_channel/resolver_registry.c \
    src/core/ext/filters/client_channel/retry_throttle.c \
    src/core/ext/filters/client_channel/subchannel.c \
    src/core/ext/filters/client_channel/subchannel_index.c \
    src/core/ext/filters/client_channel/uri_parser.c \
    src/core/ext/filters/deadline/deadline_filter.c \
    src/core/ext/transport/chttp2/transport/bin_decoder.c \
    src/core/ext/transport/chttp2/transport/bin_encoder.c \
    src/core/ext/transport/chttp2/transport/chttp2_plugin.c \
    src/core/ext/transport/chttp2/transport/chttp2_transport.c \
    src/core/ext/transport/chttp2/transport/flow_control.c \
    src/core/ext/transport/chttp2/transport/frame_data.c \
    src/core/ext/transport/chttp2/transport/frame_goaway.c \
    src/core/ext/transport/chttp2/transport/frame_ping.c \
    src/core/ext/transport/chttp2/transport/frame_rst_stream.c \
    src/core/ext/transport/chttp2/transport/frame_settings.c \
    src/core/ext/transport/chttp2/transport/frame_window_update.c \
    src/core/ext/transport/chttp2/transport/hpack_encoder.c \
    src/core/ext/transport/chttp2/transport/hpack_parser.c \
    src/core/ext/transport/chttp2/transport/hpack_table.c \
    src/core/ext/transport/chttp2/transport/http2_settings.c \
    src/core/ext/transport/chttp2/transport/huffsyms.c \
    src/core/ext/transport/chttp2/transport/incoming_metadata.c \
    src/core/ext/transport/chttp2/transport/parsing.c \
    src/core/ext/transport/chttp2/transport/stream_lists.c \
    src/core/ext/transport/chttp2/transport/stream_map.c \
    src/core/ext/transport/chttp2/transport/varint.c \
    src/core/ext/transport/chttp2/transport/writing.c \
    src/core/ext/transport/chttp2/alpn/alpn.c \
    src/core/ext/filters/http/client/http_client_filter.c \
    src/core/ext/filters/http/http_filters_plugin.c \
    src/core/ext/filters/http/message_compress/message_compress_filter.c \
    src/core/ext/filters/http/server/http_server_filter.c \

PUBLIC_HEADERS_C += \
    include/grpc/impl/codegen/byte_buffer_reader.h \
    include/grpc/impl/codegen/compression_types.h \
    include/grpc/impl/codegen/connectivity_state.h \
    include/grpc/impl/codegen/exec_ctx_fwd.h \
    include/grpc/impl/codegen/grpc_types.h \
    include/grpc/impl/codegen/propagation_bits.h \
    include/grpc/impl/codegen/slice.h \
    include/grpc/impl/codegen/status.h \
    include/grpc/impl/codegen/atm.h \
    include/grpc/impl/codegen/atm_gcc_atomic.h \
    include/grpc/impl/codegen/atm_gcc_sync.h \
    include/grpc/impl/codegen/atm_windows.h \
    include/grpc/impl/codegen/gpr_slice.h \
    include/grpc/impl/codegen/gpr_types.h \
    include/grpc/impl/codegen/port_platform.h \
    include/grpc/impl/codegen/sync.h \
    include/grpc/impl/codegen/sync_generic.h \
    include/grpc/impl/codegen/sync_posix.h \
    include/grpc/impl/codegen/sync_windows.h \

LIBGRPC_TEST_UTIL_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC_TEST_UTIL_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libgrpc_test_util.a: openssl_dep_error


else


$(LIBDIR)/$(CONFIG)/libgrpc_test_util.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(LIBGRPC_TEST_UTIL_OBJS) 
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
    src/core/ext/filters/client_channel/resolver/fake/fake_resolver.c \
    test/core/end2end/cq_verifier.c \
    test/core/end2end/fixtures/http_proxy_fixture.c \
    test/core/end2end/fixtures/proxy.c \
    test/core/iomgr/endpoint_tests.c \
    test/core/util/debugger_macros.c \
    test/core/util/grpc_profiler.c \
    test/core/util/memory_counters.c \
    test/core/util/mock_endpoint.c \
    test/core/util/parse_hexstring.c \
    test/core/util/passthru_endpoint.c \
    test/core/util/port.c \
    test/core/util/port_server_client.c \
    test/core/util/slice_splitter.c \
    test/core/util/trickle_endpoint.c \
    src/core/lib/channel/channel_args.c \
    src/core/lib/channel/channel_stack.c \
    src/core/lib/channel/channel_stack_builder.c \
    src/core/lib/channel/connected_channel.c \
    src/core/lib/channel/handshaker.c \
    src/core/lib/channel/handshaker_factory.c \
    src/core/lib/channel/handshaker_registry.c \
    src/core/lib/compression/compression.c \
    src/core/lib/compression/message_compress.c \
    src/core/lib/compression/stream_compression.c \
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
    src/core/lib/iomgr/ev_epoll1_linux.c \
    src/core/lib/iomgr/ev_epoll_limited_pollers_linux.c \
    src/core/lib/iomgr/ev_epoll_thread_pool_linux.c \
    src/core/lib/iomgr/ev_epollex_linux.c \
    src/core/lib/iomgr/ev_epollsig_linux.c \
    src/core/lib/iomgr/ev_poll_posix.c \
    src/core/lib/iomgr/ev_posix.c \
    src/core/lib/iomgr/ev_windows.c \
    src/core/lib/iomgr/exec_ctx.c \
    src/core/lib/iomgr/executor.c \
    src/core/lib/iomgr/gethostname_fallback.c \
    src/core/lib/iomgr/gethostname_host_name_max.c \
    src/core/lib/iomgr/gethostname_sysconf.c \
    src/core/lib/iomgr/iocp_windows.c \
    src/core/lib/iomgr/iomgr.c \
    src/core/lib/iomgr/iomgr_posix.c \
    src/core/lib/iomgr/iomgr_uv.c \
    src/core/lib/iomgr/iomgr_windows.c \
    src/core/lib/iomgr/is_epollexclusive_available.c \
    src/core/lib/iomgr/load_file.c \
    src/core/lib/iomgr/lockfree_event.c \
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
    src/core/lib/iomgr/socket_factory_posix.c \
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
    src/core/lib/iomgr/tcp_server_utils_posix_common.c \
    src/core/lib/iomgr/tcp_server_utils_posix_ifaddrs.c \
    src/core/lib/iomgr/tcp_server_utils_posix_noifaddrs.c \
    src/core/lib/iomgr/tcp_server_uv.c \
    src/core/lib/iomgr/tcp_server_windows.c \
    src/core/lib/iomgr/tcp_uv.c \
    src/core/lib/iomgr/tcp_windows.c \
    src/core/lib/iomgr/time_averaged_stats.c \
    src/core/lib/iomgr/timer_generic.c \
    src/core/lib/iomgr/timer_heap.c \
    src/core/lib/iomgr/timer_manager.c \
    src/core/lib/iomgr/timer_uv.c \
    src/core/lib/iomgr/udp_server.c \
    src/core/lib/iomgr/unix_sockets_posix.c \
    src/core/lib/iomgr/unix_sockets_posix_noop.c \
    src/core/lib/iomgr/wakeup_fd_cv.c \
    src/core/lib/iomgr/wakeup_fd_eventfd.c \
    src/core/lib/iomgr/wakeup_fd_nospecial.c \
    src/core/lib/iomgr/wakeup_fd_pipe.c \
    src/core/lib/iomgr/wakeup_fd_posix.c \
    src/core/lib/json/json.c \
    src/core/lib/json/json_reader.c \
    src/core/lib/json/json_string.c \
    src/core/lib/json/json_writer.c \
    src/core/lib/slice/b64.c \
    src/core/lib/slice/percent_encoding.c \
    src/core/lib/slice/slice.c \
    src/core/lib/slice/slice_buffer.c \
    src/core/lib/slice/slice_hash_table.c \
    src/core/lib/slice/slice_intern.c \
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
    src/core/lib/surface/completion_queue_factory.c \
    src/core/lib/surface/event_string.c \
    src/core/lib/surface/lame_client.cc \
    src/core/lib/surface/metadata_array.c \
    src/core/lib/surface/server.c \
    src/core/lib/surface/validate_metadata.c \
    src/core/lib/surface/version.c \
    src/core/lib/transport/bdp_estimator.c \
    src/core/lib/transport/byte_stream.c \
    src/core/lib/transport/connectivity_state.c \
    src/core/lib/transport/error_utils.c \
    src/core/lib/transport/metadata.c \
    src/core/lib/transport/metadata_batch.c \
    src/core/lib/transport/pid_controller.c \
    src/core/lib/transport/service_config.c \
    src/core/lib/transport/static_metadata.c \
    src/core/lib/transport/status_conversion.c \
    src/core/lib/transport/timeout_encoding.c \
    src/core/lib/transport/transport.c \
    src/core/lib/transport/transport_op_string.c \
    src/core/lib/debug/trace.c \
    src/core/ext/filters/client_channel/channel_connectivity.c \
    src/core/ext/filters/client_channel/client_channel.c \
    src/core/ext/filters/client_channel/client_channel_factory.c \
    src/core/ext/filters/client_channel/client_channel_plugin.c \
    src/core/ext/filters/client_channel/connector.c \
    src/core/ext/filters/client_channel/http_connect_handshaker.c \
    src/core/ext/filters/client_channel/http_proxy.c \
    src/core/ext/filters/client_channel/lb_policy.c \
    src/core/ext/filters/client_channel/lb_policy_factory.c \
    src/core/ext/filters/client_channel/lb_policy_registry.c \
    src/core/ext/filters/client_channel/parse_address.c \
    src/core/ext/filters/client_channel/proxy_mapper.c \
    src/core/ext/filters/client_channel/proxy_mapper_registry.c \
    src/core/ext/filters/client_channel/resolver.c \
    src/core/ext/filters/client_channel/resolver_factory.c \
    src/core/ext/filters/client_channel/resolver_registry.c \
    src/core/ext/filters/client_channel/retry_throttle.c \
    src/core/ext/filters/client_channel/subchannel.c \
    src/core/ext/filters/client_channel/subchannel_index.c \
    src/core/ext/filters/client_channel/uri_parser.c \
    src/core/ext/filters/deadline/deadline_filter.c \
    src/core/ext/transport/chttp2/transport/bin_decoder.c \
    src/core/ext/transport/chttp2/transport/bin_encoder.c \
    src/core/ext/transport/chttp2/transport/chttp2_plugin.c \
    src/core/ext/transport/chttp2/transport/chttp2_transport.c \
    src/core/ext/transport/chttp2/transport/flow_control.c \
    src/core/ext/transport/chttp2/transport/frame_data.c \
    src/core/ext/transport/chttp2/transport/frame_goaway.c \
    src/core/ext/transport/chttp2/transport/frame_ping.c \
    src/core/ext/transport/chttp2/transport/frame_rst_stream.c \
    src/core/ext/transport/chttp2/transport/frame_settings.c \
    src/core/ext/transport/chttp2/transport/frame_window_update.c \
    src/core/ext/transport/chttp2/transport/hpack_encoder.c \
    src/core/ext/transport/chttp2/transport/hpack_parser.c \
    src/core/ext/transport/chttp2/transport/hpack_table.c \
    src/core/ext/transport/chttp2/transport/http2_settings.c \
    src/core/ext/transport/chttp2/transport/huffsyms.c \
    src/core/ext/transport/chttp2/transport/incoming_metadata.c \
    src/core/ext/transport/chttp2/transport/parsing.c \
    src/core/ext/transport/chttp2/transport/stream_lists.c \
    src/core/ext/transport/chttp2/transport/stream_map.c \
    src/core/ext/transport/chttp2/transport/varint.c \
    src/core/ext/transport/chttp2/transport/writing.c \
    src/core/ext/transport/chttp2/alpn/alpn.c \
    src/core/ext/filters/http/client/http_client_filter.c \
    src/core/ext/filters/http/http_filters_plugin.c \
    src/core/ext/filters/http/message_compress/message_compress_filter.c \
    src/core/ext/filters/http/server/http_server_filter.c \

PUBLIC_HEADERS_C += \
    include/grpc/impl/codegen/byte_buffer_reader.h \
    include/grpc/impl/codegen/compression_types.h \
    include/grpc/impl/codegen/connectivity_state.h \
    include/grpc/impl/codegen/exec_ctx_fwd.h \
    include/grpc/impl/codegen/grpc_types.h \
    include/grpc/impl/codegen/propagation_bits.h \
    include/grpc/impl/codegen/slice.h \
    include/grpc/impl/codegen/status.h \
    include/grpc/impl/codegen/atm.h \
    include/grpc/impl/codegen/atm_gcc_atomic.h \
    include/grpc/impl/codegen/atm_gcc_sync.h \
    include/grpc/impl/codegen/atm_windows.h \
    include/grpc/impl/codegen/gpr_slice.h \
    include/grpc/impl/codegen/gpr_types.h \
    include/grpc/impl/codegen/port_platform.h \
    include/grpc/impl/codegen/sync.h \
    include/grpc/impl/codegen/sync_generic.h \
    include/grpc/impl/codegen/sync_posix.h \
    include/grpc/impl/codegen/sync_windows.h \

LIBGRPC_TEST_UTIL_UNSECURE_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC_TEST_UTIL_UNSECURE_SRC))))


$(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a: $(ZLIB_DEP) $(CARES_DEP)  $(LIBGRPC_TEST_UTIL_UNSECURE_OBJS) 
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
    src/core/lib/channel/connected_channel.c \
    src/core/lib/channel/handshaker.c \
    src/core/lib/channel/handshaker_factory.c \
    src/core/lib/channel/handshaker_registry.c \
    src/core/lib/compression/compression.c \
    src/core/lib/compression/message_compress.c \
    src/core/lib/compression/stream_compression.c \
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
    src/core/lib/iomgr/ev_epoll1_linux.c \
    src/core/lib/iomgr/ev_epoll_limited_pollers_linux.c \
    src/core/lib/iomgr/ev_epoll_thread_pool_linux.c \
    src/core/lib/iomgr/ev_epollex_linux.c \
    src/core/lib/iomgr/ev_epollsig_linux.c \
    src/core/lib/iomgr/ev_poll_posix.c \
    src/core/lib/iomgr/ev_posix.c \
    src/core/lib/iomgr/ev_windows.c \
    src/core/lib/iomgr/exec_ctx.c \
    src/core/lib/iomgr/executor.c \
    src/core/lib/iomgr/gethostname_fallback.c \
    src/core/lib/iomgr/gethostname_host_name_max.c \
    src/core/lib/iomgr/gethostname_sysconf.c \
    src/core/lib/iomgr/iocp_windows.c \
    src/core/lib/iomgr/iomgr.c \
    src/core/lib/iomgr/iomgr_posix.c \
    src/core/lib/iomgr/iomgr_uv.c \
    src/core/lib/iomgr/iomgr_windows.c \
    src/core/lib/iomgr/is_epollexclusive_available.c \
    src/core/lib/iomgr/load_file.c \
    src/core/lib/iomgr/lockfree_event.c \
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
    src/core/lib/iomgr/socket_factory_posix.c \
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
    src/core/lib/iomgr/tcp_server_utils_posix_common.c \
    src/core/lib/iomgr/tcp_server_utils_posix_ifaddrs.c \
    src/core/lib/iomgr/tcp_server_utils_posix_noifaddrs.c \
    src/core/lib/iomgr/tcp_server_uv.c \
    src/core/lib/iomgr/tcp_server_windows.c \
    src/core/lib/iomgr/tcp_uv.c \
    src/core/lib/iomgr/tcp_windows.c \
    src/core/lib/iomgr/time_averaged_stats.c \
    src/core/lib/iomgr/timer_generic.c \
    src/core/lib/iomgr/timer_heap.c \
    src/core/lib/iomgr/timer_manager.c \
    src/core/lib/iomgr/timer_uv.c \
    src/core/lib/iomgr/udp_server.c \
    src/core/lib/iomgr/unix_sockets_posix.c \
    src/core/lib/iomgr/unix_sockets_posix_noop.c \
    src/core/lib/iomgr/wakeup_fd_cv.c \
    src/core/lib/iomgr/wakeup_fd_eventfd.c \
    src/core/lib/iomgr/wakeup_fd_nospecial.c \
    src/core/lib/iomgr/wakeup_fd_pipe.c \
    src/core/lib/iomgr/wakeup_fd_posix.c \
    src/core/lib/json/json.c \
    src/core/lib/json/json_reader.c \
    src/core/lib/json/json_string.c \
    src/core/lib/json/json_writer.c \
    src/core/lib/slice/b64.c \
    src/core/lib/slice/percent_encoding.c \
    src/core/lib/slice/slice.c \
    src/core/lib/slice/slice_buffer.c \
    src/core/lib/slice/slice_hash_table.c \
    src/core/lib/slice/slice_intern.c \
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
    src/core/lib/surface/completion_queue_factory.c \
    src/core/lib/surface/event_string.c \
    src/core/lib/surface/lame_client.cc \
    src/core/lib/surface/metadata_array.c \
    src/core/lib/surface/server.c \
    src/core/lib/surface/validate_metadata.c \
    src/core/lib/surface/version.c \
    src/core/lib/transport/bdp_estimator.c \
    src/core/lib/transport/byte_stream.c \
    src/core/lib/transport/connectivity_state.c \
    src/core/lib/transport/error_utils.c \
    src/core/lib/transport/metadata.c \
    src/core/lib/transport/metadata_batch.c \
    src/core/lib/transport/pid_controller.c \
    src/core/lib/transport/service_config.c \
    src/core/lib/transport/static_metadata.c \
    src/core/lib/transport/status_conversion.c \
    src/core/lib/transport/timeout_encoding.c \
    src/core/lib/transport/transport.c \
    src/core/lib/transport/transport_op_string.c \
    src/core/lib/debug/trace.c \
    src/core/ext/transport/chttp2/server/insecure/server_chttp2.c \
    src/core/ext/transport/chttp2/server/insecure/server_chttp2_posix.c \
    src/core/ext/transport/chttp2/transport/bin_decoder.c \
    src/core/ext/transport/chttp2/transport/bin_encoder.c \
    src/core/ext/transport/chttp2/transport/chttp2_plugin.c \
    src/core/ext/transport/chttp2/transport/chttp2_transport.c \
    src/core/ext/transport/chttp2/transport/flow_control.c \
    src/core/ext/transport/chttp2/transport/frame_data.c \
    src/core/ext/transport/chttp2/transport/frame_goaway.c \
    src/core/ext/transport/chttp2/transport/frame_ping.c \
    src/core/ext/transport/chttp2/transport/frame_rst_stream.c \
    src/core/ext/transport/chttp2/transport/frame_settings.c \
    src/core/ext/transport/chttp2/transport/frame_window_update.c \
    src/core/ext/transport/chttp2/transport/hpack_encoder.c \
    src/core/ext/transport/chttp2/transport/hpack_parser.c \
    src/core/ext/transport/chttp2/transport/hpack_table.c \
    src/core/ext/transport/chttp2/transport/http2_settings.c \
    src/core/ext/transport/chttp2/transport/huffsyms.c \
    src/core/ext/transport/chttp2/transport/incoming_metadata.c \
    src/core/ext/transport/chttp2/transport/parsing.c \
    src/core/ext/transport/chttp2/transport/stream_lists.c \
    src/core/ext/transport/chttp2/transport/stream_map.c \
    src/core/ext/transport/chttp2/transport/varint.c \
    src/core/ext/transport/chttp2/transport/writing.c \
    src/core/ext/transport/chttp2/alpn/alpn.c \
    src/core/ext/filters/http/client/http_client_filter.c \
    src/core/ext/filters/http/http_filters_plugin.c \
    src/core/ext/filters/http/message_compress/message_compress_filter.c \
    src/core/ext/filters/http/server/http_server_filter.c \
    src/core/ext/transport/chttp2/server/chttp2_server.c \
    src/core/ext/transport/chttp2/client/insecure/channel_create.c \
    src/core/ext/transport/chttp2/client/insecure/channel_create_posix.c \
    src/core/ext/transport/chttp2/client/chttp2_connector.c \
    src/core/ext/filters/client_channel/channel_connectivity.c \
    src/core/ext/filters/client_channel/client_channel.c \
    src/core/ext/filters/client_channel/client_channel_factory.c \
    src/core/ext/filters/client_channel/client_channel_plugin.c \
    src/core/ext/filters/client_channel/connector.c \
    src/core/ext/filters/client_channel/http_connect_handshaker.c \
    src/core/ext/filters/client_channel/http_proxy.c \
    src/core/ext/filters/client_channel/lb_policy.c \
    src/core/ext/filters/client_channel/lb_policy_factory.c \
    src/core/ext/filters/client_channel/lb_policy_registry.c \
    src/core/ext/filters/client_channel/parse_address.c \
    src/core/ext/filters/client_channel/proxy_mapper.c \
    src/core/ext/filters/client_channel/proxy_mapper_registry.c \
    src/core/ext/filters/client_channel/resolver.c \
    src/core/ext/filters/client_channel/resolver_factory.c \
    src/core/ext/filters/client_channel/resolver_registry.c \
    src/core/ext/filters/client_channel/retry_throttle.c \
    src/core/ext/filters/client_channel/subchannel.c \
    src/core/ext/filters/client_channel/subchannel_index.c \
    src/core/ext/filters/client_channel/uri_parser.c \
    src/core/ext/filters/deadline/deadline_filter.c \
    src/core/ext/transport/inproc/inproc_plugin.c \
    src/core/ext/transport/inproc/inproc_transport.c \
    src/core/ext/filters/client_channel/resolver/dns/c_ares/dns_resolver_ares.c \
    src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_posix.c \
    src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.c \
    src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_fallback.c \
    src/core/ext/filters/client_channel/resolver/dns/native/dns_resolver.c \
    src/core/ext/filters/client_channel/resolver/sockaddr/sockaddr_resolver.c \
    src/core/ext/filters/client_channel/resolver/fake/fake_resolver.c \
    src/core/ext/filters/load_reporting/load_reporting.c \
    src/core/ext/filters/load_reporting/load_reporting_filter.c \
    src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.c \
    src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.c \
    src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel.c \
    src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.c \
    src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.c \
    src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.c \
    third_party/nanopb/pb_common.c \
    third_party/nanopb/pb_decode.c \
    third_party/nanopb/pb_encode.c \
    src/core/ext/filters/client_channel/lb_policy/pick_first/pick_first.c \
    src/core/ext/filters/client_channel/lb_policy/round_robin/round_robin.c \
    src/core/ext/census/base_resources.c \
    src/core/ext/census/context.c \
    src/core/ext/census/gen/census.pb.c \
    src/core/ext/census/gen/trace_context.pb.c \
    src/core/ext/census/grpc_context.c \
    src/core/ext/census/grpc_filter.c \
    src/core/ext/census/grpc_plugin.c \
    src/core/ext/census/initialize.c \
    src/core/ext/census/intrusive_hash_map.c \
    src/core/ext/census/mlog.c \
    src/core/ext/census/operation.c \
    src/core/ext/census/placeholders.c \
    src/core/ext/census/resource.c \
    src/core/ext/census/trace_context.c \
    src/core/ext/census/tracing.c \
    src/core/ext/filters/max_age/max_age_filter.c \
    src/core/ext/filters/message_size/message_size_filter.c \
    src/core/ext/filters/workarounds/workaround_cronet_compression_filter.c \
    src/core/ext/filters/workarounds/workaround_utils.c \
    src/core/plugin_registry/grpc_unsecure_plugin_registry.c \

PUBLIC_HEADERS_C += \
    include/grpc/impl/codegen/byte_buffer_reader.h \
    include/grpc/impl/codegen/compression_types.h \
    include/grpc/impl/codegen/connectivity_state.h \
    include/grpc/impl/codegen/exec_ctx_fwd.h \
    include/grpc/impl/codegen/grpc_types.h \
    include/grpc/impl/codegen/propagation_bits.h \
    include/grpc/impl/codegen/slice.h \
    include/grpc/impl/codegen/status.h \
    include/grpc/impl/codegen/atm.h \
    include/grpc/impl/codegen/atm_gcc_atomic.h \
    include/grpc/impl/codegen/atm_gcc_sync.h \
    include/grpc/impl/codegen/atm_windows.h \
    include/grpc/impl/codegen/gpr_slice.h \
    include/grpc/impl/codegen/gpr_types.h \
    include/grpc/impl/codegen/port_platform.h \
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
    include/grpc/load_reporting.h \
    include/grpc/slice.h \
    include/grpc/slice_buffer.h \
    include/grpc/status.h \
    include/grpc/support/workaround_list.h \
    include/grpc/census.h \

LIBGRPC_UNSECURE_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC_UNSECURE_SRC))))


$(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a: $(ZLIB_DEP) $(CARES_DEP)  $(LIBGRPC_UNSECURE_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS)  $(CARES_MERGE_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBGRPC_UNSECURE_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS)  $(CARES_MERGE_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a
endif



ifeq ($(SYSTEM),MINGW32)
$(LIBDIR)/$(CONFIG)/grpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): $(LIBGRPC_UNSECURE_OBJS)  $(ZLIB_DEP) $(CARES_DEP) $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,--output-def=$(LIBDIR)/$(CONFIG)/grpc_unsecure$(SHARED_VERSION_CORE).def -Wl,--out-implib=$(LIBDIR)/$(CONFIG)/libgrpc_unsecure$(SHARED_VERSION_CORE)-dll.a -o $(LIBDIR)/$(CONFIG)/grpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGRPC_UNSECURE_OBJS) $(LIBDIR)/$(CONFIG)/libgpr.a $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBS)
else
$(LIBDIR)/$(CONFIG)/libgrpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): $(LIBGRPC_UNSECURE_OBJS)  $(ZLIB_DEP) $(CARES_DEP) $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
ifeq ($(SYSTEM),Darwin)
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -install_name $(SHARED_PREFIX)grpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) -dynamiclib -o $(LIBDIR)/$(CONFIG)/libgrpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGRPC_UNSECURE_OBJS) $(LIBDIR)/$(CONFIG)/libgpr.a $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBS)
else
	$(Q) $(LD) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,-soname,libgrpc_unsecure.so.4 -o $(LIBDIR)/$(CONFIG)/libgrpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGRPC_UNSECURE_OBJS) $(LIBDIR)/$(CONFIG)/libgpr.a $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBS)
	$(Q) ln -sf $(SHARED_PREFIX)grpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/libgrpc_unsecure$(SHARED_VERSION_CORE).so.4
	$(Q) ln -sf $(SHARED_PREFIX)grpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/libgrpc_unsecure$(SHARED_VERSION_CORE).so
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


$(LIBDIR)/$(CONFIG)/libreconnect_server.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(LIBRECONNECT_SERVER_OBJS) 
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


$(LIBDIR)/$(CONFIG)/libtest_tcp_server.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(LIBTEST_TCP_SERVER_OBJS) 
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
    src/cpp/server/channel_argument_option.cc \
    src/cpp/server/create_default_thread_pool.cc \
    src/cpp/server/dynamic_thread_pool.cc \
    src/cpp/server/health/default_health_check_service.cc \
    src/cpp/server/health/health.pb.c \
    src/cpp/server/health/health_check_service.cc \
    src/cpp/server/health/health_check_service_server_builder_option.cc \
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
    include/grpc++/ext/health_check_service_server_builder_option.h \
    include/grpc++/generic/async_generic_service.h \
    include/grpc++/generic/generic_stub.h \
    include/grpc++/grpc++.h \
    include/grpc++/health_check_service_interface.h \
    include/grpc++/impl/call.h \
    include/grpc++/impl/channel_argument_option.h \
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
    include/grpc/impl/codegen/gpr_slice.h \
    include/grpc/impl/codegen/gpr_types.h \
    include/grpc/impl/codegen/port_platform.h \
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
    include/grpc/load_reporting.h \
    include/grpc/slice.h \
    include/grpc/slice_buffer.h \
    include/grpc/status.h \
    include/grpc/support/workaround_list.h \
    include/grpc/impl/codegen/byte_buffer_reader.h \
    include/grpc/impl/codegen/compression_types.h \
    include/grpc/impl/codegen/connectivity_state.h \
    include/grpc/impl/codegen/exec_ctx_fwd.h \
    include/grpc/impl/codegen/grpc_types.h \
    include/grpc/impl/codegen/propagation_bits.h \
    include/grpc/impl/codegen/slice.h \
    include/grpc/impl/codegen/status.h \
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
    include/grpc++/impl/codegen/metadata_map.h \
    include/grpc++/impl/codegen/method_handler_impl.h \
    include/grpc++/impl/codegen/rpc_method.h \
    include/grpc++/impl/codegen/rpc_service_method.h \
    include/grpc++/impl/codegen/security/auth_context.h \
    include/grpc++/impl/codegen/serialization_traits.h \
    include/grpc++/impl/codegen/server_context.h \
    include/grpc++/impl/codegen/server_interface.h \
    include/grpc++/impl/codegen/service_type.h \
    include/grpc++/impl/codegen/slice.h \
    include/grpc++/impl/codegen/status.h \
    include/grpc++/impl/codegen/status_code_enum.h \
    include/grpc++/impl/codegen/string_ref.h \
    include/grpc++/impl/codegen/stub_options.h \
    include/grpc++/impl/codegen/sync_stream.h \
    include/grpc++/impl/codegen/time.h \
    include/grpc++/impl/codegen/proto_utils.h \
    include/grpc++/impl/codegen/config_protobuf.h \

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

$(LIBDIR)/$(CONFIG)/libgrpc++.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBGRPC++_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS)  $(CARES_MERGE_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc++.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBGRPC++_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS)  $(CARES_MERGE_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc++.a
endif



ifeq ($(SYSTEM),MINGW32)
$(LIBDIR)/$(CONFIG)/grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP): $(LIBGRPC++_OBJS)  $(ZLIB_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBDIR)/$(CONFIG)/grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(OPENSSL_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,--output-def=$(LIBDIR)/$(CONFIG)/grpc++$(SHARED_VERSION_CPP).def -Wl,--out-implib=$(LIBDIR)/$(CONFIG)/libgrpc++$(SHARED_VERSION_CPP)-dll.a -o $(LIBDIR)/$(CONFIG)/grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBGRPC++_OBJS) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) -lgrpc$(SHARED_VERSION_CORE)-dll -lgpr$(SHARED_VERSION_CORE)-dll
else
$(LIBDIR)/$(CONFIG)/libgrpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP): $(LIBGRPC++_OBJS)  $(ZLIB_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBDIR)/$(CONFIG)/libgrpc.$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/libgpr.$(SHARED_EXT_CORE) $(OPENSSL_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
ifeq ($(SYSTEM),Darwin)
	$(Q) $(LDXX) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -install_name $(SHARED_PREFIX)grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) -dynamiclib -o $(LIBDIR)/$(CONFIG)/libgrpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBGRPC++_OBJS) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) -lgrpc -lgpr
else
	$(Q) $(LDXX) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,-soname,libgrpc++.so.1 -o $(LIBDIR)/$(CONFIG)/libgrpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBGRPC++_OBJS) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) -lgrpc -lgpr
	$(Q) ln -sf $(SHARED_PREFIX)grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBDIR)/$(CONFIG)/libgrpc++$(SHARED_VERSION_CPP).so.1
	$(Q) ln -sf $(SHARED_PREFIX)grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBDIR)/$(CONFIG)/libgrpc++$(SHARED_VERSION_CPP).so
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
    src/cpp/server/channel_argument_option.cc \
    src/cpp/server/create_default_thread_pool.cc \
    src/cpp/server/dynamic_thread_pool.cc \
    src/cpp/server/health/default_health_check_service.cc \
    src/cpp/server/health/health.pb.c \
    src/cpp/server/health/health_check_service.cc \
    src/cpp/server/health/health_check_service_server_builder_option.cc \
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
    src/core/ext/transport/chttp2/client/chttp2_connector.c \
    src/core/ext/transport/chttp2/transport/bin_decoder.c \
    src/core/ext/transport/chttp2/transport/bin_encoder.c \
    src/core/ext/transport/chttp2/transport/chttp2_plugin.c \
    src/core/ext/transport/chttp2/transport/chttp2_transport.c \
    src/core/ext/transport/chttp2/transport/flow_control.c \
    src/core/ext/transport/chttp2/transport/frame_data.c \
    src/core/ext/transport/chttp2/transport/frame_goaway.c \
    src/core/ext/transport/chttp2/transport/frame_ping.c \
    src/core/ext/transport/chttp2/transport/frame_rst_stream.c \
    src/core/ext/transport/chttp2/transport/frame_settings.c \
    src/core/ext/transport/chttp2/transport/frame_window_update.c \
    src/core/ext/transport/chttp2/transport/hpack_encoder.c \
    src/core/ext/transport/chttp2/transport/hpack_parser.c \
    src/core/ext/transport/chttp2/transport/hpack_table.c \
    src/core/ext/transport/chttp2/transport/http2_settings.c \
    src/core/ext/transport/chttp2/transport/huffsyms.c \
    src/core/ext/transport/chttp2/transport/incoming_metadata.c \
    src/core/ext/transport/chttp2/transport/parsing.c \
    src/core/ext/transport/chttp2/transport/stream_lists.c \
    src/core/ext/transport/chttp2/transport/stream_map.c \
    src/core/ext/transport/chttp2/transport/varint.c \
    src/core/ext/transport/chttp2/transport/writing.c \
    src/core/lib/channel/channel_args.c \
    src/core/lib/channel/channel_stack.c \
    src/core/lib/channel/channel_stack_builder.c \
    src/core/lib/channel/connected_channel.c \
    src/core/lib/channel/handshaker.c \
    src/core/lib/channel/handshaker_factory.c \
    src/core/lib/channel/handshaker_registry.c \
    src/core/lib/compression/compression.c \
    src/core/lib/compression/message_compress.c \
    src/core/lib/compression/stream_compression.c \
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
    src/core/lib/iomgr/ev_epoll1_linux.c \
    src/core/lib/iomgr/ev_epoll_limited_pollers_linux.c \
    src/core/lib/iomgr/ev_epoll_thread_pool_linux.c \
    src/core/lib/iomgr/ev_epollex_linux.c \
    src/core/lib/iomgr/ev_epollsig_linux.c \
    src/core/lib/iomgr/ev_poll_posix.c \
    src/core/lib/iomgr/ev_posix.c \
    src/core/lib/iomgr/ev_windows.c \
    src/core/lib/iomgr/exec_ctx.c \
    src/core/lib/iomgr/executor.c \
    src/core/lib/iomgr/gethostname_fallback.c \
    src/core/lib/iomgr/gethostname_host_name_max.c \
    src/core/lib/iomgr/gethostname_sysconf.c \
    src/core/lib/iomgr/iocp_windows.c \
    src/core/lib/iomgr/iomgr.c \
    src/core/lib/iomgr/iomgr_posix.c \
    src/core/lib/iomgr/iomgr_uv.c \
    src/core/lib/iomgr/iomgr_windows.c \
    src/core/lib/iomgr/is_epollexclusive_available.c \
    src/core/lib/iomgr/load_file.c \
    src/core/lib/iomgr/lockfree_event.c \
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
    src/core/lib/iomgr/socket_factory_posix.c \
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
    src/core/lib/iomgr/tcp_server_utils_posix_common.c \
    src/core/lib/iomgr/tcp_server_utils_posix_ifaddrs.c \
    src/core/lib/iomgr/tcp_server_utils_posix_noifaddrs.c \
    src/core/lib/iomgr/tcp_server_uv.c \
    src/core/lib/iomgr/tcp_server_windows.c \
    src/core/lib/iomgr/tcp_uv.c \
    src/core/lib/iomgr/tcp_windows.c \
    src/core/lib/iomgr/time_averaged_stats.c \
    src/core/lib/iomgr/timer_generic.c \
    src/core/lib/iomgr/timer_heap.c \
    src/core/lib/iomgr/timer_manager.c \
    src/core/lib/iomgr/timer_uv.c \
    src/core/lib/iomgr/udp_server.c \
    src/core/lib/iomgr/unix_sockets_posix.c \
    src/core/lib/iomgr/unix_sockets_posix_noop.c \
    src/core/lib/iomgr/wakeup_fd_cv.c \
    src/core/lib/iomgr/wakeup_fd_eventfd.c \
    src/core/lib/iomgr/wakeup_fd_nospecial.c \
    src/core/lib/iomgr/wakeup_fd_pipe.c \
    src/core/lib/iomgr/wakeup_fd_posix.c \
    src/core/lib/json/json.c \
    src/core/lib/json/json_reader.c \
    src/core/lib/json/json_string.c \
    src/core/lib/json/json_writer.c \
    src/core/lib/slice/b64.c \
    src/core/lib/slice/percent_encoding.c \
    src/core/lib/slice/slice.c \
    src/core/lib/slice/slice_buffer.c \
    src/core/lib/slice/slice_hash_table.c \
    src/core/lib/slice/slice_intern.c \
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
    src/core/lib/surface/completion_queue_factory.c \
    src/core/lib/surface/event_string.c \
    src/core/lib/surface/lame_client.cc \
    src/core/lib/surface/metadata_array.c \
    src/core/lib/surface/server.c \
    src/core/lib/surface/validate_metadata.c \
    src/core/lib/surface/version.c \
    src/core/lib/transport/bdp_estimator.c \
    src/core/lib/transport/byte_stream.c \
    src/core/lib/transport/connectivity_state.c \
    src/core/lib/transport/error_utils.c \
    src/core/lib/transport/metadata.c \
    src/core/lib/transport/metadata_batch.c \
    src/core/lib/transport/pid_controller.c \
    src/core/lib/transport/service_config.c \
    src/core/lib/transport/static_metadata.c \
    src/core/lib/transport/status_conversion.c \
    src/core/lib/transport/timeout_encoding.c \
    src/core/lib/transport/transport.c \
    src/core/lib/transport/transport_op_string.c \
    src/core/lib/debug/trace.c \
    src/core/ext/transport/chttp2/alpn/alpn.c \
    src/core/ext/filters/http/client/http_client_filter.c \
    src/core/ext/filters/http/http_filters_plugin.c \
    src/core/ext/filters/http/message_compress/message_compress_filter.c \
    src/core/ext/filters/http/server/http_server_filter.c \
    src/core/ext/filters/client_channel/channel_connectivity.c \
    src/core/ext/filters/client_channel/client_channel.c \
    src/core/ext/filters/client_channel/client_channel_factory.c \
    src/core/ext/filters/client_channel/client_channel_plugin.c \
    src/core/ext/filters/client_channel/connector.c \
    src/core/ext/filters/client_channel/http_connect_handshaker.c \
    src/core/ext/filters/client_channel/http_proxy.c \
    src/core/ext/filters/client_channel/lb_policy.c \
    src/core/ext/filters/client_channel/lb_policy_factory.c \
    src/core/ext/filters/client_channel/lb_policy_registry.c \
    src/core/ext/filters/client_channel/parse_address.c \
    src/core/ext/filters/client_channel/proxy_mapper.c \
    src/core/ext/filters/client_channel/proxy_mapper_registry.c \
    src/core/ext/filters/client_channel/resolver.c \
    src/core/ext/filters/client_channel/resolver_factory.c \
    src/core/ext/filters/client_channel/resolver_registry.c \
    src/core/ext/filters/client_channel/retry_throttle.c \
    src/core/ext/filters/client_channel/subchannel.c \
    src/core/ext/filters/client_channel/subchannel_index.c \
    src/core/ext/filters/client_channel/uri_parser.c \
    src/core/ext/filters/deadline/deadline_filter.c \
    src/core/ext/transport/chttp2/server/insecure/server_chttp2.c \
    src/core/ext/transport/chttp2/server/insecure/server_chttp2_posix.c \
    src/core/ext/transport/chttp2/server/chttp2_server.c \
    src/core/ext/census/base_resources.c \
    src/core/ext/census/context.c \
    src/core/ext/census/gen/census.pb.c \
    src/core/ext/census/gen/trace_context.pb.c \
    src/core/ext/census/grpc_context.c \
    src/core/ext/census/grpc_filter.c \
    src/core/ext/census/grpc_plugin.c \
    src/core/ext/census/initialize.c \
    src/core/ext/census/intrusive_hash_map.c \
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
    include/grpc++/ext/health_check_service_server_builder_option.h \
    include/grpc++/generic/async_generic_service.h \
    include/grpc++/generic/generic_stub.h \
    include/grpc++/grpc++.h \
    include/grpc++/health_check_service_interface.h \
    include/grpc++/impl/call.h \
    include/grpc++/impl/channel_argument_option.h \
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
    include/grpc/impl/codegen/gpr_slice.h \
    include/grpc/impl/codegen/gpr_types.h \
    include/grpc/impl/codegen/port_platform.h \
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
    include/grpc/load_reporting.h \
    include/grpc/slice.h \
    include/grpc/slice_buffer.h \
    include/grpc/status.h \
    include/grpc/support/workaround_list.h \
    include/grpc/impl/codegen/byte_buffer_reader.h \
    include/grpc/impl/codegen/compression_types.h \
    include/grpc/impl/codegen/connectivity_state.h \
    include/grpc/impl/codegen/exec_ctx_fwd.h \
    include/grpc/impl/codegen/grpc_types.h \
    include/grpc/impl/codegen/propagation_bits.h \
    include/grpc/impl/codegen/slice.h \
    include/grpc/impl/codegen/status.h \
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
    include/grpc++/impl/codegen/metadata_map.h \
    include/grpc++/impl/codegen/method_handler_impl.h \
    include/grpc++/impl/codegen/rpc_method.h \
    include/grpc++/impl/codegen/rpc_service_method.h \
    include/grpc++/impl/codegen/security/auth_context.h \
    include/grpc++/impl/codegen/serialization_traits.h \
    include/grpc++/impl/codegen/server_context.h \
    include/grpc++/impl/codegen/server_interface.h \
    include/grpc++/impl/codegen/service_type.h \
    include/grpc++/impl/codegen/slice.h \
    include/grpc++/impl/codegen/status.h \
    include/grpc++/impl/codegen/status_code_enum.h \
    include/grpc++/impl/codegen/string_ref.h \
    include/grpc++/impl/codegen/stub_options.h \
    include/grpc++/impl/codegen/sync_stream.h \
    include/grpc++/impl/codegen/time.h \
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

$(LIBDIR)/$(CONFIG)/libgrpc++_cronet.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBGRPC++_CRONET_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS)  $(CARES_MERGE_OBJS)  $(OPENSSL_MERGE_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc++_cronet.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc++_cronet.a $(LIBGRPC++_CRONET_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS)  $(CARES_MERGE_OBJS)  $(OPENSSL_MERGE_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc++_cronet.a
endif



ifeq ($(SYSTEM),MINGW32)
$(LIBDIR)/$(CONFIG)/grpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP): $(LIBGRPC++_CRONET_OBJS)  $(ZLIB_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBDIR)/$(CONFIG)/gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/grpc_cronet$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(OPENSSL_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,--output-def=$(LIBDIR)/$(CONFIG)/grpc++_cronet$(SHARED_VERSION_CPP).def -Wl,--out-implib=$(LIBDIR)/$(CONFIG)/libgrpc++_cronet$(SHARED_VERSION_CPP)-dll.a -o $(LIBDIR)/$(CONFIG)/grpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBGRPC++_CRONET_OBJS) $(OPENSSL_MERGE_LIBS) $(LDLIBS_SECURE) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) -lgpr$(SHARED_VERSION_CORE)-dll -lgrpc_cronet$(SHARED_VERSION_CORE)-dll -lgrpc$(SHARED_VERSION_CORE)-dll
else
$(LIBDIR)/$(CONFIG)/libgrpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP): $(LIBGRPC++_CRONET_OBJS)  $(ZLIB_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBDIR)/$(CONFIG)/libgpr.$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/libgrpc_cronet.$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/libgrpc.$(SHARED_EXT_CORE) $(OPENSSL_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
ifeq ($(SYSTEM),Darwin)
	$(Q) $(LDXX) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -install_name $(SHARED_PREFIX)grpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) -dynamiclib -o $(LIBDIR)/$(CONFIG)/libgrpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBGRPC++_CRONET_OBJS) $(OPENSSL_MERGE_LIBS) $(LDLIBS_SECURE) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) -lgpr -lgrpc_cronet -lgrpc
else
	$(Q) $(LDXX) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,-soname,libgrpc++_cronet.so.1 -o $(LIBDIR)/$(CONFIG)/libgrpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBGRPC++_CRONET_OBJS) $(OPENSSL_MERGE_LIBS) $(LDLIBS_SECURE) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) -lgpr -lgrpc_cronet -lgrpc
	$(Q) ln -sf $(SHARED_PREFIX)grpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBDIR)/$(CONFIG)/libgrpc++_cronet$(SHARED_VERSION_CPP).so.1
	$(Q) ln -sf $(SHARED_PREFIX)grpc++_cronet$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBDIR)/$(CONFIG)/libgrpc++_cronet$(SHARED_VERSION_CPP).so
endif
endif

endif

endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBGRPC++_CRONET_OBJS:.o=.dep)
endif
endif


LIBGRPC++_ERROR_DETAILS_SRC = \
    $(GENDIR)/src/proto/grpc/status/status.pb.cc $(GENDIR)/src/proto/grpc/status/status.grpc.pb.cc \
    src/cpp/util/error_details.cc \

PUBLIC_HEADERS_CXX += \
    include/grpc++/support/error_details.h \

LIBGRPC++_ERROR_DETAILS_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC++_ERROR_DETAILS_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libgrpc++_error_details.a: openssl_dep_error

$(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_error_details$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP): openssl_dep_error

else

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libgrpc++_error_details.a: protobuf_dep_error

$(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_error_details$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP): protobuf_dep_error

else

$(LIBDIR)/$(CONFIG)/libgrpc++_error_details.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBGRPC++_ERROR_DETAILS_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS)  $(CARES_MERGE_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc++_error_details.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc++_error_details.a $(LIBGRPC++_ERROR_DETAILS_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS)  $(CARES_MERGE_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc++_error_details.a
endif



ifeq ($(SYSTEM),MINGW32)
$(LIBDIR)/$(CONFIG)/grpc++_error_details$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP): $(LIBGRPC++_ERROR_DETAILS_OBJS)  $(ZLIB_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBDIR)/$(CONFIG)/grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(OPENSSL_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,--output-def=$(LIBDIR)/$(CONFIG)/grpc++_error_details$(SHARED_VERSION_CPP).def -Wl,--out-implib=$(LIBDIR)/$(CONFIG)/libgrpc++_error_details$(SHARED_VERSION_CPP)-dll.a -o $(LIBDIR)/$(CONFIG)/grpc++_error_details$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBGRPC++_ERROR_DETAILS_OBJS) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) -lgrpc++$(SHARED_VERSION_CPP)-dll
else
$(LIBDIR)/$(CONFIG)/libgrpc++_error_details$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP): $(LIBGRPC++_ERROR_DETAILS_OBJS)  $(ZLIB_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBDIR)/$(CONFIG)/libgrpc++.$(SHARED_EXT_CPP) $(OPENSSL_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
ifeq ($(SYSTEM),Darwin)
	$(Q) $(LDXX) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -install_name $(SHARED_PREFIX)grpc++_error_details$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) -dynamiclib -o $(LIBDIR)/$(CONFIG)/libgrpc++_error_details$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBGRPC++_ERROR_DETAILS_OBJS) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) -lgrpc++
else
	$(Q) $(LDXX) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,-soname,libgrpc++_error_details.so.1 -o $(LIBDIR)/$(CONFIG)/libgrpc++_error_details$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBGRPC++_ERROR_DETAILS_OBJS) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) -lgrpc++
	$(Q) ln -sf $(SHARED_PREFIX)grpc++_error_details$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBDIR)/$(CONFIG)/libgrpc++_error_details$(SHARED_VERSION_CPP).so.1
	$(Q) ln -sf $(SHARED_PREFIX)grpc++_error_details$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBDIR)/$(CONFIG)/libgrpc++_error_details$(SHARED_VERSION_CPP).so
endif
endif

endif

endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBGRPC++_ERROR_DETAILS_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/src/cpp/util/error_details.o: $(GENDIR)/src/proto/grpc/status/status.pb.cc $(GENDIR)/src/proto/grpc/status/status.grpc.pb.cc


LIBGRPC++_PROTO_REFLECTION_DESC_DB_SRC = \
    test/cpp/util/proto_reflection_descriptor_database.cc \
    $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.pb.cc $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc \

PUBLIC_HEADERS_CXX += \
    include/grpc++/impl/codegen/config_protobuf.h \

LIBGRPC++_PROTO_REFLECTION_DESC_DB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC++_PROTO_REFLECTION_DESC_DB_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libgrpc++_proto_reflection_desc_db.a: openssl_dep_error


else

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libgrpc++_proto_reflection_desc_db.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libgrpc++_proto_reflection_desc_db.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBGRPC++_PROTO_REFLECTION_DESC_DB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc++_proto_reflection_desc_db.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc++_proto_reflection_desc_db.a $(LIBGRPC++_PROTO_REFLECTION_DESC_DB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc++_proto_reflection_desc_db.a
endif




endif

endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBGRPC++_PROTO_REFLECTION_DESC_DB_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/util/proto_reflection_descriptor_database.o: $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.pb.cc $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc


LIBGRPC++_REFLECTION_SRC = \
    src/cpp/ext/proto_server_reflection.cc \
    src/cpp/ext/proto_server_reflection_plugin.cc \
    $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.pb.cc $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc \

PUBLIC_HEADERS_CXX += \
    include/grpc++/ext/proto_server_reflection_plugin.h \

LIBGRPC++_REFLECTION_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC++_REFLECTION_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libgrpc++_reflection.a: openssl_dep_error

$(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP): openssl_dep_error

else

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libgrpc++_reflection.a: protobuf_dep_error

$(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP): protobuf_dep_error

else

$(LIBDIR)/$(CONFIG)/libgrpc++_reflection.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBGRPC++_REFLECTION_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc++_reflection.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc++_reflection.a $(LIBGRPC++_REFLECTION_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc++_reflection.a
endif



ifeq ($(SYSTEM),MINGW32)
$(LIBDIR)/$(CONFIG)/grpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP): $(LIBGRPC++_REFLECTION_OBJS)  $(ZLIB_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBDIR)/$(CONFIG)/grpc++$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBDIR)/$(CONFIG)/grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(OPENSSL_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,--output-def=$(LIBDIR)/$(CONFIG)/grpc++_reflection$(SHARED_VERSION_CPP).def -Wl,--out-implib=$(LIBDIR)/$(CONFIG)/libgrpc++_reflection$(SHARED_VERSION_CPP)-dll.a -o $(LIBDIR)/$(CONFIG)/grpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBGRPC++_REFLECTION_OBJS) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) -lgrpc++$(SHARED_VERSION_CPP)-dll -lgrpc$(SHARED_VERSION_CORE)-dll
else
$(LIBDIR)/$(CONFIG)/libgrpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP): $(LIBGRPC++_REFLECTION_OBJS)  $(ZLIB_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBDIR)/$(CONFIG)/libgrpc++.$(SHARED_EXT_CPP) $(LIBDIR)/$(CONFIG)/libgrpc.$(SHARED_EXT_CORE) $(OPENSSL_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
ifeq ($(SYSTEM),Darwin)
	$(Q) $(LDXX) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -install_name $(SHARED_PREFIX)grpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) -dynamiclib -o $(LIBDIR)/$(CONFIG)/libgrpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBGRPC++_REFLECTION_OBJS) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) -lgrpc++ -lgrpc
else
	$(Q) $(LDXX) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,-soname,libgrpc++_reflection.so.1 -o $(LIBDIR)/$(CONFIG)/libgrpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBGRPC++_REFLECTION_OBJS) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) -lgrpc++ -lgrpc
	$(Q) ln -sf $(SHARED_PREFIX)grpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBDIR)/$(CONFIG)/libgrpc++_reflection$(SHARED_VERSION_CPP).so.1
	$(Q) ln -sf $(SHARED_PREFIX)grpc++_reflection$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBDIR)/$(CONFIG)/libgrpc++_reflection$(SHARED_VERSION_CPP).so
endif
endif

endif

endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBGRPC++_REFLECTION_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/src/cpp/ext/proto_server_reflection.o: $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.pb.cc $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/src/cpp/ext/proto_server_reflection_plugin.o: $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.pb.cc $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc


LIBGRPC++_TEST_CONFIG_SRC = \
    test/cpp/util/test_config_cc.cc \

PUBLIC_HEADERS_CXX += \

LIBGRPC++_TEST_CONFIG_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC++_TEST_CONFIG_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a: openssl_dep_error


else

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBGRPC++_TEST_CONFIG_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a $(LIBGRPC++_TEST_CONFIG_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a
endif




endif

endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBGRPC++_TEST_CONFIG_OBJS:.o=.dep)
endif
endif


LIBGRPC++_TEST_UTIL_SRC = \
    $(GENDIR)/src/proto/grpc/health/v1/health.pb.cc $(GENDIR)/src/proto/grpc/health/v1/health.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/echo.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc \
    test/cpp/end2end/test_service_impl.cc \
    test/cpp/util/byte_buffer_proto_helper.cc \
    test/cpp/util/create_test_channel.cc \
    test/cpp/util/string_ref_helper.cc \
    test/cpp/util/subprocess.cc \
    test/cpp/util/test_credentials_provider.cc \
    src/cpp/codegen/codegen_init.cc \

PUBLIC_HEADERS_CXX += \
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
    include/grpc++/impl/codegen/metadata_map.h \
    include/grpc++/impl/codegen/method_handler_impl.h \
    include/grpc++/impl/codegen/rpc_method.h \
    include/grpc++/impl/codegen/rpc_service_method.h \
    include/grpc++/impl/codegen/security/auth_context.h \
    include/grpc++/impl/codegen/serialization_traits.h \
    include/grpc++/impl/codegen/server_context.h \
    include/grpc++/impl/codegen/server_interface.h \
    include/grpc++/impl/codegen/service_type.h \
    include/grpc++/impl/codegen/slice.h \
    include/grpc++/impl/codegen/status.h \
    include/grpc++/impl/codegen/status_code_enum.h \
    include/grpc++/impl/codegen/string_ref.h \
    include/grpc++/impl/codegen/stub_options.h \
    include/grpc++/impl/codegen/sync_stream.h \
    include/grpc++/impl/codegen/time.h \
    include/grpc/impl/codegen/byte_buffer_reader.h \
    include/grpc/impl/codegen/compression_types.h \
    include/grpc/impl/codegen/connectivity_state.h \
    include/grpc/impl/codegen/exec_ctx_fwd.h \
    include/grpc/impl/codegen/grpc_types.h \
    include/grpc/impl/codegen/propagation_bits.h \
    include/grpc/impl/codegen/slice.h \
    include/grpc/impl/codegen/status.h \
    include/grpc/impl/codegen/atm.h \
    include/grpc/impl/codegen/atm_gcc_atomic.h \
    include/grpc/impl/codegen/atm_gcc_sync.h \
    include/grpc/impl/codegen/atm_windows.h \
    include/grpc/impl/codegen/gpr_slice.h \
    include/grpc/impl/codegen/gpr_types.h \
    include/grpc/impl/codegen/port_platform.h \
    include/grpc/impl/codegen/sync.h \
    include/grpc/impl/codegen/sync_generic.h \
    include/grpc/impl/codegen/sync_posix.h \
    include/grpc/impl/codegen/sync_windows.h \
    include/grpc++/impl/codegen/proto_utils.h \
    include/grpc++/impl/codegen/config_protobuf.h \

LIBGRPC++_TEST_UTIL_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC++_TEST_UTIL_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a: openssl_dep_error


else

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBGRPC++_TEST_UTIL_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBGRPC++_TEST_UTIL_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a
endif




endif

endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBGRPC++_TEST_UTIL_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/end2end/test_service_impl.o: $(GENDIR)/src/proto/grpc/health/v1/health.pb.cc $(GENDIR)/src/proto/grpc/health/v1/health.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/util/byte_buffer_proto_helper.o: $(GENDIR)/src/proto/grpc/health/v1/health.pb.cc $(GENDIR)/src/proto/grpc/health/v1/health.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/util/create_test_channel.o: $(GENDIR)/src/proto/grpc/health/v1/health.pb.cc $(GENDIR)/src/proto/grpc/health/v1/health.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/util/string_ref_helper.o: $(GENDIR)/src/proto/grpc/health/v1/health.pb.cc $(GENDIR)/src/proto/grpc/health/v1/health.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/util/subprocess.o: $(GENDIR)/src/proto/grpc/health/v1/health.pb.cc $(GENDIR)/src/proto/grpc/health/v1/health.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/util/test_credentials_provider.o: $(GENDIR)/src/proto/grpc/health/v1/health.pb.cc $(GENDIR)/src/proto/grpc/health/v1/health.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/src/cpp/codegen/codegen_init.o: $(GENDIR)/src/proto/grpc/health/v1/health.pb.cc $(GENDIR)/src/proto/grpc/health/v1/health.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc


LIBGRPC++_TEST_UTIL_UNSECURE_SRC = \
    $(GENDIR)/src/proto/grpc/health/v1/health.pb.cc $(GENDIR)/src/proto/grpc/health/v1/health.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/echo.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc \
    test/cpp/end2end/test_service_impl.cc \
    test/cpp/util/byte_buffer_proto_helper.cc \
    test/cpp/util/string_ref_helper.cc \
    test/cpp/util/subprocess.cc \
    src/cpp/codegen/codegen_init.cc \

PUBLIC_HEADERS_CXX += \
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
    include/grpc++/impl/codegen/metadata_map.h \
    include/grpc++/impl/codegen/method_handler_impl.h \
    include/grpc++/impl/codegen/rpc_method.h \
    include/grpc++/impl/codegen/rpc_service_method.h \
    include/grpc++/impl/codegen/security/auth_context.h \
    include/grpc++/impl/codegen/serialization_traits.h \
    include/grpc++/impl/codegen/server_context.h \
    include/grpc++/impl/codegen/server_interface.h \
    include/grpc++/impl/codegen/service_type.h \
    include/grpc++/impl/codegen/slice.h \
    include/grpc++/impl/codegen/status.h \
    include/grpc++/impl/codegen/status_code_enum.h \
    include/grpc++/impl/codegen/string_ref.h \
    include/grpc++/impl/codegen/stub_options.h \
    include/grpc++/impl/codegen/sync_stream.h \
    include/grpc++/impl/codegen/time.h \
    include/grpc/impl/codegen/byte_buffer_reader.h \
    include/grpc/impl/codegen/compression_types.h \
    include/grpc/impl/codegen/connectivity_state.h \
    include/grpc/impl/codegen/exec_ctx_fwd.h \
    include/grpc/impl/codegen/grpc_types.h \
    include/grpc/impl/codegen/propagation_bits.h \
    include/grpc/impl/codegen/slice.h \
    include/grpc/impl/codegen/status.h \
    include/grpc/impl/codegen/atm.h \
    include/grpc/impl/codegen/atm_gcc_atomic.h \
    include/grpc/impl/codegen/atm_gcc_sync.h \
    include/grpc/impl/codegen/atm_windows.h \
    include/grpc/impl/codegen/gpr_slice.h \
    include/grpc/impl/codegen/gpr_types.h \
    include/grpc/impl/codegen/port_platform.h \
    include/grpc/impl/codegen/sync.h \
    include/grpc/impl/codegen/sync_generic.h \
    include/grpc/impl/codegen/sync_posix.h \
    include/grpc/impl/codegen/sync_windows.h \
    include/grpc++/impl/codegen/proto_utils.h \
    include/grpc++/impl/codegen/config_protobuf.h \

LIBGRPC++_TEST_UTIL_UNSECURE_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC++_TEST_UTIL_UNSECURE_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a: openssl_dep_error


else

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBGRPC++_TEST_UTIL_UNSECURE_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBGRPC++_TEST_UTIL_UNSECURE_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a
endif




endif

endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBGRPC++_TEST_UTIL_UNSECURE_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/end2end/test_service_impl.o: $(GENDIR)/src/proto/grpc/health/v1/health.pb.cc $(GENDIR)/src/proto/grpc/health/v1/health.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/util/byte_buffer_proto_helper.o: $(GENDIR)/src/proto/grpc/health/v1/health.pb.cc $(GENDIR)/src/proto/grpc/health/v1/health.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/util/string_ref_helper.o: $(GENDIR)/src/proto/grpc/health/v1/health.pb.cc $(GENDIR)/src/proto/grpc/health/v1/health.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/util/subprocess.o: $(GENDIR)/src/proto/grpc/health/v1/health.pb.cc $(GENDIR)/src/proto/grpc/health/v1/health.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/src/cpp/codegen/codegen_init.o: $(GENDIR)/src/proto/grpc/health/v1/health.pb.cc $(GENDIR)/src/proto/grpc/health/v1/health.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc $(GENDIR)/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc


LIBGRPC++_UNSECURE_SRC = \
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
    src/cpp/server/channel_argument_option.cc \
    src/cpp/server/create_default_thread_pool.cc \
    src/cpp/server/dynamic_thread_pool.cc \
    src/cpp/server/health/default_health_check_service.cc \
    src/cpp/server/health/health.pb.c \
    src/cpp/server/health/health_check_service.cc \
    src/cpp/server/health/health_check_service_server_builder_option.cc \
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
    include/grpc++/ext/health_check_service_server_builder_option.h \
    include/grpc++/generic/async_generic_service.h \
    include/grpc++/generic/generic_stub.h \
    include/grpc++/grpc++.h \
    include/grpc++/health_check_service_interface.h \
    include/grpc++/impl/call.h \
    include/grpc++/impl/channel_argument_option.h \
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
    include/grpc/impl/codegen/gpr_slice.h \
    include/grpc/impl/codegen/gpr_types.h \
    include/grpc/impl/codegen/port_platform.h \
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
    include/grpc/load_reporting.h \
    include/grpc/slice.h \
    include/grpc/slice_buffer.h \
    include/grpc/status.h \
    include/grpc/support/workaround_list.h \
    include/grpc/impl/codegen/byte_buffer_reader.h \
    include/grpc/impl/codegen/compression_types.h \
    include/grpc/impl/codegen/connectivity_state.h \
    include/grpc/impl/codegen/exec_ctx_fwd.h \
    include/grpc/impl/codegen/grpc_types.h \
    include/grpc/impl/codegen/propagation_bits.h \
    include/grpc/impl/codegen/slice.h \
    include/grpc/impl/codegen/status.h \
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
    include/grpc++/impl/codegen/metadata_map.h \
    include/grpc++/impl/codegen/method_handler_impl.h \
    include/grpc++/impl/codegen/rpc_method.h \
    include/grpc++/impl/codegen/rpc_service_method.h \
    include/grpc++/impl/codegen/security/auth_context.h \
    include/grpc++/impl/codegen/serialization_traits.h \
    include/grpc++/impl/codegen/server_context.h \
    include/grpc++/impl/codegen/server_interface.h \
    include/grpc++/impl/codegen/service_type.h \
    include/grpc++/impl/codegen/slice.h \
    include/grpc++/impl/codegen/status.h \
    include/grpc++/impl/codegen/status_code_enum.h \
    include/grpc++/impl/codegen/string_ref.h \
    include/grpc++/impl/codegen/stub_options.h \
    include/grpc++/impl/codegen/sync_stream.h \
    include/grpc++/impl/codegen/time.h \

LIBGRPC++_UNSECURE_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC++_UNSECURE_SRC))))


ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a: protobuf_dep_error

$(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc++_unsecure$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP): protobuf_dep_error

else

$(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBGRPC++_UNSECURE_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS)  $(CARES_MERGE_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBGRPC++_UNSECURE_OBJS)  $(LIBGPR_OBJS)  $(ZLIB_MERGE_OBJS)  $(CARES_MERGE_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a
endif



ifeq ($(SYSTEM),MINGW32)
$(LIBDIR)/$(CONFIG)/grpc++_unsecure$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP): $(LIBGRPC++_UNSECURE_OBJS)  $(ZLIB_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBDIR)/$(CONFIG)/gpr$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/grpc_unsecure$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,--output-def=$(LIBDIR)/$(CONFIG)/grpc++_unsecure$(SHARED_VERSION_CPP).def -Wl,--out-implib=$(LIBDIR)/$(CONFIG)/libgrpc++_unsecure$(SHARED_VERSION_CPP)-dll.a -o $(LIBDIR)/$(CONFIG)/grpc++_unsecure$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBGRPC++_UNSECURE_OBJS) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) -lgpr$(SHARED_VERSION_CORE)-dll -lgrpc_unsecure$(SHARED_VERSION_CORE)-dll
else
$(LIBDIR)/$(CONFIG)/libgrpc++_unsecure$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP): $(LIBGRPC++_UNSECURE_OBJS)  $(ZLIB_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBDIR)/$(CONFIG)/libgpr.$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.$(SHARED_EXT_CORE)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
ifeq ($(SYSTEM),Darwin)
	$(Q) $(LDXX) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -install_name $(SHARED_PREFIX)grpc++_unsecure$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) -dynamiclib -o $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBGRPC++_UNSECURE_OBJS) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) -lgpr -lgrpc_unsecure
else
	$(Q) $(LDXX) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,-soname,libgrpc++_unsecure.so.1 -o $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBGRPC++_UNSECURE_OBJS) $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) -lgpr -lgrpc_unsecure
	$(Q) ln -sf $(SHARED_PREFIX)grpc++_unsecure$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure$(SHARED_VERSION_CPP).so.1
	$(Q) ln -sf $(SHARED_PREFIX)grpc++_unsecure$(SHARED_VERSION_CPP).$(SHARED_EXT_CPP) $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure$(SHARED_VERSION_CPP).so
endif
endif

endif

ifneq ($(NO_DEPS),true)
-include $(LIBGRPC++_UNSECURE_OBJS:.o=.dep)
endif


LIBGRPC_BENCHMARK_SRC = \
    test/cpp/microbenchmarks/helpers.cc \

PUBLIC_HEADERS_CXX += \

LIBGRPC_BENCHMARK_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC_BENCHMARK_SRC))))

$(LIBGRPC_BENCHMARK_OBJS): CPPFLAGS += -Ithird_party/benchmark/include -DHAVE_POSIX_REGEX

ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a: openssl_dep_error


else

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBGRPC_BENCHMARK_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBGRPC_BENCHMARK_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a
endif




endif

endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBGRPC_BENCHMARK_OBJS:.o=.dep)
endif
endif


LIBGRPC_CLI_LIBS_SRC = \
    test/cpp/util/cli_call.cc \
    test/cpp/util/cli_credentials.cc \
    test/cpp/util/grpc_tool.cc \
    test/cpp/util/proto_file_parser.cc \
    test/cpp/util/service_describer.cc \
    $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.pb.cc $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc \

PUBLIC_HEADERS_CXX += \
    include/grpc++/impl/codegen/config_protobuf.h \

LIBGRPC_CLI_LIBS_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC_CLI_LIBS_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libgrpc_cli_libs.a: openssl_dep_error


else

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libgrpc_cli_libs.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libgrpc_cli_libs.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBGRPC_CLI_LIBS_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc_cli_libs.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc_cli_libs.a $(LIBGRPC_CLI_LIBS_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc_cli_libs.a
endif




endif

endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBGRPC_CLI_LIBS_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/util/cli_call.o: $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.pb.cc $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/util/cli_credentials.o: $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.pb.cc $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/util/grpc_tool.o: $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.pb.cc $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/util/proto_file_parser.o: $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.pb.cc $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/util/service_describer.o: $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.pb.cc $(GENDIR)/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc


LIBGRPC_PLUGIN_SUPPORT_SRC = \
    src/compiler/cpp_generator.cc \
    src/compiler/csharp_generator.cc \
    src/compiler/node_generator.cc \
    src/compiler/objective_c_generator.cc \
    src/compiler/php_generator.cc \
    src/compiler/python_generator.cc \
    src/compiler/ruby_generator.cc \

PUBLIC_HEADERS_CXX += \
    include/grpc++/impl/codegen/config_protobuf.h \

LIBGRPC_PLUGIN_SUPPORT_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC_PLUGIN_SUPPORT_SRC))))


ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBGRPC_PLUGIN_SUPPORT_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a $(LIBGRPC_PLUGIN_SUPPORT_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBGRPC_PLUGIN_SUPPORT_OBJS:.o=.dep)
endif


LIBHTTP2_CLIENT_MAIN_SRC = \
    $(GENDIR)/src/proto/grpc/testing/empty.pb.cc $(GENDIR)/src/proto/grpc/testing/empty.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/test.pb.cc $(GENDIR)/src/proto/grpc/testing/test.grpc.pb.cc \
    test/cpp/interop/http2_client.cc \

PUBLIC_HEADERS_CXX += \

LIBHTTP2_CLIENT_MAIN_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBHTTP2_CLIENT_MAIN_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libhttp2_client_main.a: openssl_dep_error


else

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libhttp2_client_main.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libhttp2_client_main.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBHTTP2_CLIENT_MAIN_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libhttp2_client_main.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libhttp2_client_main.a $(LIBHTTP2_CLIENT_MAIN_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libhttp2_client_main.a
endif




endif

endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBHTTP2_CLIENT_MAIN_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/interop/http2_client.o: $(GENDIR)/src/proto/grpc/testing/empty.pb.cc $(GENDIR)/src/proto/grpc/testing/empty.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/test.pb.cc $(GENDIR)/src/proto/grpc/testing/test.grpc.pb.cc


LIBINTEROP_CLIENT_HELPER_SRC = \
    $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc \
    test/cpp/interop/client_helper.cc \

PUBLIC_HEADERS_CXX += \

LIBINTEROP_CLIENT_HELPER_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBINTEROP_CLIENT_HELPER_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libinterop_client_helper.a: openssl_dep_error


else

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libinterop_client_helper.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libinterop_client_helper.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBINTEROP_CLIENT_HELPER_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libinterop_client_helper.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libinterop_client_helper.a $(LIBINTEROP_CLIENT_HELPER_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libinterop_client_helper.a
endif




endif

endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBINTEROP_CLIENT_HELPER_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/interop/client_helper.o: $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc


LIBINTEROP_CLIENT_MAIN_SRC = \
    $(GENDIR)/src/proto/grpc/testing/empty.pb.cc $(GENDIR)/src/proto/grpc/testing/empty.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/test.pb.cc $(GENDIR)/src/proto/grpc/testing/test.grpc.pb.cc \
    test/cpp/interop/client.cc \
    test/cpp/interop/interop_client.cc \

PUBLIC_HEADERS_CXX += \

LIBINTEROP_CLIENT_MAIN_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBINTEROP_CLIENT_MAIN_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libinterop_client_main.a: openssl_dep_error


else

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libinterop_client_main.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libinterop_client_main.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBINTEROP_CLIENT_MAIN_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libinterop_client_main.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libinterop_client_main.a $(LIBINTEROP_CLIENT_MAIN_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libinterop_client_main.a
endif




endif

endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBINTEROP_CLIENT_MAIN_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/interop/client.o: $(GENDIR)/src/proto/grpc/testing/empty.pb.cc $(GENDIR)/src/proto/grpc/testing/empty.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/test.pb.cc $(GENDIR)/src/proto/grpc/testing/test.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/interop/interop_client.o: $(GENDIR)/src/proto/grpc/testing/empty.pb.cc $(GENDIR)/src/proto/grpc/testing/empty.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/test.pb.cc $(GENDIR)/src/proto/grpc/testing/test.grpc.pb.cc


LIBINTEROP_SERVER_HELPER_SRC = \
    test/cpp/interop/server_helper.cc \

PUBLIC_HEADERS_CXX += \

LIBINTEROP_SERVER_HELPER_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBINTEROP_SERVER_HELPER_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libinterop_server_helper.a: openssl_dep_error


else

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libinterop_server_helper.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libinterop_server_helper.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBINTEROP_SERVER_HELPER_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libinterop_server_helper.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libinterop_server_helper.a $(LIBINTEROP_SERVER_HELPER_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libinterop_server_helper.a
endif




endif

endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBINTEROP_SERVER_HELPER_OBJS:.o=.dep)
endif
endif


LIBINTEROP_SERVER_LIB_SRC = \
    $(GENDIR)/src/proto/grpc/testing/empty.pb.cc $(GENDIR)/src/proto/grpc/testing/empty.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/test.pb.cc $(GENDIR)/src/proto/grpc/testing/test.grpc.pb.cc \
    test/cpp/interop/interop_server.cc \

PUBLIC_HEADERS_CXX += \

LIBINTEROP_SERVER_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBINTEROP_SERVER_LIB_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libinterop_server_lib.a: openssl_dep_error


else

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libinterop_server_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libinterop_server_lib.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBINTEROP_SERVER_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libinterop_server_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libinterop_server_lib.a $(LIBINTEROP_SERVER_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libinterop_server_lib.a
endif




endif

endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBINTEROP_SERVER_LIB_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/interop/interop_server.o: $(GENDIR)/src/proto/grpc/testing/empty.pb.cc $(GENDIR)/src/proto/grpc/testing/empty.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/test.pb.cc $(GENDIR)/src/proto/grpc/testing/test.grpc.pb.cc


LIBINTEROP_SERVER_MAIN_SRC = \
    test/cpp/interop/interop_server_bootstrap.cc \

PUBLIC_HEADERS_CXX += \

LIBINTEROP_SERVER_MAIN_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBINTEROP_SERVER_MAIN_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libinterop_server_main.a: openssl_dep_error


else

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libinterop_server_main.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libinterop_server_main.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBINTEROP_SERVER_MAIN_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libinterop_server_main.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libinterop_server_main.a $(LIBINTEROP_SERVER_MAIN_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libinterop_server_main.a
endif




endif

endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBINTEROP_SERVER_MAIN_OBJS:.o=.dep)
endif
endif


LIBQPS_SRC = \
    $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/payloads.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/stats.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/control.pb.cc $(GENDIR)/src/proto/grpc/testing/control.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/services.pb.cc $(GENDIR)/src/proto/grpc/testing/services.grpc.pb.cc \
    test/cpp/qps/benchmark_config.cc \
    test/cpp/qps/client_async.cc \
    test/cpp/qps/client_sync.cc \
    test/cpp/qps/driver.cc \
    test/cpp/qps/parse_json.cc \
    test/cpp/qps/qps_worker.cc \
    test/cpp/qps/report.cc \
    test/cpp/qps/server_async.cc \
    test/cpp/qps/server_sync.cc \
    test/cpp/qps/usage_timer.cc \

PUBLIC_HEADERS_CXX += \

LIBQPS_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBQPS_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libqps.a: openssl_dep_error


else

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libqps.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libqps.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(PROTOBUF_DEP) $(LIBQPS_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libqps.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libqps.a $(LIBQPS_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libqps.a
endif




endif

endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBQPS_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/qps/benchmark_config.o: $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/control.pb.cc $(GENDIR)/src/proto/grpc/testing/control.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/services.pb.cc $(GENDIR)/src/proto/grpc/testing/services.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/qps/client_async.o: $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/control.pb.cc $(GENDIR)/src/proto/grpc/testing/control.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/services.pb.cc $(GENDIR)/src/proto/grpc/testing/services.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/qps/client_sync.o: $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/control.pb.cc $(GENDIR)/src/proto/grpc/testing/control.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/services.pb.cc $(GENDIR)/src/proto/grpc/testing/services.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/qps/driver.o: $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/control.pb.cc $(GENDIR)/src/proto/grpc/testing/control.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/services.pb.cc $(GENDIR)/src/proto/grpc/testing/services.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/qps/parse_json.o: $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/control.pb.cc $(GENDIR)/src/proto/grpc/testing/control.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/services.pb.cc $(GENDIR)/src/proto/grpc/testing/services.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/qps/qps_worker.o: $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/control.pb.cc $(GENDIR)/src/proto/grpc/testing/control.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/services.pb.cc $(GENDIR)/src/proto/grpc/testing/services.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/qps/report.o: $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/control.pb.cc $(GENDIR)/src/proto/grpc/testing/control.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/services.pb.cc $(GENDIR)/src/proto/grpc/testing/services.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/qps/server_async.o: $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/control.pb.cc $(GENDIR)/src/proto/grpc/testing/control.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/services.pb.cc $(GENDIR)/src/proto/grpc/testing/services.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/qps/server_sync.o: $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/control.pb.cc $(GENDIR)/src/proto/grpc/testing/control.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/services.pb.cc $(GENDIR)/src/proto/grpc/testing/services.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/qps/usage_timer.o: $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/control.pb.cc $(GENDIR)/src/proto/grpc/testing/control.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/services.pb.cc $(GENDIR)/src/proto/grpc/testing/services.grpc.pb.cc


LIBGRPC_CSHARP_EXT_SRC = \
    src/csharp/ext/grpc_csharp_ext.c \

PUBLIC_HEADERS_C += \

LIBGRPC_CSHARP_EXT_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC_CSHARP_EXT_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libgrpc_csharp_ext.a: openssl_dep_error

$(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc_csharp_ext$(SHARED_VERSION_CSHARP).$(SHARED_EXT_CSHARP): openssl_dep_error

else


$(LIBDIR)/$(CONFIG)/libgrpc_csharp_ext.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(LIBGRPC_CSHARP_EXT_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc_csharp_ext.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libgrpc_csharp_ext.a $(LIBGRPC_CSHARP_EXT_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libgrpc_csharp_ext.a
endif



ifeq ($(SYSTEM),MINGW32)
$(LIBDIR)/$(CONFIG)/grpc_csharp_ext$(SHARED_VERSION_CSHARP).$(SHARED_EXT_CSHARP): $(LIBGRPC_CSHARP_EXT_OBJS)  $(ZLIB_DEP) $(CARES_DEP) $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(OPENSSL_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(if $(subst Linux,,$(SYSTEM)),,-Wl$(comma)-wrap$(comma)memcpy) -L$(LIBDIR)/$(CONFIG) -shared -Wl,--output-def=$(LIBDIR)/$(CONFIG)/grpc_csharp_ext$(SHARED_VERSION_CSHARP).def -Wl,--out-implib=$(LIBDIR)/$(CONFIG)/libgrpc_csharp_ext$(SHARED_VERSION_CSHARP)-dll.a -o $(LIBDIR)/$(CONFIG)/grpc_csharp_ext$(SHARED_VERSION_CSHARP).$(SHARED_EXT_CSHARP) $(LIBGRPC_CSHARP_EXT_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBS)
else
$(LIBDIR)/$(CONFIG)/libgrpc_csharp_ext$(SHARED_VERSION_CSHARP).$(SHARED_EXT_CSHARP): $(LIBGRPC_CSHARP_EXT_OBJS)  $(ZLIB_DEP) $(CARES_DEP) $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(OPENSSL_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
ifeq ($(SYSTEM),Darwin)
	$(Q) $(LD) $(LDFLAGS) $(if $(subst Linux,,$(SYSTEM)),,-Wl$(comma)-wrap$(comma)memcpy) -L$(LIBDIR)/$(CONFIG) -install_name $(SHARED_PREFIX)grpc_csharp_ext$(SHARED_VERSION_CSHARP).$(SHARED_EXT_CSHARP) -dynamiclib -o $(LIBDIR)/$(CONFIG)/libgrpc_csharp_ext$(SHARED_VERSION_CSHARP).$(SHARED_EXT_CSHARP) $(LIBGRPC_CSHARP_EXT_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBS)
else
	$(Q) $(LD) $(LDFLAGS) $(if $(subst Linux,,$(SYSTEM)),,-Wl$(comma)-wrap$(comma)memcpy) -L$(LIBDIR)/$(CONFIG) -shared -Wl,-soname,libgrpc_csharp_ext.so.1 -o $(LIBDIR)/$(CONFIG)/libgrpc_csharp_ext$(SHARED_VERSION_CSHARP).$(SHARED_EXT_CSHARP) $(LIBGRPC_CSHARP_EXT_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(ZLIB_MERGE_LIBS) $(CARES_MERGE_LIBS) $(LDLIBS)
	$(Q) ln -sf $(SHARED_PREFIX)grpc_csharp_ext$(SHARED_VERSION_CSHARP).$(SHARED_EXT_CSHARP) $(LIBDIR)/$(CONFIG)/libgrpc_csharp_ext$(SHARED_VERSION_CSHARP).so.1
	$(Q) ln -sf $(SHARED_PREFIX)grpc_csharp_ext$(SHARED_VERSION_CSHARP).$(SHARED_EXT_CSHARP) $(LIBDIR)/$(CONFIG)/libgrpc_csharp_ext$(SHARED_VERSION_CSHARP).so
endif
endif

endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBGRPC_CSHARP_EXT_OBJS:.o=.dep)
endif
endif


LIBBORINGSSL_SRC = \
    src/boringssl/err_data.c \
    third_party/boringssl/crypto/aes/aes.c \
    third_party/boringssl/crypto/aes/key_wrap.c \
    third_party/boringssl/crypto/aes/mode_wrappers.c \
    third_party/boringssl/crypto/asn1/a_bitstr.c \
    third_party/boringssl/crypto/asn1/a_bool.c \
    third_party/boringssl/crypto/asn1/a_d2i_fp.c \
    third_party/boringssl/crypto/asn1/a_dup.c \
    third_party/boringssl/crypto/asn1/a_enum.c \
    third_party/boringssl/crypto/asn1/a_gentm.c \
    third_party/boringssl/crypto/asn1/a_i2d_fp.c \
    third_party/boringssl/crypto/asn1/a_int.c \
    third_party/boringssl/crypto/asn1/a_mbstr.c \
    third_party/boringssl/crypto/asn1/a_object.c \
    third_party/boringssl/crypto/asn1/a_octet.c \
    third_party/boringssl/crypto/asn1/a_print.c \
    third_party/boringssl/crypto/asn1/a_strnid.c \
    third_party/boringssl/crypto/asn1/a_time.c \
    third_party/boringssl/crypto/asn1/a_type.c \
    third_party/boringssl/crypto/asn1/a_utctm.c \
    third_party/boringssl/crypto/asn1/a_utf8.c \
    third_party/boringssl/crypto/asn1/asn1_lib.c \
    third_party/boringssl/crypto/asn1/asn1_par.c \
    third_party/boringssl/crypto/asn1/asn_pack.c \
    third_party/boringssl/crypto/asn1/f_enum.c \
    third_party/boringssl/crypto/asn1/f_int.c \
    third_party/boringssl/crypto/asn1/f_string.c \
    third_party/boringssl/crypto/asn1/t_bitst.c \
    third_party/boringssl/crypto/asn1/tasn_dec.c \
    third_party/boringssl/crypto/asn1/tasn_enc.c \
    third_party/boringssl/crypto/asn1/tasn_fre.c \
    third_party/boringssl/crypto/asn1/tasn_new.c \
    third_party/boringssl/crypto/asn1/tasn_typ.c \
    third_party/boringssl/crypto/asn1/tasn_utl.c \
    third_party/boringssl/crypto/asn1/time_support.c \
    third_party/boringssl/crypto/asn1/x_bignum.c \
    third_party/boringssl/crypto/asn1/x_long.c \
    third_party/boringssl/crypto/base64/base64.c \
    third_party/boringssl/crypto/bio/bio.c \
    third_party/boringssl/crypto/bio/bio_mem.c \
    third_party/boringssl/crypto/bio/connect.c \
    third_party/boringssl/crypto/bio/fd.c \
    third_party/boringssl/crypto/bio/file.c \
    third_party/boringssl/crypto/bio/hexdump.c \
    third_party/boringssl/crypto/bio/pair.c \
    third_party/boringssl/crypto/bio/printf.c \
    third_party/boringssl/crypto/bio/socket.c \
    third_party/boringssl/crypto/bio/socket_helper.c \
    third_party/boringssl/crypto/bn/add.c \
    third_party/boringssl/crypto/bn/asm/x86_64-gcc.c \
    third_party/boringssl/crypto/bn/bn.c \
    third_party/boringssl/crypto/bn/bn_asn1.c \
    third_party/boringssl/crypto/bn/cmp.c \
    third_party/boringssl/crypto/bn/convert.c \
    third_party/boringssl/crypto/bn/ctx.c \
    third_party/boringssl/crypto/bn/div.c \
    third_party/boringssl/crypto/bn/exponentiation.c \
    third_party/boringssl/crypto/bn/gcd.c \
    third_party/boringssl/crypto/bn/generic.c \
    third_party/boringssl/crypto/bn/kronecker.c \
    third_party/boringssl/crypto/bn/montgomery.c \
    third_party/boringssl/crypto/bn/montgomery_inv.c \
    third_party/boringssl/crypto/bn/mul.c \
    third_party/boringssl/crypto/bn/prime.c \
    third_party/boringssl/crypto/bn/random.c \
    third_party/boringssl/crypto/bn/rsaz_exp.c \
    third_party/boringssl/crypto/bn/shift.c \
    third_party/boringssl/crypto/bn/sqrt.c \
    third_party/boringssl/crypto/buf/buf.c \
    third_party/boringssl/crypto/bytestring/asn1_compat.c \
    third_party/boringssl/crypto/bytestring/ber.c \
    third_party/boringssl/crypto/bytestring/cbb.c \
    third_party/boringssl/crypto/bytestring/cbs.c \
    third_party/boringssl/crypto/chacha/chacha.c \
    third_party/boringssl/crypto/cipher/aead.c \
    third_party/boringssl/crypto/cipher/cipher.c \
    third_party/boringssl/crypto/cipher/derive_key.c \
    third_party/boringssl/crypto/cipher/e_aes.c \
    third_party/boringssl/crypto/cipher/e_chacha20poly1305.c \
    third_party/boringssl/crypto/cipher/e_des.c \
    third_party/boringssl/crypto/cipher/e_null.c \
    third_party/boringssl/crypto/cipher/e_rc2.c \
    third_party/boringssl/crypto/cipher/e_rc4.c \
    third_party/boringssl/crypto/cipher/e_ssl3.c \
    third_party/boringssl/crypto/cipher/e_tls.c \
    third_party/boringssl/crypto/cipher/tls_cbc.c \
    third_party/boringssl/crypto/cmac/cmac.c \
    third_party/boringssl/crypto/conf/conf.c \
    third_party/boringssl/crypto/cpu-aarch64-linux.c \
    third_party/boringssl/crypto/cpu-arm-linux.c \
    third_party/boringssl/crypto/cpu-arm.c \
    third_party/boringssl/crypto/cpu-intel.c \
    third_party/boringssl/crypto/cpu-ppc64le.c \
    third_party/boringssl/crypto/crypto.c \
    third_party/boringssl/crypto/curve25519/curve25519.c \
    third_party/boringssl/crypto/curve25519/spake25519.c \
    third_party/boringssl/crypto/curve25519/x25519-x86_64.c \
    third_party/boringssl/crypto/des/des.c \
    third_party/boringssl/crypto/dh/check.c \
    third_party/boringssl/crypto/dh/dh.c \
    third_party/boringssl/crypto/dh/dh_asn1.c \
    third_party/boringssl/crypto/dh/params.c \
    third_party/boringssl/crypto/digest/digest.c \
    third_party/boringssl/crypto/digest/digests.c \
    third_party/boringssl/crypto/dsa/dsa.c \
    third_party/boringssl/crypto/dsa/dsa_asn1.c \
    third_party/boringssl/crypto/ec/ec.c \
    third_party/boringssl/crypto/ec/ec_asn1.c \
    third_party/boringssl/crypto/ec/ec_key.c \
    third_party/boringssl/crypto/ec/ec_montgomery.c \
    third_party/boringssl/crypto/ec/oct.c \
    third_party/boringssl/crypto/ec/p224-64.c \
    third_party/boringssl/crypto/ec/p256-64.c \
    third_party/boringssl/crypto/ec/p256-x86_64.c \
    third_party/boringssl/crypto/ec/simple.c \
    third_party/boringssl/crypto/ec/util-64.c \
    third_party/boringssl/crypto/ec/wnaf.c \
    third_party/boringssl/crypto/ecdh/ecdh.c \
    third_party/boringssl/crypto/ecdsa/ecdsa.c \
    third_party/boringssl/crypto/ecdsa/ecdsa_asn1.c \
    third_party/boringssl/crypto/engine/engine.c \
    third_party/boringssl/crypto/err/err.c \
    third_party/boringssl/crypto/evp/digestsign.c \
    third_party/boringssl/crypto/evp/evp.c \
    third_party/boringssl/crypto/evp/evp_asn1.c \
    third_party/boringssl/crypto/evp/evp_ctx.c \
    third_party/boringssl/crypto/evp/p_dsa_asn1.c \
    third_party/boringssl/crypto/evp/p_ec.c \
    third_party/boringssl/crypto/evp/p_ec_asn1.c \
    third_party/boringssl/crypto/evp/p_rsa.c \
    third_party/boringssl/crypto/evp/p_rsa_asn1.c \
    third_party/boringssl/crypto/evp/pbkdf.c \
    third_party/boringssl/crypto/evp/print.c \
    third_party/boringssl/crypto/evp/sign.c \
    third_party/boringssl/crypto/ex_data.c \
    third_party/boringssl/crypto/hkdf/hkdf.c \
    third_party/boringssl/crypto/hmac/hmac.c \
    third_party/boringssl/crypto/lhash/lhash.c \
    third_party/boringssl/crypto/md4/md4.c \
    third_party/boringssl/crypto/md5/md5.c \
    third_party/boringssl/crypto/mem.c \
    third_party/boringssl/crypto/modes/cbc.c \
    third_party/boringssl/crypto/modes/cfb.c \
    third_party/boringssl/crypto/modes/ctr.c \
    third_party/boringssl/crypto/modes/gcm.c \
    third_party/boringssl/crypto/modes/ofb.c \
    third_party/boringssl/crypto/modes/polyval.c \
    third_party/boringssl/crypto/obj/obj.c \
    third_party/boringssl/crypto/obj/obj_xref.c \
    third_party/boringssl/crypto/pem/pem_all.c \
    third_party/boringssl/crypto/pem/pem_info.c \
    third_party/boringssl/crypto/pem/pem_lib.c \
    third_party/boringssl/crypto/pem/pem_oth.c \
    third_party/boringssl/crypto/pem/pem_pk8.c \
    third_party/boringssl/crypto/pem/pem_pkey.c \
    third_party/boringssl/crypto/pem/pem_x509.c \
    third_party/boringssl/crypto/pem/pem_xaux.c \
    third_party/boringssl/crypto/pkcs8/p5_pbev2.c \
    third_party/boringssl/crypto/pkcs8/p8_pkey.c \
    third_party/boringssl/crypto/pkcs8/pkcs8.c \
    third_party/boringssl/crypto/poly1305/poly1305.c \
    third_party/boringssl/crypto/poly1305/poly1305_arm.c \
    third_party/boringssl/crypto/poly1305/poly1305_vec.c \
    third_party/boringssl/crypto/pool/pool.c \
    third_party/boringssl/crypto/rand/deterministic.c \
    third_party/boringssl/crypto/rand/fuchsia.c \
    third_party/boringssl/crypto/rand/rand.c \
    third_party/boringssl/crypto/rand/urandom.c \
    third_party/boringssl/crypto/rand/windows.c \
    third_party/boringssl/crypto/rc4/rc4.c \
    third_party/boringssl/crypto/refcount_c11.c \
    third_party/boringssl/crypto/refcount_lock.c \
    third_party/boringssl/crypto/rsa/blinding.c \
    third_party/boringssl/crypto/rsa/padding.c \
    third_party/boringssl/crypto/rsa/rsa.c \
    third_party/boringssl/crypto/rsa/rsa_asn1.c \
    third_party/boringssl/crypto/rsa/rsa_impl.c \
    third_party/boringssl/crypto/sha/sha1-altivec.c \
    third_party/boringssl/crypto/sha/sha1.c \
    third_party/boringssl/crypto/sha/sha256.c \
    third_party/boringssl/crypto/sha/sha512.c \
    third_party/boringssl/crypto/stack/stack.c \
    third_party/boringssl/crypto/thread.c \
    third_party/boringssl/crypto/thread_none.c \
    third_party/boringssl/crypto/thread_pthread.c \
    third_party/boringssl/crypto/thread_win.c \
    third_party/boringssl/crypto/x509/a_digest.c \
    third_party/boringssl/crypto/x509/a_sign.c \
    third_party/boringssl/crypto/x509/a_strex.c \
    third_party/boringssl/crypto/x509/a_verify.c \
    third_party/boringssl/crypto/x509/algorithm.c \
    third_party/boringssl/crypto/x509/asn1_gen.c \
    third_party/boringssl/crypto/x509/by_dir.c \
    third_party/boringssl/crypto/x509/by_file.c \
    third_party/boringssl/crypto/x509/i2d_pr.c \
    third_party/boringssl/crypto/x509/pkcs7.c \
    third_party/boringssl/crypto/x509/rsa_pss.c \
    third_party/boringssl/crypto/x509/t_crl.c \
    third_party/boringssl/crypto/x509/t_req.c \
    third_party/boringssl/crypto/x509/t_x509.c \
    third_party/boringssl/crypto/x509/t_x509a.c \
    third_party/boringssl/crypto/x509/x509.c \
    third_party/boringssl/crypto/x509/x509_att.c \
    third_party/boringssl/crypto/x509/x509_cmp.c \
    third_party/boringssl/crypto/x509/x509_d2.c \
    third_party/boringssl/crypto/x509/x509_def.c \
    third_party/boringssl/crypto/x509/x509_ext.c \
    third_party/boringssl/crypto/x509/x509_lu.c \
    third_party/boringssl/crypto/x509/x509_obj.c \
    third_party/boringssl/crypto/x509/x509_r2x.c \
    third_party/boringssl/crypto/x509/x509_req.c \
    third_party/boringssl/crypto/x509/x509_set.c \
    third_party/boringssl/crypto/x509/x509_trs.c \
    third_party/boringssl/crypto/x509/x509_txt.c \
    third_party/boringssl/crypto/x509/x509_v3.c \
    third_party/boringssl/crypto/x509/x509_vfy.c \
    third_party/boringssl/crypto/x509/x509_vpm.c \
    third_party/boringssl/crypto/x509/x509cset.c \
    third_party/boringssl/crypto/x509/x509name.c \
    third_party/boringssl/crypto/x509/x509rset.c \
    third_party/boringssl/crypto/x509/x509spki.c \
    third_party/boringssl/crypto/x509/x509type.c \
    third_party/boringssl/crypto/x509/x_algor.c \
    third_party/boringssl/crypto/x509/x_all.c \
    third_party/boringssl/crypto/x509/x_attrib.c \
    third_party/boringssl/crypto/x509/x_crl.c \
    third_party/boringssl/crypto/x509/x_exten.c \
    third_party/boringssl/crypto/x509/x_info.c \
    third_party/boringssl/crypto/x509/x_name.c \
    third_party/boringssl/crypto/x509/x_pkey.c \
    third_party/boringssl/crypto/x509/x_pubkey.c \
    third_party/boringssl/crypto/x509/x_req.c \
    third_party/boringssl/crypto/x509/x_sig.c \
    third_party/boringssl/crypto/x509/x_spki.c \
    third_party/boringssl/crypto/x509/x_val.c \
    third_party/boringssl/crypto/x509/x_x509.c \
    third_party/boringssl/crypto/x509/x_x509a.c \
    third_party/boringssl/crypto/x509v3/pcy_cache.c \
    third_party/boringssl/crypto/x509v3/pcy_data.c \
    third_party/boringssl/crypto/x509v3/pcy_lib.c \
    third_party/boringssl/crypto/x509v3/pcy_map.c \
    third_party/boringssl/crypto/x509v3/pcy_node.c \
    third_party/boringssl/crypto/x509v3/pcy_tree.c \
    third_party/boringssl/crypto/x509v3/v3_akey.c \
    third_party/boringssl/crypto/x509v3/v3_akeya.c \
    third_party/boringssl/crypto/x509v3/v3_alt.c \
    third_party/boringssl/crypto/x509v3/v3_bcons.c \
    third_party/boringssl/crypto/x509v3/v3_bitst.c \
    third_party/boringssl/crypto/x509v3/v3_conf.c \
    third_party/boringssl/crypto/x509v3/v3_cpols.c \
    third_party/boringssl/crypto/x509v3/v3_crld.c \
    third_party/boringssl/crypto/x509v3/v3_enum.c \
    third_party/boringssl/crypto/x509v3/v3_extku.c \
    third_party/boringssl/crypto/x509v3/v3_genn.c \
    third_party/boringssl/crypto/x509v3/v3_ia5.c \
    third_party/boringssl/crypto/x509v3/v3_info.c \
    third_party/boringssl/crypto/x509v3/v3_int.c \
    third_party/boringssl/crypto/x509v3/v3_lib.c \
    third_party/boringssl/crypto/x509v3/v3_ncons.c \
    third_party/boringssl/crypto/x509v3/v3_pci.c \
    third_party/boringssl/crypto/x509v3/v3_pcia.c \
    third_party/boringssl/crypto/x509v3/v3_pcons.c \
    third_party/boringssl/crypto/x509v3/v3_pku.c \
    third_party/boringssl/crypto/x509v3/v3_pmaps.c \
    third_party/boringssl/crypto/x509v3/v3_prn.c \
    third_party/boringssl/crypto/x509v3/v3_purp.c \
    third_party/boringssl/crypto/x509v3/v3_skey.c \
    third_party/boringssl/crypto/x509v3/v3_sxnet.c \
    third_party/boringssl/crypto/x509v3/v3_utl.c \
    third_party/boringssl/ssl/bio_ssl.c \
    third_party/boringssl/ssl/custom_extensions.c \
    third_party/boringssl/ssl/d1_both.c \
    third_party/boringssl/ssl/d1_lib.c \
    third_party/boringssl/ssl/d1_pkt.c \
    third_party/boringssl/ssl/d1_srtp.c \
    third_party/boringssl/ssl/dtls_method.c \
    third_party/boringssl/ssl/dtls_record.c \
    third_party/boringssl/ssl/handshake_client.c \
    third_party/boringssl/ssl/handshake_server.c \
    third_party/boringssl/ssl/s3_both.c \
    third_party/boringssl/ssl/s3_lib.c \
    third_party/boringssl/ssl/s3_pkt.c \
    third_party/boringssl/ssl/ssl_aead_ctx.c \
    third_party/boringssl/ssl/ssl_asn1.c \
    third_party/boringssl/ssl/ssl_buffer.c \
    third_party/boringssl/ssl/ssl_cert.c \
    third_party/boringssl/ssl/ssl_cipher.c \
    third_party/boringssl/ssl/ssl_ecdh.c \
    third_party/boringssl/ssl/ssl_file.c \
    third_party/boringssl/ssl/ssl_lib.c \
    third_party/boringssl/ssl/ssl_privkey.c \
    third_party/boringssl/ssl/ssl_privkey_cc.cc \
    third_party/boringssl/ssl/ssl_session.c \
    third_party/boringssl/ssl/ssl_stat.c \
    third_party/boringssl/ssl/ssl_transcript.c \
    third_party/boringssl/ssl/ssl_x509.c \
    third_party/boringssl/ssl/t1_enc.c \
    third_party/boringssl/ssl/t1_lib.c \
    third_party/boringssl/ssl/tls13_both.c \
    third_party/boringssl/ssl/tls13_client.c \
    third_party/boringssl/ssl/tls13_enc.c \
    third_party/boringssl/ssl/tls13_server.c \
    third_party/boringssl/ssl/tls_method.c \
    third_party/boringssl/ssl/tls_record.c \

PUBLIC_HEADERS_C += \

LIBBORINGSSL_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_SRC))))

$(LIBBORINGSSL_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

$(LIBDIR)/$(CONFIG)/libboringssl.a: $(ZLIB_DEP) $(CARES_DEP)  $(LIBBORINGSSL_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl.a $(LIBBORINGSSL_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl.a
endif




ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_OBJS:.o=.dep)
endif


LIBBORINGSSL_TEST_UTIL_SRC = \
    third_party/boringssl/crypto/test/file_test.cc \
    third_party/boringssl/crypto/test/malloc.cc \
    third_party/boringssl/crypto/test/test_util.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_TEST_UTIL_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_TEST_UTIL_SRC))))

$(LIBBORINGSSL_TEST_UTIL_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_TEST_UTIL_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_test_util.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_test_util.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_TEST_UTIL_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBBORINGSSL_TEST_UTIL_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_TEST_UTIL_OBJS:.o=.dep)
endif


LIBBORINGSSL_AES_TEST_LIB_SRC = \
    third_party/boringssl/crypto/aes/aes_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_AES_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_AES_TEST_LIB_SRC))))

$(LIBBORINGSSL_AES_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_AES_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_aes_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_aes_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_AES_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_aes_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_aes_test_lib.a $(LIBBORINGSSL_AES_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_aes_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_AES_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_ASN1_TEST_LIB_SRC = \
    third_party/boringssl/crypto/asn1/asn1_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_ASN1_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_ASN1_TEST_LIB_SRC))))

$(LIBBORINGSSL_ASN1_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_ASN1_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_asn1_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_asn1_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_ASN1_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_asn1_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_asn1_test_lib.a $(LIBBORINGSSL_ASN1_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_asn1_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_ASN1_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_BASE64_TEST_LIB_SRC = \
    third_party/boringssl/crypto/base64/base64_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_BASE64_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_BASE64_TEST_LIB_SRC))))

$(LIBBORINGSSL_BASE64_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_BASE64_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_base64_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_base64_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_BASE64_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_base64_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_base64_test_lib.a $(LIBBORINGSSL_BASE64_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_base64_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_BASE64_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_BIO_TEST_LIB_SRC = \
    third_party/boringssl/crypto/bio/bio_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_BIO_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_BIO_TEST_LIB_SRC))))

$(LIBBORINGSSL_BIO_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_BIO_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_bio_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_bio_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_BIO_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_bio_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_bio_test_lib.a $(LIBBORINGSSL_BIO_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_bio_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_BIO_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_BN_TEST_LIB_SRC = \
    third_party/boringssl/crypto/bn/bn_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_BN_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_BN_TEST_LIB_SRC))))

$(LIBBORINGSSL_BN_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_BN_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_bn_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_bn_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_BN_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_bn_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_bn_test_lib.a $(LIBBORINGSSL_BN_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_bn_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_BN_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_BYTESTRING_TEST_LIB_SRC = \
    third_party/boringssl/crypto/bytestring/bytestring_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_BYTESTRING_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_BYTESTRING_TEST_LIB_SRC))))

$(LIBBORINGSSL_BYTESTRING_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_BYTESTRING_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_bytestring_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_bytestring_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_BYTESTRING_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_bytestring_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_bytestring_test_lib.a $(LIBBORINGSSL_BYTESTRING_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_bytestring_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_BYTESTRING_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_AEAD_TEST_LIB_SRC = \
    third_party/boringssl/crypto/cipher/aead_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_AEAD_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_AEAD_TEST_LIB_SRC))))

$(LIBBORINGSSL_AEAD_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_AEAD_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_aead_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_aead_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_AEAD_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_aead_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_aead_test_lib.a $(LIBBORINGSSL_AEAD_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_aead_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_AEAD_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_CIPHER_TEST_LIB_SRC = \
    third_party/boringssl/crypto/cipher/cipher_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_CIPHER_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_CIPHER_TEST_LIB_SRC))))

$(LIBBORINGSSL_CIPHER_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_CIPHER_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_cipher_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_cipher_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_CIPHER_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_cipher_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_cipher_test_lib.a $(LIBBORINGSSL_CIPHER_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_cipher_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_CIPHER_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_CMAC_TEST_LIB_SRC = \
    third_party/boringssl/crypto/cmac/cmac_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_CMAC_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_CMAC_TEST_LIB_SRC))))

$(LIBBORINGSSL_CMAC_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_CMAC_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_cmac_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_cmac_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_CMAC_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_cmac_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_cmac_test_lib.a $(LIBBORINGSSL_CMAC_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_cmac_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_CMAC_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_CONSTANT_TIME_TEST_LIB_SRC = \
    third_party/boringssl/crypto/constant_time_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_CONSTANT_TIME_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_CONSTANT_TIME_TEST_LIB_SRC))))

$(LIBBORINGSSL_CONSTANT_TIME_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_CONSTANT_TIME_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_constant_time_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_constant_time_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_CONSTANT_TIME_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_constant_time_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_constant_time_test_lib.a $(LIBBORINGSSL_CONSTANT_TIME_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_constant_time_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_CONSTANT_TIME_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_ED25519_TEST_LIB_SRC = \
    third_party/boringssl/crypto/curve25519/ed25519_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_ED25519_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_ED25519_TEST_LIB_SRC))))

$(LIBBORINGSSL_ED25519_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_ED25519_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_ed25519_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_ed25519_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_ED25519_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_ed25519_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_ed25519_test_lib.a $(LIBBORINGSSL_ED25519_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_ed25519_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_ED25519_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_SPAKE25519_TEST_LIB_SRC = \
    third_party/boringssl/crypto/curve25519/spake25519_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_SPAKE25519_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_SPAKE25519_TEST_LIB_SRC))))

$(LIBBORINGSSL_SPAKE25519_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_SPAKE25519_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_spake25519_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_spake25519_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_SPAKE25519_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_spake25519_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_spake25519_test_lib.a $(LIBBORINGSSL_SPAKE25519_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_spake25519_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_SPAKE25519_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_X25519_TEST_LIB_SRC = \
    third_party/boringssl/crypto/curve25519/x25519_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_X25519_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_X25519_TEST_LIB_SRC))))

$(LIBBORINGSSL_X25519_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_X25519_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_x25519_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_x25519_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_X25519_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_x25519_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_x25519_test_lib.a $(LIBBORINGSSL_X25519_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_x25519_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_X25519_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_DIGEST_TEST_LIB_SRC = \
    third_party/boringssl/crypto/digest/digest_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_DIGEST_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_DIGEST_TEST_LIB_SRC))))

$(LIBBORINGSSL_DIGEST_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_DIGEST_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_digest_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_digest_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_DIGEST_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_digest_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_digest_test_lib.a $(LIBBORINGSSL_DIGEST_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_digest_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_DIGEST_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_EXAMPLE_MUL_LIB_SRC = \
    third_party/boringssl/crypto/ec/example_mul.c \

PUBLIC_HEADERS_C += \

LIBBORINGSSL_EXAMPLE_MUL_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_EXAMPLE_MUL_LIB_SRC))))

$(LIBBORINGSSL_EXAMPLE_MUL_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_EXAMPLE_MUL_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

$(LIBDIR)/$(CONFIG)/libboringssl_example_mul_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(LIBBORINGSSL_EXAMPLE_MUL_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_example_mul_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_example_mul_lib.a $(LIBBORINGSSL_EXAMPLE_MUL_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_example_mul_lib.a
endif




ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_EXAMPLE_MUL_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_P256-X86_64_TEST_LIB_SRC = \
    third_party/boringssl/crypto/ec/p256-x86_64_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_P256-X86_64_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_P256-X86_64_TEST_LIB_SRC))))

$(LIBBORINGSSL_P256-X86_64_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_P256-X86_64_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_p256-x86_64_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_p256-x86_64_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_P256-X86_64_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_p256-x86_64_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_p256-x86_64_test_lib.a $(LIBBORINGSSL_P256-X86_64_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_p256-x86_64_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_P256-X86_64_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_ECDH_TEST_LIB_SRC = \
    third_party/boringssl/crypto/ecdh/ecdh_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_ECDH_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_ECDH_TEST_LIB_SRC))))

$(LIBBORINGSSL_ECDH_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_ECDH_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_ecdh_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_ecdh_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_ECDH_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_ecdh_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_ecdh_test_lib.a $(LIBBORINGSSL_ECDH_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_ecdh_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_ECDH_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_ECDSA_SIGN_TEST_LIB_SRC = \
    third_party/boringssl/crypto/ecdsa/ecdsa_sign_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_ECDSA_SIGN_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_ECDSA_SIGN_TEST_LIB_SRC))))

$(LIBBORINGSSL_ECDSA_SIGN_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_ECDSA_SIGN_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_sign_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_sign_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_ECDSA_SIGN_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_sign_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_sign_test_lib.a $(LIBBORINGSSL_ECDSA_SIGN_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_sign_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_ECDSA_SIGN_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_ECDSA_TEST_LIB_SRC = \
    third_party/boringssl/crypto/ecdsa/ecdsa_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_ECDSA_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_ECDSA_TEST_LIB_SRC))))

$(LIBBORINGSSL_ECDSA_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_ECDSA_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_ECDSA_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_test_lib.a $(LIBBORINGSSL_ECDSA_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_ECDSA_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_ECDSA_VERIFY_TEST_LIB_SRC = \
    third_party/boringssl/crypto/ecdsa/ecdsa_verify_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_ECDSA_VERIFY_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_ECDSA_VERIFY_TEST_LIB_SRC))))

$(LIBBORINGSSL_ECDSA_VERIFY_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_ECDSA_VERIFY_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_verify_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_verify_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_ECDSA_VERIFY_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_verify_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_verify_test_lib.a $(LIBBORINGSSL_ECDSA_VERIFY_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_verify_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_ECDSA_VERIFY_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_EVP_EXTRA_TEST_LIB_SRC = \
    third_party/boringssl/crypto/evp/evp_extra_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_EVP_EXTRA_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_EVP_EXTRA_TEST_LIB_SRC))))

$(LIBBORINGSSL_EVP_EXTRA_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_EVP_EXTRA_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_evp_extra_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_evp_extra_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_EVP_EXTRA_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_evp_extra_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_evp_extra_test_lib.a $(LIBBORINGSSL_EVP_EXTRA_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_evp_extra_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_EVP_EXTRA_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_EVP_TEST_LIB_SRC = \
    third_party/boringssl/crypto/evp/evp_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_EVP_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_EVP_TEST_LIB_SRC))))

$(LIBBORINGSSL_EVP_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_EVP_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_evp_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_evp_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_EVP_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_evp_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_evp_test_lib.a $(LIBBORINGSSL_EVP_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_evp_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_EVP_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_PBKDF_TEST_LIB_SRC = \
    third_party/boringssl/crypto/evp/pbkdf_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_PBKDF_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_PBKDF_TEST_LIB_SRC))))

$(LIBBORINGSSL_PBKDF_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_PBKDF_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_pbkdf_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_pbkdf_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_PBKDF_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_pbkdf_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_pbkdf_test_lib.a $(LIBBORINGSSL_PBKDF_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_pbkdf_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_PBKDF_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_HKDF_TEST_LIB_SRC = \
    third_party/boringssl/crypto/hkdf/hkdf_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_HKDF_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_HKDF_TEST_LIB_SRC))))

$(LIBBORINGSSL_HKDF_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_HKDF_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_hkdf_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_hkdf_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_HKDF_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_hkdf_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_hkdf_test_lib.a $(LIBBORINGSSL_HKDF_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_hkdf_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_HKDF_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_HMAC_TEST_LIB_SRC = \
    third_party/boringssl/crypto/hmac/hmac_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_HMAC_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_HMAC_TEST_LIB_SRC))))

$(LIBBORINGSSL_HMAC_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_HMAC_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_hmac_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_hmac_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_HMAC_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_hmac_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_hmac_test_lib.a $(LIBBORINGSSL_HMAC_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_hmac_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_HMAC_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_LHASH_TEST_LIB_SRC = \
    third_party/boringssl/crypto/lhash/lhash_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_LHASH_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_LHASH_TEST_LIB_SRC))))

$(LIBBORINGSSL_LHASH_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_LHASH_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_lhash_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_lhash_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_LHASH_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_lhash_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_lhash_test_lib.a $(LIBBORINGSSL_LHASH_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_lhash_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_LHASH_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_GCM_TEST_LIB_SRC = \
    third_party/boringssl/crypto/modes/gcm_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_GCM_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_GCM_TEST_LIB_SRC))))

$(LIBBORINGSSL_GCM_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_GCM_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_gcm_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_gcm_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_GCM_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_gcm_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_gcm_test_lib.a $(LIBBORINGSSL_GCM_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_gcm_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_GCM_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_OBJ_TEST_LIB_SRC = \
    third_party/boringssl/crypto/obj/obj_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_OBJ_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_OBJ_TEST_LIB_SRC))))

$(LIBBORINGSSL_OBJ_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_OBJ_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_obj_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_obj_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_OBJ_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_obj_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_obj_test_lib.a $(LIBBORINGSSL_OBJ_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_obj_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_OBJ_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_PKCS12_TEST_LIB_SRC = \
    third_party/boringssl/crypto/pkcs8/pkcs12_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_PKCS12_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_PKCS12_TEST_LIB_SRC))))

$(LIBBORINGSSL_PKCS12_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_PKCS12_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_pkcs12_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_pkcs12_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_PKCS12_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_pkcs12_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_pkcs12_test_lib.a $(LIBBORINGSSL_PKCS12_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_pkcs12_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_PKCS12_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_PKCS8_TEST_LIB_SRC = \
    third_party/boringssl/crypto/pkcs8/pkcs8_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_PKCS8_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_PKCS8_TEST_LIB_SRC))))

$(LIBBORINGSSL_PKCS8_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_PKCS8_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_pkcs8_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_pkcs8_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_PKCS8_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_pkcs8_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_pkcs8_test_lib.a $(LIBBORINGSSL_PKCS8_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_pkcs8_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_PKCS8_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_POLY1305_TEST_LIB_SRC = \
    third_party/boringssl/crypto/poly1305/poly1305_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_POLY1305_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_POLY1305_TEST_LIB_SRC))))

$(LIBBORINGSSL_POLY1305_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_POLY1305_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_poly1305_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_poly1305_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_POLY1305_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_poly1305_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_poly1305_test_lib.a $(LIBBORINGSSL_POLY1305_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_poly1305_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_POLY1305_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_POOL_TEST_LIB_SRC = \
    third_party/boringssl/crypto/pool/pool_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_POOL_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_POOL_TEST_LIB_SRC))))

$(LIBBORINGSSL_POOL_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_POOL_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_pool_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_pool_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_POOL_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_pool_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_pool_test_lib.a $(LIBBORINGSSL_POOL_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_pool_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_POOL_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_REFCOUNT_TEST_LIB_SRC = \
    third_party/boringssl/crypto/refcount_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_REFCOUNT_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_REFCOUNT_TEST_LIB_SRC))))

$(LIBBORINGSSL_REFCOUNT_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_REFCOUNT_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_refcount_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_refcount_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_REFCOUNT_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_refcount_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_refcount_test_lib.a $(LIBBORINGSSL_REFCOUNT_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_refcount_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_REFCOUNT_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_THREAD_TEST_LIB_SRC = \
    third_party/boringssl/crypto/thread_test.c \

PUBLIC_HEADERS_C += \

LIBBORINGSSL_THREAD_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_THREAD_TEST_LIB_SRC))))

$(LIBBORINGSSL_THREAD_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_THREAD_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

$(LIBDIR)/$(CONFIG)/libboringssl_thread_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(LIBBORINGSSL_THREAD_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_thread_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_thread_test_lib.a $(LIBBORINGSSL_THREAD_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_thread_test_lib.a
endif




ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_THREAD_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_PKCS7_TEST_LIB_SRC = \
    third_party/boringssl/crypto/x509/pkcs7_test.c \

PUBLIC_HEADERS_C += \

LIBBORINGSSL_PKCS7_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_PKCS7_TEST_LIB_SRC))))

$(LIBBORINGSSL_PKCS7_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_PKCS7_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

$(LIBDIR)/$(CONFIG)/libboringssl_pkcs7_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(LIBBORINGSSL_PKCS7_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_pkcs7_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_pkcs7_test_lib.a $(LIBBORINGSSL_PKCS7_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_pkcs7_test_lib.a
endif




ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_PKCS7_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_X509_TEST_LIB_SRC = \
    third_party/boringssl/crypto/x509/x509_test.cc \

PUBLIC_HEADERS_CXX += \

LIBBORINGSSL_X509_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_X509_TEST_LIB_SRC))))

$(LIBBORINGSSL_X509_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_X509_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libboringssl_x509_test_lib.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libboringssl_x509_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBORINGSSL_X509_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_x509_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_x509_test_lib.a $(LIBBORINGSSL_X509_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_x509_test_lib.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_X509_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_TAB_TEST_LIB_SRC = \
    third_party/boringssl/crypto/x509v3/tab_test.c \

PUBLIC_HEADERS_C += \

LIBBORINGSSL_TAB_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_TAB_TEST_LIB_SRC))))

$(LIBBORINGSSL_TAB_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_TAB_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

$(LIBDIR)/$(CONFIG)/libboringssl_tab_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(LIBBORINGSSL_TAB_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_tab_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_tab_test_lib.a $(LIBBORINGSSL_TAB_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_tab_test_lib.a
endif




ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_TAB_TEST_LIB_OBJS:.o=.dep)
endif


LIBBORINGSSL_V3NAME_TEST_LIB_SRC = \
    third_party/boringssl/crypto/x509v3/v3name_test.c \

PUBLIC_HEADERS_C += \

LIBBORINGSSL_V3NAME_TEST_LIB_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_V3NAME_TEST_LIB_SRC))))

$(LIBBORINGSSL_V3NAME_TEST_LIB_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_V3NAME_TEST_LIB_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)

$(LIBDIR)/$(CONFIG)/libboringssl_v3name_test_lib.a: $(ZLIB_DEP) $(CARES_DEP)  $(LIBBORINGSSL_V3NAME_TEST_LIB_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl_v3name_test_lib.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libboringssl_v3name_test_lib.a $(LIBBORINGSSL_V3NAME_TEST_LIB_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libboringssl_v3name_test_lib.a
endif




ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_V3NAME_TEST_LIB_OBJS:.o=.dep)
endif


LIBBENCHMARK_SRC = \
    third_party/benchmark/src/benchmark.cc \
    third_party/benchmark/src/benchmark_register.cc \
    third_party/benchmark/src/colorprint.cc \
    third_party/benchmark/src/commandlineflags.cc \
    third_party/benchmark/src/complexity.cc \
    third_party/benchmark/src/console_reporter.cc \
    third_party/benchmark/src/csv_reporter.cc \
    third_party/benchmark/src/json_reporter.cc \
    third_party/benchmark/src/reporter.cc \
    third_party/benchmark/src/sleep.cc \
    third_party/benchmark/src/string_util.cc \
    third_party/benchmark/src/sysinfo.cc \
    third_party/benchmark/src/timers.cc \

PUBLIC_HEADERS_CXX += \

LIBBENCHMARK_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBENCHMARK_SRC))))

$(LIBBENCHMARK_OBJS): CPPFLAGS += -Ithird_party/benchmark/include -DHAVE_POSIX_REGEX

ifeq ($(NO_PROTOBUF),true)

# You can't build a C++ library if you don't have protobuf - a bit overreached, but still okay.

$(LIBDIR)/$(CONFIG)/libbenchmark.a: protobuf_dep_error


else

$(LIBDIR)/$(CONFIG)/libbenchmark.a: $(ZLIB_DEP) $(CARES_DEP)  $(PROTOBUF_DEP) $(LIBBENCHMARK_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libbenchmark.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBBENCHMARK_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libbenchmark.a
endif




endif

ifneq ($(NO_DEPS),true)
-include $(LIBBENCHMARK_OBJS:.o=.dep)
endif


LIBZ_SRC = \
    third_party/zlib/adler32.c \
    third_party/zlib/compress.c \
    third_party/zlib/crc32.c \
    third_party/zlib/deflate.c \
    third_party/zlib/gzclose.c \
    third_party/zlib/gzlib.c \
    third_party/zlib/gzread.c \
    third_party/zlib/gzwrite.c \
    third_party/zlib/infback.c \
    third_party/zlib/inffast.c \
    third_party/zlib/inflate.c \
    third_party/zlib/inftrees.c \
    third_party/zlib/trees.c \
    third_party/zlib/uncompr.c \
    third_party/zlib/zutil.c \

PUBLIC_HEADERS_C += \

LIBZ_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBZ_SRC))))

$(LIBZ_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-implicit-function-declaration $(W_NO_SHIFT_NEGATIVE_VALUE) -fvisibility=hidden

$(LIBDIR)/$(CONFIG)/libz.a: $(CARES_DEP)  $(LIBZ_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libz.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libz.a $(LIBZ_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libz.a
endif




ifneq ($(NO_DEPS),true)
-include $(LIBZ_OBJS:.o=.dep)
endif


LIBARES_SRC = \
    third_party/cares/cares/ares__close_sockets.c \
    third_party/cares/cares/ares__get_hostent.c \
    third_party/cares/cares/ares__read_line.c \
    third_party/cares/cares/ares__timeval.c \
    third_party/cares/cares/ares_cancel.c \
    third_party/cares/cares/ares_create_query.c \
    third_party/cares/cares/ares_data.c \
    third_party/cares/cares/ares_destroy.c \
    third_party/cares/cares/ares_expand_name.c \
    third_party/cares/cares/ares_expand_string.c \
    third_party/cares/cares/ares_fds.c \
    third_party/cares/cares/ares_free_hostent.c \
    third_party/cares/cares/ares_free_string.c \
    third_party/cares/cares/ares_getenv.c \
    third_party/cares/cares/ares_gethostbyaddr.c \
    third_party/cares/cares/ares_gethostbyname.c \
    third_party/cares/cares/ares_getnameinfo.c \
    third_party/cares/cares/ares_getopt.c \
    third_party/cares/cares/ares_getsock.c \
    third_party/cares/cares/ares_init.c \
    third_party/cares/cares/ares_library_init.c \
    third_party/cares/cares/ares_llist.c \
    third_party/cares/cares/ares_mkquery.c \
    third_party/cares/cares/ares_nowarn.c \
    third_party/cares/cares/ares_options.c \
    third_party/cares/cares/ares_parse_a_reply.c \
    third_party/cares/cares/ares_parse_aaaa_reply.c \
    third_party/cares/cares/ares_parse_mx_reply.c \
    third_party/cares/cares/ares_parse_naptr_reply.c \
    third_party/cares/cares/ares_parse_ns_reply.c \
    third_party/cares/cares/ares_parse_ptr_reply.c \
    third_party/cares/cares/ares_parse_soa_reply.c \
    third_party/cares/cares/ares_parse_srv_reply.c \
    third_party/cares/cares/ares_parse_txt_reply.c \
    third_party/cares/cares/ares_platform.c \
    third_party/cares/cares/ares_process.c \
    third_party/cares/cares/ares_query.c \
    third_party/cares/cares/ares_search.c \
    third_party/cares/cares/ares_send.c \
    third_party/cares/cares/ares_strcasecmp.c \
    third_party/cares/cares/ares_strdup.c \
    third_party/cares/cares/ares_strerror.c \
    third_party/cares/cares/ares_timeout.c \
    third_party/cares/cares/ares_version.c \
    third_party/cares/cares/ares_writev.c \
    third_party/cares/cares/bitncmp.c \
    third_party/cares/cares/inet_net_pton.c \
    third_party/cares/cares/inet_ntop.c \
    third_party/cares/cares/windows_port.c \

PUBLIC_HEADERS_C += \

LIBARES_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBARES_SRC))))

$(LIBARES_OBJS): CPPFLAGS += -Ithird_party/cares -Ithird_party/cares/cares $(if $(subst Linux,,$(SYSTEM)),,-Ithird_party/cares/config_linux) $(if $(subst Darwin,,$(SYSTEM)),,-Ithird_party/cares/config_darwin) -fvisibility=hidden -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX $(if $(subst MINGW32,,$(SYSTEM)),-DHAVE_CONFIG_H,)
$(LIBARES_OBJS): CFLAGS += -Wno-sign-conversion $(if $(subst MINGW32,,$(SYSTEM)),-Wno-invalid-source-encoding,)

$(LIBDIR)/$(CONFIG)/libares.a: $(ZLIB_DEP)  $(LIBARES_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libares.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libares.a $(LIBARES_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libares.a
endif




ifneq ($(NO_DEPS),true)
-include $(LIBARES_OBJS:.o=.dep)
endif


LIBBAD_CLIENT_TEST_SRC = \
    test/core/bad_client/bad_client.c \

PUBLIC_HEADERS_C += \

LIBBAD_CLIENT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBAD_CLIENT_TEST_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libbad_client_test.a: openssl_dep_error


else


$(LIBDIR)/$(CONFIG)/libbad_client_test.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(LIBBAD_CLIENT_TEST_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libbad_client_test.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBBAD_CLIENT_TEST_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libbad_client_test.a
endif




endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBBAD_CLIENT_TEST_OBJS:.o=.dep)
endif
endif


LIBBAD_SSL_TEST_SERVER_SRC = \
    test/core/bad_ssl/server_common.c \

PUBLIC_HEADERS_C += \

LIBBAD_SSL_TEST_SERVER_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBAD_SSL_TEST_SERVER_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libbad_ssl_test_server.a: openssl_dep_error


else


$(LIBDIR)/$(CONFIG)/libbad_ssl_test_server.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(LIBBAD_SSL_TEST_SERVER_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libbad_ssl_test_server.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libbad_ssl_test_server.a $(LIBBAD_SSL_TEST_SERVER_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libbad_ssl_test_server.a
endif




endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBBAD_SSL_TEST_SERVER_OBJS:.o=.dep)
endif
endif


LIBEND2END_TESTS_SRC = \
    test/core/end2end/end2end_tests.c \
    test/core/end2end/end2end_test_utils.c \
    test/core/end2end/tests/authority_not_supported.c \
    test/core/end2end/tests/bad_hostname.c \
    test/core/end2end/tests/bad_ping.c \
    test/core/end2end/tests/binary_metadata.c \
    test/core/end2end/tests/call_creds.c \
    test/core/end2end/tests/cancel_after_accept.c \
    test/core/end2end/tests/cancel_after_client_done.c \
    test/core/end2end/tests/cancel_after_invoke.c \
    test/core/end2end/tests/cancel_after_round_trip.c \
    test/core/end2end/tests/cancel_before_invoke.c \
    test/core/end2end/tests/cancel_in_a_vacuum.c \
    test/core/end2end/tests/cancel_with_status.c \
    test/core/end2end/tests/compressed_payload.c \
    test/core/end2end/tests/connectivity.c \
    test/core/end2end/tests/default_host.c \
    test/core/end2end/tests/disappearing_server.c \
    test/core/end2end/tests/empty_batch.c \
    test/core/end2end/tests/filter_call_init_fails.c \
    test/core/end2end/tests/filter_causes_close.c \
    test/core/end2end/tests/filter_latency.c \
    test/core/end2end/tests/graceful_server_shutdown.c \
    test/core/end2end/tests/high_initial_seqno.c \
    test/core/end2end/tests/hpack_size.c \
    test/core/end2end/tests/idempotent_request.c \
    test/core/end2end/tests/invoke_large_request.c \
    test/core/end2end/tests/keepalive_timeout.c \
    test/core/end2end/tests/large_metadata.c \
    test/core/end2end/tests/load_reporting_hook.c \
    test/core/end2end/tests/max_concurrent_streams.c \
    test/core/end2end/tests/max_connection_age.c \
    test/core/end2end/tests/max_connection_idle.c \
    test/core/end2end/tests/max_message_length.c \
    test/core/end2end/tests/negative_deadline.c \
    test/core/end2end/tests/network_status_change.c \
    test/core/end2end/tests/no_logging.c \
    test/core/end2end/tests/no_op.c \
    test/core/end2end/tests/payload.c \
    test/core/end2end/tests/ping.c \
    test/core/end2end/tests/ping_pong_streaming.c \
    test/core/end2end/tests/proxy_auth.c \
    test/core/end2end/tests/registered_call.c \
    test/core/end2end/tests/request_with_flags.c \
    test/core/end2end/tests/request_with_payload.c \
    test/core/end2end/tests/resource_quota_server.c \
    test/core/end2end/tests/server_finishes_request.c \
    test/core/end2end/tests/shutdown_finishes_calls.c \
    test/core/end2end/tests/shutdown_finishes_tags.c \
    test/core/end2end/tests/simple_cacheable_request.c \
    test/core/end2end/tests/simple_delayed_request.c \
    test/core/end2end/tests/simple_metadata.c \
    test/core/end2end/tests/simple_request.c \
    test/core/end2end/tests/streaming_error_response.c \
    test/core/end2end/tests/trailing_metadata.c \
    test/core/end2end/tests/workaround_cronet_compression.c \
    test/core/end2end/tests/write_buffering.c \
    test/core/end2end/tests/write_buffering_at_end.c \

PUBLIC_HEADERS_C += \

LIBEND2END_TESTS_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBEND2END_TESTS_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libend2end_tests.a: openssl_dep_error


else


$(LIBDIR)/$(CONFIG)/libend2end_tests.a: $(ZLIB_DEP) $(OPENSSL_DEP) $(CARES_DEP) $(LIBEND2END_TESTS_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libend2end_tests.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBEND2END_TESTS_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libend2end_tests.a
endif




endif

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_TESTS_OBJS:.o=.dep)
endif
endif


LIBEND2END_NOSEC_TESTS_SRC = \
    test/core/end2end/end2end_nosec_tests.c \
    test/core/end2end/end2end_test_utils.c \
    test/core/end2end/tests/authority_not_supported.c \
    test/core/end2end/tests/bad_hostname.c \
    test/core/end2end/tests/bad_ping.c \
    test/core/end2end/tests/binary_metadata.c \
    test/core/end2end/tests/cancel_after_accept.c \
    test/core/end2end/tests/cancel_after_client_done.c \
    test/core/end2end/tests/cancel_after_invoke.c \
    test/core/end2end/tests/cancel_after_round_trip.c \
    test/core/end2end/tests/cancel_before_invoke.c \
    test/core/end2end/tests/cancel_in_a_vacuum.c \
    test/core/end2end/tests/cancel_with_status.c \
    test/core/end2end/tests/compressed_payload.c \
    test/core/end2end/tests/connectivity.c \
    test/core/end2end/tests/default_host.c \
    test/core/end2end/tests/disappearing_server.c \
    test/core/end2end/tests/empty_batch.c \
    test/core/end2end/tests/filter_call_init_fails.c \
    test/core/end2end/tests/filter_causes_close.c \
    test/core/end2end/tests/filter_latency.c \
    test/core/end2end/tests/graceful_server_shutdown.c \
    test/core/end2end/tests/high_initial_seqno.c \
    test/core/end2end/tests/hpack_size.c \
    test/core/end2end/tests/idempotent_request.c \
    test/core/end2end/tests/invoke_large_request.c \
    test/core/end2end/tests/keepalive_timeout.c \
    test/core/end2end/tests/large_metadata.c \
    test/core/end2end/tests/load_reporting_hook.c \
    test/core/end2end/tests/max_concurrent_streams.c \
    test/core/end2end/tests/max_connection_age.c \
    test/core/end2end/tests/max_connection_idle.c \
    test/core/end2end/tests/max_message_length.c \
    test/core/end2end/tests/negative_deadline.c \
    test/core/end2end/tests/network_status_change.c \
    test/core/end2end/tests/no_logging.c \
    test/core/end2end/tests/no_op.c \
    test/core/end2end/tests/payload.c \
    test/core/end2end/tests/ping.c \
    test/core/end2end/tests/ping_pong_streaming.c \
    test/core/end2end/tests/proxy_auth.c \
    test/core/end2end/tests/registered_call.c \
    test/core/end2end/tests/request_with_flags.c \
    test/core/end2end/tests/request_with_payload.c \
    test/core/end2end/tests/resource_quota_server.c \
    test/core/end2end/tests/server_finishes_request.c \
    test/core/end2end/tests/shutdown_finishes_calls.c \
    test/core/end2end/tests/shutdown_finishes_tags.c \
    test/core/end2end/tests/simple_cacheable_request.c \
    test/core/end2end/tests/simple_delayed_request.c \
    test/core/end2end/tests/simple_metadata.c \
    test/core/end2end/tests/simple_request.c \
    test/core/end2end/tests/streaming_error_response.c \
    test/core/end2end/tests/trailing_metadata.c \
    test/core/end2end/tests/workaround_cronet_compression.c \
    test/core/end2end/tests/write_buffering.c \
    test/core/end2end/tests/write_buffering_at_end.c \

PUBLIC_HEADERS_C += \

LIBEND2END_NOSEC_TESTS_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBEND2END_NOSEC_TESTS_SRC))))


$(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a: $(ZLIB_DEP) $(CARES_DEP)  $(LIBEND2END_NOSEC_TESTS_OBJS) 
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a
	$(Q) $(AR) $(AROPTS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBEND2END_NOSEC_TESTS_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) ranlib -no_warning_for_no_symbols $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a
endif




ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_NOSEC_TESTS_OBJS:.o=.dep)
endif



# All of the test targets, and protoc plugins


ALARM_TEST_SRC = \
    test/core/surface/alarm_test.c \

ALARM_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(ALARM_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/alarm_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/alarm_test: $(ALARM_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(ALARM_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/alarm_test

endif

$(OBJDIR)/$(CONFIG)/test/core/surface/alarm_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_alarm_test: $(ALARM_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(ALARM_TEST_OBJS:.o=.dep)
endif
endif


ALGORITHM_TEST_SRC = \
    test/core/compression/algorithm_test.c \

ALGORITHM_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(ALGORITHM_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/algorithm_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/algorithm_test: $(ALGORITHM_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(ALGORITHM_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/algorithm_test

endif

$(OBJDIR)/$(CONFIG)/test/core/compression/algorithm_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_algorithm_test: $(ALGORITHM_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(ALGORITHM_TEST_OBJS:.o=.dep)
endif
endif


ALLOC_TEST_SRC = \
    test/core/support/alloc_test.c \

ALLOC_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(ALLOC_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/alloc_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/alloc_test: $(ALLOC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(ALLOC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/alloc_test

endif

$(OBJDIR)/$(CONFIG)/test/core/support/alloc_test.o:  $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_alloc_test: $(ALLOC_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(ALLOC_TEST_OBJS:.o=.dep)
endif
endif


ALPN_TEST_SRC = \
    test/core/transport/chttp2/alpn_test.c \

ALPN_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(ALPN_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/alpn_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/alpn_test: $(ALPN_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(ALPN_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/alpn_test

endif

$(OBJDIR)/$(CONFIG)/test/core/transport/chttp2/alpn_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_alpn_test: $(ALPN_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(ALPN_TEST_OBJS:.o=.dep)
endif
endif


API_FUZZER_SRC = \
    test/core/end2end/fuzzers/api_fuzzer.c \

API_FUZZER_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(API_FUZZER_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/api_fuzzer: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/api_fuzzer: $(API_FUZZER_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(API_FUZZER_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -lFuzzer -o $(BINDIR)/$(CONFIG)/api_fuzzer

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fuzzers/api_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_api_fuzzer: $(API_FUZZER_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(API_FUZZER_OBJS:.o=.dep)
endif
endif


ARENA_TEST_SRC = \
    test/core/support/arena_test.c \

ARENA_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(ARENA_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/arena_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/arena_test: $(ARENA_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(ARENA_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/arena_test

endif

$(OBJDIR)/$(CONFIG)/test/core/support/arena_test.o:  $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_arena_test: $(ARENA_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(ARENA_TEST_OBJS:.o=.dep)
endif
endif


BAD_SERVER_RESPONSE_TEST_SRC = \
    test/core/end2end/bad_server_response_test.c \

BAD_SERVER_RESPONSE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(BAD_SERVER_RESPONSE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/bad_server_response_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/bad_server_response_test: $(BAD_SERVER_RESPONSE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libtest_tcp_server.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(BAD_SERVER_RESPONSE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libtest_tcp_server.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/bad_server_response_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/bad_server_response_test.o:  $(LIBDIR)/$(CONFIG)/libtest_tcp_server.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_bad_server_response_test: $(BAD_SERVER_RESPONSE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(BAD_SERVER_RESPONSE_TEST_OBJS:.o=.dep)
endif
endif


BDP_ESTIMATOR_TEST_SRC = \
    test/core/transport/bdp_estimator_test.c \

BDP_ESTIMATOR_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(BDP_ESTIMATOR_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/bdp_estimator_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/bdp_estimator_test: $(BDP_ESTIMATOR_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(BDP_ESTIMATOR_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/bdp_estimator_test

endif

$(OBJDIR)/$(CONFIG)/test/core/transport/bdp_estimator_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_bdp_estimator_test: $(BDP_ESTIMATOR_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(BDP_ESTIMATOR_TEST_OBJS:.o=.dep)
endif
endif


BIN_DECODER_TEST_SRC = \
    test/core/transport/chttp2/bin_decoder_test.c \

BIN_DECODER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(BIN_DECODER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/bin_decoder_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/bin_decoder_test: $(BIN_DECODER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(BIN_DECODER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/bin_decoder_test

endif

$(OBJDIR)/$(CONFIG)/test/core/transport/chttp2/bin_decoder_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a

deps_bin_decoder_test: $(BIN_DECODER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(BIN_DECODER_TEST_OBJS:.o=.dep)
endif
endif


BIN_ENCODER_TEST_SRC = \
    test/core/transport/chttp2/bin_encoder_test.c \

BIN_ENCODER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(BIN_ENCODER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/bin_encoder_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/bin_encoder_test: $(BIN_ENCODER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(BIN_ENCODER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/bin_encoder_test

endif

$(OBJDIR)/$(CONFIG)/test/core/transport/chttp2/bin_encoder_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a

deps_bin_encoder_test: $(BIN_ENCODER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(BIN_ENCODER_TEST_OBJS:.o=.dep)
endif
endif


BYTE_STREAM_TEST_SRC = \
    test/core/transport/byte_stream_test.c \

BYTE_STREAM_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(BYTE_STREAM_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/byte_stream_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/byte_stream_test: $(BYTE_STREAM_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(BYTE_STREAM_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/byte_stream_test

endif

$(OBJDIR)/$(CONFIG)/test/core/transport/byte_stream_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_byte_stream_test: $(BYTE_STREAM_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(BYTE_STREAM_TEST_OBJS:.o=.dep)
endif
endif


CENSUS_CONTEXT_TEST_SRC = \
    test/core/census/context_test.c \

CENSUS_CONTEXT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CENSUS_CONTEXT_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/census_context_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/census_context_test: $(CENSUS_CONTEXT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CENSUS_CONTEXT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/census_context_test

endif

$(OBJDIR)/$(CONFIG)/test/core/census/context_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_census_context_test: $(CENSUS_CONTEXT_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CENSUS_CONTEXT_TEST_OBJS:.o=.dep)
endif
endif


CENSUS_INTRUSIVE_HASH_MAP_TEST_SRC = \
    test/core/census/intrusive_hash_map_test.c \

CENSUS_INTRUSIVE_HASH_MAP_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CENSUS_INTRUSIVE_HASH_MAP_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/census_intrusive_hash_map_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/census_intrusive_hash_map_test: $(CENSUS_INTRUSIVE_HASH_MAP_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CENSUS_INTRUSIVE_HASH_MAP_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/census_intrusive_hash_map_test

endif

$(OBJDIR)/$(CONFIG)/test/core/census/intrusive_hash_map_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_census_intrusive_hash_map_test: $(CENSUS_INTRUSIVE_HASH_MAP_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CENSUS_INTRUSIVE_HASH_MAP_TEST_OBJS:.o=.dep)
endif
endif


CENSUS_RESOURCE_TEST_SRC = \
    test/core/census/resource_test.c \

CENSUS_RESOURCE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CENSUS_RESOURCE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/census_resource_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/census_resource_test: $(CENSUS_RESOURCE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CENSUS_RESOURCE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/census_resource_test

endif

$(OBJDIR)/$(CONFIG)/test/core/census/resource_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_census_resource_test: $(CENSUS_RESOURCE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CENSUS_RESOURCE_TEST_OBJS:.o=.dep)
endif
endif


CENSUS_TRACE_CONTEXT_TEST_SRC = \
    test/core/census/trace_context_test.c \

CENSUS_TRACE_CONTEXT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CENSUS_TRACE_CONTEXT_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/census_trace_context_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/census_trace_context_test: $(CENSUS_TRACE_CONTEXT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CENSUS_TRACE_CONTEXT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/census_trace_context_test

endif

$(OBJDIR)/$(CONFIG)/test/core/census/trace_context_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_census_trace_context_test: $(CENSUS_TRACE_CONTEXT_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CENSUS_TRACE_CONTEXT_TEST_OBJS:.o=.dep)
endif
endif


CHANNEL_CREATE_TEST_SRC = \
    test/core/surface/channel_create_test.c \

CHANNEL_CREATE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CHANNEL_CREATE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/channel_create_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/channel_create_test: $(CHANNEL_CREATE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHANNEL_CREATE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/channel_create_test

endif

$(OBJDIR)/$(CONFIG)/test/core/surface/channel_create_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_channel_create_test: $(CHANNEL_CREATE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHANNEL_CREATE_TEST_OBJS:.o=.dep)
endif
endif


CHECK_EPOLLEXCLUSIVE_SRC = \
    test/build/check_epollexclusive.c \

CHECK_EPOLLEXCLUSIVE_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CHECK_EPOLLEXCLUSIVE_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/check_epollexclusive: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/check_epollexclusive: $(CHECK_EPOLLEXCLUSIVE_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHECK_EPOLLEXCLUSIVE_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/check_epollexclusive

endif

$(OBJDIR)/$(CONFIG)/test/build/check_epollexclusive.o:  $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_check_epollexclusive: $(CHECK_EPOLLEXCLUSIVE_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHECK_EPOLLEXCLUSIVE_OBJS:.o=.dep)
endif
endif


CHTTP2_HPACK_ENCODER_TEST_SRC = \
    test/core/transport/chttp2/hpack_encoder_test.c \

CHTTP2_HPACK_ENCODER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CHTTP2_HPACK_ENCODER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/chttp2_hpack_encoder_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/chttp2_hpack_encoder_test: $(CHTTP2_HPACK_ENCODER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_HPACK_ENCODER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/chttp2_hpack_encoder_test

endif

$(OBJDIR)/$(CONFIG)/test/core/transport/chttp2/hpack_encoder_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_chttp2_hpack_encoder_test: $(CHTTP2_HPACK_ENCODER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_HPACK_ENCODER_TEST_OBJS:.o=.dep)
endif
endif


CHTTP2_STREAM_MAP_TEST_SRC = \
    test/core/transport/chttp2/stream_map_test.c \

CHTTP2_STREAM_MAP_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CHTTP2_STREAM_MAP_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/chttp2_stream_map_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/chttp2_stream_map_test: $(CHTTP2_STREAM_MAP_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_STREAM_MAP_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/chttp2_stream_map_test

endif

$(OBJDIR)/$(CONFIG)/test/core/transport/chttp2/stream_map_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_chttp2_stream_map_test: $(CHTTP2_STREAM_MAP_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_STREAM_MAP_TEST_OBJS:.o=.dep)
endif
endif


CHTTP2_VARINT_TEST_SRC = \
    test/core/transport/chttp2/varint_test.c \

CHTTP2_VARINT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CHTTP2_VARINT_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/chttp2_varint_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/chttp2_varint_test: $(CHTTP2_VARINT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_VARINT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/chttp2_varint_test

endif

$(OBJDIR)/$(CONFIG)/test/core/transport/chttp2/varint_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_chttp2_varint_test: $(CHTTP2_VARINT_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_VARINT_TEST_OBJS:.o=.dep)
endif
endif


CLIENT_FUZZER_SRC = \
    test/core/end2end/fuzzers/client_fuzzer.c \

CLIENT_FUZZER_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CLIENT_FUZZER_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/client_fuzzer: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/client_fuzzer: $(CLIENT_FUZZER_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(CLIENT_FUZZER_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -lFuzzer -o $(BINDIR)/$(CONFIG)/client_fuzzer

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fuzzers/client_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_client_fuzzer: $(CLIENT_FUZZER_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CLIENT_FUZZER_OBJS:.o=.dep)
endif
endif


COMBINER_TEST_SRC = \
    test/core/iomgr/combiner_test.c \

COMBINER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(COMBINER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/combiner_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/combiner_test: $(COMBINER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(COMBINER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/combiner_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/combiner_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_combiner_test: $(COMBINER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(COMBINER_TEST_OBJS:.o=.dep)
endif
endif


COMPRESSION_TEST_SRC = \
    test/core/compression/compression_test.c \

COMPRESSION_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(COMPRESSION_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/compression_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/compression_test: $(COMPRESSION_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(COMPRESSION_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/compression_test

endif

$(OBJDIR)/$(CONFIG)/test/core/compression/compression_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_compression_test: $(COMPRESSION_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(COMPRESSION_TEST_OBJS:.o=.dep)
endif
endif


CONCURRENT_CONNECTIVITY_TEST_SRC = \
    test/core/surface/concurrent_connectivity_test.c \

CONCURRENT_CONNECTIVITY_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CONCURRENT_CONNECTIVITY_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/concurrent_connectivity_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/concurrent_connectivity_test: $(CONCURRENT_CONNECTIVITY_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CONCURRENT_CONNECTIVITY_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/concurrent_connectivity_test

endif

$(OBJDIR)/$(CONFIG)/test/core/surface/concurrent_connectivity_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_concurrent_connectivity_test: $(CONCURRENT_CONNECTIVITY_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CONCURRENT_CONNECTIVITY_TEST_OBJS:.o=.dep)
endif
endif


CONNECTION_REFUSED_TEST_SRC = \
    test/core/end2end/connection_refused_test.c \

CONNECTION_REFUSED_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CONNECTION_REFUSED_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/connection_refused_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/connection_refused_test: $(CONNECTION_REFUSED_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CONNECTION_REFUSED_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/connection_refused_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/connection_refused_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_connection_refused_test: $(CONNECTION_REFUSED_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CONNECTION_REFUSED_TEST_OBJS:.o=.dep)
endif
endif


DNS_RESOLVER_CONNECTIVITY_TEST_SRC = \
    test/core/client_channel/resolvers/dns_resolver_connectivity_test.c \

DNS_RESOLVER_CONNECTIVITY_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(DNS_RESOLVER_CONNECTIVITY_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/dns_resolver_connectivity_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/dns_resolver_connectivity_test: $(DNS_RESOLVER_CONNECTIVITY_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(DNS_RESOLVER_CONNECTIVITY_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/dns_resolver_connectivity_test

endif

$(OBJDIR)/$(CONFIG)/test/core/client_channel/resolvers/dns_resolver_connectivity_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_dns_resolver_connectivity_test: $(DNS_RESOLVER_CONNECTIVITY_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(DNS_RESOLVER_CONNECTIVITY_TEST_OBJS:.o=.dep)
endif
endif


DNS_RESOLVER_TEST_SRC = \
    test/core/client_channel/resolvers/dns_resolver_test.c \

DNS_RESOLVER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(DNS_RESOLVER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/dns_resolver_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/dns_resolver_test: $(DNS_RESOLVER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(DNS_RESOLVER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/dns_resolver_test

endif

$(OBJDIR)/$(CONFIG)/test/core/client_channel/resolvers/dns_resolver_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_dns_resolver_test: $(DNS_RESOLVER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(DNS_RESOLVER_TEST_OBJS:.o=.dep)
endif
endif


DUALSTACK_SOCKET_TEST_SRC = \
    test/core/end2end/dualstack_socket_test.c \

DUALSTACK_SOCKET_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(DUALSTACK_SOCKET_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/dualstack_socket_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/dualstack_socket_test: $(DUALSTACK_SOCKET_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(DUALSTACK_SOCKET_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/dualstack_socket_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/dualstack_socket_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_dualstack_socket_test: $(DUALSTACK_SOCKET_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(DUALSTACK_SOCKET_TEST_OBJS:.o=.dep)
endif
endif


ENDPOINT_PAIR_TEST_SRC = \
    test/core/iomgr/endpoint_pair_test.c \

ENDPOINT_PAIR_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(ENDPOINT_PAIR_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/endpoint_pair_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/endpoint_pair_test: $(ENDPOINT_PAIR_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(ENDPOINT_PAIR_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/endpoint_pair_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/endpoint_pair_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_endpoint_pair_test: $(ENDPOINT_PAIR_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(ENDPOINT_PAIR_TEST_OBJS:.o=.dep)
endif
endif


ERROR_TEST_SRC = \
    test/core/iomgr/error_test.c \

ERROR_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(ERROR_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/error_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/error_test: $(ERROR_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(ERROR_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/error_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/error_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_error_test: $(ERROR_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(ERROR_TEST_OBJS:.o=.dep)
endif
endif


EV_EPOLLSIG_LINUX_TEST_SRC = \
    test/core/iomgr/ev_epollsig_linux_test.c \

EV_EPOLLSIG_LINUX_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(EV_EPOLLSIG_LINUX_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/ev_epollsig_linux_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/ev_epollsig_linux_test: $(EV_EPOLLSIG_LINUX_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(EV_EPOLLSIG_LINUX_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/ev_epollsig_linux_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/ev_epollsig_linux_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_ev_epollsig_linux_test: $(EV_EPOLLSIG_LINUX_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(EV_EPOLLSIG_LINUX_TEST_OBJS:.o=.dep)
endif
endif


FAKE_RESOLVER_TEST_SRC = \
    test/core/client_channel/resolvers/fake_resolver_test.c \

FAKE_RESOLVER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(FAKE_RESOLVER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/fake_resolver_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/fake_resolver_test: $(FAKE_RESOLVER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(FAKE_RESOLVER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/fake_resolver_test

endif

$(OBJDIR)/$(CONFIG)/test/core/client_channel/resolvers/fake_resolver_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_fake_resolver_test: $(FAKE_RESOLVER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(FAKE_RESOLVER_TEST_OBJS:.o=.dep)
endif
endif


FD_CONSERVATION_POSIX_TEST_SRC = \
    test/core/iomgr/fd_conservation_posix_test.c \

FD_CONSERVATION_POSIX_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(FD_CONSERVATION_POSIX_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/fd_conservation_posix_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/fd_conservation_posix_test: $(FD_CONSERVATION_POSIX_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(FD_CONSERVATION_POSIX_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/fd_conservation_posix_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/fd_conservation_posix_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_fd_conservation_posix_test: $(FD_CONSERVATION_POSIX_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(FD_CONSERVATION_POSIX_TEST_OBJS:.o=.dep)
endif
endif


FD_POSIX_TEST_SRC = \
    test/core/iomgr/fd_posix_test.c \

FD_POSIX_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(FD_POSIX_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/fd_posix_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/fd_posix_test: $(FD_POSIX_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(FD_POSIX_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/fd_posix_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/fd_posix_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_fd_posix_test: $(FD_POSIX_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(FD_POSIX_TEST_OBJS:.o=.dep)
endif
endif


FLING_CLIENT_SRC = \
    test/core/fling/client.c \

FLING_CLIENT_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(FLING_CLIENT_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/fling_client: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/fling_client: $(FLING_CLIENT_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(FLING_CLIENT_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/fling_client

endif

$(OBJDIR)/$(CONFIG)/test/core/fling/client.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_fling_client: $(FLING_CLIENT_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(FLING_CLIENT_OBJS:.o=.dep)
endif
endif


FLING_SERVER_SRC = \
    test/core/fling/server.c \

FLING_SERVER_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(FLING_SERVER_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/fling_server: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/fling_server: $(FLING_SERVER_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(FLING_SERVER_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/fling_server

endif

$(OBJDIR)/$(CONFIG)/test/core/fling/server.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_fling_server: $(FLING_SERVER_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(FLING_SERVER_OBJS:.o=.dep)
endif
endif


FLING_STREAM_TEST_SRC = \
    test/core/fling/fling_stream_test.c \

FLING_STREAM_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(FLING_STREAM_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/fling_stream_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/fling_stream_test: $(FLING_STREAM_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(FLING_STREAM_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/fling_stream_test

endif

$(OBJDIR)/$(CONFIG)/test/core/fling/fling_stream_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_fling_stream_test: $(FLING_STREAM_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(FLING_STREAM_TEST_OBJS:.o=.dep)
endif
endif


FLING_TEST_SRC = \
    test/core/fling/fling_test.c \

FLING_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(FLING_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/fling_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/fling_test: $(FLING_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(FLING_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/fling_test

endif

$(OBJDIR)/$(CONFIG)/test/core/fling/fling_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_fling_test: $(FLING_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(FLING_TEST_OBJS:.o=.dep)
endif
endif


GEN_HPACK_TABLES_SRC = \
    tools/codegen/core/gen_hpack_tables.c \

GEN_HPACK_TABLES_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GEN_HPACK_TABLES_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/gen_hpack_tables: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/gen_hpack_tables: $(GEN_HPACK_TABLES_OBJS) $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GEN_HPACK_TABLES_OBJS) $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/gen_hpack_tables

endif

$(OBJDIR)/$(CONFIG)/tools/codegen/core/gen_hpack_tables.o:  $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc.a

deps_gen_hpack_tables: $(GEN_HPACK_TABLES_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GEN_HPACK_TABLES_OBJS:.o=.dep)
endif
endif


GEN_LEGAL_METADATA_CHARACTERS_SRC = \
    tools/codegen/core/gen_legal_metadata_characters.c \

GEN_LEGAL_METADATA_CHARACTERS_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GEN_LEGAL_METADATA_CHARACTERS_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/gen_legal_metadata_characters: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/gen_legal_metadata_characters: $(GEN_LEGAL_METADATA_CHARACTERS_OBJS)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GEN_LEGAL_METADATA_CHARACTERS_OBJS) $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/gen_legal_metadata_characters

endif

$(OBJDIR)/$(CONFIG)/tools/codegen/core/gen_legal_metadata_characters.o: 

deps_gen_legal_metadata_characters: $(GEN_LEGAL_METADATA_CHARACTERS_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GEN_LEGAL_METADATA_CHARACTERS_OBJS:.o=.dep)
endif
endif


GEN_PERCENT_ENCODING_TABLES_SRC = \
    tools/codegen/core/gen_percent_encoding_tables.c \

GEN_PERCENT_ENCODING_TABLES_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GEN_PERCENT_ENCODING_TABLES_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/gen_percent_encoding_tables: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/gen_percent_encoding_tables: $(GEN_PERCENT_ENCODING_TABLES_OBJS)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GEN_PERCENT_ENCODING_TABLES_OBJS) $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/gen_percent_encoding_tables

endif

$(OBJDIR)/$(CONFIG)/tools/codegen/core/gen_percent_encoding_tables.o: 

deps_gen_percent_encoding_tables: $(GEN_PERCENT_ENCODING_TABLES_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GEN_PERCENT_ENCODING_TABLES_OBJS:.o=.dep)
endif
endif


GOAWAY_SERVER_TEST_SRC = \
    test/core/end2end/goaway_server_test.c \

GOAWAY_SERVER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GOAWAY_SERVER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/goaway_server_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/goaway_server_test: $(GOAWAY_SERVER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GOAWAY_SERVER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/goaway_server_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/goaway_server_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_goaway_server_test: $(GOAWAY_SERVER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GOAWAY_SERVER_TEST_OBJS:.o=.dep)
endif
endif


GPR_AVL_TEST_SRC = \
    test/core/support/avl_test.c \

GPR_AVL_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GPR_AVL_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/gpr_avl_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/gpr_avl_test: $(GPR_AVL_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_AVL_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/gpr_avl_test

endif

$(OBJDIR)/$(CONFIG)/test/core/support/avl_test.o:  $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_gpr_avl_test: $(GPR_AVL_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_AVL_TEST_OBJS:.o=.dep)
endif
endif


GPR_BACKOFF_TEST_SRC = \
    test/core/support/backoff_test.c \

GPR_BACKOFF_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GPR_BACKOFF_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/gpr_backoff_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/gpr_backoff_test: $(GPR_BACKOFF_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_BACKOFF_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/gpr_backoff_test

endif

$(OBJDIR)/$(CONFIG)/test/core/support/backoff_test.o:  $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_gpr_backoff_test: $(GPR_BACKOFF_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_BACKOFF_TEST_OBJS:.o=.dep)
endif
endif


GPR_CMDLINE_TEST_SRC = \
    test/core/support/cmdline_test.c \

GPR_CMDLINE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GPR_CMDLINE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/gpr_cmdline_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/gpr_cmdline_test: $(GPR_CMDLINE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_CMDLINE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/gpr_cmdline_test

endif

$(OBJDIR)/$(CONFIG)/test/core/support/cmdline_test.o:  $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_gpr_cmdline_test: $(GPR_CMDLINE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_CMDLINE_TEST_OBJS:.o=.dep)
endif
endif


GPR_CPU_TEST_SRC = \
    test/core/support/cpu_test.c \

GPR_CPU_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GPR_CPU_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/gpr_cpu_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/gpr_cpu_test: $(GPR_CPU_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_CPU_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/gpr_cpu_test

endif

$(OBJDIR)/$(CONFIG)/test/core/support/cpu_test.o:  $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_gpr_cpu_test: $(GPR_CPU_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_CPU_TEST_OBJS:.o=.dep)
endif
endif


GPR_ENV_TEST_SRC = \
    test/core/support/env_test.c \

GPR_ENV_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GPR_ENV_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/gpr_env_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/gpr_env_test: $(GPR_ENV_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_ENV_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/gpr_env_test

endif

$(OBJDIR)/$(CONFIG)/test/core/support/env_test.o:  $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_gpr_env_test: $(GPR_ENV_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_ENV_TEST_OBJS:.o=.dep)
endif
endif


GPR_HISTOGRAM_TEST_SRC = \
    test/core/support/histogram_test.c \

GPR_HISTOGRAM_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GPR_HISTOGRAM_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/gpr_histogram_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/gpr_histogram_test: $(GPR_HISTOGRAM_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_HISTOGRAM_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/gpr_histogram_test

endif

$(OBJDIR)/$(CONFIG)/test/core/support/histogram_test.o:  $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_gpr_histogram_test: $(GPR_HISTOGRAM_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_HISTOGRAM_TEST_OBJS:.o=.dep)
endif
endif


GPR_HOST_PORT_TEST_SRC = \
    test/core/support/host_port_test.c \

GPR_HOST_PORT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GPR_HOST_PORT_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/gpr_host_port_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/gpr_host_port_test: $(GPR_HOST_PORT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_HOST_PORT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/gpr_host_port_test

endif

$(OBJDIR)/$(CONFIG)/test/core/support/host_port_test.o:  $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_gpr_host_port_test: $(GPR_HOST_PORT_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_HOST_PORT_TEST_OBJS:.o=.dep)
endif
endif


GPR_LOG_TEST_SRC = \
    test/core/support/log_test.c \

GPR_LOG_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GPR_LOG_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/gpr_log_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/gpr_log_test: $(GPR_LOG_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_LOG_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/gpr_log_test

endif

$(OBJDIR)/$(CONFIG)/test/core/support/log_test.o:  $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_gpr_log_test: $(GPR_LOG_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_LOG_TEST_OBJS:.o=.dep)
endif
endif


GPR_MPSCQ_TEST_SRC = \
    test/core/support/mpscq_test.c \

GPR_MPSCQ_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GPR_MPSCQ_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/gpr_mpscq_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/gpr_mpscq_test: $(GPR_MPSCQ_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_MPSCQ_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/gpr_mpscq_test

endif

$(OBJDIR)/$(CONFIG)/test/core/support/mpscq_test.o:  $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_gpr_mpscq_test: $(GPR_MPSCQ_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_MPSCQ_TEST_OBJS:.o=.dep)
endif
endif


GPR_SPINLOCK_TEST_SRC = \
    test/core/support/spinlock_test.c \

GPR_SPINLOCK_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GPR_SPINLOCK_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/gpr_spinlock_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/gpr_spinlock_test: $(GPR_SPINLOCK_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_SPINLOCK_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/gpr_spinlock_test

endif

$(OBJDIR)/$(CONFIG)/test/core/support/spinlock_test.o:  $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_gpr_spinlock_test: $(GPR_SPINLOCK_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_SPINLOCK_TEST_OBJS:.o=.dep)
endif
endif


GPR_STACK_LOCKFREE_TEST_SRC = \
    test/core/support/stack_lockfree_test.c \

GPR_STACK_LOCKFREE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GPR_STACK_LOCKFREE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/gpr_stack_lockfree_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/gpr_stack_lockfree_test: $(GPR_STACK_LOCKFREE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_STACK_LOCKFREE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/gpr_stack_lockfree_test

endif

$(OBJDIR)/$(CONFIG)/test/core/support/stack_lockfree_test.o:  $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_gpr_stack_lockfree_test: $(GPR_STACK_LOCKFREE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_STACK_LOCKFREE_TEST_OBJS:.o=.dep)
endif
endif


GPR_STRING_TEST_SRC = \
    test/core/support/string_test.c \

GPR_STRING_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GPR_STRING_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/gpr_string_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/gpr_string_test: $(GPR_STRING_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_STRING_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/gpr_string_test

endif

$(OBJDIR)/$(CONFIG)/test/core/support/string_test.o:  $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_gpr_string_test: $(GPR_STRING_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_STRING_TEST_OBJS:.o=.dep)
endif
endif


GPR_SYNC_TEST_SRC = \
    test/core/support/sync_test.c \

GPR_SYNC_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GPR_SYNC_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/gpr_sync_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/gpr_sync_test: $(GPR_SYNC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_SYNC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/gpr_sync_test

endif

$(OBJDIR)/$(CONFIG)/test/core/support/sync_test.o:  $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_gpr_sync_test: $(GPR_SYNC_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_SYNC_TEST_OBJS:.o=.dep)
endif
endif


GPR_THD_TEST_SRC = \
    test/core/support/thd_test.c \

GPR_THD_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GPR_THD_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/gpr_thd_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/gpr_thd_test: $(GPR_THD_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_THD_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/gpr_thd_test

endif

$(OBJDIR)/$(CONFIG)/test/core/support/thd_test.o:  $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_gpr_thd_test: $(GPR_THD_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_THD_TEST_OBJS:.o=.dep)
endif
endif


GPR_TIME_TEST_SRC = \
    test/core/support/time_test.c \

GPR_TIME_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GPR_TIME_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/gpr_time_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/gpr_time_test: $(GPR_TIME_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_TIME_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/gpr_time_test

endif

$(OBJDIR)/$(CONFIG)/test/core/support/time_test.o:  $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_gpr_time_test: $(GPR_TIME_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_TIME_TEST_OBJS:.o=.dep)
endif
endif


GPR_TLS_TEST_SRC = \
    test/core/support/tls_test.c \

GPR_TLS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GPR_TLS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/gpr_tls_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/gpr_tls_test: $(GPR_TLS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_TLS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/gpr_tls_test

endif

$(OBJDIR)/$(CONFIG)/test/core/support/tls_test.o:  $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_gpr_tls_test: $(GPR_TLS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_TLS_TEST_OBJS:.o=.dep)
endif
endif


GPR_USEFUL_TEST_SRC = \
    test/core/support/useful_test.c \

GPR_USEFUL_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GPR_USEFUL_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/gpr_useful_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/gpr_useful_test: $(GPR_USEFUL_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_USEFUL_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/gpr_useful_test

endif

$(OBJDIR)/$(CONFIG)/test/core/support/useful_test.o:  $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_gpr_useful_test: $(GPR_USEFUL_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_USEFUL_TEST_OBJS:.o=.dep)
endif
endif


GRPC_AUTH_CONTEXT_TEST_SRC = \
    test/core/security/auth_context_test.c \

GRPC_AUTH_CONTEXT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_AUTH_CONTEXT_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/grpc_auth_context_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/grpc_auth_context_test: $(GRPC_AUTH_CONTEXT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_AUTH_CONTEXT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/grpc_auth_context_test

endif

$(OBJDIR)/$(CONFIG)/test/core/security/auth_context_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_grpc_auth_context_test: $(GRPC_AUTH_CONTEXT_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_AUTH_CONTEXT_TEST_OBJS:.o=.dep)
endif
endif


GRPC_B64_TEST_SRC = \
    test/core/slice/b64_test.c \

GRPC_B64_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_B64_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/grpc_b64_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/grpc_b64_test: $(GRPC_B64_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_B64_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/grpc_b64_test

endif

$(OBJDIR)/$(CONFIG)/test/core/slice/b64_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_grpc_b64_test: $(GRPC_B64_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_B64_TEST_OBJS:.o=.dep)
endif
endif


GRPC_BYTE_BUFFER_READER_TEST_SRC = \
    test/core/surface/byte_buffer_reader_test.c \

GRPC_BYTE_BUFFER_READER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_BYTE_BUFFER_READER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/grpc_byte_buffer_reader_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/grpc_byte_buffer_reader_test: $(GRPC_BYTE_BUFFER_READER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_BYTE_BUFFER_READER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/grpc_byte_buffer_reader_test

endif

$(OBJDIR)/$(CONFIG)/test/core/surface/byte_buffer_reader_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_grpc_byte_buffer_reader_test: $(GRPC_BYTE_BUFFER_READER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_BYTE_BUFFER_READER_TEST_OBJS:.o=.dep)
endif
endif


GRPC_CHANNEL_ARGS_TEST_SRC = \
    test/core/channel/channel_args_test.c \

GRPC_CHANNEL_ARGS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_CHANNEL_ARGS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/grpc_channel_args_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/grpc_channel_args_test: $(GRPC_CHANNEL_ARGS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_CHANNEL_ARGS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/grpc_channel_args_test

endif

$(OBJDIR)/$(CONFIG)/test/core/channel/channel_args_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_grpc_channel_args_test: $(GRPC_CHANNEL_ARGS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_CHANNEL_ARGS_TEST_OBJS:.o=.dep)
endif
endif


GRPC_CHANNEL_STACK_TEST_SRC = \
    test/core/channel/channel_stack_test.c \

GRPC_CHANNEL_STACK_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_CHANNEL_STACK_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/grpc_channel_stack_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/grpc_channel_stack_test: $(GRPC_CHANNEL_STACK_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_CHANNEL_STACK_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/grpc_channel_stack_test

endif

$(OBJDIR)/$(CONFIG)/test/core/channel/channel_stack_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_grpc_channel_stack_test: $(GRPC_CHANNEL_STACK_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_CHANNEL_STACK_TEST_OBJS:.o=.dep)
endif
endif


GRPC_COMPLETION_QUEUE_TEST_SRC = \
    test/core/surface/completion_queue_test.c \

GRPC_COMPLETION_QUEUE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_COMPLETION_QUEUE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/grpc_completion_queue_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/grpc_completion_queue_test: $(GRPC_COMPLETION_QUEUE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_COMPLETION_QUEUE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/grpc_completion_queue_test

endif

$(OBJDIR)/$(CONFIG)/test/core/surface/completion_queue_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_grpc_completion_queue_test: $(GRPC_COMPLETION_QUEUE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_COMPLETION_QUEUE_TEST_OBJS:.o=.dep)
endif
endif


GRPC_COMPLETION_QUEUE_THREADING_TEST_SRC = \
    test/core/surface/completion_queue_threading_test.c \

GRPC_COMPLETION_QUEUE_THREADING_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_COMPLETION_QUEUE_THREADING_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/grpc_completion_queue_threading_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/grpc_completion_queue_threading_test: $(GRPC_COMPLETION_QUEUE_THREADING_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_COMPLETION_QUEUE_THREADING_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/grpc_completion_queue_threading_test

endif

$(OBJDIR)/$(CONFIG)/test/core/surface/completion_queue_threading_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_grpc_completion_queue_threading_test: $(GRPC_COMPLETION_QUEUE_THREADING_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_COMPLETION_QUEUE_THREADING_TEST_OBJS:.o=.dep)
endif
endif


GRPC_CREATE_JWT_SRC = \
    test/core/security/create_jwt.c \

GRPC_CREATE_JWT_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_CREATE_JWT_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/grpc_create_jwt: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/grpc_create_jwt: $(GRPC_CREATE_JWT_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_CREATE_JWT_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/grpc_create_jwt

endif

$(OBJDIR)/$(CONFIG)/test/core/security/create_jwt.o:  $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_grpc_create_jwt: $(GRPC_CREATE_JWT_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_CREATE_JWT_OBJS:.o=.dep)
endif
endif


GRPC_CREDENTIALS_TEST_SRC = \
    test/core/security/credentials_test.c \

GRPC_CREDENTIALS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_CREDENTIALS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/grpc_credentials_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/grpc_credentials_test: $(GRPC_CREDENTIALS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_CREDENTIALS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/grpc_credentials_test

endif

$(OBJDIR)/$(CONFIG)/test/core/security/credentials_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_grpc_credentials_test: $(GRPC_CREDENTIALS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_CREDENTIALS_TEST_OBJS:.o=.dep)
endif
endif


GRPC_FETCH_OAUTH2_SRC = \
    test/core/security/fetch_oauth2.c \

GRPC_FETCH_OAUTH2_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_FETCH_OAUTH2_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/grpc_fetch_oauth2: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/grpc_fetch_oauth2: $(GRPC_FETCH_OAUTH2_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_FETCH_OAUTH2_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/grpc_fetch_oauth2

endif

$(OBJDIR)/$(CONFIG)/test/core/security/fetch_oauth2.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_grpc_fetch_oauth2: $(GRPC_FETCH_OAUTH2_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_FETCH_OAUTH2_OBJS:.o=.dep)
endif
endif


GRPC_INVALID_CHANNEL_ARGS_TEST_SRC = \
    test/core/surface/invalid_channel_args_test.c \

GRPC_INVALID_CHANNEL_ARGS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_INVALID_CHANNEL_ARGS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/grpc_invalid_channel_args_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/grpc_invalid_channel_args_test: $(GRPC_INVALID_CHANNEL_ARGS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_INVALID_CHANNEL_ARGS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/grpc_invalid_channel_args_test

endif

$(OBJDIR)/$(CONFIG)/test/core/surface/invalid_channel_args_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_grpc_invalid_channel_args_test: $(GRPC_INVALID_CHANNEL_ARGS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_INVALID_CHANNEL_ARGS_TEST_OBJS:.o=.dep)
endif
endif


GRPC_JSON_TOKEN_TEST_SRC = \
    test/core/security/json_token_test.c \

GRPC_JSON_TOKEN_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_JSON_TOKEN_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/grpc_json_token_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/grpc_json_token_test: $(GRPC_JSON_TOKEN_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_JSON_TOKEN_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/grpc_json_token_test

endif

$(OBJDIR)/$(CONFIG)/test/core/security/json_token_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_grpc_json_token_test: $(GRPC_JSON_TOKEN_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_JSON_TOKEN_TEST_OBJS:.o=.dep)
endif
endif


GRPC_JWT_VERIFIER_TEST_SRC = \
    test/core/security/jwt_verifier_test.c \

GRPC_JWT_VERIFIER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_JWT_VERIFIER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/grpc_jwt_verifier_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/grpc_jwt_verifier_test: $(GRPC_JWT_VERIFIER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_JWT_VERIFIER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/grpc_jwt_verifier_test

endif

$(OBJDIR)/$(CONFIG)/test/core/security/jwt_verifier_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_grpc_jwt_verifier_test: $(GRPC_JWT_VERIFIER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_JWT_VERIFIER_TEST_OBJS:.o=.dep)
endif
endif


GRPC_PRINT_GOOGLE_DEFAULT_CREDS_TOKEN_SRC = \
    test/core/security/print_google_default_creds_token.c \

GRPC_PRINT_GOOGLE_DEFAULT_CREDS_TOKEN_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_PRINT_GOOGLE_DEFAULT_CREDS_TOKEN_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/grpc_print_google_default_creds_token: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/grpc_print_google_default_creds_token: $(GRPC_PRINT_GOOGLE_DEFAULT_CREDS_TOKEN_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_PRINT_GOOGLE_DEFAULT_CREDS_TOKEN_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/grpc_print_google_default_creds_token

endif

$(OBJDIR)/$(CONFIG)/test/core/security/print_google_default_creds_token.o:  $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_grpc_print_google_default_creds_token: $(GRPC_PRINT_GOOGLE_DEFAULT_CREDS_TOKEN_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_PRINT_GOOGLE_DEFAULT_CREDS_TOKEN_OBJS:.o=.dep)
endif
endif


GRPC_SECURITY_CONNECTOR_TEST_SRC = \
    test/core/security/security_connector_test.c \

GRPC_SECURITY_CONNECTOR_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_SECURITY_CONNECTOR_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/grpc_security_connector_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/grpc_security_connector_test: $(GRPC_SECURITY_CONNECTOR_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_SECURITY_CONNECTOR_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/grpc_security_connector_test

endif

$(OBJDIR)/$(CONFIG)/test/core/security/security_connector_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_grpc_security_connector_test: $(GRPC_SECURITY_CONNECTOR_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_SECURITY_CONNECTOR_TEST_OBJS:.o=.dep)
endif
endif


GRPC_VERIFY_JWT_SRC = \
    test/core/security/verify_jwt.c \

GRPC_VERIFY_JWT_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_VERIFY_JWT_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/grpc_verify_jwt: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/grpc_verify_jwt: $(GRPC_VERIFY_JWT_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_VERIFY_JWT_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/grpc_verify_jwt

endif

$(OBJDIR)/$(CONFIG)/test/core/security/verify_jwt.o:  $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_grpc_verify_jwt: $(GRPC_VERIFY_JWT_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_VERIFY_JWT_OBJS:.o=.dep)
endif
endif


HANDSHAKE_CLIENT_SRC = \
    test/core/handshake/client_ssl.c \

HANDSHAKE_CLIENT_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(HANDSHAKE_CLIENT_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/handshake_client: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/handshake_client: $(HANDSHAKE_CLIENT_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(HANDSHAKE_CLIENT_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/handshake_client

endif

$(OBJDIR)/$(CONFIG)/test/core/handshake/client_ssl.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_handshake_client: $(HANDSHAKE_CLIENT_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(HANDSHAKE_CLIENT_OBJS:.o=.dep)
endif
endif


HANDSHAKE_SERVER_SRC = \
    test/core/handshake/server_ssl.c \

HANDSHAKE_SERVER_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(HANDSHAKE_SERVER_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/handshake_server: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/handshake_server: $(HANDSHAKE_SERVER_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(HANDSHAKE_SERVER_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/handshake_server

endif

$(OBJDIR)/$(CONFIG)/test/core/handshake/server_ssl.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_handshake_server: $(HANDSHAKE_SERVER_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(HANDSHAKE_SERVER_OBJS:.o=.dep)
endif
endif


HPACK_PARSER_FUZZER_TEST_SRC = \
    test/core/transport/chttp2/hpack_parser_fuzzer_test.c \

HPACK_PARSER_FUZZER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(HPACK_PARSER_FUZZER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/hpack_parser_fuzzer_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/hpack_parser_fuzzer_test: $(HPACK_PARSER_FUZZER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(HPACK_PARSER_FUZZER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -lFuzzer -o $(BINDIR)/$(CONFIG)/hpack_parser_fuzzer_test

endif

$(OBJDIR)/$(CONFIG)/test/core/transport/chttp2/hpack_parser_fuzzer_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_hpack_parser_fuzzer_test: $(HPACK_PARSER_FUZZER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(HPACK_PARSER_FUZZER_TEST_OBJS:.o=.dep)
endif
endif


HPACK_PARSER_TEST_SRC = \
    test/core/transport/chttp2/hpack_parser_test.c \

HPACK_PARSER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(HPACK_PARSER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/hpack_parser_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/hpack_parser_test: $(HPACK_PARSER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(HPACK_PARSER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/hpack_parser_test

endif

$(OBJDIR)/$(CONFIG)/test/core/transport/chttp2/hpack_parser_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_hpack_parser_test: $(HPACK_PARSER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(HPACK_PARSER_TEST_OBJS:.o=.dep)
endif
endif


HPACK_TABLE_TEST_SRC = \
    test/core/transport/chttp2/hpack_table_test.c \

HPACK_TABLE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(HPACK_TABLE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/hpack_table_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/hpack_table_test: $(HPACK_TABLE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(HPACK_TABLE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/hpack_table_test

endif

$(OBJDIR)/$(CONFIG)/test/core/transport/chttp2/hpack_table_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_hpack_table_test: $(HPACK_TABLE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(HPACK_TABLE_TEST_OBJS:.o=.dep)
endif
endif


HTTP_PARSER_TEST_SRC = \
    test/core/http/parser_test.c \

HTTP_PARSER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(HTTP_PARSER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/http_parser_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/http_parser_test: $(HTTP_PARSER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(HTTP_PARSER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/http_parser_test

endif

$(OBJDIR)/$(CONFIG)/test/core/http/parser_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_http_parser_test: $(HTTP_PARSER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(HTTP_PARSER_TEST_OBJS:.o=.dep)
endif
endif


HTTP_REQUEST_FUZZER_TEST_SRC = \
    test/core/http/request_fuzzer.c \

HTTP_REQUEST_FUZZER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(HTTP_REQUEST_FUZZER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/http_request_fuzzer_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/http_request_fuzzer_test: $(HTTP_REQUEST_FUZZER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(HTTP_REQUEST_FUZZER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -lFuzzer -o $(BINDIR)/$(CONFIG)/http_request_fuzzer_test

endif

$(OBJDIR)/$(CONFIG)/test/core/http/request_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_http_request_fuzzer_test: $(HTTP_REQUEST_FUZZER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(HTTP_REQUEST_FUZZER_TEST_OBJS:.o=.dep)
endif
endif


HTTP_RESPONSE_FUZZER_TEST_SRC = \
    test/core/http/response_fuzzer.c \

HTTP_RESPONSE_FUZZER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(HTTP_RESPONSE_FUZZER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/http_response_fuzzer_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/http_response_fuzzer_test: $(HTTP_RESPONSE_FUZZER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(HTTP_RESPONSE_FUZZER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -lFuzzer -o $(BINDIR)/$(CONFIG)/http_response_fuzzer_test

endif

$(OBJDIR)/$(CONFIG)/test/core/http/response_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_http_response_fuzzer_test: $(HTTP_RESPONSE_FUZZER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(HTTP_RESPONSE_FUZZER_TEST_OBJS:.o=.dep)
endif
endif


HTTPCLI_FORMAT_REQUEST_TEST_SRC = \
    test/core/http/format_request_test.c \

HTTPCLI_FORMAT_REQUEST_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(HTTPCLI_FORMAT_REQUEST_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/httpcli_format_request_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/httpcli_format_request_test: $(HTTPCLI_FORMAT_REQUEST_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(HTTPCLI_FORMAT_REQUEST_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/httpcli_format_request_test

endif

$(OBJDIR)/$(CONFIG)/test/core/http/format_request_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_httpcli_format_request_test: $(HTTPCLI_FORMAT_REQUEST_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(HTTPCLI_FORMAT_REQUEST_TEST_OBJS:.o=.dep)
endif
endif


HTTPCLI_TEST_SRC = \
    test/core/http/httpcli_test.c \

HTTPCLI_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(HTTPCLI_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/httpcli_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/httpcli_test: $(HTTPCLI_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(HTTPCLI_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/httpcli_test

endif

$(OBJDIR)/$(CONFIG)/test/core/http/httpcli_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_httpcli_test: $(HTTPCLI_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(HTTPCLI_TEST_OBJS:.o=.dep)
endif
endif


HTTPSCLI_TEST_SRC = \
    test/core/http/httpscli_test.c \

HTTPSCLI_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(HTTPSCLI_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/httpscli_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/httpscli_test: $(HTTPSCLI_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(HTTPSCLI_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/httpscli_test

endif

$(OBJDIR)/$(CONFIG)/test/core/http/httpscli_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_httpscli_test: $(HTTPSCLI_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(HTTPSCLI_TEST_OBJS:.o=.dep)
endif
endif


INIT_TEST_SRC = \
    test/core/surface/init_test.c \

INIT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(INIT_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/init_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/init_test: $(INIT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(INIT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/init_test

endif

$(OBJDIR)/$(CONFIG)/test/core/surface/init_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_init_test: $(INIT_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(INIT_TEST_OBJS:.o=.dep)
endif
endif


INVALID_CALL_ARGUMENT_TEST_SRC = \
    test/core/end2end/invalid_call_argument_test.c \

INVALID_CALL_ARGUMENT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(INVALID_CALL_ARGUMENT_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/invalid_call_argument_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/invalid_call_argument_test: $(INVALID_CALL_ARGUMENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(INVALID_CALL_ARGUMENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/invalid_call_argument_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/invalid_call_argument_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_invalid_call_argument_test: $(INVALID_CALL_ARGUMENT_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(INVALID_CALL_ARGUMENT_TEST_OBJS:.o=.dep)
endif
endif


JSON_FUZZER_TEST_SRC = \
    test/core/json/fuzzer.c \

JSON_FUZZER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(JSON_FUZZER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/json_fuzzer_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/json_fuzzer_test: $(JSON_FUZZER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(JSON_FUZZER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -lFuzzer -o $(BINDIR)/$(CONFIG)/json_fuzzer_test

endif

$(OBJDIR)/$(CONFIG)/test/core/json/fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_json_fuzzer_test: $(JSON_FUZZER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(JSON_FUZZER_TEST_OBJS:.o=.dep)
endif
endif


JSON_REWRITE_SRC = \
    test/core/json/json_rewrite.c \

JSON_REWRITE_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(JSON_REWRITE_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/json_rewrite: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/json_rewrite: $(JSON_REWRITE_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(JSON_REWRITE_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/json_rewrite

endif

$(OBJDIR)/$(CONFIG)/test/core/json/json_rewrite.o:  $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_json_rewrite: $(JSON_REWRITE_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(JSON_REWRITE_OBJS:.o=.dep)
endif
endif


JSON_REWRITE_TEST_SRC = \
    test/core/json/json_rewrite_test.c \

JSON_REWRITE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(JSON_REWRITE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/json_rewrite_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/json_rewrite_test: $(JSON_REWRITE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(JSON_REWRITE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/json_rewrite_test

endif

$(OBJDIR)/$(CONFIG)/test/core/json/json_rewrite_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_json_rewrite_test: $(JSON_REWRITE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(JSON_REWRITE_TEST_OBJS:.o=.dep)
endif
endif


JSON_STREAM_ERROR_TEST_SRC = \
    test/core/json/json_stream_error_test.c \

JSON_STREAM_ERROR_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(JSON_STREAM_ERROR_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/json_stream_error_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/json_stream_error_test: $(JSON_STREAM_ERROR_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(JSON_STREAM_ERROR_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/json_stream_error_test

endif

$(OBJDIR)/$(CONFIG)/test/core/json/json_stream_error_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_json_stream_error_test: $(JSON_STREAM_ERROR_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(JSON_STREAM_ERROR_TEST_OBJS:.o=.dep)
endif
endif


JSON_TEST_SRC = \
    test/core/json/json_test.c \

JSON_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(JSON_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/json_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/json_test: $(JSON_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(JSON_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/json_test

endif

$(OBJDIR)/$(CONFIG)/test/core/json/json_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_json_test: $(JSON_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(JSON_TEST_OBJS:.o=.dep)
endif
endif


LAME_CLIENT_TEST_SRC = \
    test/core/surface/lame_client_test.c \

LAME_CLIENT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LAME_CLIENT_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/lame_client_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/lame_client_test: $(LAME_CLIENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(LAME_CLIENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/lame_client_test

endif

$(OBJDIR)/$(CONFIG)/test/core/surface/lame_client_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_lame_client_test: $(LAME_CLIENT_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LAME_CLIENT_TEST_OBJS:.o=.dep)
endif
endif


LB_POLICIES_TEST_SRC = \
    test/core/client_channel/lb_policies_test.c \

LB_POLICIES_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LB_POLICIES_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/lb_policies_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/lb_policies_test: $(LB_POLICIES_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(LB_POLICIES_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/lb_policies_test

endif

$(OBJDIR)/$(CONFIG)/test/core/client_channel/lb_policies_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_lb_policies_test: $(LB_POLICIES_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LB_POLICIES_TEST_OBJS:.o=.dep)
endif
endif


LOAD_FILE_TEST_SRC = \
    test/core/iomgr/load_file_test.c \

LOAD_FILE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LOAD_FILE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/load_file_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/load_file_test: $(LOAD_FILE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(LOAD_FILE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/load_file_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/load_file_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_load_file_test: $(LOAD_FILE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LOAD_FILE_TEST_OBJS:.o=.dep)
endif
endif


LOW_LEVEL_PING_PONG_BENCHMARK_SRC = \
    test/core/network_benchmarks/low_level_ping_pong.c \

LOW_LEVEL_PING_PONG_BENCHMARK_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LOW_LEVEL_PING_PONG_BENCHMARK_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/low_level_ping_pong_benchmark: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/low_level_ping_pong_benchmark: $(LOW_LEVEL_PING_PONG_BENCHMARK_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(LOW_LEVEL_PING_PONG_BENCHMARK_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/low_level_ping_pong_benchmark

endif

$(OBJDIR)/$(CONFIG)/test/core/network_benchmarks/low_level_ping_pong.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_low_level_ping_pong_benchmark: $(LOW_LEVEL_PING_PONG_BENCHMARK_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LOW_LEVEL_PING_PONG_BENCHMARK_OBJS:.o=.dep)
endif
endif


MEMORY_PROFILE_CLIENT_SRC = \
    test/core/memory_usage/client.c \

MEMORY_PROFILE_CLIENT_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(MEMORY_PROFILE_CLIENT_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/memory_profile_client: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/memory_profile_client: $(MEMORY_PROFILE_CLIENT_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(MEMORY_PROFILE_CLIENT_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/memory_profile_client

endif

$(OBJDIR)/$(CONFIG)/test/core/memory_usage/client.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_memory_profile_client: $(MEMORY_PROFILE_CLIENT_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(MEMORY_PROFILE_CLIENT_OBJS:.o=.dep)
endif
endif


MEMORY_PROFILE_SERVER_SRC = \
    test/core/memory_usage/server.c \

MEMORY_PROFILE_SERVER_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(MEMORY_PROFILE_SERVER_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/memory_profile_server: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/memory_profile_server: $(MEMORY_PROFILE_SERVER_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(MEMORY_PROFILE_SERVER_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/memory_profile_server

endif

$(OBJDIR)/$(CONFIG)/test/core/memory_usage/server.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_memory_profile_server: $(MEMORY_PROFILE_SERVER_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(MEMORY_PROFILE_SERVER_OBJS:.o=.dep)
endif
endif


MEMORY_PROFILE_TEST_SRC = \
    test/core/memory_usage/memory_usage_test.c \

MEMORY_PROFILE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(MEMORY_PROFILE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/memory_profile_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/memory_profile_test: $(MEMORY_PROFILE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(MEMORY_PROFILE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/memory_profile_test

endif

$(OBJDIR)/$(CONFIG)/test/core/memory_usage/memory_usage_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_memory_profile_test: $(MEMORY_PROFILE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(MEMORY_PROFILE_TEST_OBJS:.o=.dep)
endif
endif


MESSAGE_COMPRESS_TEST_SRC = \
    test/core/compression/message_compress_test.c \

MESSAGE_COMPRESS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(MESSAGE_COMPRESS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/message_compress_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/message_compress_test: $(MESSAGE_COMPRESS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(MESSAGE_COMPRESS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/message_compress_test

endif

$(OBJDIR)/$(CONFIG)/test/core/compression/message_compress_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_message_compress_test: $(MESSAGE_COMPRESS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(MESSAGE_COMPRESS_TEST_OBJS:.o=.dep)
endif
endif


MINIMAL_STACK_IS_MINIMAL_TEST_SRC = \
    test/core/channel/minimal_stack_is_minimal_test.c \

MINIMAL_STACK_IS_MINIMAL_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(MINIMAL_STACK_IS_MINIMAL_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/minimal_stack_is_minimal_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/minimal_stack_is_minimal_test: $(MINIMAL_STACK_IS_MINIMAL_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(MINIMAL_STACK_IS_MINIMAL_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/minimal_stack_is_minimal_test

endif

$(OBJDIR)/$(CONFIG)/test/core/channel/minimal_stack_is_minimal_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_minimal_stack_is_minimal_test: $(MINIMAL_STACK_IS_MINIMAL_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(MINIMAL_STACK_IS_MINIMAL_TEST_OBJS:.o=.dep)
endif
endif


MLOG_TEST_SRC = \
    test/core/census/mlog_test.c \

MLOG_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(MLOG_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/mlog_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/mlog_test: $(MLOG_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(MLOG_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/mlog_test

endif

$(OBJDIR)/$(CONFIG)/test/core/census/mlog_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_mlog_test: $(MLOG_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(MLOG_TEST_OBJS:.o=.dep)
endif
endif


MULTIPLE_SERVER_QUEUES_TEST_SRC = \
    test/core/end2end/multiple_server_queues_test.c \

MULTIPLE_SERVER_QUEUES_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(MULTIPLE_SERVER_QUEUES_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/multiple_server_queues_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/multiple_server_queues_test: $(MULTIPLE_SERVER_QUEUES_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(MULTIPLE_SERVER_QUEUES_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/multiple_server_queues_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/multiple_server_queues_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_multiple_server_queues_test: $(MULTIPLE_SERVER_QUEUES_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(MULTIPLE_SERVER_QUEUES_TEST_OBJS:.o=.dep)
endif
endif


MURMUR_HASH_TEST_SRC = \
    test/core/support/murmur_hash_test.c \

MURMUR_HASH_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(MURMUR_HASH_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/murmur_hash_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/murmur_hash_test: $(MURMUR_HASH_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(MURMUR_HASH_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/murmur_hash_test

endif

$(OBJDIR)/$(CONFIG)/test/core/support/murmur_hash_test.o:  $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_murmur_hash_test: $(MURMUR_HASH_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(MURMUR_HASH_TEST_OBJS:.o=.dep)
endif
endif


NANOPB_FUZZER_RESPONSE_TEST_SRC = \
    test/core/nanopb/fuzzer_response.c \

NANOPB_FUZZER_RESPONSE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(NANOPB_FUZZER_RESPONSE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/nanopb_fuzzer_response_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/nanopb_fuzzer_response_test: $(NANOPB_FUZZER_RESPONSE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(NANOPB_FUZZER_RESPONSE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -lFuzzer -o $(BINDIR)/$(CONFIG)/nanopb_fuzzer_response_test

endif

$(OBJDIR)/$(CONFIG)/test/core/nanopb/fuzzer_response.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_nanopb_fuzzer_response_test: $(NANOPB_FUZZER_RESPONSE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(NANOPB_FUZZER_RESPONSE_TEST_OBJS:.o=.dep)
endif
endif


NANOPB_FUZZER_SERVERLIST_TEST_SRC = \
    test/core/nanopb/fuzzer_serverlist.c \

NANOPB_FUZZER_SERVERLIST_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(NANOPB_FUZZER_SERVERLIST_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/nanopb_fuzzer_serverlist_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/nanopb_fuzzer_serverlist_test: $(NANOPB_FUZZER_SERVERLIST_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(NANOPB_FUZZER_SERVERLIST_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -lFuzzer -o $(BINDIR)/$(CONFIG)/nanopb_fuzzer_serverlist_test

endif

$(OBJDIR)/$(CONFIG)/test/core/nanopb/fuzzer_serverlist.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_nanopb_fuzzer_serverlist_test: $(NANOPB_FUZZER_SERVERLIST_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(NANOPB_FUZZER_SERVERLIST_TEST_OBJS:.o=.dep)
endif
endif


NO_SERVER_TEST_SRC = \
    test/core/end2end/no_server_test.c \

NO_SERVER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(NO_SERVER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/no_server_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/no_server_test: $(NO_SERVER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(NO_SERVER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/no_server_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/no_server_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_no_server_test: $(NO_SERVER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(NO_SERVER_TEST_OBJS:.o=.dep)
endif
endif


NUM_EXTERNAL_CONNECTIVITY_WATCHERS_TEST_SRC = \
    test/core/surface/num_external_connectivity_watchers_test.c \

NUM_EXTERNAL_CONNECTIVITY_WATCHERS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(NUM_EXTERNAL_CONNECTIVITY_WATCHERS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/num_external_connectivity_watchers_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/num_external_connectivity_watchers_test: $(NUM_EXTERNAL_CONNECTIVITY_WATCHERS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(NUM_EXTERNAL_CONNECTIVITY_WATCHERS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/num_external_connectivity_watchers_test

endif

$(OBJDIR)/$(CONFIG)/test/core/surface/num_external_connectivity_watchers_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_num_external_connectivity_watchers_test: $(NUM_EXTERNAL_CONNECTIVITY_WATCHERS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(NUM_EXTERNAL_CONNECTIVITY_WATCHERS_TEST_OBJS:.o=.dep)
endif
endif


PARSE_ADDRESS_TEST_SRC = \
    test/core/client_channel/parse_address_test.c \

PARSE_ADDRESS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(PARSE_ADDRESS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/parse_address_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/parse_address_test: $(PARSE_ADDRESS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(PARSE_ADDRESS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/parse_address_test

endif

$(OBJDIR)/$(CONFIG)/test/core/client_channel/parse_address_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_parse_address_test: $(PARSE_ADDRESS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(PARSE_ADDRESS_TEST_OBJS:.o=.dep)
endif
endif


PERCENT_DECODE_FUZZER_SRC = \
    test/core/slice/percent_decode_fuzzer.c \

PERCENT_DECODE_FUZZER_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(PERCENT_DECODE_FUZZER_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/percent_decode_fuzzer: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/percent_decode_fuzzer: $(PERCENT_DECODE_FUZZER_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(PERCENT_DECODE_FUZZER_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -lFuzzer -o $(BINDIR)/$(CONFIG)/percent_decode_fuzzer

endif

$(OBJDIR)/$(CONFIG)/test/core/slice/percent_decode_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_percent_decode_fuzzer: $(PERCENT_DECODE_FUZZER_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(PERCENT_DECODE_FUZZER_OBJS:.o=.dep)
endif
endif


PERCENT_ENCODE_FUZZER_SRC = \
    test/core/slice/percent_encode_fuzzer.c \

PERCENT_ENCODE_FUZZER_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(PERCENT_ENCODE_FUZZER_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/percent_encode_fuzzer: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/percent_encode_fuzzer: $(PERCENT_ENCODE_FUZZER_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(PERCENT_ENCODE_FUZZER_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -lFuzzer -o $(BINDIR)/$(CONFIG)/percent_encode_fuzzer

endif

$(OBJDIR)/$(CONFIG)/test/core/slice/percent_encode_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_percent_encode_fuzzer: $(PERCENT_ENCODE_FUZZER_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(PERCENT_ENCODE_FUZZER_OBJS:.o=.dep)
endif
endif


PERCENT_ENCODING_TEST_SRC = \
    test/core/slice/percent_encoding_test.c \

PERCENT_ENCODING_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(PERCENT_ENCODING_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/percent_encoding_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/percent_encoding_test: $(PERCENT_ENCODING_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(PERCENT_ENCODING_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/percent_encoding_test

endif

$(OBJDIR)/$(CONFIG)/test/core/slice/percent_encoding_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_percent_encoding_test: $(PERCENT_ENCODING_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(PERCENT_ENCODING_TEST_OBJS:.o=.dep)
endif
endif


POLLSET_SET_TEST_SRC = \
    test/core/iomgr/pollset_set_test.c \

POLLSET_SET_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(POLLSET_SET_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/pollset_set_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/pollset_set_test: $(POLLSET_SET_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(POLLSET_SET_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/pollset_set_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/pollset_set_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_pollset_set_test: $(POLLSET_SET_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(POLLSET_SET_TEST_OBJS:.o=.dep)
endif
endif


RESOLVE_ADDRESS_POSIX_TEST_SRC = \
    test/core/iomgr/resolve_address_posix_test.c \

RESOLVE_ADDRESS_POSIX_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(RESOLVE_ADDRESS_POSIX_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/resolve_address_posix_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/resolve_address_posix_test: $(RESOLVE_ADDRESS_POSIX_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(RESOLVE_ADDRESS_POSIX_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/resolve_address_posix_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/resolve_address_posix_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_resolve_address_posix_test: $(RESOLVE_ADDRESS_POSIX_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(RESOLVE_ADDRESS_POSIX_TEST_OBJS:.o=.dep)
endif
endif


RESOLVE_ADDRESS_TEST_SRC = \
    test/core/iomgr/resolve_address_test.c \

RESOLVE_ADDRESS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(RESOLVE_ADDRESS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/resolve_address_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/resolve_address_test: $(RESOLVE_ADDRESS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(RESOLVE_ADDRESS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/resolve_address_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/resolve_address_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_resolve_address_test: $(RESOLVE_ADDRESS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(RESOLVE_ADDRESS_TEST_OBJS:.o=.dep)
endif
endif


RESOURCE_QUOTA_TEST_SRC = \
    test/core/iomgr/resource_quota_test.c \

RESOURCE_QUOTA_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(RESOURCE_QUOTA_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/resource_quota_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/resource_quota_test: $(RESOURCE_QUOTA_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(RESOURCE_QUOTA_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/resource_quota_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/resource_quota_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_resource_quota_test: $(RESOURCE_QUOTA_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(RESOURCE_QUOTA_TEST_OBJS:.o=.dep)
endif
endif


SECURE_CHANNEL_CREATE_TEST_SRC = \
    test/core/surface/secure_channel_create_test.c \

SECURE_CHANNEL_CREATE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SECURE_CHANNEL_CREATE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/secure_channel_create_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/secure_channel_create_test: $(SECURE_CHANNEL_CREATE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(SECURE_CHANNEL_CREATE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/secure_channel_create_test

endif

$(OBJDIR)/$(CONFIG)/test/core/surface/secure_channel_create_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_secure_channel_create_test: $(SECURE_CHANNEL_CREATE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SECURE_CHANNEL_CREATE_TEST_OBJS:.o=.dep)
endif
endif


SECURE_ENDPOINT_TEST_SRC = \
    test/core/security/secure_endpoint_test.c \

SECURE_ENDPOINT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SECURE_ENDPOINT_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/secure_endpoint_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/secure_endpoint_test: $(SECURE_ENDPOINT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(SECURE_ENDPOINT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/secure_endpoint_test

endif

$(OBJDIR)/$(CONFIG)/test/core/security/secure_endpoint_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_secure_endpoint_test: $(SECURE_ENDPOINT_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SECURE_ENDPOINT_TEST_OBJS:.o=.dep)
endif
endif


SEQUENTIAL_CONNECTIVITY_TEST_SRC = \
    test/core/surface/sequential_connectivity_test.c \

SEQUENTIAL_CONNECTIVITY_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SEQUENTIAL_CONNECTIVITY_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/sequential_connectivity_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/sequential_connectivity_test: $(SEQUENTIAL_CONNECTIVITY_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(SEQUENTIAL_CONNECTIVITY_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/sequential_connectivity_test

endif

$(OBJDIR)/$(CONFIG)/test/core/surface/sequential_connectivity_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_sequential_connectivity_test: $(SEQUENTIAL_CONNECTIVITY_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SEQUENTIAL_CONNECTIVITY_TEST_OBJS:.o=.dep)
endif
endif


SERVER_CHTTP2_TEST_SRC = \
    test/core/surface/server_chttp2_test.c \

SERVER_CHTTP2_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SERVER_CHTTP2_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/server_chttp2_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/server_chttp2_test: $(SERVER_CHTTP2_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(SERVER_CHTTP2_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/server_chttp2_test

endif

$(OBJDIR)/$(CONFIG)/test/core/surface/server_chttp2_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_server_chttp2_test: $(SERVER_CHTTP2_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SERVER_CHTTP2_TEST_OBJS:.o=.dep)
endif
endif


SERVER_FUZZER_SRC = \
    test/core/end2end/fuzzers/server_fuzzer.c \

SERVER_FUZZER_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SERVER_FUZZER_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/server_fuzzer: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/server_fuzzer: $(SERVER_FUZZER_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(SERVER_FUZZER_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -lFuzzer -o $(BINDIR)/$(CONFIG)/server_fuzzer

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fuzzers/server_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_server_fuzzer: $(SERVER_FUZZER_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SERVER_FUZZER_OBJS:.o=.dep)
endif
endif


SERVER_TEST_SRC = \
    test/core/surface/server_test.c \

SERVER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SERVER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/server_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/server_test: $(SERVER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(SERVER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/server_test

endif

$(OBJDIR)/$(CONFIG)/test/core/surface/server_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_server_test: $(SERVER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SERVER_TEST_OBJS:.o=.dep)
endif
endif


SLICE_BUFFER_TEST_SRC = \
    test/core/slice/slice_buffer_test.c \

SLICE_BUFFER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SLICE_BUFFER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/slice_buffer_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/slice_buffer_test: $(SLICE_BUFFER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(SLICE_BUFFER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/slice_buffer_test

endif

$(OBJDIR)/$(CONFIG)/test/core/slice/slice_buffer_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_slice_buffer_test: $(SLICE_BUFFER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SLICE_BUFFER_TEST_OBJS:.o=.dep)
endif
endif


SLICE_HASH_TABLE_TEST_SRC = \
    test/core/slice/slice_hash_table_test.c \

SLICE_HASH_TABLE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SLICE_HASH_TABLE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/slice_hash_table_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/slice_hash_table_test: $(SLICE_HASH_TABLE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(SLICE_HASH_TABLE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/slice_hash_table_test

endif

$(OBJDIR)/$(CONFIG)/test/core/slice/slice_hash_table_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_slice_hash_table_test: $(SLICE_HASH_TABLE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SLICE_HASH_TABLE_TEST_OBJS:.o=.dep)
endif
endif


SLICE_STRING_HELPERS_TEST_SRC = \
    test/core/slice/slice_string_helpers_test.c \

SLICE_STRING_HELPERS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SLICE_STRING_HELPERS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/slice_string_helpers_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/slice_string_helpers_test: $(SLICE_STRING_HELPERS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(SLICE_STRING_HELPERS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/slice_string_helpers_test

endif

$(OBJDIR)/$(CONFIG)/test/core/slice/slice_string_helpers_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_slice_string_helpers_test: $(SLICE_STRING_HELPERS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SLICE_STRING_HELPERS_TEST_OBJS:.o=.dep)
endif
endif


SLICE_TEST_SRC = \
    test/core/slice/slice_test.c \

SLICE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SLICE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/slice_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/slice_test: $(SLICE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(SLICE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/slice_test

endif

$(OBJDIR)/$(CONFIG)/test/core/slice/slice_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_slice_test: $(SLICE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SLICE_TEST_OBJS:.o=.dep)
endif
endif


SOCKADDR_RESOLVER_TEST_SRC = \
    test/core/client_channel/resolvers/sockaddr_resolver_test.c \

SOCKADDR_RESOLVER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SOCKADDR_RESOLVER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/sockaddr_resolver_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/sockaddr_resolver_test: $(SOCKADDR_RESOLVER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(SOCKADDR_RESOLVER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/sockaddr_resolver_test

endif

$(OBJDIR)/$(CONFIG)/test/core/client_channel/resolvers/sockaddr_resolver_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_sockaddr_resolver_test: $(SOCKADDR_RESOLVER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SOCKADDR_RESOLVER_TEST_OBJS:.o=.dep)
endif
endif


SOCKADDR_UTILS_TEST_SRC = \
    test/core/iomgr/sockaddr_utils_test.c \

SOCKADDR_UTILS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SOCKADDR_UTILS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/sockaddr_utils_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/sockaddr_utils_test: $(SOCKADDR_UTILS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(SOCKADDR_UTILS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/sockaddr_utils_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/sockaddr_utils_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_sockaddr_utils_test: $(SOCKADDR_UTILS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SOCKADDR_UTILS_TEST_OBJS:.o=.dep)
endif
endif


SOCKET_UTILS_TEST_SRC = \
    test/core/iomgr/socket_utils_test.c \

SOCKET_UTILS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SOCKET_UTILS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/socket_utils_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/socket_utils_test: $(SOCKET_UTILS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(SOCKET_UTILS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/socket_utils_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/socket_utils_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_socket_utils_test: $(SOCKET_UTILS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SOCKET_UTILS_TEST_OBJS:.o=.dep)
endif
endif


SSL_SERVER_FUZZER_SRC = \
    test/core/security/ssl_server_fuzzer.c \

SSL_SERVER_FUZZER_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SSL_SERVER_FUZZER_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/ssl_server_fuzzer: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/ssl_server_fuzzer: $(SSL_SERVER_FUZZER_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(SSL_SERVER_FUZZER_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -lFuzzer -o $(BINDIR)/$(CONFIG)/ssl_server_fuzzer

endif

$(OBJDIR)/$(CONFIG)/test/core/security/ssl_server_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_ssl_server_fuzzer: $(SSL_SERVER_FUZZER_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SSL_SERVER_FUZZER_OBJS:.o=.dep)
endif
endif


STATUS_CONVERSION_TEST_SRC = \
    test/core/transport/status_conversion_test.c \

STATUS_CONVERSION_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(STATUS_CONVERSION_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/status_conversion_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/status_conversion_test: $(STATUS_CONVERSION_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(STATUS_CONVERSION_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/status_conversion_test

endif

$(OBJDIR)/$(CONFIG)/test/core/transport/status_conversion_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_status_conversion_test: $(STATUS_CONVERSION_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(STATUS_CONVERSION_TEST_OBJS:.o=.dep)
endif
endif


STREAM_COMPRESSION_TEST_SRC = \
    test/core/compression/stream_compression_test.c \

STREAM_COMPRESSION_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(STREAM_COMPRESSION_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/stream_compression_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/stream_compression_test: $(STREAM_COMPRESSION_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(STREAM_COMPRESSION_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/stream_compression_test

endif

$(OBJDIR)/$(CONFIG)/test/core/compression/stream_compression_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_stream_compression_test: $(STREAM_COMPRESSION_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(STREAM_COMPRESSION_TEST_OBJS:.o=.dep)
endif
endif


STREAM_OWNED_SLICE_TEST_SRC = \
    test/core/transport/stream_owned_slice_test.c \

STREAM_OWNED_SLICE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(STREAM_OWNED_SLICE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/stream_owned_slice_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/stream_owned_slice_test: $(STREAM_OWNED_SLICE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(STREAM_OWNED_SLICE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/stream_owned_slice_test

endif

$(OBJDIR)/$(CONFIG)/test/core/transport/stream_owned_slice_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_stream_owned_slice_test: $(STREAM_OWNED_SLICE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(STREAM_OWNED_SLICE_TEST_OBJS:.o=.dep)
endif
endif


TCP_CLIENT_POSIX_TEST_SRC = \
    test/core/iomgr/tcp_client_posix_test.c \

TCP_CLIENT_POSIX_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(TCP_CLIENT_POSIX_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/tcp_client_posix_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/tcp_client_posix_test: $(TCP_CLIENT_POSIX_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(TCP_CLIENT_POSIX_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/tcp_client_posix_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/tcp_client_posix_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_tcp_client_posix_test: $(TCP_CLIENT_POSIX_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(TCP_CLIENT_POSIX_TEST_OBJS:.o=.dep)
endif
endif


TCP_CLIENT_UV_TEST_SRC = \
    test/core/iomgr/tcp_client_uv_test.c \

TCP_CLIENT_UV_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(TCP_CLIENT_UV_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/tcp_client_uv_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/tcp_client_uv_test: $(TCP_CLIENT_UV_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(TCP_CLIENT_UV_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/tcp_client_uv_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/tcp_client_uv_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_tcp_client_uv_test: $(TCP_CLIENT_UV_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(TCP_CLIENT_UV_TEST_OBJS:.o=.dep)
endif
endif


TCP_POSIX_TEST_SRC = \
    test/core/iomgr/tcp_posix_test.c \

TCP_POSIX_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(TCP_POSIX_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/tcp_posix_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/tcp_posix_test: $(TCP_POSIX_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(TCP_POSIX_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/tcp_posix_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/tcp_posix_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_tcp_posix_test: $(TCP_POSIX_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(TCP_POSIX_TEST_OBJS:.o=.dep)
endif
endif


TCP_SERVER_POSIX_TEST_SRC = \
    test/core/iomgr/tcp_server_posix_test.c \

TCP_SERVER_POSIX_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(TCP_SERVER_POSIX_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/tcp_server_posix_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/tcp_server_posix_test: $(TCP_SERVER_POSIX_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(TCP_SERVER_POSIX_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/tcp_server_posix_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/tcp_server_posix_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_tcp_server_posix_test: $(TCP_SERVER_POSIX_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(TCP_SERVER_POSIX_TEST_OBJS:.o=.dep)
endif
endif


TCP_SERVER_UV_TEST_SRC = \
    test/core/iomgr/tcp_server_uv_test.c \

TCP_SERVER_UV_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(TCP_SERVER_UV_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/tcp_server_uv_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/tcp_server_uv_test: $(TCP_SERVER_UV_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(TCP_SERVER_UV_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/tcp_server_uv_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/tcp_server_uv_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_tcp_server_uv_test: $(TCP_SERVER_UV_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(TCP_SERVER_UV_TEST_OBJS:.o=.dep)
endif
endif


TIME_AVERAGED_STATS_TEST_SRC = \
    test/core/iomgr/time_averaged_stats_test.c \

TIME_AVERAGED_STATS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(TIME_AVERAGED_STATS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/time_averaged_stats_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/time_averaged_stats_test: $(TIME_AVERAGED_STATS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(TIME_AVERAGED_STATS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/time_averaged_stats_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/time_averaged_stats_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_time_averaged_stats_test: $(TIME_AVERAGED_STATS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(TIME_AVERAGED_STATS_TEST_OBJS:.o=.dep)
endif
endif


TIMEOUT_ENCODING_TEST_SRC = \
    test/core/transport/timeout_encoding_test.c \

TIMEOUT_ENCODING_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(TIMEOUT_ENCODING_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/timeout_encoding_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/timeout_encoding_test: $(TIMEOUT_ENCODING_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(TIMEOUT_ENCODING_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/timeout_encoding_test

endif

$(OBJDIR)/$(CONFIG)/test/core/transport/timeout_encoding_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_timeout_encoding_test: $(TIMEOUT_ENCODING_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(TIMEOUT_ENCODING_TEST_OBJS:.o=.dep)
endif
endif


TIMER_HEAP_TEST_SRC = \
    test/core/iomgr/timer_heap_test.c \

TIMER_HEAP_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(TIMER_HEAP_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/timer_heap_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/timer_heap_test: $(TIMER_HEAP_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(TIMER_HEAP_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/timer_heap_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/timer_heap_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_timer_heap_test: $(TIMER_HEAP_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(TIMER_HEAP_TEST_OBJS:.o=.dep)
endif
endif


TIMER_LIST_TEST_SRC = \
    test/core/iomgr/timer_list_test.c \

TIMER_LIST_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(TIMER_LIST_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/timer_list_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/timer_list_test: $(TIMER_LIST_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(TIMER_LIST_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/timer_list_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/timer_list_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_timer_list_test: $(TIMER_LIST_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(TIMER_LIST_TEST_OBJS:.o=.dep)
endif
endif


TRANSPORT_CONNECTIVITY_STATE_TEST_SRC = \
    test/core/transport/connectivity_state_test.c \

TRANSPORT_CONNECTIVITY_STATE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(TRANSPORT_CONNECTIVITY_STATE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/transport_connectivity_state_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/transport_connectivity_state_test: $(TRANSPORT_CONNECTIVITY_STATE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(TRANSPORT_CONNECTIVITY_STATE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/transport_connectivity_state_test

endif

$(OBJDIR)/$(CONFIG)/test/core/transport/connectivity_state_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_transport_connectivity_state_test: $(TRANSPORT_CONNECTIVITY_STATE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(TRANSPORT_CONNECTIVITY_STATE_TEST_OBJS:.o=.dep)
endif
endif


TRANSPORT_METADATA_TEST_SRC = \
    test/core/transport/metadata_test.c \

TRANSPORT_METADATA_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(TRANSPORT_METADATA_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/transport_metadata_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/transport_metadata_test: $(TRANSPORT_METADATA_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(TRANSPORT_METADATA_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/transport_metadata_test

endif

$(OBJDIR)/$(CONFIG)/test/core/transport/metadata_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_transport_metadata_test: $(TRANSPORT_METADATA_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(TRANSPORT_METADATA_TEST_OBJS:.o=.dep)
endif
endif


TRANSPORT_PID_CONTROLLER_TEST_SRC = \
    test/core/transport/pid_controller_test.c \

TRANSPORT_PID_CONTROLLER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(TRANSPORT_PID_CONTROLLER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/transport_pid_controller_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/transport_pid_controller_test: $(TRANSPORT_PID_CONTROLLER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(TRANSPORT_PID_CONTROLLER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/transport_pid_controller_test

endif

$(OBJDIR)/$(CONFIG)/test/core/transport/pid_controller_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_transport_pid_controller_test: $(TRANSPORT_PID_CONTROLLER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(TRANSPORT_PID_CONTROLLER_TEST_OBJS:.o=.dep)
endif
endif


TRANSPORT_SECURITY_TEST_SRC = \
    test/core/tsi/transport_security_test.c \

TRANSPORT_SECURITY_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(TRANSPORT_SECURITY_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/transport_security_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/transport_security_test: $(TRANSPORT_SECURITY_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(TRANSPORT_SECURITY_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/transport_security_test

endif

$(OBJDIR)/$(CONFIG)/test/core/tsi/transport_security_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_transport_security_test: $(TRANSPORT_SECURITY_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(TRANSPORT_SECURITY_TEST_OBJS:.o=.dep)
endif
endif


UDP_SERVER_TEST_SRC = \
    test/core/iomgr/udp_server_test.c \

UDP_SERVER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(UDP_SERVER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/udp_server_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/udp_server_test: $(UDP_SERVER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(UDP_SERVER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/udp_server_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/udp_server_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_udp_server_test: $(UDP_SERVER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(UDP_SERVER_TEST_OBJS:.o=.dep)
endif
endif


URI_FUZZER_TEST_SRC = \
    test/core/client_channel/uri_fuzzer_test.c \

URI_FUZZER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(URI_FUZZER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/uri_fuzzer_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/uri_fuzzer_test: $(URI_FUZZER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(URI_FUZZER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -lFuzzer -o $(BINDIR)/$(CONFIG)/uri_fuzzer_test

endif

$(OBJDIR)/$(CONFIG)/test/core/client_channel/uri_fuzzer_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_uri_fuzzer_test: $(URI_FUZZER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(URI_FUZZER_TEST_OBJS:.o=.dep)
endif
endif


URI_PARSER_TEST_SRC = \
    test/core/client_channel/uri_parser_test.c \

URI_PARSER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(URI_PARSER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/uri_parser_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/uri_parser_test: $(URI_PARSER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(URI_PARSER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/uri_parser_test

endif

$(OBJDIR)/$(CONFIG)/test/core/client_channel/uri_parser_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_uri_parser_test: $(URI_PARSER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(URI_PARSER_TEST_OBJS:.o=.dep)
endif
endif


WAKEUP_FD_CV_TEST_SRC = \
    test/core/iomgr/wakeup_fd_cv_test.c \

WAKEUP_FD_CV_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(WAKEUP_FD_CV_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/wakeup_fd_cv_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/wakeup_fd_cv_test: $(WAKEUP_FD_CV_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(WAKEUP_FD_CV_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/wakeup_fd_cv_test

endif

$(OBJDIR)/$(CONFIG)/test/core/iomgr/wakeup_fd_cv_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_wakeup_fd_cv_test: $(WAKEUP_FD_CV_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(WAKEUP_FD_CV_TEST_OBJS:.o=.dep)
endif
endif


ALARM_CPP_TEST_SRC = \
    test/cpp/common/alarm_cpp_test.cc \

ALARM_CPP_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(ALARM_CPP_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/alarm_cpp_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/alarm_cpp_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/alarm_cpp_test: $(PROTOBUF_DEP) $(ALARM_CPP_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(ALARM_CPP_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/alarm_cpp_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/common/alarm_cpp_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_alarm_cpp_test: $(ALARM_CPP_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(ALARM_CPP_TEST_OBJS:.o=.dep)
endif
endif


ASYNC_END2END_TEST_SRC = \
    test/cpp/end2end/async_end2end_test.cc \

ASYNC_END2END_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(ASYNC_END2END_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/async_end2end_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/async_end2end_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/async_end2end_test: $(PROTOBUF_DEP) $(ASYNC_END2END_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(ASYNC_END2END_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/async_end2end_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/end2end/async_end2end_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_async_end2end_test: $(ASYNC_END2END_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(ASYNC_END2END_TEST_OBJS:.o=.dep)
endif
endif


AUTH_PROPERTY_ITERATOR_TEST_SRC = \
    test/cpp/common/auth_property_iterator_test.cc \

AUTH_PROPERTY_ITERATOR_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(AUTH_PROPERTY_ITERATOR_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/auth_property_iterator_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/auth_property_iterator_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/auth_property_iterator_test: $(PROTOBUF_DEP) $(AUTH_PROPERTY_ITERATOR_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(AUTH_PROPERTY_ITERATOR_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/auth_property_iterator_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/common/auth_property_iterator_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_auth_property_iterator_test: $(AUTH_PROPERTY_ITERATOR_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(AUTH_PROPERTY_ITERATOR_TEST_OBJS:.o=.dep)
endif
endif


BM_ARENA_SRC = \
    test/cpp/microbenchmarks/bm_arena.cc \

BM_ARENA_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(BM_ARENA_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/bm_arena: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/bm_arena: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/bm_arena: $(PROTOBUF_DEP) $(BM_ARENA_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(BM_ARENA_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/bm_arena

endif

endif

$(BM_ARENA_OBJS): CPPFLAGS += -Ithird_party/benchmark/include -DHAVE_POSIX_REGEX
$(OBJDIR)/$(CONFIG)/test/cpp/microbenchmarks/bm_arena.o:  $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_bm_arena: $(BM_ARENA_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(BM_ARENA_OBJS:.o=.dep)
endif
endif


BM_CALL_CREATE_SRC = \
    test/cpp/microbenchmarks/bm_call_create.cc \

BM_CALL_CREATE_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(BM_CALL_CREATE_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/bm_call_create: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/bm_call_create: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/bm_call_create: $(PROTOBUF_DEP) $(BM_CALL_CREATE_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(BM_CALL_CREATE_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/bm_call_create

endif

endif

$(BM_CALL_CREATE_OBJS): CPPFLAGS += -Ithird_party/benchmark/include -DHAVE_POSIX_REGEX
$(OBJDIR)/$(CONFIG)/test/cpp/microbenchmarks/bm_call_create.o:  $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_bm_call_create: $(BM_CALL_CREATE_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(BM_CALL_CREATE_OBJS:.o=.dep)
endif
endif


BM_CHTTP2_HPACK_SRC = \
    test/cpp/microbenchmarks/bm_chttp2_hpack.cc \

BM_CHTTP2_HPACK_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(BM_CHTTP2_HPACK_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/bm_chttp2_hpack: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/bm_chttp2_hpack: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/bm_chttp2_hpack: $(PROTOBUF_DEP) $(BM_CHTTP2_HPACK_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(BM_CHTTP2_HPACK_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/bm_chttp2_hpack

endif

endif

$(BM_CHTTP2_HPACK_OBJS): CPPFLAGS += -Ithird_party/benchmark/include -DHAVE_POSIX_REGEX
$(OBJDIR)/$(CONFIG)/test/cpp/microbenchmarks/bm_chttp2_hpack.o:  $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_bm_chttp2_hpack: $(BM_CHTTP2_HPACK_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(BM_CHTTP2_HPACK_OBJS:.o=.dep)
endif
endif


BM_CHTTP2_TRANSPORT_SRC = \
    test/cpp/microbenchmarks/bm_chttp2_transport.cc \

BM_CHTTP2_TRANSPORT_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(BM_CHTTP2_TRANSPORT_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/bm_chttp2_transport: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/bm_chttp2_transport: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/bm_chttp2_transport: $(PROTOBUF_DEP) $(BM_CHTTP2_TRANSPORT_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(BM_CHTTP2_TRANSPORT_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/bm_chttp2_transport

endif

endif

$(BM_CHTTP2_TRANSPORT_OBJS): CPPFLAGS += -Ithird_party/benchmark/include -DHAVE_POSIX_REGEX
$(OBJDIR)/$(CONFIG)/test/cpp/microbenchmarks/bm_chttp2_transport.o:  $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_bm_chttp2_transport: $(BM_CHTTP2_TRANSPORT_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(BM_CHTTP2_TRANSPORT_OBJS:.o=.dep)
endif
endif


BM_CLOSURE_SRC = \
    test/cpp/microbenchmarks/bm_closure.cc \

BM_CLOSURE_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(BM_CLOSURE_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/bm_closure: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/bm_closure: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/bm_closure: $(PROTOBUF_DEP) $(BM_CLOSURE_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(BM_CLOSURE_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/bm_closure

endif

endif

$(BM_CLOSURE_OBJS): CPPFLAGS += -Ithird_party/benchmark/include -DHAVE_POSIX_REGEX
$(OBJDIR)/$(CONFIG)/test/cpp/microbenchmarks/bm_closure.o:  $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_bm_closure: $(BM_CLOSURE_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(BM_CLOSURE_OBJS:.o=.dep)
endif
endif


BM_CQ_SRC = \
    test/cpp/microbenchmarks/bm_cq.cc \

BM_CQ_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(BM_CQ_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/bm_cq: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/bm_cq: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/bm_cq: $(PROTOBUF_DEP) $(BM_CQ_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(BM_CQ_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/bm_cq

endif

endif

$(BM_CQ_OBJS): CPPFLAGS += -Ithird_party/benchmark/include -DHAVE_POSIX_REGEX
$(OBJDIR)/$(CONFIG)/test/cpp/microbenchmarks/bm_cq.o:  $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_bm_cq: $(BM_CQ_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(BM_CQ_OBJS:.o=.dep)
endif
endif


BM_CQ_MULTIPLE_THREADS_SRC = \
    test/cpp/microbenchmarks/bm_cq_multiple_threads.cc \

BM_CQ_MULTIPLE_THREADS_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(BM_CQ_MULTIPLE_THREADS_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/bm_cq_multiple_threads: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/bm_cq_multiple_threads: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/bm_cq_multiple_threads: $(PROTOBUF_DEP) $(BM_CQ_MULTIPLE_THREADS_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(BM_CQ_MULTIPLE_THREADS_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/bm_cq_multiple_threads

endif

endif

$(BM_CQ_MULTIPLE_THREADS_OBJS): CPPFLAGS += -Ithird_party/benchmark/include -DHAVE_POSIX_REGEX
$(OBJDIR)/$(CONFIG)/test/cpp/microbenchmarks/bm_cq_multiple_threads.o:  $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_bm_cq_multiple_threads: $(BM_CQ_MULTIPLE_THREADS_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(BM_CQ_MULTIPLE_THREADS_OBJS:.o=.dep)
endif
endif


BM_ERROR_SRC = \
    test/cpp/microbenchmarks/bm_error.cc \

BM_ERROR_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(BM_ERROR_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/bm_error: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/bm_error: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/bm_error: $(PROTOBUF_DEP) $(BM_ERROR_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(BM_ERROR_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/bm_error

endif

endif

$(BM_ERROR_OBJS): CPPFLAGS += -Ithird_party/benchmark/include -DHAVE_POSIX_REGEX
$(OBJDIR)/$(CONFIG)/test/cpp/microbenchmarks/bm_error.o:  $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_bm_error: $(BM_ERROR_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(BM_ERROR_OBJS:.o=.dep)
endif
endif


BM_FULLSTACK_STREAMING_PING_PONG_SRC = \
    test/cpp/microbenchmarks/bm_fullstack_streaming_ping_pong.cc \

BM_FULLSTACK_STREAMING_PING_PONG_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(BM_FULLSTACK_STREAMING_PING_PONG_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/bm_fullstack_streaming_ping_pong: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/bm_fullstack_streaming_ping_pong: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/bm_fullstack_streaming_ping_pong: $(PROTOBUF_DEP) $(BM_FULLSTACK_STREAMING_PING_PONG_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(BM_FULLSTACK_STREAMING_PING_PONG_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/bm_fullstack_streaming_ping_pong

endif

endif

$(BM_FULLSTACK_STREAMING_PING_PONG_OBJS): CPPFLAGS += -Ithird_party/benchmark/include -DHAVE_POSIX_REGEX
$(OBJDIR)/$(CONFIG)/test/cpp/microbenchmarks/bm_fullstack_streaming_ping_pong.o:  $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_bm_fullstack_streaming_ping_pong: $(BM_FULLSTACK_STREAMING_PING_PONG_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(BM_FULLSTACK_STREAMING_PING_PONG_OBJS:.o=.dep)
endif
endif


BM_FULLSTACK_STREAMING_PUMP_SRC = \
    test/cpp/microbenchmarks/bm_fullstack_streaming_pump.cc \

BM_FULLSTACK_STREAMING_PUMP_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(BM_FULLSTACK_STREAMING_PUMP_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/bm_fullstack_streaming_pump: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/bm_fullstack_streaming_pump: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/bm_fullstack_streaming_pump: $(PROTOBUF_DEP) $(BM_FULLSTACK_STREAMING_PUMP_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(BM_FULLSTACK_STREAMING_PUMP_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/bm_fullstack_streaming_pump

endif

endif

$(BM_FULLSTACK_STREAMING_PUMP_OBJS): CPPFLAGS += -Ithird_party/benchmark/include -DHAVE_POSIX_REGEX
$(OBJDIR)/$(CONFIG)/test/cpp/microbenchmarks/bm_fullstack_streaming_pump.o:  $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_bm_fullstack_streaming_pump: $(BM_FULLSTACK_STREAMING_PUMP_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(BM_FULLSTACK_STREAMING_PUMP_OBJS:.o=.dep)
endif
endif


BM_FULLSTACK_TRICKLE_SRC = \
    test/cpp/microbenchmarks/bm_fullstack_trickle.cc \

BM_FULLSTACK_TRICKLE_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(BM_FULLSTACK_TRICKLE_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/bm_fullstack_trickle: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/bm_fullstack_trickle: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/bm_fullstack_trickle: $(PROTOBUF_DEP) $(BM_FULLSTACK_TRICKLE_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(BM_FULLSTACK_TRICKLE_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/bm_fullstack_trickle

endif

endif

$(BM_FULLSTACK_TRICKLE_OBJS): CPPFLAGS += -Ithird_party/benchmark/include -DHAVE_POSIX_REGEX
$(OBJDIR)/$(CONFIG)/test/cpp/microbenchmarks/bm_fullstack_trickle.o:  $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_bm_fullstack_trickle: $(BM_FULLSTACK_TRICKLE_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(BM_FULLSTACK_TRICKLE_OBJS:.o=.dep)
endif
endif


BM_FULLSTACK_UNARY_PING_PONG_SRC = \
    test/cpp/microbenchmarks/bm_fullstack_unary_ping_pong.cc \

BM_FULLSTACK_UNARY_PING_PONG_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(BM_FULLSTACK_UNARY_PING_PONG_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/bm_fullstack_unary_ping_pong: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/bm_fullstack_unary_ping_pong: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/bm_fullstack_unary_ping_pong: $(PROTOBUF_DEP) $(BM_FULLSTACK_UNARY_PING_PONG_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(BM_FULLSTACK_UNARY_PING_PONG_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/bm_fullstack_unary_ping_pong

endif

endif

$(BM_FULLSTACK_UNARY_PING_PONG_OBJS): CPPFLAGS += -Ithird_party/benchmark/include -DHAVE_POSIX_REGEX
$(OBJDIR)/$(CONFIG)/test/cpp/microbenchmarks/bm_fullstack_unary_ping_pong.o:  $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_bm_fullstack_unary_ping_pong: $(BM_FULLSTACK_UNARY_PING_PONG_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(BM_FULLSTACK_UNARY_PING_PONG_OBJS:.o=.dep)
endif
endif


BM_METADATA_SRC = \
    test/cpp/microbenchmarks/bm_metadata.cc \

BM_METADATA_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(BM_METADATA_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/bm_metadata: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/bm_metadata: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/bm_metadata: $(PROTOBUF_DEP) $(BM_METADATA_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(BM_METADATA_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/bm_metadata

endif

endif

$(BM_METADATA_OBJS): CPPFLAGS += -Ithird_party/benchmark/include -DHAVE_POSIX_REGEX
$(OBJDIR)/$(CONFIG)/test/cpp/microbenchmarks/bm_metadata.o:  $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_bm_metadata: $(BM_METADATA_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(BM_METADATA_OBJS:.o=.dep)
endif
endif


BM_POLLSET_SRC = \
    test/cpp/microbenchmarks/bm_pollset.cc \

BM_POLLSET_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(BM_POLLSET_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/bm_pollset: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/bm_pollset: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/bm_pollset: $(PROTOBUF_DEP) $(BM_POLLSET_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(BM_POLLSET_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/bm_pollset

endif

endif

$(BM_POLLSET_OBJS): CPPFLAGS += -Ithird_party/benchmark/include -DHAVE_POSIX_REGEX
$(OBJDIR)/$(CONFIG)/test/cpp/microbenchmarks/bm_pollset.o:  $(LIBDIR)/$(CONFIG)/libgrpc_benchmark.a $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_bm_pollset: $(BM_POLLSET_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(BM_POLLSET_OBJS:.o=.dep)
endif
endif


CHANNEL_ARGUMENTS_TEST_SRC = \
    test/cpp/common/channel_arguments_test.cc \

CHANNEL_ARGUMENTS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CHANNEL_ARGUMENTS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/channel_arguments_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/channel_arguments_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/channel_arguments_test: $(PROTOBUF_DEP) $(CHANNEL_ARGUMENTS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(CHANNEL_ARGUMENTS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/channel_arguments_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/common/channel_arguments_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_channel_arguments_test: $(CHANNEL_ARGUMENTS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHANNEL_ARGUMENTS_TEST_OBJS:.o=.dep)
endif
endif


CHANNEL_FILTER_TEST_SRC = \
    test/cpp/common/channel_filter_test.cc \

CHANNEL_FILTER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CHANNEL_FILTER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/channel_filter_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/channel_filter_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/channel_filter_test: $(PROTOBUF_DEP) $(CHANNEL_FILTER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(CHANNEL_FILTER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/channel_filter_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/common/channel_filter_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_channel_filter_test: $(CHANNEL_FILTER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHANNEL_FILTER_TEST_OBJS:.o=.dep)
endif
endif


CLI_CALL_TEST_SRC = \
    test/cpp/util/cli_call_test.cc \

CLI_CALL_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CLI_CALL_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/cli_call_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/cli_call_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/cli_call_test: $(PROTOBUF_DEP) $(CLI_CALL_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_cli_libs.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(CLI_CALL_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_cli_libs.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/cli_call_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/util/cli_call_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_cli_libs.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_cli_call_test: $(CLI_CALL_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CLI_CALL_TEST_OBJS:.o=.dep)
endif
endif


CLIENT_CRASH_TEST_SRC = \
    test/cpp/end2end/client_crash_test.cc \

CLIENT_CRASH_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CLIENT_CRASH_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/client_crash_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/client_crash_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/client_crash_test: $(PROTOBUF_DEP) $(CLIENT_CRASH_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(CLIENT_CRASH_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/client_crash_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/end2end/client_crash_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_client_crash_test: $(CLIENT_CRASH_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CLIENT_CRASH_TEST_OBJS:.o=.dep)
endif
endif


CLIENT_CRASH_TEST_SERVER_SRC = \
    test/cpp/end2end/client_crash_test_server.cc \

CLIENT_CRASH_TEST_SERVER_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CLIENT_CRASH_TEST_SERVER_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/client_crash_test_server: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/client_crash_test_server: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/client_crash_test_server: $(PROTOBUF_DEP) $(CLIENT_CRASH_TEST_SERVER_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(CLIENT_CRASH_TEST_SERVER_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/client_crash_test_server

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/end2end/client_crash_test_server.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_client_crash_test_server: $(CLIENT_CRASH_TEST_SERVER_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CLIENT_CRASH_TEST_SERVER_OBJS:.o=.dep)
endif
endif


CLIENT_LB_END2END_TEST_SRC = \
    test/cpp/end2end/client_lb_end2end_test.cc \

CLIENT_LB_END2END_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CLIENT_LB_END2END_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/client_lb_end2end_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/client_lb_end2end_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/client_lb_end2end_test: $(PROTOBUF_DEP) $(CLIENT_LB_END2END_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(CLIENT_LB_END2END_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/client_lb_end2end_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/end2end/client_lb_end2end_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_client_lb_end2end_test: $(CLIENT_LB_END2END_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CLIENT_LB_END2END_TEST_OBJS:.o=.dep)
endif
endif


CODEGEN_TEST_FULL_SRC = \
    $(GENDIR)/src/proto/grpc/testing/control.pb.cc $(GENDIR)/src/proto/grpc/testing/control.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/payloads.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/services.pb.cc $(GENDIR)/src/proto/grpc/testing/services.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/stats.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc \
    test/cpp/codegen/codegen_test_full.cc \

CODEGEN_TEST_FULL_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CODEGEN_TEST_FULL_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/codegen_test_full: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/codegen_test_full: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/codegen_test_full: $(PROTOBUF_DEP) $(CODEGEN_TEST_FULL_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(CODEGEN_TEST_FULL_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/codegen_test_full

endif

endif

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/control.o:  $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/messages.o:  $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/payloads.o:  $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/services.o:  $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/stats.o:  $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/test/cpp/codegen/codegen_test_full.o:  $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_codegen_test_full: $(CODEGEN_TEST_FULL_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CODEGEN_TEST_FULL_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/codegen/codegen_test_full.o: $(GENDIR)/src/proto/grpc/testing/control.pb.cc $(GENDIR)/src/proto/grpc/testing/control.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/services.pb.cc $(GENDIR)/src/proto/grpc/testing/services.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc


CODEGEN_TEST_MINIMAL_SRC = \
    $(GENDIR)/src/proto/grpc/testing/control.pb.cc $(GENDIR)/src/proto/grpc/testing/control.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/payloads.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/services.pb.cc $(GENDIR)/src/proto/grpc/testing/services.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/stats.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc \
    test/cpp/codegen/codegen_test_minimal.cc \
    src/cpp/codegen/codegen_init.cc \

CODEGEN_TEST_MINIMAL_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CODEGEN_TEST_MINIMAL_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/codegen_test_minimal: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/codegen_test_minimal: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/codegen_test_minimal: $(PROTOBUF_DEP) $(CODEGEN_TEST_MINIMAL_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(CODEGEN_TEST_MINIMAL_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/codegen_test_minimal

endif

endif

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/control.o:  $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/messages.o:  $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/payloads.o:  $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/services.o:  $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/stats.o:  $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/test/cpp/codegen/codegen_test_minimal.o:  $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/src/cpp/codegen/codegen_init.o:  $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_codegen_test_minimal: $(CODEGEN_TEST_MINIMAL_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CODEGEN_TEST_MINIMAL_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/codegen/codegen_test_minimal.o: $(GENDIR)/src/proto/grpc/testing/control.pb.cc $(GENDIR)/src/proto/grpc/testing/control.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/services.pb.cc $(GENDIR)/src/proto/grpc/testing/services.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/src/cpp/codegen/codegen_init.o: $(GENDIR)/src/proto/grpc/testing/control.pb.cc $(GENDIR)/src/proto/grpc/testing/control.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.pb.cc $(GENDIR)/src/proto/grpc/testing/payloads.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/services.pb.cc $(GENDIR)/src/proto/grpc/testing/services.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.pb.cc $(GENDIR)/src/proto/grpc/testing/stats.grpc.pb.cc


CREDENTIALS_TEST_SRC = \
    test/cpp/client/credentials_test.cc \

CREDENTIALS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CREDENTIALS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/credentials_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/credentials_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/credentials_test: $(PROTOBUF_DEP) $(CREDENTIALS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(CREDENTIALS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/credentials_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/client/credentials_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_credentials_test: $(CREDENTIALS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CREDENTIALS_TEST_OBJS:.o=.dep)
endif
endif


CXX_BYTE_BUFFER_TEST_SRC = \
    test/cpp/util/byte_buffer_test.cc \

CXX_BYTE_BUFFER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CXX_BYTE_BUFFER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/cxx_byte_buffer_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/cxx_byte_buffer_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/cxx_byte_buffer_test: $(PROTOBUF_DEP) $(CXX_BYTE_BUFFER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(CXX_BYTE_BUFFER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/cxx_byte_buffer_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/util/byte_buffer_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_cxx_byte_buffer_test: $(CXX_BYTE_BUFFER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CXX_BYTE_BUFFER_TEST_OBJS:.o=.dep)
endif
endif


CXX_SLICE_TEST_SRC = \
    test/cpp/util/slice_test.cc \

CXX_SLICE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CXX_SLICE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/cxx_slice_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/cxx_slice_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/cxx_slice_test: $(PROTOBUF_DEP) $(CXX_SLICE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(CXX_SLICE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/cxx_slice_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/util/slice_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_cxx_slice_test: $(CXX_SLICE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CXX_SLICE_TEST_OBJS:.o=.dep)
endif
endif


CXX_STRING_REF_TEST_SRC = \
    test/cpp/util/string_ref_test.cc \

CXX_STRING_REF_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CXX_STRING_REF_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/cxx_string_ref_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/cxx_string_ref_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/cxx_string_ref_test: $(PROTOBUF_DEP) $(CXX_STRING_REF_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(CXX_STRING_REF_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/cxx_string_ref_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/util/string_ref_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a

deps_cxx_string_ref_test: $(CXX_STRING_REF_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CXX_STRING_REF_TEST_OBJS:.o=.dep)
endif
endif


CXX_TIME_TEST_SRC = \
    test/cpp/util/time_test.cc \

CXX_TIME_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CXX_TIME_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/cxx_time_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/cxx_time_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/cxx_time_test: $(PROTOBUF_DEP) $(CXX_TIME_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(CXX_TIME_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/cxx_time_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/util/time_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_cxx_time_test: $(CXX_TIME_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CXX_TIME_TEST_OBJS:.o=.dep)
endif
endif


END2END_TEST_SRC = \
    test/cpp/end2end/end2end_test.cc \

END2END_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(END2END_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/end2end_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/end2end_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/end2end_test: $(PROTOBUF_DEP) $(END2END_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(END2END_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/end2end_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/end2end/end2end_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_end2end_test: $(END2END_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(END2END_TEST_OBJS:.o=.dep)
endif
endif


ERROR_DETAILS_TEST_SRC = \
    $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc \
    test/cpp/util/error_details_test.cc \

ERROR_DETAILS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(ERROR_DETAILS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/error_details_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/error_details_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/error_details_test: $(PROTOBUF_DEP) $(ERROR_DETAILS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_error_details.a $(LIBDIR)/$(CONFIG)/libgrpc++.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(ERROR_DETAILS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_error_details.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/error_details_test

endif

endif

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/echo_messages.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_error_details.a $(LIBDIR)/$(CONFIG)/libgrpc++.a

$(OBJDIR)/$(CONFIG)/test/cpp/util/error_details_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_error_details.a $(LIBDIR)/$(CONFIG)/libgrpc++.a

deps_error_details_test: $(ERROR_DETAILS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(ERROR_DETAILS_TEST_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/util/error_details_test.o: $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc


FILTER_END2END_TEST_SRC = \
    test/cpp/end2end/filter_end2end_test.cc \

FILTER_END2END_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(FILTER_END2END_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/filter_end2end_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/filter_end2end_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/filter_end2end_test: $(PROTOBUF_DEP) $(FILTER_END2END_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(FILTER_END2END_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/filter_end2end_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/end2end/filter_end2end_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_filter_end2end_test: $(FILTER_END2END_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(FILTER_END2END_TEST_OBJS:.o=.dep)
endif
endif


GENERIC_END2END_TEST_SRC = \
    test/cpp/end2end/generic_end2end_test.cc \

GENERIC_END2END_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GENERIC_END2END_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/generic_end2end_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/generic_end2end_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/generic_end2end_test: $(PROTOBUF_DEP) $(GENERIC_END2END_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(GENERIC_END2END_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/generic_end2end_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/end2end/generic_end2end_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_generic_end2end_test: $(GENERIC_END2END_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GENERIC_END2END_TEST_OBJS:.o=.dep)
endif
endif


GOLDEN_FILE_TEST_SRC = \
    $(GENDIR)/src/proto/grpc/testing/compiler_test.pb.cc $(GENDIR)/src/proto/grpc/testing/compiler_test.grpc.pb.cc \
    test/cpp/codegen/golden_file_test.cc \

GOLDEN_FILE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GOLDEN_FILE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/golden_file_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/golden_file_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/golden_file_test: $(PROTOBUF_DEP) $(GOLDEN_FILE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(GOLDEN_FILE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/golden_file_test

endif

endif

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/compiler_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/test/cpp/codegen/golden_file_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_golden_file_test: $(GOLDEN_FILE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GOLDEN_FILE_TEST_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/codegen/golden_file_test.o: $(GENDIR)/src/proto/grpc/testing/compiler_test.pb.cc $(GENDIR)/src/proto/grpc/testing/compiler_test.grpc.pb.cc


GRPC_CLI_SRC = \
    test/cpp/util/grpc_cli.cc \

GRPC_CLI_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_CLI_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/grpc_cli: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/grpc_cli: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/grpc_cli: $(PROTOBUF_DEP) $(GRPC_CLI_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_cli_libs.a $(LIBDIR)/$(CONFIG)/libgrpc++_proto_reflection_desc_db.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(GRPC_CLI_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_cli_libs.a $(LIBDIR)/$(CONFIG)/libgrpc++_proto_reflection_desc_db.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/grpc_cli

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/util/grpc_cli.o:  $(LIBDIR)/$(CONFIG)/libgrpc_cli_libs.a $(LIBDIR)/$(CONFIG)/libgrpc++_proto_reflection_desc_db.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

deps_grpc_cli: $(GRPC_CLI_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_CLI_OBJS:.o=.dep)
endif
endif


GRPC_CPP_PLUGIN_SRC = \
    src/compiler/cpp_plugin.cc \

GRPC_CPP_PLUGIN_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_CPP_PLUGIN_SRC))))



ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/grpc_cpp_plugin: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/grpc_cpp_plugin: $(PROTOBUF_DEP) $(GRPC_CPP_PLUGIN_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a
	$(E) "[HOSTLD]  Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(HOST_LDXX) $(HOST_LDFLAGS) $(GRPC_CPP_PLUGIN_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a $(HOST_LDLIBSXX) $(HOST_LDLIBS_PROTOC) $(HOST_LDLIBS) $(HOST_LDLIBS_PROTOC) -o $(BINDIR)/$(CONFIG)/grpc_cpp_plugin

endif

$(OBJDIR)/$(CONFIG)/src/compiler/cpp_plugin.o:  $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a

deps_grpc_cpp_plugin: $(GRPC_CPP_PLUGIN_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(GRPC_CPP_PLUGIN_OBJS:.o=.dep)
endif


GRPC_CSHARP_PLUGIN_SRC = \
    src/compiler/csharp_plugin.cc \

GRPC_CSHARP_PLUGIN_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_CSHARP_PLUGIN_SRC))))



ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/grpc_csharp_plugin: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/grpc_csharp_plugin: $(PROTOBUF_DEP) $(GRPC_CSHARP_PLUGIN_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a
	$(E) "[HOSTLD]  Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(HOST_LDXX) $(HOST_LDFLAGS) $(GRPC_CSHARP_PLUGIN_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a $(HOST_LDLIBSXX) $(HOST_LDLIBS_PROTOC) $(HOST_LDLIBS) $(HOST_LDLIBS_PROTOC) -o $(BINDIR)/$(CONFIG)/grpc_csharp_plugin

endif

$(OBJDIR)/$(CONFIG)/src/compiler/csharp_plugin.o:  $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a

deps_grpc_csharp_plugin: $(GRPC_CSHARP_PLUGIN_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(GRPC_CSHARP_PLUGIN_OBJS:.o=.dep)
endif


GRPC_NODE_PLUGIN_SRC = \
    src/compiler/node_plugin.cc \

GRPC_NODE_PLUGIN_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_NODE_PLUGIN_SRC))))



ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/grpc_node_plugin: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/grpc_node_plugin: $(PROTOBUF_DEP) $(GRPC_NODE_PLUGIN_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a
	$(E) "[HOSTLD]  Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(HOST_LDXX) $(HOST_LDFLAGS) $(GRPC_NODE_PLUGIN_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a $(HOST_LDLIBSXX) $(HOST_LDLIBS_PROTOC) $(HOST_LDLIBS) $(HOST_LDLIBS_PROTOC) -o $(BINDIR)/$(CONFIG)/grpc_node_plugin

endif

$(OBJDIR)/$(CONFIG)/src/compiler/node_plugin.o:  $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a

deps_grpc_node_plugin: $(GRPC_NODE_PLUGIN_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(GRPC_NODE_PLUGIN_OBJS:.o=.dep)
endif


GRPC_OBJECTIVE_C_PLUGIN_SRC = \
    src/compiler/objective_c_plugin.cc \

GRPC_OBJECTIVE_C_PLUGIN_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_OBJECTIVE_C_PLUGIN_SRC))))



ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/grpc_objective_c_plugin: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/grpc_objective_c_plugin: $(PROTOBUF_DEP) $(GRPC_OBJECTIVE_C_PLUGIN_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a
	$(E) "[HOSTLD]  Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(HOST_LDXX) $(HOST_LDFLAGS) $(GRPC_OBJECTIVE_C_PLUGIN_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a $(HOST_LDLIBSXX) $(HOST_LDLIBS_PROTOC) $(HOST_LDLIBS) $(HOST_LDLIBS_PROTOC) -o $(BINDIR)/$(CONFIG)/grpc_objective_c_plugin

endif

$(OBJDIR)/$(CONFIG)/src/compiler/objective_c_plugin.o:  $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a

deps_grpc_objective_c_plugin: $(GRPC_OBJECTIVE_C_PLUGIN_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(GRPC_OBJECTIVE_C_PLUGIN_OBJS:.o=.dep)
endif


GRPC_PHP_PLUGIN_SRC = \
    src/compiler/php_plugin.cc \

GRPC_PHP_PLUGIN_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_PHP_PLUGIN_SRC))))



ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/grpc_php_plugin: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/grpc_php_plugin: $(PROTOBUF_DEP) $(GRPC_PHP_PLUGIN_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a
	$(E) "[HOSTLD]  Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(HOST_LDXX) $(HOST_LDFLAGS) $(GRPC_PHP_PLUGIN_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a $(HOST_LDLIBSXX) $(HOST_LDLIBS_PROTOC) $(HOST_LDLIBS) $(HOST_LDLIBS_PROTOC) -o $(BINDIR)/$(CONFIG)/grpc_php_plugin

endif

$(OBJDIR)/$(CONFIG)/src/compiler/php_plugin.o:  $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a

deps_grpc_php_plugin: $(GRPC_PHP_PLUGIN_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(GRPC_PHP_PLUGIN_OBJS:.o=.dep)
endif


GRPC_PYTHON_PLUGIN_SRC = \
    src/compiler/python_plugin.cc \

GRPC_PYTHON_PLUGIN_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_PYTHON_PLUGIN_SRC))))



ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/grpc_python_plugin: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/grpc_python_plugin: $(PROTOBUF_DEP) $(GRPC_PYTHON_PLUGIN_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a
	$(E) "[HOSTLD]  Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(HOST_LDXX) $(HOST_LDFLAGS) $(GRPC_PYTHON_PLUGIN_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a $(HOST_LDLIBSXX) $(HOST_LDLIBS_PROTOC) $(HOST_LDLIBS) $(HOST_LDLIBS_PROTOC) -o $(BINDIR)/$(CONFIG)/grpc_python_plugin

endif

$(OBJDIR)/$(CONFIG)/src/compiler/python_plugin.o:  $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a

deps_grpc_python_plugin: $(GRPC_PYTHON_PLUGIN_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(GRPC_PYTHON_PLUGIN_OBJS:.o=.dep)
endif


GRPC_RUBY_PLUGIN_SRC = \
    src/compiler/ruby_plugin.cc \

GRPC_RUBY_PLUGIN_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_RUBY_PLUGIN_SRC))))



ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/grpc_ruby_plugin: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/grpc_ruby_plugin: $(PROTOBUF_DEP) $(GRPC_RUBY_PLUGIN_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a
	$(E) "[HOSTLD]  Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(HOST_LDXX) $(HOST_LDFLAGS) $(GRPC_RUBY_PLUGIN_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a $(HOST_LDLIBSXX) $(HOST_LDLIBS_PROTOC) $(HOST_LDLIBS) $(HOST_LDLIBS_PROTOC) -o $(BINDIR)/$(CONFIG)/grpc_ruby_plugin

endif

$(OBJDIR)/$(CONFIG)/src/compiler/ruby_plugin.o:  $(LIBDIR)/$(CONFIG)/libgrpc_plugin_support.a

deps_grpc_ruby_plugin: $(GRPC_RUBY_PLUGIN_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(GRPC_RUBY_PLUGIN_OBJS:.o=.dep)
endif


GRPC_TOOL_TEST_SRC = \
    $(GENDIR)/src/proto/grpc/testing/echo.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc \
    test/cpp/util/grpc_tool_test.cc \

GRPC_TOOL_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPC_TOOL_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/grpc_tool_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/grpc_tool_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/grpc_tool_test: $(PROTOBUF_DEP) $(GRPC_TOOL_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_cli_libs.a $(LIBDIR)/$(CONFIG)/libgrpc++_proto_reflection_desc_db.a $(LIBDIR)/$(CONFIG)/libgrpc++_reflection.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(GRPC_TOOL_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_cli_libs.a $(LIBDIR)/$(CONFIG)/libgrpc++_proto_reflection_desc_db.a $(LIBDIR)/$(CONFIG)/libgrpc++_reflection.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/grpc_tool_test

endif

endif

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/echo.o:  $(LIBDIR)/$(CONFIG)/libgrpc_cli_libs.a $(LIBDIR)/$(CONFIG)/libgrpc++_proto_reflection_desc_db.a $(LIBDIR)/$(CONFIG)/libgrpc++_reflection.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/echo_messages.o:  $(LIBDIR)/$(CONFIG)/libgrpc_cli_libs.a $(LIBDIR)/$(CONFIG)/libgrpc++_proto_reflection_desc_db.a $(LIBDIR)/$(CONFIG)/libgrpc++_reflection.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/test/cpp/util/grpc_tool_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_cli_libs.a $(LIBDIR)/$(CONFIG)/libgrpc++_proto_reflection_desc_db.a $(LIBDIR)/$(CONFIG)/libgrpc++_reflection.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_grpc_tool_test: $(GRPC_TOOL_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_TOOL_TEST_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/util/grpc_tool_test.o: $(GENDIR)/src/proto/grpc/testing/echo.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc


GRPCLB_API_TEST_SRC = \
    $(GENDIR)/src/proto/grpc/lb/v1/load_balancer.pb.cc $(GENDIR)/src/proto/grpc/lb/v1/load_balancer.grpc.pb.cc \
    test/cpp/grpclb/grpclb_api_test.cc \

GRPCLB_API_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPCLB_API_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/grpclb_api_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/grpclb_api_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/grpclb_api_test: $(PROTOBUF_DEP) $(GRPCLB_API_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(GRPCLB_API_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/grpclb_api_test

endif

endif

$(OBJDIR)/$(CONFIG)/src/proto/grpc/lb/v1/load_balancer.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a

$(OBJDIR)/$(CONFIG)/test/cpp/grpclb/grpclb_api_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a

deps_grpclb_api_test: $(GRPCLB_API_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPCLB_API_TEST_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/grpclb/grpclb_api_test.o: $(GENDIR)/src/proto/grpc/lb/v1/load_balancer.pb.cc $(GENDIR)/src/proto/grpc/lb/v1/load_balancer.grpc.pb.cc


GRPCLB_END2END_TEST_SRC = \
    $(GENDIR)/src/proto/grpc/lb/v1/load_balancer.pb.cc $(GENDIR)/src/proto/grpc/lb/v1/load_balancer.grpc.pb.cc \
    test/cpp/end2end/grpclb_end2end_test.cc \

GRPCLB_END2END_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPCLB_END2END_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/grpclb_end2end_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/grpclb_end2end_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/grpclb_end2end_test: $(PROTOBUF_DEP) $(GRPCLB_END2END_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(GRPCLB_END2END_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/grpclb_end2end_test

endif

endif

$(OBJDIR)/$(CONFIG)/src/proto/grpc/lb/v1/load_balancer.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/test/cpp/end2end/grpclb_end2end_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_grpclb_end2end_test: $(GRPCLB_END2END_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPCLB_END2END_TEST_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/end2end/grpclb_end2end_test.o: $(GENDIR)/src/proto/grpc/lb/v1/load_balancer.pb.cc $(GENDIR)/src/proto/grpc/lb/v1/load_balancer.grpc.pb.cc


GRPCLB_TEST_SRC = \
    $(GENDIR)/src/proto/grpc/lb/v1/load_balancer.pb.cc $(GENDIR)/src/proto/grpc/lb/v1/load_balancer.grpc.pb.cc \
    test/cpp/grpclb/grpclb_test.cc \

GRPCLB_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(GRPCLB_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/grpclb_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/grpclb_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/grpclb_test: $(PROTOBUF_DEP) $(GRPCLB_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(GRPCLB_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/grpclb_test

endif

endif

$(OBJDIR)/$(CONFIG)/src/proto/grpc/lb/v1/load_balancer.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/test/cpp/grpclb/grpclb_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_grpclb_test: $(GRPCLB_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPCLB_TEST_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/grpclb/grpclb_test.o: $(GENDIR)/src/proto/grpc/lb/v1/load_balancer.pb.cc $(GENDIR)/src/proto/grpc/lb/v1/load_balancer.grpc.pb.cc


HEALTH_SERVICE_END2END_TEST_SRC = \
    test/cpp/end2end/health_service_end2end_test.cc \

HEALTH_SERVICE_END2END_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(HEALTH_SERVICE_END2END_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/health_service_end2end_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/health_service_end2end_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/health_service_end2end_test: $(PROTOBUF_DEP) $(HEALTH_SERVICE_END2END_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(HEALTH_SERVICE_END2END_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/health_service_end2end_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/end2end/health_service_end2end_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_health_service_end2end_test: $(HEALTH_SERVICE_END2END_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(HEALTH_SERVICE_END2END_TEST_OBJS:.o=.dep)
endif
endif


ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/http2_client: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/http2_client: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/http2_client:  $(LIBDIR)/$(CONFIG)/libhttp2_client_main.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libhttp2_client_main.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/http2_client

endif

endif




HYBRID_END2END_TEST_SRC = \
    test/cpp/end2end/hybrid_end2end_test.cc \

HYBRID_END2END_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(HYBRID_END2END_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/hybrid_end2end_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/hybrid_end2end_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/hybrid_end2end_test: $(PROTOBUF_DEP) $(HYBRID_END2END_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(HYBRID_END2END_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/hybrid_end2end_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/end2end/hybrid_end2end_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_hybrid_end2end_test: $(HYBRID_END2END_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(HYBRID_END2END_TEST_OBJS:.o=.dep)
endif
endif


ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/interop_client: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/interop_client: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/interop_client:  $(LIBDIR)/$(CONFIG)/libinterop_client_main.a $(LIBDIR)/$(CONFIG)/libinterop_client_helper.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libinterop_client_main.a $(LIBDIR)/$(CONFIG)/libinterop_client_helper.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/interop_client

endif

endif




ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/interop_server: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/interop_server: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/interop_server:  $(LIBDIR)/$(CONFIG)/libinterop_server_main.a $(LIBDIR)/$(CONFIG)/libinterop_server_helper.a $(LIBDIR)/$(CONFIG)/libinterop_server_lib.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libinterop_server_main.a $(LIBDIR)/$(CONFIG)/libinterop_server_helper.a $(LIBDIR)/$(CONFIG)/libinterop_server_lib.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/interop_server

endif

endif




INTEROP_TEST_SRC = \
    test/cpp/interop/interop_test.cc \

INTEROP_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(INTEROP_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/interop_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/interop_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/interop_test: $(PROTOBUF_DEP) $(INTEROP_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(INTEROP_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/interop_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/interop/interop_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

deps_interop_test: $(INTEROP_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(INTEROP_TEST_OBJS:.o=.dep)
endif
endif


JSON_RUN_LOCALHOST_SRC = \
    test/cpp/qps/json_run_localhost.cc \

JSON_RUN_LOCALHOST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(JSON_RUN_LOCALHOST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/json_run_localhost: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/json_run_localhost: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/json_run_localhost: $(PROTOBUF_DEP) $(JSON_RUN_LOCALHOST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(JSON_RUN_LOCALHOST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/json_run_localhost

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/qps/json_run_localhost.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

deps_json_run_localhost: $(JSON_RUN_LOCALHOST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(JSON_RUN_LOCALHOST_OBJS:.o=.dep)
endif
endif


MEMORY_TEST_SRC = \
    test/core/support/memory_test.cc \

MEMORY_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(MEMORY_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/memory_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/memory_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/memory_test: $(PROTOBUF_DEP) $(MEMORY_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(MEMORY_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/memory_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/core/support/memory_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_memory_test: $(MEMORY_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(MEMORY_TEST_OBJS:.o=.dep)
endif
endif


METRICS_CLIENT_SRC = \
    $(GENDIR)/src/proto/grpc/testing/metrics.pb.cc $(GENDIR)/src/proto/grpc/testing/metrics.grpc.pb.cc \
    test/cpp/interop/metrics_client.cc \

METRICS_CLIENT_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(METRICS_CLIENT_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/metrics_client: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/metrics_client: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/metrics_client: $(PROTOBUF_DEP) $(METRICS_CLIENT_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(METRICS_CLIENT_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/metrics_client

endif

endif

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/metrics.o:  $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

$(OBJDIR)/$(CONFIG)/test/cpp/interop/metrics_client.o:  $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

deps_metrics_client: $(METRICS_CLIENT_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(METRICS_CLIENT_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/interop/metrics_client.o: $(GENDIR)/src/proto/grpc/testing/metrics.pb.cc $(GENDIR)/src/proto/grpc/testing/metrics.grpc.pb.cc


MOCK_TEST_SRC = \
    test/cpp/end2end/mock_test.cc \

MOCK_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(MOCK_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/mock_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/mock_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/mock_test: $(PROTOBUF_DEP) $(MOCK_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(MOCK_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/mock_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/end2end/mock_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_mock_test: $(MOCK_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(MOCK_TEST_OBJS:.o=.dep)
endif
endif


NOOP-BENCHMARK_SRC = \
    test/cpp/microbenchmarks/noop-benchmark.cc \

NOOP-BENCHMARK_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(NOOP-BENCHMARK_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/noop-benchmark: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/noop-benchmark: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/noop-benchmark: $(PROTOBUF_DEP) $(NOOP-BENCHMARK_OBJS) $(LIBDIR)/$(CONFIG)/libbenchmark.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(NOOP-BENCHMARK_OBJS) $(LIBDIR)/$(CONFIG)/libbenchmark.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/noop-benchmark

endif

endif

$(NOOP-BENCHMARK_OBJS): CPPFLAGS += -Ithird_party/benchmark/include -DHAVE_POSIX_REGEX
$(OBJDIR)/$(CONFIG)/test/cpp/microbenchmarks/noop-benchmark.o:  $(LIBDIR)/$(CONFIG)/libbenchmark.a

deps_noop-benchmark: $(NOOP-BENCHMARK_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(NOOP-BENCHMARK_OBJS:.o=.dep)
endif
endif


PROTO_SERVER_REFLECTION_TEST_SRC = \
    test/cpp/end2end/proto_server_reflection_test.cc \

PROTO_SERVER_REFLECTION_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(PROTO_SERVER_REFLECTION_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/proto_server_reflection_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/proto_server_reflection_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/proto_server_reflection_test: $(PROTOBUF_DEP) $(PROTO_SERVER_REFLECTION_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_proto_reflection_desc_db.a $(LIBDIR)/$(CONFIG)/libgrpc++_reflection.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(PROTO_SERVER_REFLECTION_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_proto_reflection_desc_db.a $(LIBDIR)/$(CONFIG)/libgrpc++_reflection.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/proto_server_reflection_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/end2end/proto_server_reflection_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_proto_reflection_desc_db.a $(LIBDIR)/$(CONFIG)/libgrpc++_reflection.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_proto_server_reflection_test: $(PROTO_SERVER_REFLECTION_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(PROTO_SERVER_REFLECTION_TEST_OBJS:.o=.dep)
endif
endif


PROTO_UTILS_TEST_SRC = \
    test/cpp/codegen/proto_utils_test.cc \

PROTO_UTILS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(PROTO_UTILS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/proto_utils_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/proto_utils_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/proto_utils_test: $(PROTOBUF_DEP) $(PROTO_UTILS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(PROTO_UTILS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/proto_utils_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/codegen/proto_utils_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a

deps_proto_utils_test: $(PROTO_UTILS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(PROTO_UTILS_TEST_OBJS:.o=.dep)
endif
endif


QPS_INTERARRIVAL_TEST_SRC = \
    test/cpp/qps/qps_interarrival_test.cc \

QPS_INTERARRIVAL_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(QPS_INTERARRIVAL_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/qps_interarrival_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/qps_interarrival_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/qps_interarrival_test: $(PROTOBUF_DEP) $(QPS_INTERARRIVAL_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libqps.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(QPS_INTERARRIVAL_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libqps.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/qps_interarrival_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/qps/qps_interarrival_test.o:  $(LIBDIR)/$(CONFIG)/libqps.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

deps_qps_interarrival_test: $(QPS_INTERARRIVAL_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(QPS_INTERARRIVAL_TEST_OBJS:.o=.dep)
endif
endif


QPS_JSON_DRIVER_SRC = \
    test/cpp/qps/qps_json_driver.cc \

QPS_JSON_DRIVER_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(QPS_JSON_DRIVER_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/qps_json_driver: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/qps_json_driver: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/qps_json_driver: $(PROTOBUF_DEP) $(QPS_JSON_DRIVER_OBJS) $(LIBDIR)/$(CONFIG)/libqps.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(QPS_JSON_DRIVER_OBJS) $(LIBDIR)/$(CONFIG)/libqps.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/qps_json_driver

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/qps/qps_json_driver.o:  $(LIBDIR)/$(CONFIG)/libqps.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

deps_qps_json_driver: $(QPS_JSON_DRIVER_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(QPS_JSON_DRIVER_OBJS:.o=.dep)
endif
endif


QPS_OPENLOOP_TEST_SRC = \
    test/cpp/qps/qps_openloop_test.cc \

QPS_OPENLOOP_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(QPS_OPENLOOP_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/qps_openloop_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/qps_openloop_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/qps_openloop_test: $(PROTOBUF_DEP) $(QPS_OPENLOOP_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libqps.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(QPS_OPENLOOP_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libqps.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/qps_openloop_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/qps/qps_openloop_test.o:  $(LIBDIR)/$(CONFIG)/libqps.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

deps_qps_openloop_test: $(QPS_OPENLOOP_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(QPS_OPENLOOP_TEST_OBJS:.o=.dep)
endif
endif


QPS_WORKER_SRC = \
    test/cpp/qps/worker.cc \

QPS_WORKER_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(QPS_WORKER_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/qps_worker: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/qps_worker: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/qps_worker: $(PROTOBUF_DEP) $(QPS_WORKER_OBJS) $(LIBDIR)/$(CONFIG)/libqps.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(QPS_WORKER_OBJS) $(LIBDIR)/$(CONFIG)/libqps.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/qps_worker

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/qps/worker.o:  $(LIBDIR)/$(CONFIG)/libqps.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

deps_qps_worker: $(QPS_WORKER_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(QPS_WORKER_OBJS:.o=.dep)
endif
endif


RECONNECT_INTEROP_CLIENT_SRC = \
    $(GENDIR)/src/proto/grpc/testing/empty.pb.cc $(GENDIR)/src/proto/grpc/testing/empty.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/test.pb.cc $(GENDIR)/src/proto/grpc/testing/test.grpc.pb.cc \
    test/cpp/interop/reconnect_interop_client.cc \

RECONNECT_INTEROP_CLIENT_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(RECONNECT_INTEROP_CLIENT_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/reconnect_interop_client: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/reconnect_interop_client: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/reconnect_interop_client: $(PROTOBUF_DEP) $(RECONNECT_INTEROP_CLIENT_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(RECONNECT_INTEROP_CLIENT_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/reconnect_interop_client

endif

endif

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/empty.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/messages.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

$(OBJDIR)/$(CONFIG)/test/cpp/interop/reconnect_interop_client.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

deps_reconnect_interop_client: $(RECONNECT_INTEROP_CLIENT_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(RECONNECT_INTEROP_CLIENT_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/interop/reconnect_interop_client.o: $(GENDIR)/src/proto/grpc/testing/empty.pb.cc $(GENDIR)/src/proto/grpc/testing/empty.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/test.pb.cc $(GENDIR)/src/proto/grpc/testing/test.grpc.pb.cc


RECONNECT_INTEROP_SERVER_SRC = \
    $(GENDIR)/src/proto/grpc/testing/empty.pb.cc $(GENDIR)/src/proto/grpc/testing/empty.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/test.pb.cc $(GENDIR)/src/proto/grpc/testing/test.grpc.pb.cc \
    test/cpp/interop/reconnect_interop_server.cc \

RECONNECT_INTEROP_SERVER_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(RECONNECT_INTEROP_SERVER_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/reconnect_interop_server: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/reconnect_interop_server: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/reconnect_interop_server: $(PROTOBUF_DEP) $(RECONNECT_INTEROP_SERVER_OBJS) $(LIBDIR)/$(CONFIG)/libreconnect_server.a $(LIBDIR)/$(CONFIG)/libtest_tcp_server.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(RECONNECT_INTEROP_SERVER_OBJS) $(LIBDIR)/$(CONFIG)/libreconnect_server.a $(LIBDIR)/$(CONFIG)/libtest_tcp_server.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/reconnect_interop_server

endif

endif

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/empty.o:  $(LIBDIR)/$(CONFIG)/libreconnect_server.a $(LIBDIR)/$(CONFIG)/libtest_tcp_server.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/messages.o:  $(LIBDIR)/$(CONFIG)/libreconnect_server.a $(LIBDIR)/$(CONFIG)/libtest_tcp_server.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/test.o:  $(LIBDIR)/$(CONFIG)/libreconnect_server.a $(LIBDIR)/$(CONFIG)/libtest_tcp_server.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

$(OBJDIR)/$(CONFIG)/test/cpp/interop/reconnect_interop_server.o:  $(LIBDIR)/$(CONFIG)/libreconnect_server.a $(LIBDIR)/$(CONFIG)/libtest_tcp_server.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

deps_reconnect_interop_server: $(RECONNECT_INTEROP_SERVER_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(RECONNECT_INTEROP_SERVER_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/interop/reconnect_interop_server.o: $(GENDIR)/src/proto/grpc/testing/empty.pb.cc $(GENDIR)/src/proto/grpc/testing/empty.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/test.pb.cc $(GENDIR)/src/proto/grpc/testing/test.grpc.pb.cc


SECURE_AUTH_CONTEXT_TEST_SRC = \
    test/cpp/common/secure_auth_context_test.cc \

SECURE_AUTH_CONTEXT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SECURE_AUTH_CONTEXT_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/secure_auth_context_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/secure_auth_context_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/secure_auth_context_test: $(PROTOBUF_DEP) $(SECURE_AUTH_CONTEXT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(SECURE_AUTH_CONTEXT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/secure_auth_context_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/common/secure_auth_context_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_secure_auth_context_test: $(SECURE_AUTH_CONTEXT_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SECURE_AUTH_CONTEXT_TEST_OBJS:.o=.dep)
endif
endif


SECURE_SYNC_UNARY_PING_PONG_TEST_SRC = \
    test/cpp/qps/secure_sync_unary_ping_pong_test.cc \

SECURE_SYNC_UNARY_PING_PONG_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SECURE_SYNC_UNARY_PING_PONG_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/secure_sync_unary_ping_pong_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/secure_sync_unary_ping_pong_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/secure_sync_unary_ping_pong_test: $(PROTOBUF_DEP) $(SECURE_SYNC_UNARY_PING_PONG_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libqps.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(SECURE_SYNC_UNARY_PING_PONG_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libqps.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/secure_sync_unary_ping_pong_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/qps/secure_sync_unary_ping_pong_test.o:  $(LIBDIR)/$(CONFIG)/libqps.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

deps_secure_sync_unary_ping_pong_test: $(SECURE_SYNC_UNARY_PING_PONG_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SECURE_SYNC_UNARY_PING_PONG_TEST_OBJS:.o=.dep)
endif
endif


SERVER_BUILDER_PLUGIN_TEST_SRC = \
    test/cpp/end2end/server_builder_plugin_test.cc \

SERVER_BUILDER_PLUGIN_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SERVER_BUILDER_PLUGIN_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/server_builder_plugin_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/server_builder_plugin_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/server_builder_plugin_test: $(PROTOBUF_DEP) $(SERVER_BUILDER_PLUGIN_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(SERVER_BUILDER_PLUGIN_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/server_builder_plugin_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/end2end/server_builder_plugin_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_server_builder_plugin_test: $(SERVER_BUILDER_PLUGIN_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SERVER_BUILDER_PLUGIN_TEST_OBJS:.o=.dep)
endif
endif


SERVER_BUILDER_TEST_SRC = \
    $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/echo.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc \
    test/cpp/server/server_builder_test.cc \

SERVER_BUILDER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SERVER_BUILDER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/server_builder_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/server_builder_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/server_builder_test: $(PROTOBUF_DEP) $(SERVER_BUILDER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(SERVER_BUILDER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/server_builder_test

endif

endif

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/echo_messages.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/echo.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/test/cpp/server/server_builder_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_server_builder_test: $(SERVER_BUILDER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SERVER_BUILDER_TEST_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/server/server_builder_test.o: $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc


SERVER_CONTEXT_TEST_SPOUSE_TEST_SRC = \
    test/cpp/test/server_context_test_spouse_test.cc \

SERVER_CONTEXT_TEST_SPOUSE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SERVER_CONTEXT_TEST_SPOUSE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/server_context_test_spouse_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/server_context_test_spouse_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/server_context_test_spouse_test: $(PROTOBUF_DEP) $(SERVER_CONTEXT_TEST_SPOUSE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(SERVER_CONTEXT_TEST_SPOUSE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/server_context_test_spouse_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/test/server_context_test_spouse_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_server_context_test_spouse_test: $(SERVER_CONTEXT_TEST_SPOUSE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SERVER_CONTEXT_TEST_SPOUSE_TEST_OBJS:.o=.dep)
endif
endif


SERVER_CRASH_TEST_SRC = \
    test/cpp/end2end/server_crash_test.cc \

SERVER_CRASH_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SERVER_CRASH_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/server_crash_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/server_crash_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/server_crash_test: $(PROTOBUF_DEP) $(SERVER_CRASH_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(SERVER_CRASH_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/server_crash_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/end2end/server_crash_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_server_crash_test: $(SERVER_CRASH_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SERVER_CRASH_TEST_OBJS:.o=.dep)
endif
endif


SERVER_CRASH_TEST_CLIENT_SRC = \
    test/cpp/end2end/server_crash_test_client.cc \

SERVER_CRASH_TEST_CLIENT_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SERVER_CRASH_TEST_CLIENT_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/server_crash_test_client: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/server_crash_test_client: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/server_crash_test_client: $(PROTOBUF_DEP) $(SERVER_CRASH_TEST_CLIENT_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(SERVER_CRASH_TEST_CLIENT_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/server_crash_test_client

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/end2end/server_crash_test_client.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_server_crash_test_client: $(SERVER_CRASH_TEST_CLIENT_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SERVER_CRASH_TEST_CLIENT_OBJS:.o=.dep)
endif
endif


SERVER_REQUEST_CALL_TEST_SRC = \
    $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/echo.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc \
    test/cpp/server/server_request_call_test.cc \

SERVER_REQUEST_CALL_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SERVER_REQUEST_CALL_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/server_request_call_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/server_request_call_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/server_request_call_test: $(PROTOBUF_DEP) $(SERVER_REQUEST_CALL_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(SERVER_REQUEST_CALL_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/server_request_call_test

endif

endif

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/echo_messages.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/echo.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/test/cpp/server/server_request_call_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_server_request_call_test: $(SERVER_REQUEST_CALL_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SERVER_REQUEST_CALL_TEST_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/server/server_request_call_test.o: $(GENDIR)/src/proto/grpc/testing/echo_messages.pb.cc $(GENDIR)/src/proto/grpc/testing/echo_messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.pb.cc $(GENDIR)/src/proto/grpc/testing/echo.grpc.pb.cc


SHUTDOWN_TEST_SRC = \
    test/cpp/end2end/shutdown_test.cc \

SHUTDOWN_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SHUTDOWN_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/shutdown_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/shutdown_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/shutdown_test: $(PROTOBUF_DEP) $(SHUTDOWN_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(SHUTDOWN_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/shutdown_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/end2end/shutdown_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_shutdown_test: $(SHUTDOWN_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SHUTDOWN_TEST_OBJS:.o=.dep)
endif
endif


STATUS_TEST_SRC = \
    test/cpp/util/status_test.cc \

STATUS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(STATUS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/status_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/status_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/status_test: $(PROTOBUF_DEP) $(STATUS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(STATUS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/status_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/util/status_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_status_test: $(STATUS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(STATUS_TEST_OBJS:.o=.dep)
endif
endif


STREAMING_THROUGHPUT_TEST_SRC = \
    test/cpp/end2end/streaming_throughput_test.cc \

STREAMING_THROUGHPUT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(STREAMING_THROUGHPUT_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/streaming_throughput_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/streaming_throughput_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/streaming_throughput_test: $(PROTOBUF_DEP) $(STREAMING_THROUGHPUT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(STREAMING_THROUGHPUT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/streaming_throughput_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/end2end/streaming_throughput_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_streaming_throughput_test: $(STREAMING_THROUGHPUT_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(STREAMING_THROUGHPUT_TEST_OBJS:.o=.dep)
endif
endif


STRESS_TEST_SRC = \
    $(GENDIR)/src/proto/grpc/testing/empty.pb.cc $(GENDIR)/src/proto/grpc/testing/empty.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/metrics.pb.cc $(GENDIR)/src/proto/grpc/testing/metrics.grpc.pb.cc \
    $(GENDIR)/src/proto/grpc/testing/test.pb.cc $(GENDIR)/src/proto/grpc/testing/test.grpc.pb.cc \
    test/cpp/interop/interop_client.cc \
    test/cpp/interop/stress_interop_client.cc \
    test/cpp/interop/stress_test.cc \
    test/cpp/util/metrics_server.cc \

STRESS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(STRESS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/stress_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/stress_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/stress_test: $(PROTOBUF_DEP) $(STRESS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(STRESS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/stress_test

endif

endif

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/empty.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/messages.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/metrics.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

$(OBJDIR)/$(CONFIG)/src/proto/grpc/testing/test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

$(OBJDIR)/$(CONFIG)/test/cpp/interop/interop_client.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

$(OBJDIR)/$(CONFIG)/test/cpp/interop/stress_interop_client.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

$(OBJDIR)/$(CONFIG)/test/cpp/interop/stress_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

$(OBJDIR)/$(CONFIG)/test/cpp/util/metrics_server.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

deps_stress_test: $(STRESS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(STRESS_TEST_OBJS:.o=.dep)
endif
endif
$(OBJDIR)/$(CONFIG)/test/cpp/interop/interop_client.o: $(GENDIR)/src/proto/grpc/testing/empty.pb.cc $(GENDIR)/src/proto/grpc/testing/empty.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/metrics.pb.cc $(GENDIR)/src/proto/grpc/testing/metrics.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/test.pb.cc $(GENDIR)/src/proto/grpc/testing/test.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/interop/stress_interop_client.o: $(GENDIR)/src/proto/grpc/testing/empty.pb.cc $(GENDIR)/src/proto/grpc/testing/empty.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/metrics.pb.cc $(GENDIR)/src/proto/grpc/testing/metrics.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/test.pb.cc $(GENDIR)/src/proto/grpc/testing/test.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/interop/stress_test.o: $(GENDIR)/src/proto/grpc/testing/empty.pb.cc $(GENDIR)/src/proto/grpc/testing/empty.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/metrics.pb.cc $(GENDIR)/src/proto/grpc/testing/metrics.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/test.pb.cc $(GENDIR)/src/proto/grpc/testing/test.grpc.pb.cc
$(OBJDIR)/$(CONFIG)/test/cpp/util/metrics_server.o: $(GENDIR)/src/proto/grpc/testing/empty.pb.cc $(GENDIR)/src/proto/grpc/testing/empty.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.pb.cc $(GENDIR)/src/proto/grpc/testing/messages.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/metrics.pb.cc $(GENDIR)/src/proto/grpc/testing/metrics.grpc.pb.cc $(GENDIR)/src/proto/grpc/testing/test.pb.cc $(GENDIR)/src/proto/grpc/testing/test.grpc.pb.cc


THREAD_MANAGER_TEST_SRC = \
    test/cpp/thread_manager/thread_manager_test.cc \

THREAD_MANAGER_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(THREAD_MANAGER_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/thread_manager_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/thread_manager_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/thread_manager_test: $(PROTOBUF_DEP) $(THREAD_MANAGER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(THREAD_MANAGER_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/thread_manager_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/thread_manager/thread_manager_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LIBDIR)/$(CONFIG)/libgrpc++_test_config.a

deps_thread_manager_test: $(THREAD_MANAGER_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(THREAD_MANAGER_TEST_OBJS:.o=.dep)
endif
endif


THREAD_STRESS_TEST_SRC = \
    test/cpp/end2end/thread_stress_test.cc \

THREAD_STRESS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(THREAD_STRESS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/thread_stress_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/thread_stress_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/thread_stress_test: $(PROTOBUF_DEP) $(THREAD_STRESS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(THREAD_STRESS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/thread_stress_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/end2end/thread_stress_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc++_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_thread_stress_test: $(THREAD_STRESS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(THREAD_STRESS_TEST_OBJS:.o=.dep)
endif
endif


WRITES_PER_RPC_TEST_SRC = \
    test/cpp/performance/writes_per_rpc_test.cc \

WRITES_PER_RPC_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(WRITES_PER_RPC_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/writes_per_rpc_test: openssl_dep_error

else




ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/writes_per_rpc_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/writes_per_rpc_test: $(PROTOBUF_DEP) $(WRITES_PER_RPC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(WRITES_PER_RPC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(LDLIBS_SECURE) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/writes_per_rpc_test

endif

endif

$(OBJDIR)/$(CONFIG)/test/cpp/performance/writes_per_rpc_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc++_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc++.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_writes_per_rpc_test: $(WRITES_PER_RPC_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(WRITES_PER_RPC_TEST_OBJS:.o=.dep)
endif
endif


PUBLIC_HEADERS_MUST_BE_C89_SRC = \
    test/core/surface/public_headers_must_be_c89.c \

PUBLIC_HEADERS_MUST_BE_C89_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(PUBLIC_HEADERS_MUST_BE_C89_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/public_headers_must_be_c89: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/public_headers_must_be_c89: $(PUBLIC_HEADERS_MUST_BE_C89_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(PUBLIC_HEADERS_MUST_BE_C89_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/public_headers_must_be_c89

endif

$(OBJDIR)/$(CONFIG)/test/core/surface/public_headers_must_be_c89.o:  $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr.a
$(OBJDIR)/$(CONFIG)/test/core/surface/public_headers_must_be_c89.o : test/core/surface/public_headers_must_be_c89.c
	$(E) "[C]       Compiling $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(CC) $(CPPFLAGS) $(CFLAGS) -std=c89 -pedantic -MMD -MF $(addsuffix .dep, $(basename $@)) -c -o $@ $<

deps_public_headers_must_be_c89: $(PUBLIC_HEADERS_MUST_BE_C89_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(PUBLIC_HEADERS_MUST_BE_C89_OBJS:.o=.dep)
endif
endif



# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_AES_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_AES_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_AES_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_aes_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_aes_test:  $(LIBDIR)/$(CONFIG)/libboringssl_aes_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_aes_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_aes_test

endif

$(BORINGSSL_AES_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_AES_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_ASN1_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_ASN1_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_ASN1_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_asn1_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_asn1_test:  $(LIBDIR)/$(CONFIG)/libboringssl_asn1_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_asn1_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_asn1_test

endif

$(BORINGSSL_ASN1_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_ASN1_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_BASE64_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_BASE64_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_BASE64_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_base64_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_base64_test:  $(LIBDIR)/$(CONFIG)/libboringssl_base64_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_base64_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_base64_test

endif

$(BORINGSSL_BASE64_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_BASE64_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_BIO_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_BIO_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_BIO_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_bio_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_bio_test:  $(LIBDIR)/$(CONFIG)/libboringssl_bio_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_bio_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_bio_test

endif

$(BORINGSSL_BIO_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_BIO_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_BN_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_BN_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_BN_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_bn_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_bn_test:  $(LIBDIR)/$(CONFIG)/libboringssl_bn_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_bn_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_bn_test

endif

$(BORINGSSL_BN_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_BN_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_BYTESTRING_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_BYTESTRING_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_BYTESTRING_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_bytestring_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_bytestring_test:  $(LIBDIR)/$(CONFIG)/libboringssl_bytestring_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_bytestring_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_bytestring_test

endif

$(BORINGSSL_BYTESTRING_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_BYTESTRING_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_AEAD_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_AEAD_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_AEAD_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_aead_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_aead_test:  $(LIBDIR)/$(CONFIG)/libboringssl_aead_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_aead_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_aead_test

endif

$(BORINGSSL_AEAD_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_AEAD_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_CIPHER_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_CIPHER_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_CIPHER_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_cipher_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_cipher_test:  $(LIBDIR)/$(CONFIG)/libboringssl_cipher_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_cipher_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_cipher_test

endif

$(BORINGSSL_CIPHER_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_CIPHER_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_CMAC_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_CMAC_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_CMAC_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_cmac_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_cmac_test:  $(LIBDIR)/$(CONFIG)/libboringssl_cmac_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_cmac_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_cmac_test

endif

$(BORINGSSL_CMAC_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_CMAC_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_CONSTANT_TIME_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_CONSTANT_TIME_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_CONSTANT_TIME_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_constant_time_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_constant_time_test:  $(LIBDIR)/$(CONFIG)/libboringssl_constant_time_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_constant_time_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_constant_time_test

endif

$(BORINGSSL_CONSTANT_TIME_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_CONSTANT_TIME_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_ED25519_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_ED25519_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_ED25519_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_ed25519_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_ed25519_test:  $(LIBDIR)/$(CONFIG)/libboringssl_ed25519_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_ed25519_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_ed25519_test

endif

$(BORINGSSL_ED25519_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_ED25519_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_SPAKE25519_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_SPAKE25519_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_SPAKE25519_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_spake25519_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_spake25519_test:  $(LIBDIR)/$(CONFIG)/libboringssl_spake25519_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_spake25519_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_spake25519_test

endif

$(BORINGSSL_SPAKE25519_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_SPAKE25519_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_X25519_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_X25519_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_X25519_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_x25519_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_x25519_test:  $(LIBDIR)/$(CONFIG)/libboringssl_x25519_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_x25519_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_x25519_test

endif

$(BORINGSSL_X25519_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_X25519_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_DIGEST_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_DIGEST_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_DIGEST_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_digest_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_digest_test:  $(LIBDIR)/$(CONFIG)/libboringssl_digest_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_digest_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_digest_test

endif

$(BORINGSSL_DIGEST_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_DIGEST_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_EXAMPLE_MUL_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_EXAMPLE_MUL_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_EXAMPLE_MUL_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_example_mul: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_example_mul:  $(LIBDIR)/$(CONFIG)/libboringssl_example_mul_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_example_mul_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_example_mul

endif

$(BORINGSSL_EXAMPLE_MUL_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_EXAMPLE_MUL_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_P256-X86_64_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_P256-X86_64_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_P256-X86_64_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_p256-x86_64_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_p256-x86_64_test:  $(LIBDIR)/$(CONFIG)/libboringssl_p256-x86_64_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_p256-x86_64_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_p256-x86_64_test

endif

$(BORINGSSL_P256-X86_64_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_P256-X86_64_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_ECDH_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_ECDH_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_ECDH_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_ecdh_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_ecdh_test:  $(LIBDIR)/$(CONFIG)/libboringssl_ecdh_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_ecdh_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_ecdh_test

endif

$(BORINGSSL_ECDH_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_ECDH_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_ECDSA_SIGN_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_ECDSA_SIGN_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_ECDSA_SIGN_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_ecdsa_sign_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_ecdsa_sign_test:  $(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_sign_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_sign_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_ecdsa_sign_test

endif

$(BORINGSSL_ECDSA_SIGN_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_ECDSA_SIGN_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_ECDSA_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_ECDSA_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_ECDSA_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_ecdsa_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_ecdsa_test:  $(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_ecdsa_test

endif

$(BORINGSSL_ECDSA_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_ECDSA_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_ECDSA_VERIFY_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_ECDSA_VERIFY_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_ECDSA_VERIFY_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_ecdsa_verify_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_ecdsa_verify_test:  $(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_verify_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_ecdsa_verify_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_ecdsa_verify_test

endif

$(BORINGSSL_ECDSA_VERIFY_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_ECDSA_VERIFY_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_EVP_EXTRA_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_EVP_EXTRA_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_EVP_EXTRA_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_evp_extra_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_evp_extra_test:  $(LIBDIR)/$(CONFIG)/libboringssl_evp_extra_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_evp_extra_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_evp_extra_test

endif

$(BORINGSSL_EVP_EXTRA_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_EVP_EXTRA_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_EVP_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_EVP_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_EVP_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_evp_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_evp_test:  $(LIBDIR)/$(CONFIG)/libboringssl_evp_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_evp_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_evp_test

endif

$(BORINGSSL_EVP_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_EVP_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_PBKDF_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_PBKDF_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_PBKDF_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_pbkdf_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_pbkdf_test:  $(LIBDIR)/$(CONFIG)/libboringssl_pbkdf_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_pbkdf_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_pbkdf_test

endif

$(BORINGSSL_PBKDF_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_PBKDF_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_HKDF_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_HKDF_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_HKDF_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_hkdf_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_hkdf_test:  $(LIBDIR)/$(CONFIG)/libboringssl_hkdf_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_hkdf_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_hkdf_test

endif

$(BORINGSSL_HKDF_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_HKDF_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_HMAC_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_HMAC_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_HMAC_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_hmac_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_hmac_test:  $(LIBDIR)/$(CONFIG)/libboringssl_hmac_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_hmac_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_hmac_test

endif

$(BORINGSSL_HMAC_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_HMAC_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_LHASH_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_LHASH_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_LHASH_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_lhash_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_lhash_test:  $(LIBDIR)/$(CONFIG)/libboringssl_lhash_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_lhash_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_lhash_test

endif

$(BORINGSSL_LHASH_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_LHASH_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_GCM_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_GCM_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_GCM_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_gcm_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_gcm_test:  $(LIBDIR)/$(CONFIG)/libboringssl_gcm_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_gcm_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_gcm_test

endif

$(BORINGSSL_GCM_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_GCM_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_OBJ_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_OBJ_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_OBJ_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_obj_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_obj_test:  $(LIBDIR)/$(CONFIG)/libboringssl_obj_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_obj_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_obj_test

endif

$(BORINGSSL_OBJ_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_OBJ_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_PKCS12_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_PKCS12_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_PKCS12_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_pkcs12_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_pkcs12_test:  $(LIBDIR)/$(CONFIG)/libboringssl_pkcs12_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_pkcs12_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_pkcs12_test

endif

$(BORINGSSL_PKCS12_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_PKCS12_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_PKCS8_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_PKCS8_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_PKCS8_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_pkcs8_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_pkcs8_test:  $(LIBDIR)/$(CONFIG)/libboringssl_pkcs8_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_pkcs8_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_pkcs8_test

endif

$(BORINGSSL_PKCS8_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_PKCS8_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_POLY1305_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_POLY1305_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_POLY1305_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_poly1305_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_poly1305_test:  $(LIBDIR)/$(CONFIG)/libboringssl_poly1305_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_poly1305_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_poly1305_test

endif

$(BORINGSSL_POLY1305_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_POLY1305_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_POOL_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_POOL_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_POOL_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_pool_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_pool_test:  $(LIBDIR)/$(CONFIG)/libboringssl_pool_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_pool_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_pool_test

endif

$(BORINGSSL_POOL_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_POOL_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_REFCOUNT_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_REFCOUNT_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_REFCOUNT_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_refcount_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_refcount_test:  $(LIBDIR)/$(CONFIG)/libboringssl_refcount_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_refcount_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_refcount_test

endif

$(BORINGSSL_REFCOUNT_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_REFCOUNT_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_THREAD_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_THREAD_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_THREAD_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_thread_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_thread_test:  $(LIBDIR)/$(CONFIG)/libboringssl_thread_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_thread_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_thread_test

endif

$(BORINGSSL_THREAD_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_THREAD_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_PKCS7_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_PKCS7_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_PKCS7_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_pkcs7_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_pkcs7_test:  $(LIBDIR)/$(CONFIG)/libboringssl_pkcs7_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_pkcs7_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_pkcs7_test

endif

$(BORINGSSL_PKCS7_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_PKCS7_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_X509_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_X509_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_X509_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_x509_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_x509_test:  $(LIBDIR)/$(CONFIG)/libboringssl_x509_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_x509_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_x509_test

endif

$(BORINGSSL_X509_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_X509_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_TAB_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_TAB_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_TAB_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_tab_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_tab_test:  $(LIBDIR)/$(CONFIG)/libboringssl_tab_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_tab_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_tab_test

endif

$(BORINGSSL_TAB_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_TAB_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)




# boringssl needs an override to ensure that it does not include
# system openssl headers regardless of other configuration
# we do so here with a target specific variable assignment
$(BORINGSSL_V3NAME_TEST_OBJS): CFLAGS := -Ithird_party/boringssl/include $(CFLAGS) -Wno-sign-conversion -Wno-conversion -Wno-unused-value $(NO_W_EXTRA_SEMI)
$(BORINGSSL_V3NAME_TEST_OBJS): CXXFLAGS := -Ithird_party/boringssl/include $(CXXFLAGS)
$(BORINGSSL_V3NAME_TEST_OBJS): CPPFLAGS += -DOPENSSL_NO_ASM -D_GNU_SOURCE


ifeq ($(NO_PROTOBUF),true)

# You can't build the protoc plugins or protobuf-enabled targets if you don't have protobuf 3.0.0+.

$(BINDIR)/$(CONFIG)/boringssl_v3name_test: protobuf_dep_error

else

$(BINDIR)/$(CONFIG)/boringssl_v3name_test:  $(LIBDIR)/$(CONFIG)/libboringssl_v3name_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS)  $(LIBDIR)/$(CONFIG)/libboringssl_v3name_test_lib.a $(LIBDIR)/$(CONFIG)/libboringssl_test_util.a $(LIBDIR)/$(CONFIG)/libboringssl.a $(LDLIBSXX) $(LDLIBS_PROTOBUF) $(LDLIBS) $(GTEST_LIB) -o $(BINDIR)/$(CONFIG)/boringssl_v3name_test

endif

$(BORINGSSL_V3NAME_TEST_OBJS): CPPFLAGS += -Ithird_party/boringssl/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(BORINGSSL_V3NAME_TEST_OBJS): CFLAGS += -Wno-sign-conversion -Wno-conversion -Wno-unused-value -Wno-unknown-pragmas -Wno-implicit-function-declaration -Wno-unused-variable -Wno-sign-compare $(NO_W_EXTRA_SEMI)



BADREQ_BAD_CLIENT_TEST_SRC = \
    test/core/bad_client/tests/badreq.c \

BADREQ_BAD_CLIENT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(BADREQ_BAD_CLIENT_TEST_SRC))))


$(BINDIR)/$(CONFIG)/badreq_bad_client_test: $(BADREQ_BAD_CLIENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(BADREQ_BAD_CLIENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/badreq_bad_client_test

$(OBJDIR)/$(CONFIG)/test/core/bad_client/tests/badreq.o:  $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_badreq_bad_client_test: $(BADREQ_BAD_CLIENT_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(BADREQ_BAD_CLIENT_TEST_OBJS:.o=.dep)
endif


CONNECTION_PREFIX_BAD_CLIENT_TEST_SRC = \
    test/core/bad_client/tests/connection_prefix.c \

CONNECTION_PREFIX_BAD_CLIENT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CONNECTION_PREFIX_BAD_CLIENT_TEST_SRC))))


$(BINDIR)/$(CONFIG)/connection_prefix_bad_client_test: $(CONNECTION_PREFIX_BAD_CLIENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CONNECTION_PREFIX_BAD_CLIENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/connection_prefix_bad_client_test

$(OBJDIR)/$(CONFIG)/test/core/bad_client/tests/connection_prefix.o:  $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_connection_prefix_bad_client_test: $(CONNECTION_PREFIX_BAD_CLIENT_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(CONNECTION_PREFIX_BAD_CLIENT_TEST_OBJS:.o=.dep)
endif


HEAD_OF_LINE_BLOCKING_BAD_CLIENT_TEST_SRC = \
    test/core/bad_client/tests/head_of_line_blocking.c \

HEAD_OF_LINE_BLOCKING_BAD_CLIENT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(HEAD_OF_LINE_BLOCKING_BAD_CLIENT_TEST_SRC))))


$(BINDIR)/$(CONFIG)/head_of_line_blocking_bad_client_test: $(HEAD_OF_LINE_BLOCKING_BAD_CLIENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(HEAD_OF_LINE_BLOCKING_BAD_CLIENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/head_of_line_blocking_bad_client_test

$(OBJDIR)/$(CONFIG)/test/core/bad_client/tests/head_of_line_blocking.o:  $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_head_of_line_blocking_bad_client_test: $(HEAD_OF_LINE_BLOCKING_BAD_CLIENT_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(HEAD_OF_LINE_BLOCKING_BAD_CLIENT_TEST_OBJS:.o=.dep)
endif


HEADERS_BAD_CLIENT_TEST_SRC = \
    test/core/bad_client/tests/headers.c \

HEADERS_BAD_CLIENT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(HEADERS_BAD_CLIENT_TEST_SRC))))


$(BINDIR)/$(CONFIG)/headers_bad_client_test: $(HEADERS_BAD_CLIENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(HEADERS_BAD_CLIENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/headers_bad_client_test

$(OBJDIR)/$(CONFIG)/test/core/bad_client/tests/headers.o:  $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_headers_bad_client_test: $(HEADERS_BAD_CLIENT_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(HEADERS_BAD_CLIENT_TEST_OBJS:.o=.dep)
endif


INITIAL_SETTINGS_FRAME_BAD_CLIENT_TEST_SRC = \
    test/core/bad_client/tests/initial_settings_frame.c \

INITIAL_SETTINGS_FRAME_BAD_CLIENT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(INITIAL_SETTINGS_FRAME_BAD_CLIENT_TEST_SRC))))


$(BINDIR)/$(CONFIG)/initial_settings_frame_bad_client_test: $(INITIAL_SETTINGS_FRAME_BAD_CLIENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(INITIAL_SETTINGS_FRAME_BAD_CLIENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/initial_settings_frame_bad_client_test

$(OBJDIR)/$(CONFIG)/test/core/bad_client/tests/initial_settings_frame.o:  $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_initial_settings_frame_bad_client_test: $(INITIAL_SETTINGS_FRAME_BAD_CLIENT_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(INITIAL_SETTINGS_FRAME_BAD_CLIENT_TEST_OBJS:.o=.dep)
endif


LARGE_METADATA_BAD_CLIENT_TEST_SRC = \
    test/core/bad_client/tests/large_metadata.c \

LARGE_METADATA_BAD_CLIENT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LARGE_METADATA_BAD_CLIENT_TEST_SRC))))


$(BINDIR)/$(CONFIG)/large_metadata_bad_client_test: $(LARGE_METADATA_BAD_CLIENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(LARGE_METADATA_BAD_CLIENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/large_metadata_bad_client_test

$(OBJDIR)/$(CONFIG)/test/core/bad_client/tests/large_metadata.o:  $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_large_metadata_bad_client_test: $(LARGE_METADATA_BAD_CLIENT_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(LARGE_METADATA_BAD_CLIENT_TEST_OBJS:.o=.dep)
endif


SERVER_REGISTERED_METHOD_BAD_CLIENT_TEST_SRC = \
    test/core/bad_client/tests/server_registered_method.c \

SERVER_REGISTERED_METHOD_BAD_CLIENT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SERVER_REGISTERED_METHOD_BAD_CLIENT_TEST_SRC))))


$(BINDIR)/$(CONFIG)/server_registered_method_bad_client_test: $(SERVER_REGISTERED_METHOD_BAD_CLIENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(SERVER_REGISTERED_METHOD_BAD_CLIENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/server_registered_method_bad_client_test

$(OBJDIR)/$(CONFIG)/test/core/bad_client/tests/server_registered_method.o:  $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_server_registered_method_bad_client_test: $(SERVER_REGISTERED_METHOD_BAD_CLIENT_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(SERVER_REGISTERED_METHOD_BAD_CLIENT_TEST_OBJS:.o=.dep)
endif


SIMPLE_REQUEST_BAD_CLIENT_TEST_SRC = \
    test/core/bad_client/tests/simple_request.c \

SIMPLE_REQUEST_BAD_CLIENT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SIMPLE_REQUEST_BAD_CLIENT_TEST_SRC))))


$(BINDIR)/$(CONFIG)/simple_request_bad_client_test: $(SIMPLE_REQUEST_BAD_CLIENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(SIMPLE_REQUEST_BAD_CLIENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/simple_request_bad_client_test

$(OBJDIR)/$(CONFIG)/test/core/bad_client/tests/simple_request.o:  $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_simple_request_bad_client_test: $(SIMPLE_REQUEST_BAD_CLIENT_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(SIMPLE_REQUEST_BAD_CLIENT_TEST_OBJS:.o=.dep)
endif


UNKNOWN_FRAME_BAD_CLIENT_TEST_SRC = \
    test/core/bad_client/tests/unknown_frame.c \

UNKNOWN_FRAME_BAD_CLIENT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(UNKNOWN_FRAME_BAD_CLIENT_TEST_SRC))))


$(BINDIR)/$(CONFIG)/unknown_frame_bad_client_test: $(UNKNOWN_FRAME_BAD_CLIENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(UNKNOWN_FRAME_BAD_CLIENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/unknown_frame_bad_client_test

$(OBJDIR)/$(CONFIG)/test/core/bad_client/tests/unknown_frame.o:  $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_unknown_frame_bad_client_test: $(UNKNOWN_FRAME_BAD_CLIENT_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(UNKNOWN_FRAME_BAD_CLIENT_TEST_OBJS:.o=.dep)
endif


WINDOW_OVERFLOW_BAD_CLIENT_TEST_SRC = \
    test/core/bad_client/tests/window_overflow.c \

WINDOW_OVERFLOW_BAD_CLIENT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(WINDOW_OVERFLOW_BAD_CLIENT_TEST_SRC))))


$(BINDIR)/$(CONFIG)/window_overflow_bad_client_test: $(WINDOW_OVERFLOW_BAD_CLIENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(WINDOW_OVERFLOW_BAD_CLIENT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/window_overflow_bad_client_test

$(OBJDIR)/$(CONFIG)/test/core/bad_client/tests/window_overflow.o:  $(LIBDIR)/$(CONFIG)/libbad_client_test.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_window_overflow_bad_client_test: $(WINDOW_OVERFLOW_BAD_CLIENT_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(WINDOW_OVERFLOW_BAD_CLIENT_TEST_OBJS:.o=.dep)
endif


BAD_SSL_CERT_SERVER_SRC = \
    test/core/bad_ssl/servers/cert.c \

BAD_SSL_CERT_SERVER_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(BAD_SSL_CERT_SERVER_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/bad_ssl_cert_server: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/bad_ssl_cert_server: $(BAD_SSL_CERT_SERVER_OBJS) $(LIBDIR)/$(CONFIG)/libbad_ssl_test_server.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(BAD_SSL_CERT_SERVER_OBJS) $(LIBDIR)/$(CONFIG)/libbad_ssl_test_server.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/bad_ssl_cert_server

endif

$(OBJDIR)/$(CONFIG)/test/core/bad_ssl/servers/cert.o:  $(LIBDIR)/$(CONFIG)/libbad_ssl_test_server.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_bad_ssl_cert_server: $(BAD_SSL_CERT_SERVER_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(BAD_SSL_CERT_SERVER_OBJS:.o=.dep)
endif
endif


BAD_SSL_CERT_TEST_SRC = \
    test/core/bad_ssl/bad_ssl_test.c \

BAD_SSL_CERT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(BAD_SSL_CERT_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/bad_ssl_cert_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/bad_ssl_cert_test: $(BAD_SSL_CERT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(BAD_SSL_CERT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/bad_ssl_cert_test

endif

$(OBJDIR)/$(CONFIG)/test/core/bad_ssl/bad_ssl_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_bad_ssl_cert_test: $(BAD_SSL_CERT_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(BAD_SSL_CERT_TEST_OBJS:.o=.dep)
endif
endif


H2_CENSUS_TEST_SRC = \
    test/core/end2end/fixtures/h2_census.c \

H2_CENSUS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_CENSUS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/h2_census_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/h2_census_test: $(H2_CENSUS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_CENSUS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/h2_census_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_census.o:  $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_census_test: $(H2_CENSUS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(H2_CENSUS_TEST_OBJS:.o=.dep)
endif
endif


H2_COMPRESS_TEST_SRC = \
    test/core/end2end/fixtures/h2_compress.c \

H2_COMPRESS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_COMPRESS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/h2_compress_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/h2_compress_test: $(H2_COMPRESS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_COMPRESS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/h2_compress_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_compress.o:  $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_compress_test: $(H2_COMPRESS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(H2_COMPRESS_TEST_OBJS:.o=.dep)
endif
endif


H2_FAKESEC_TEST_SRC = \
    test/core/end2end/fixtures/h2_fakesec.c \

H2_FAKESEC_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_FAKESEC_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/h2_fakesec_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/h2_fakesec_test: $(H2_FAKESEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_FAKESEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/h2_fakesec_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_fakesec.o:  $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_fakesec_test: $(H2_FAKESEC_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(H2_FAKESEC_TEST_OBJS:.o=.dep)
endif
endif


H2_FD_TEST_SRC = \
    test/core/end2end/fixtures/h2_fd.c \

H2_FD_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_FD_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/h2_fd_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/h2_fd_test: $(H2_FD_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_FD_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/h2_fd_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_fd.o:  $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_fd_test: $(H2_FD_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(H2_FD_TEST_OBJS:.o=.dep)
endif
endif


H2_FULL_TEST_SRC = \
    test/core/end2end/fixtures/h2_full.c \

H2_FULL_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_FULL_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/h2_full_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/h2_full_test: $(H2_FULL_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_FULL_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/h2_full_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_full.o:  $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_full_test: $(H2_FULL_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(H2_FULL_TEST_OBJS:.o=.dep)
endif
endif


H2_FULL+PIPE_TEST_SRC = \
    test/core/end2end/fixtures/h2_full+pipe.c \

H2_FULL+PIPE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_FULL+PIPE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/h2_full+pipe_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/h2_full+pipe_test: $(H2_FULL+PIPE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_FULL+PIPE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/h2_full+pipe_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_full+pipe.o:  $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_full+pipe_test: $(H2_FULL+PIPE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(H2_FULL+PIPE_TEST_OBJS:.o=.dep)
endif
endif


H2_FULL+TRACE_TEST_SRC = \
    test/core/end2end/fixtures/h2_full+trace.c \

H2_FULL+TRACE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_FULL+TRACE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/h2_full+trace_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/h2_full+trace_test: $(H2_FULL+TRACE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_FULL+TRACE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/h2_full+trace_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_full+trace.o:  $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_full+trace_test: $(H2_FULL+TRACE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(H2_FULL+TRACE_TEST_OBJS:.o=.dep)
endif
endif


H2_FULL+WORKAROUNDS_TEST_SRC = \
    test/core/end2end/fixtures/h2_full+workarounds.c \

H2_FULL+WORKAROUNDS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_FULL+WORKAROUNDS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/h2_full+workarounds_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/h2_full+workarounds_test: $(H2_FULL+WORKAROUNDS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_FULL+WORKAROUNDS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/h2_full+workarounds_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_full+workarounds.o:  $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_full+workarounds_test: $(H2_FULL+WORKAROUNDS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(H2_FULL+WORKAROUNDS_TEST_OBJS:.o=.dep)
endif
endif


H2_HTTP_PROXY_TEST_SRC = \
    test/core/end2end/fixtures/h2_http_proxy.c \

H2_HTTP_PROXY_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_HTTP_PROXY_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/h2_http_proxy_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/h2_http_proxy_test: $(H2_HTTP_PROXY_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_HTTP_PROXY_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/h2_http_proxy_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_http_proxy.o:  $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_http_proxy_test: $(H2_HTTP_PROXY_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(H2_HTTP_PROXY_TEST_OBJS:.o=.dep)
endif
endif


H2_LOAD_REPORTING_TEST_SRC = \
    test/core/end2end/fixtures/h2_load_reporting.c \

H2_LOAD_REPORTING_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_LOAD_REPORTING_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/h2_load_reporting_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/h2_load_reporting_test: $(H2_LOAD_REPORTING_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_LOAD_REPORTING_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/h2_load_reporting_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_load_reporting.o:  $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_load_reporting_test: $(H2_LOAD_REPORTING_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(H2_LOAD_REPORTING_TEST_OBJS:.o=.dep)
endif
endif


H2_OAUTH2_TEST_SRC = \
    test/core/end2end/fixtures/h2_oauth2.c \

H2_OAUTH2_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_OAUTH2_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/h2_oauth2_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/h2_oauth2_test: $(H2_OAUTH2_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_OAUTH2_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/h2_oauth2_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_oauth2.o:  $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_oauth2_test: $(H2_OAUTH2_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(H2_OAUTH2_TEST_OBJS:.o=.dep)
endif
endif


H2_PROXY_TEST_SRC = \
    test/core/end2end/fixtures/h2_proxy.c \

H2_PROXY_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_PROXY_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/h2_proxy_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/h2_proxy_test: $(H2_PROXY_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_PROXY_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/h2_proxy_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_proxy.o:  $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_proxy_test: $(H2_PROXY_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(H2_PROXY_TEST_OBJS:.o=.dep)
endif
endif


H2_SOCKPAIR_TEST_SRC = \
    test/core/end2end/fixtures/h2_sockpair.c \

H2_SOCKPAIR_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_SOCKPAIR_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/h2_sockpair_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/h2_sockpair_test: $(H2_SOCKPAIR_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_SOCKPAIR_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/h2_sockpair_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_sockpair.o:  $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_sockpair_test: $(H2_SOCKPAIR_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(H2_SOCKPAIR_TEST_OBJS:.o=.dep)
endif
endif


H2_SOCKPAIR+TRACE_TEST_SRC = \
    test/core/end2end/fixtures/h2_sockpair+trace.c \

H2_SOCKPAIR+TRACE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_SOCKPAIR+TRACE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/h2_sockpair+trace_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/h2_sockpair+trace_test: $(H2_SOCKPAIR+TRACE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_SOCKPAIR+TRACE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/h2_sockpair+trace_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_sockpair+trace.o:  $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_sockpair+trace_test: $(H2_SOCKPAIR+TRACE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(H2_SOCKPAIR+TRACE_TEST_OBJS:.o=.dep)
endif
endif


H2_SOCKPAIR_1BYTE_TEST_SRC = \
    test/core/end2end/fixtures/h2_sockpair_1byte.c \

H2_SOCKPAIR_1BYTE_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_SOCKPAIR_1BYTE_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/h2_sockpair_1byte_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/h2_sockpair_1byte_test: $(H2_SOCKPAIR_1BYTE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_SOCKPAIR_1BYTE_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/h2_sockpair_1byte_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_sockpair_1byte.o:  $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_sockpair_1byte_test: $(H2_SOCKPAIR_1BYTE_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(H2_SOCKPAIR_1BYTE_TEST_OBJS:.o=.dep)
endif
endif


H2_SSL_TEST_SRC = \
    test/core/end2end/fixtures/h2_ssl.c \

H2_SSL_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_SSL_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/h2_ssl_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/h2_ssl_test: $(H2_SSL_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_SSL_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/h2_ssl_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_ssl.o:  $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_ssl_test: $(H2_SSL_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(H2_SSL_TEST_OBJS:.o=.dep)
endif
endif


H2_SSL_CERT_TEST_SRC = \
    test/core/end2end/fixtures/h2_ssl_cert.c \

H2_SSL_CERT_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_SSL_CERT_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/h2_ssl_cert_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/h2_ssl_cert_test: $(H2_SSL_CERT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_SSL_CERT_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/h2_ssl_cert_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_ssl_cert.o:  $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_ssl_cert_test: $(H2_SSL_CERT_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(H2_SSL_CERT_TEST_OBJS:.o=.dep)
endif
endif


H2_SSL_PROXY_TEST_SRC = \
    test/core/end2end/fixtures/h2_ssl_proxy.c \

H2_SSL_PROXY_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_SSL_PROXY_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/h2_ssl_proxy_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/h2_ssl_proxy_test: $(H2_SSL_PROXY_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_SSL_PROXY_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/h2_ssl_proxy_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_ssl_proxy.o:  $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_ssl_proxy_test: $(H2_SSL_PROXY_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(H2_SSL_PROXY_TEST_OBJS:.o=.dep)
endif
endif


H2_UDS_TEST_SRC = \
    test/core/end2end/fixtures/h2_uds.c \

H2_UDS_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_UDS_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/h2_uds_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/h2_uds_test: $(H2_UDS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_UDS_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/h2_uds_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_uds.o:  $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_uds_test: $(H2_UDS_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(H2_UDS_TEST_OBJS:.o=.dep)
endif
endif


INPROC_TEST_SRC = \
    test/core/end2end/fixtures/inproc.c \

INPROC_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(INPROC_TEST_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/inproc_test: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/inproc_test: $(INPROC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(INPROC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/inproc_test

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/inproc.o:  $(LIBDIR)/$(CONFIG)/libend2end_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_inproc_test: $(INPROC_TEST_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(INPROC_TEST_OBJS:.o=.dep)
endif
endif


H2_CENSUS_NOSEC_TEST_SRC = \
    test/core/end2end/fixtures/h2_census.c \

H2_CENSUS_NOSEC_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_CENSUS_NOSEC_TEST_SRC))))


$(BINDIR)/$(CONFIG)/h2_census_nosec_test: $(H2_CENSUS_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_CENSUS_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/h2_census_nosec_test

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_census.o:  $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_census_nosec_test: $(H2_CENSUS_NOSEC_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(H2_CENSUS_NOSEC_TEST_OBJS:.o=.dep)
endif


H2_COMPRESS_NOSEC_TEST_SRC = \
    test/core/end2end/fixtures/h2_compress.c \

H2_COMPRESS_NOSEC_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_COMPRESS_NOSEC_TEST_SRC))))


$(BINDIR)/$(CONFIG)/h2_compress_nosec_test: $(H2_COMPRESS_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_COMPRESS_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/h2_compress_nosec_test

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_compress.o:  $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_compress_nosec_test: $(H2_COMPRESS_NOSEC_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(H2_COMPRESS_NOSEC_TEST_OBJS:.o=.dep)
endif


H2_FD_NOSEC_TEST_SRC = \
    test/core/end2end/fixtures/h2_fd.c \

H2_FD_NOSEC_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_FD_NOSEC_TEST_SRC))))


$(BINDIR)/$(CONFIG)/h2_fd_nosec_test: $(H2_FD_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_FD_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/h2_fd_nosec_test

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_fd.o:  $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_fd_nosec_test: $(H2_FD_NOSEC_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(H2_FD_NOSEC_TEST_OBJS:.o=.dep)
endif


H2_FULL_NOSEC_TEST_SRC = \
    test/core/end2end/fixtures/h2_full.c \

H2_FULL_NOSEC_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_FULL_NOSEC_TEST_SRC))))


$(BINDIR)/$(CONFIG)/h2_full_nosec_test: $(H2_FULL_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_FULL_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/h2_full_nosec_test

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_full.o:  $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_full_nosec_test: $(H2_FULL_NOSEC_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(H2_FULL_NOSEC_TEST_OBJS:.o=.dep)
endif


H2_FULL+PIPE_NOSEC_TEST_SRC = \
    test/core/end2end/fixtures/h2_full+pipe.c \

H2_FULL+PIPE_NOSEC_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_FULL+PIPE_NOSEC_TEST_SRC))))


$(BINDIR)/$(CONFIG)/h2_full+pipe_nosec_test: $(H2_FULL+PIPE_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_FULL+PIPE_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/h2_full+pipe_nosec_test

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_full+pipe.o:  $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_full+pipe_nosec_test: $(H2_FULL+PIPE_NOSEC_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(H2_FULL+PIPE_NOSEC_TEST_OBJS:.o=.dep)
endif


H2_FULL+TRACE_NOSEC_TEST_SRC = \
    test/core/end2end/fixtures/h2_full+trace.c \

H2_FULL+TRACE_NOSEC_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_FULL+TRACE_NOSEC_TEST_SRC))))


$(BINDIR)/$(CONFIG)/h2_full+trace_nosec_test: $(H2_FULL+TRACE_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_FULL+TRACE_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/h2_full+trace_nosec_test

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_full+trace.o:  $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_full+trace_nosec_test: $(H2_FULL+TRACE_NOSEC_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(H2_FULL+TRACE_NOSEC_TEST_OBJS:.o=.dep)
endif


H2_FULL+WORKAROUNDS_NOSEC_TEST_SRC = \
    test/core/end2end/fixtures/h2_full+workarounds.c \

H2_FULL+WORKAROUNDS_NOSEC_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_FULL+WORKAROUNDS_NOSEC_TEST_SRC))))


$(BINDIR)/$(CONFIG)/h2_full+workarounds_nosec_test: $(H2_FULL+WORKAROUNDS_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_FULL+WORKAROUNDS_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/h2_full+workarounds_nosec_test

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_full+workarounds.o:  $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_full+workarounds_nosec_test: $(H2_FULL+WORKAROUNDS_NOSEC_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(H2_FULL+WORKAROUNDS_NOSEC_TEST_OBJS:.o=.dep)
endif


H2_HTTP_PROXY_NOSEC_TEST_SRC = \
    test/core/end2end/fixtures/h2_http_proxy.c \

H2_HTTP_PROXY_NOSEC_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_HTTP_PROXY_NOSEC_TEST_SRC))))


$(BINDIR)/$(CONFIG)/h2_http_proxy_nosec_test: $(H2_HTTP_PROXY_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_HTTP_PROXY_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/h2_http_proxy_nosec_test

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_http_proxy.o:  $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_http_proxy_nosec_test: $(H2_HTTP_PROXY_NOSEC_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(H2_HTTP_PROXY_NOSEC_TEST_OBJS:.o=.dep)
endif


H2_LOAD_REPORTING_NOSEC_TEST_SRC = \
    test/core/end2end/fixtures/h2_load_reporting.c \

H2_LOAD_REPORTING_NOSEC_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_LOAD_REPORTING_NOSEC_TEST_SRC))))


$(BINDIR)/$(CONFIG)/h2_load_reporting_nosec_test: $(H2_LOAD_REPORTING_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_LOAD_REPORTING_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/h2_load_reporting_nosec_test

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_load_reporting.o:  $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_load_reporting_nosec_test: $(H2_LOAD_REPORTING_NOSEC_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(H2_LOAD_REPORTING_NOSEC_TEST_OBJS:.o=.dep)
endif


H2_PROXY_NOSEC_TEST_SRC = \
    test/core/end2end/fixtures/h2_proxy.c \

H2_PROXY_NOSEC_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_PROXY_NOSEC_TEST_SRC))))


$(BINDIR)/$(CONFIG)/h2_proxy_nosec_test: $(H2_PROXY_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_PROXY_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/h2_proxy_nosec_test

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_proxy.o:  $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_proxy_nosec_test: $(H2_PROXY_NOSEC_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(H2_PROXY_NOSEC_TEST_OBJS:.o=.dep)
endif


H2_SOCKPAIR_NOSEC_TEST_SRC = \
    test/core/end2end/fixtures/h2_sockpair.c \

H2_SOCKPAIR_NOSEC_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_SOCKPAIR_NOSEC_TEST_SRC))))


$(BINDIR)/$(CONFIG)/h2_sockpair_nosec_test: $(H2_SOCKPAIR_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_SOCKPAIR_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/h2_sockpair_nosec_test

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_sockpair.o:  $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_sockpair_nosec_test: $(H2_SOCKPAIR_NOSEC_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(H2_SOCKPAIR_NOSEC_TEST_OBJS:.o=.dep)
endif


H2_SOCKPAIR+TRACE_NOSEC_TEST_SRC = \
    test/core/end2end/fixtures/h2_sockpair+trace.c \

H2_SOCKPAIR+TRACE_NOSEC_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_SOCKPAIR+TRACE_NOSEC_TEST_SRC))))


$(BINDIR)/$(CONFIG)/h2_sockpair+trace_nosec_test: $(H2_SOCKPAIR+TRACE_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_SOCKPAIR+TRACE_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/h2_sockpair+trace_nosec_test

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_sockpair+trace.o:  $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_sockpair+trace_nosec_test: $(H2_SOCKPAIR+TRACE_NOSEC_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(H2_SOCKPAIR+TRACE_NOSEC_TEST_OBJS:.o=.dep)
endif


H2_SOCKPAIR_1BYTE_NOSEC_TEST_SRC = \
    test/core/end2end/fixtures/h2_sockpair_1byte.c \

H2_SOCKPAIR_1BYTE_NOSEC_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_SOCKPAIR_1BYTE_NOSEC_TEST_SRC))))


$(BINDIR)/$(CONFIG)/h2_sockpair_1byte_nosec_test: $(H2_SOCKPAIR_1BYTE_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_SOCKPAIR_1BYTE_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/h2_sockpair_1byte_nosec_test

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_sockpair_1byte.o:  $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_sockpair_1byte_nosec_test: $(H2_SOCKPAIR_1BYTE_NOSEC_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(H2_SOCKPAIR_1BYTE_NOSEC_TEST_OBJS:.o=.dep)
endif


H2_UDS_NOSEC_TEST_SRC = \
    test/core/end2end/fixtures/h2_uds.c \

H2_UDS_NOSEC_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(H2_UDS_NOSEC_TEST_SRC))))


$(BINDIR)/$(CONFIG)/h2_uds_nosec_test: $(H2_UDS_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(H2_UDS_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/h2_uds_nosec_test

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/h2_uds.o:  $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_h2_uds_nosec_test: $(H2_UDS_NOSEC_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(H2_UDS_NOSEC_TEST_OBJS:.o=.dep)
endif


INPROC_NOSEC_TEST_SRC = \
    test/core/end2end/fixtures/inproc.c \

INPROC_NOSEC_TEST_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(INPROC_NOSEC_TEST_SRC))))


$(BINDIR)/$(CONFIG)/inproc_nosec_test: $(INPROC_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(INPROC_NOSEC_TEST_OBJS) $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) -o $(BINDIR)/$(CONFIG)/inproc_nosec_test

$(OBJDIR)/$(CONFIG)/test/core/end2end/fixtures/inproc.o:  $(LIBDIR)/$(CONFIG)/libend2end_nosec_tests.a $(LIBDIR)/$(CONFIG)/libgrpc_test_util_unsecure.a $(LIBDIR)/$(CONFIG)/libgrpc_unsecure.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_inproc_nosec_test: $(INPROC_NOSEC_TEST_OBJS:.o=.dep)

ifneq ($(NO_DEPS),true)
-include $(INPROC_NOSEC_TEST_OBJS:.o=.dep)
endif


API_FUZZER_ONE_ENTRY_SRC = \
    test/core/end2end/fuzzers/api_fuzzer.c \
    test/core/util/one_corpus_entry_fuzzer.c \

API_FUZZER_ONE_ENTRY_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(API_FUZZER_ONE_ENTRY_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/api_fuzzer_one_entry: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/api_fuzzer_one_entry: $(API_FUZZER_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(API_FUZZER_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/api_fuzzer_one_entry

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fuzzers/api_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/test/core/util/one_corpus_entry_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_api_fuzzer_one_entry: $(API_FUZZER_ONE_ENTRY_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(API_FUZZER_ONE_ENTRY_OBJS:.o=.dep)
endif
endif


CLIENT_FUZZER_ONE_ENTRY_SRC = \
    test/core/end2end/fuzzers/client_fuzzer.c \
    test/core/util/one_corpus_entry_fuzzer.c \

CLIENT_FUZZER_ONE_ENTRY_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(CLIENT_FUZZER_ONE_ENTRY_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/client_fuzzer_one_entry: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/client_fuzzer_one_entry: $(CLIENT_FUZZER_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CLIENT_FUZZER_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/client_fuzzer_one_entry

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fuzzers/client_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/test/core/util/one_corpus_entry_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_client_fuzzer_one_entry: $(CLIENT_FUZZER_ONE_ENTRY_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CLIENT_FUZZER_ONE_ENTRY_OBJS:.o=.dep)
endif
endif


HPACK_PARSER_FUZZER_TEST_ONE_ENTRY_SRC = \
    test/core/transport/chttp2/hpack_parser_fuzzer_test.c \
    test/core/util/one_corpus_entry_fuzzer.c \

HPACK_PARSER_FUZZER_TEST_ONE_ENTRY_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(HPACK_PARSER_FUZZER_TEST_ONE_ENTRY_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/hpack_parser_fuzzer_test_one_entry: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/hpack_parser_fuzzer_test_one_entry: $(HPACK_PARSER_FUZZER_TEST_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(HPACK_PARSER_FUZZER_TEST_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/hpack_parser_fuzzer_test_one_entry

endif

$(OBJDIR)/$(CONFIG)/test/core/transport/chttp2/hpack_parser_fuzzer_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/test/core/util/one_corpus_entry_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_hpack_parser_fuzzer_test_one_entry: $(HPACK_PARSER_FUZZER_TEST_ONE_ENTRY_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(HPACK_PARSER_FUZZER_TEST_ONE_ENTRY_OBJS:.o=.dep)
endif
endif


HTTP_REQUEST_FUZZER_TEST_ONE_ENTRY_SRC = \
    test/core/http/request_fuzzer.c \
    test/core/util/one_corpus_entry_fuzzer.c \

HTTP_REQUEST_FUZZER_TEST_ONE_ENTRY_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(HTTP_REQUEST_FUZZER_TEST_ONE_ENTRY_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/http_request_fuzzer_test_one_entry: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/http_request_fuzzer_test_one_entry: $(HTTP_REQUEST_FUZZER_TEST_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(HTTP_REQUEST_FUZZER_TEST_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/http_request_fuzzer_test_one_entry

endif

$(OBJDIR)/$(CONFIG)/test/core/http/request_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/test/core/util/one_corpus_entry_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_http_request_fuzzer_test_one_entry: $(HTTP_REQUEST_FUZZER_TEST_ONE_ENTRY_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(HTTP_REQUEST_FUZZER_TEST_ONE_ENTRY_OBJS:.o=.dep)
endif
endif


HTTP_RESPONSE_FUZZER_TEST_ONE_ENTRY_SRC = \
    test/core/http/response_fuzzer.c \
    test/core/util/one_corpus_entry_fuzzer.c \

HTTP_RESPONSE_FUZZER_TEST_ONE_ENTRY_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(HTTP_RESPONSE_FUZZER_TEST_ONE_ENTRY_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/http_response_fuzzer_test_one_entry: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/http_response_fuzzer_test_one_entry: $(HTTP_RESPONSE_FUZZER_TEST_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(HTTP_RESPONSE_FUZZER_TEST_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/http_response_fuzzer_test_one_entry

endif

$(OBJDIR)/$(CONFIG)/test/core/http/response_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/test/core/util/one_corpus_entry_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_http_response_fuzzer_test_one_entry: $(HTTP_RESPONSE_FUZZER_TEST_ONE_ENTRY_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(HTTP_RESPONSE_FUZZER_TEST_ONE_ENTRY_OBJS:.o=.dep)
endif
endif


JSON_FUZZER_TEST_ONE_ENTRY_SRC = \
    test/core/json/fuzzer.c \
    test/core/util/one_corpus_entry_fuzzer.c \

JSON_FUZZER_TEST_ONE_ENTRY_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(JSON_FUZZER_TEST_ONE_ENTRY_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/json_fuzzer_test_one_entry: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/json_fuzzer_test_one_entry: $(JSON_FUZZER_TEST_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(JSON_FUZZER_TEST_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/json_fuzzer_test_one_entry

endif

$(OBJDIR)/$(CONFIG)/test/core/json/fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/test/core/util/one_corpus_entry_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_json_fuzzer_test_one_entry: $(JSON_FUZZER_TEST_ONE_ENTRY_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(JSON_FUZZER_TEST_ONE_ENTRY_OBJS:.o=.dep)
endif
endif


NANOPB_FUZZER_RESPONSE_TEST_ONE_ENTRY_SRC = \
    test/core/nanopb/fuzzer_response.c \
    test/core/util/one_corpus_entry_fuzzer.c \

NANOPB_FUZZER_RESPONSE_TEST_ONE_ENTRY_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(NANOPB_FUZZER_RESPONSE_TEST_ONE_ENTRY_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/nanopb_fuzzer_response_test_one_entry: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/nanopb_fuzzer_response_test_one_entry: $(NANOPB_FUZZER_RESPONSE_TEST_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(NANOPB_FUZZER_RESPONSE_TEST_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/nanopb_fuzzer_response_test_one_entry

endif

$(OBJDIR)/$(CONFIG)/test/core/nanopb/fuzzer_response.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/test/core/util/one_corpus_entry_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_nanopb_fuzzer_response_test_one_entry: $(NANOPB_FUZZER_RESPONSE_TEST_ONE_ENTRY_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(NANOPB_FUZZER_RESPONSE_TEST_ONE_ENTRY_OBJS:.o=.dep)
endif
endif


NANOPB_FUZZER_SERVERLIST_TEST_ONE_ENTRY_SRC = \
    test/core/nanopb/fuzzer_serverlist.c \
    test/core/util/one_corpus_entry_fuzzer.c \

NANOPB_FUZZER_SERVERLIST_TEST_ONE_ENTRY_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(NANOPB_FUZZER_SERVERLIST_TEST_ONE_ENTRY_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/nanopb_fuzzer_serverlist_test_one_entry: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/nanopb_fuzzer_serverlist_test_one_entry: $(NANOPB_FUZZER_SERVERLIST_TEST_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(NANOPB_FUZZER_SERVERLIST_TEST_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/nanopb_fuzzer_serverlist_test_one_entry

endif

$(OBJDIR)/$(CONFIG)/test/core/nanopb/fuzzer_serverlist.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/test/core/util/one_corpus_entry_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_nanopb_fuzzer_serverlist_test_one_entry: $(NANOPB_FUZZER_SERVERLIST_TEST_ONE_ENTRY_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(NANOPB_FUZZER_SERVERLIST_TEST_ONE_ENTRY_OBJS:.o=.dep)
endif
endif


PERCENT_DECODE_FUZZER_ONE_ENTRY_SRC = \
    test/core/slice/percent_decode_fuzzer.c \
    test/core/util/one_corpus_entry_fuzzer.c \

PERCENT_DECODE_FUZZER_ONE_ENTRY_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(PERCENT_DECODE_FUZZER_ONE_ENTRY_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/percent_decode_fuzzer_one_entry: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/percent_decode_fuzzer_one_entry: $(PERCENT_DECODE_FUZZER_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(PERCENT_DECODE_FUZZER_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/percent_decode_fuzzer_one_entry

endif

$(OBJDIR)/$(CONFIG)/test/core/slice/percent_decode_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/test/core/util/one_corpus_entry_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_percent_decode_fuzzer_one_entry: $(PERCENT_DECODE_FUZZER_ONE_ENTRY_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(PERCENT_DECODE_FUZZER_ONE_ENTRY_OBJS:.o=.dep)
endif
endif


PERCENT_ENCODE_FUZZER_ONE_ENTRY_SRC = \
    test/core/slice/percent_encode_fuzzer.c \
    test/core/util/one_corpus_entry_fuzzer.c \

PERCENT_ENCODE_FUZZER_ONE_ENTRY_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(PERCENT_ENCODE_FUZZER_ONE_ENTRY_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/percent_encode_fuzzer_one_entry: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/percent_encode_fuzzer_one_entry: $(PERCENT_ENCODE_FUZZER_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(PERCENT_ENCODE_FUZZER_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/percent_encode_fuzzer_one_entry

endif

$(OBJDIR)/$(CONFIG)/test/core/slice/percent_encode_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/test/core/util/one_corpus_entry_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_percent_encode_fuzzer_one_entry: $(PERCENT_ENCODE_FUZZER_ONE_ENTRY_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(PERCENT_ENCODE_FUZZER_ONE_ENTRY_OBJS:.o=.dep)
endif
endif


SERVER_FUZZER_ONE_ENTRY_SRC = \
    test/core/end2end/fuzzers/server_fuzzer.c \
    test/core/util/one_corpus_entry_fuzzer.c \

SERVER_FUZZER_ONE_ENTRY_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SERVER_FUZZER_ONE_ENTRY_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/server_fuzzer_one_entry: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/server_fuzzer_one_entry: $(SERVER_FUZZER_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(SERVER_FUZZER_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/server_fuzzer_one_entry

endif

$(OBJDIR)/$(CONFIG)/test/core/end2end/fuzzers/server_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/test/core/util/one_corpus_entry_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_server_fuzzer_one_entry: $(SERVER_FUZZER_ONE_ENTRY_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SERVER_FUZZER_ONE_ENTRY_OBJS:.o=.dep)
endif
endif


SSL_SERVER_FUZZER_ONE_ENTRY_SRC = \
    test/core/security/ssl_server_fuzzer.c \
    test/core/util/one_corpus_entry_fuzzer.c \

SSL_SERVER_FUZZER_ONE_ENTRY_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(SSL_SERVER_FUZZER_ONE_ENTRY_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/ssl_server_fuzzer_one_entry: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/ssl_server_fuzzer_one_entry: $(SSL_SERVER_FUZZER_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(SSL_SERVER_FUZZER_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/ssl_server_fuzzer_one_entry

endif

$(OBJDIR)/$(CONFIG)/test/core/security/ssl_server_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/test/core/util/one_corpus_entry_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_ssl_server_fuzzer_one_entry: $(SSL_SERVER_FUZZER_ONE_ENTRY_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SSL_SERVER_FUZZER_ONE_ENTRY_OBJS:.o=.dep)
endif
endif


URI_FUZZER_TEST_ONE_ENTRY_SRC = \
    test/core/client_channel/uri_fuzzer_test.c \
    test/core/util/one_corpus_entry_fuzzer.c \

URI_FUZZER_TEST_ONE_ENTRY_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(URI_FUZZER_TEST_ONE_ENTRY_SRC))))
ifeq ($(NO_SECURE),true)

# You can't build secure targets if you don't have OpenSSL.

$(BINDIR)/$(CONFIG)/uri_fuzzer_test_one_entry: openssl_dep_error

else



$(BINDIR)/$(CONFIG)/uri_fuzzer_test_one_entry: $(URI_FUZZER_TEST_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(URI_FUZZER_TEST_ONE_ENTRY_OBJS) $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a $(LDLIBS) $(LDLIBS_SECURE) -o $(BINDIR)/$(CONFIG)/uri_fuzzer_test_one_entry

endif

$(OBJDIR)/$(CONFIG)/test/core/client_channel/uri_fuzzer_test.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

$(OBJDIR)/$(CONFIG)/test/core/util/one_corpus_entry_fuzzer.o:  $(LIBDIR)/$(CONFIG)/libgrpc_test_util.a $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBDIR)/$(CONFIG)/libgpr_test_util.a $(LIBDIR)/$(CONFIG)/libgpr.a

deps_uri_fuzzer_test_one_entry: $(URI_FUZZER_TEST_ONE_ENTRY_OBJS:.o=.dep)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(URI_FUZZER_TEST_ONE_ENTRY_OBJS:.o=.dep)
endif
endif






ifneq ($(OPENSSL_DEP),)
# This is to ensure the embedded OpenSSL is built beforehand, properly
# installing headers to their final destination on the drive. We need this
# otherwise parallel compilation will fail if a source is compiled first.
src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel_secure.c: $(OPENSSL_DEP)
src/core/ext/transport/chttp2/client/secure/secure_channel_create.c: $(OPENSSL_DEP)
src/core/ext/transport/chttp2/server/secure/server_secure_chttp2.c: $(OPENSSL_DEP)
src/core/ext/transport/cronet/client/secure/cronet_channel_create.c: $(OPENSSL_DEP)
src/core/ext/transport/cronet/transport/cronet_api_dummy.c: $(OPENSSL_DEP)
src/core/ext/transport/cronet/transport/cronet_transport.c: $(OPENSSL_DEP)
src/core/lib/http/httpcli_security_connector.c: $(OPENSSL_DEP)
src/core/lib/security/context/security_context.c: $(OPENSSL_DEP)
src/core/lib/security/credentials/composite/composite_credentials.c: $(OPENSSL_DEP)
src/core/lib/security/credentials/credentials.c: $(OPENSSL_DEP)
src/core/lib/security/credentials/credentials_metadata.c: $(OPENSSL_DEP)
src/core/lib/security/credentials/fake/fake_credentials.c: $(OPENSSL_DEP)
src/core/lib/security/credentials/google_default/credentials_generic.c: $(OPENSSL_DEP)
src/core/lib/security/credentials/google_default/google_default_credentials.c: $(OPENSSL_DEP)
src/core/lib/security/credentials/iam/iam_credentials.c: $(OPENSSL_DEP)
src/core/lib/security/credentials/jwt/json_token.c: $(OPENSSL_DEP)
src/core/lib/security/credentials/jwt/jwt_credentials.c: $(OPENSSL_DEP)
src/core/lib/security/credentials/jwt/jwt_verifier.c: $(OPENSSL_DEP)
src/core/lib/security/credentials/oauth2/oauth2_credentials.c: $(OPENSSL_DEP)
src/core/lib/security/credentials/plugin/plugin_credentials.c: $(OPENSSL_DEP)
src/core/lib/security/credentials/ssl/ssl_credentials.c: $(OPENSSL_DEP)
src/core/lib/security/transport/client_auth_filter.c: $(OPENSSL_DEP)
src/core/lib/security/transport/lb_targets_info.c: $(OPENSSL_DEP)
src/core/lib/security/transport/secure_endpoint.c: $(OPENSSL_DEP)
src/core/lib/security/transport/security_connector.c: $(OPENSSL_DEP)
src/core/lib/security/transport/security_handshaker.c: $(OPENSSL_DEP)
src/core/lib/security/transport/server_auth_filter.c: $(OPENSSL_DEP)
src/core/lib/security/transport/tsi_error.c: $(OPENSSL_DEP)
src/core/lib/security/util/json_util.c: $(OPENSSL_DEP)
src/core/lib/surface/init_secure.c: $(OPENSSL_DEP)
src/core/plugin_registry/grpc_cronet_plugin_registry.c: $(OPENSSL_DEP)
src/core/plugin_registry/grpc_plugin_registry.c: $(OPENSSL_DEP)
src/core/tsi/fake_transport_security.c: $(OPENSSL_DEP)
src/core/tsi/gts_transport_security.c: $(OPENSSL_DEP)
src/core/tsi/ssl_transport_security.c: $(OPENSSL_DEP)
src/core/tsi/transport_security.c: $(OPENSSL_DEP)
src/core/tsi/transport_security_adapter.c: $(OPENSSL_DEP)
src/core/tsi/transport_security_grpc.c: $(OPENSSL_DEP)
src/cpp/client/cronet_credentials.cc: $(OPENSSL_DEP)
src/cpp/client/secure_credentials.cc: $(OPENSSL_DEP)
src/cpp/common/auth_property_iterator.cc: $(OPENSSL_DEP)
src/cpp/common/secure_auth_context.cc: $(OPENSSL_DEP)
src/cpp/common/secure_channel_arguments.cc: $(OPENSSL_DEP)
src/cpp/common/secure_create_auth_context.cc: $(OPENSSL_DEP)
src/cpp/ext/proto_server_reflection.cc: $(OPENSSL_DEP)
src/cpp/ext/proto_server_reflection_plugin.cc: $(OPENSSL_DEP)
src/cpp/server/secure_server_credentials.cc: $(OPENSSL_DEP)
src/cpp/util/error_details.cc: $(OPENSSL_DEP)
src/csharp/ext/grpc_csharp_ext.c: $(OPENSSL_DEP)
test/core/bad_client/bad_client.c: $(OPENSSL_DEP)
test/core/bad_ssl/server_common.c: $(OPENSSL_DEP)
test/core/end2end/data/client_certs.c: $(OPENSSL_DEP)
test/core/end2end/data/server1_cert.c: $(OPENSSL_DEP)
test/core/end2end/data/server1_key.c: $(OPENSSL_DEP)
test/core/end2end/data/test_root_cert.c: $(OPENSSL_DEP)
test/core/end2end/end2end_tests.c: $(OPENSSL_DEP)
test/core/end2end/tests/call_creds.c: $(OPENSSL_DEP)
test/core/security/oauth2_utils.c: $(OPENSSL_DEP)
test/core/util/reconnect_server.c: $(OPENSSL_DEP)
test/core/util/test_tcp_server.c: $(OPENSSL_DEP)
test/cpp/end2end/test_service_impl.cc: $(OPENSSL_DEP)
test/cpp/interop/client.cc: $(OPENSSL_DEP)
test/cpp/interop/client_helper.cc: $(OPENSSL_DEP)
test/cpp/interop/http2_client.cc: $(OPENSSL_DEP)
test/cpp/interop/interop_client.cc: $(OPENSSL_DEP)
test/cpp/interop/interop_server.cc: $(OPENSSL_DEP)
test/cpp/interop/interop_server_bootstrap.cc: $(OPENSSL_DEP)
test/cpp/interop/server_helper.cc: $(OPENSSL_DEP)
test/cpp/microbenchmarks/helpers.cc: $(OPENSSL_DEP)
test/cpp/qps/benchmark_config.cc: $(OPENSSL_DEP)
test/cpp/qps/client_async.cc: $(OPENSSL_DEP)
test/cpp/qps/client_sync.cc: $(OPENSSL_DEP)
test/cpp/qps/driver.cc: $(OPENSSL_DEP)
test/cpp/qps/parse_json.cc: $(OPENSSL_DEP)
test/cpp/qps/qps_worker.cc: $(OPENSSL_DEP)
test/cpp/qps/report.cc: $(OPENSSL_DEP)
test/cpp/qps/server_async.cc: $(OPENSSL_DEP)
test/cpp/qps/server_sync.cc: $(OPENSSL_DEP)
test/cpp/qps/usage_timer.cc: $(OPENSSL_DEP)
test/cpp/util/byte_buffer_proto_helper.cc: $(OPENSSL_DEP)
test/cpp/util/cli_call.cc: $(OPENSSL_DEP)
test/cpp/util/cli_credentials.cc: $(OPENSSL_DEP)
test/cpp/util/create_test_channel.cc: $(OPENSSL_DEP)
test/cpp/util/grpc_tool.cc: $(OPENSSL_DEP)
test/cpp/util/proto_file_parser.cc: $(OPENSSL_DEP)
test/cpp/util/proto_reflection_descriptor_database.cc: $(OPENSSL_DEP)
test/cpp/util/service_describer.cc: $(OPENSSL_DEP)
test/cpp/util/string_ref_helper.cc: $(OPENSSL_DEP)
test/cpp/util/subprocess.cc: $(OPENSSL_DEP)
test/cpp/util/test_config_cc.cc: $(OPENSSL_DEP)
test/cpp/util/test_credentials_provider.cc: $(OPENSSL_DEP)
endif

.PHONY: all strip tools dep_error openssl_dep_error openssl_dep_message git_update stop buildtests buildtests_c buildtests_cxx test test_c test_cxx install install_c install_cxx install-headers install-headers_c install-headers_cxx install-shared install-shared_c install-shared_cxx install-static install-static_c install-static_cxx strip strip-shared strip-static strip_c strip-shared_c strip-static_c strip_cxx strip-shared_cxx strip-static_cxx dep_c dep_cxx bins_dep_c bins_dep_cxx clean

.PHONY: printvars
printvars:
	@$(foreach V,$(sort $(.VARIABLES)),                 	  $(if $(filter-out environment% default automatic, 	  $(origin $V)),$(warning $V=$($V) ($(value $V)))))
