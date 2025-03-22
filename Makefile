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

# Configurations (as defined under "configs" section in build_handwritten.yaml)

VALID_CONFIG_asan = 1
REQUIRE_CUSTOM_LIBRARIES_asan = 1
CC_asan = clang
CXX_asan = clang++
LD_asan = clang++
LDXX_asan = clang++
CPPFLAGS_asan = -O0 -fsanitize-coverage=edge,trace-pc-guard -fsanitize=address -fno-omit-frame-pointer -Wno-unused-command-line-argument -DGPR_NO_DIRECT_SYSCALLS
LDFLAGS_asan = -fsanitize=address

VALID_CONFIG_asan-noleaks = 1
REQUIRE_CUSTOM_LIBRARIES_asan-noleaks = 1
CC_asan-noleaks = clang
CXX_asan-noleaks = clang++
LD_asan-noleaks = clang++
LDXX_asan-noleaks = clang++
CPPFLAGS_asan-noleaks = -O0 -fsanitize-coverage=edge,trace-pc-guard -fsanitize=address -fno-omit-frame-pointer -Wno-unused-command-line-argument -DGPR_NO_DIRECT_SYSCALLS
LDFLAGS_asan-noleaks = fsanitize=address

VALID_CONFIG_asan-trace-cmp = 1
REQUIRE_CUSTOM_LIBRARIES_asan-trace-cmp = 1
CC_asan-trace-cmp = clang
CXX_asan-trace-cmp = clang++
LD_asan-trace-cmp = clang++
LDXX_asan-trace-cmp = clang++
CPPFLAGS_asan-trace-cmp = -O0 -fsanitize-coverage=edge,trace-pc-guard -fsanitize-coverage=trace-cmp -fsanitize=address -fno-omit-frame-pointer -Wno-unused-command-line-argument -DGPR_NO_DIRECT_SYSCALLS
LDFLAGS_asan-trace-cmp = -fsanitize=address

VALID_CONFIG_c++-compat = 1
CC_c++-compat = $(DEFAULT_CC)
CXX_c++-compat = $(DEFAULT_CXX)
LD_c++-compat = $(DEFAULT_CC)
LDXX_c++-compat = $(DEFAULT_CXX)
CFLAGS_c++-compat = -Wc++-compat
CPPFLAGS_c++-compat = -O0
DEFINES_c++-compat = _DEBUG DEBUG

VALID_CONFIG_dbg = 1
CC_dbg = $(DEFAULT_CC)
CXX_dbg = $(DEFAULT_CXX)
LD_dbg = $(DEFAULT_CC)
LDXX_dbg = $(DEFAULT_CXX)
CPPFLAGS_dbg = -O0
DEFINES_dbg = _DEBUG DEBUG

VALID_CONFIG_gcov = 1
CC_gcov = gcc
CXX_gcov = g++
LD_gcov = gcc
LDXX_gcov = g++
CPPFLAGS_gcov = -O0 -fprofile-arcs -ftest-coverage -Wno-return-type
LDFLAGS_gcov = -fprofile-arcs -ftest-coverage -rdynamic -lstdc++
DEFINES_gcov = _DEBUG DEBUG GPR_GCOV

VALID_CONFIG_helgrind = 1
CC_helgrind = $(DEFAULT_CC)
CXX_helgrind = $(DEFAULT_CXX)
LD_helgrind = $(DEFAULT_CC)
LDXX_helgrind = $(DEFAULT_CXX)
CPPFLAGS_helgrind = -O0
LDFLAGS_helgrind = -rdynamic
DEFINES_helgrind = _DEBUG DEBUG

VALID_CONFIG_lto = 1
CC_lto = $(DEFAULT_CC)
CXX_lto = $(DEFAULT_CXX)
LD_lto = $(DEFAULT_CC)
LDXX_lto = $(DEFAULT_CXX)
CPPFLAGS_lto = -O2
DEFINES_lto = NDEBUG

VALID_CONFIG_memcheck = 1
CC_memcheck = $(DEFAULT_CC)
CXX_memcheck = $(DEFAULT_CXX)
LD_memcheck = $(DEFAULT_CC)
LDXX_memcheck = $(DEFAULT_CXX)
CPPFLAGS_memcheck = -O0
LDFLAGS_memcheck = -rdynamic
DEFINES_memcheck = _DEBUG DEBUG

VALID_CONFIG_msan = 1
REQUIRE_CUSTOM_LIBRARIES_msan = 1
CC_msan = clang
CXX_msan = clang++
LD_msan = clang++
LDXX_msan = clang++
CPPFLAGS_msan = -O0 -stdlib=libc++ -fsanitize-coverage=edge,trace-pc-guard -fsanitize=memory -fsanitize-memory-track-origins -fsanitize-memory-use-after-dtor -fno-omit-frame-pointer -DGTEST_HAS_TR1_TUPLE=0 -DGTEST_USE_OWN_TR1_TUPLE=1 -Wno-unused-command-line-argument -fPIE -pie -DGPR_NO_DIRECT_SYSCALLS
LDFLAGS_msan = -stdlib=libc++ -fsanitize=memory -DGTEST_HAS_TR1_TUPLE=0 -DGTEST_USE_OWN_TR1_TUPLE=1 -fPIE -pie $(if $(JENKINS_BUILD),-Wl$(comma)-Ttext-segment=0x7e0000000000,)
DEFINES_msan = NDEBUG

VALID_CONFIG_noexcept = 1
CC_noexcept = $(DEFAULT_CC)
CXX_noexcept = $(DEFAULT_CXX)
LD_noexcept = $(DEFAULT_CC)
LDXX_noexcept = $(DEFAULT_CXX)
CXXFLAGS_noexcept = -fno-exceptions
CPPFLAGS_noexcept = -O2 -Wframe-larger-than=16384
DEFINES_noexcept = NDEBUG

VALID_CONFIG_opt = 1
CC_opt = $(DEFAULT_CC)
CXX_opt = $(DEFAULT_CXX)
LD_opt = $(DEFAULT_CC)
LDXX_opt = $(DEFAULT_CXX)
CPPFLAGS_opt = -O2 -Wframe-larger-than=16384
DEFINES_opt = NDEBUG

VALID_CONFIG_tsan = 1
REQUIRE_CUSTOM_LIBRARIES_tsan = 1
CC_tsan = clang
CXX_tsan = clang++
LD_tsan = clang++
LDXX_tsan = clang++
CPPFLAGS_tsan = -O0 -fsanitize=thread -fno-omit-frame-pointer -Wno-unused-command-line-argument -DGPR_NO_DIRECT_SYSCALLS
LDFLAGS_tsan = -fsanitize=thread
DEFINES_tsan = GRPC_TSAN

VALID_CONFIG_ubsan = 1
REQUIRE_CUSTOM_LIBRARIES_ubsan = 1
CC_ubsan = clang
CXX_ubsan = clang++
LD_ubsan = clang++
LDXX_ubsan = clang++
CPPFLAGS_ubsan = -O0 -stdlib=libc++ -fsanitize-coverage=edge,trace-pc-guard -fsanitize=undefined -fno-omit-frame-pointer -Wno-unused-command-line-argument -Wvarargs
LDFLAGS_ubsan = -stdlib=libc++ -fsanitize=undefined,unsigned-integer-overflow
DEFINES_ubsan = NDEBUG GRPC_UBSAN



# General settings.
# You may want to change these depending on your system.

prefix ?= /usr/local

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
ARFLAGS ?= rcs
ifeq ($(SYSTEM),Linux)
ifeq ($(origin AR), default)
AR = ar
endif
STRIP ?= strip --strip-unneeded
else
ifeq ($(SYSTEM),Darwin)
ifeq ($(origin AR), default)
AR = libtool
ARFLAGS = -no_warning_for_no_symbols -o
endif
STRIP ?= strip -x
else
ifeq ($(SYSTEM),MINGW32)
ifeq ($(origin AR), default)
AR = ar
endif
STRIP ?= strip --strip-unneeded
else
ifeq ($(origin AR), default)
AR = ar
endif
STRIP ?= strip
endif
endif
endif
INSTALL ?= install
RM ?= rm -f
PKG_CONFIG ?= pkg-config
RANLIB ?= ranlib
ifeq ($(SYSTEM),Darwin)
APPLE_RANLIB = $(shell [[ "`$(RANLIB) -V 2>/dev/null`" == "Apple Inc."* ]]; echo $$?)
ifeq ($(APPLE_RANLIB),0)
RANLIBFLAGS = -no_warning_for_no_symbols
endif
endif

ifndef VALID_CONFIG_$(CONFIG)
$(error Invalid CONFIG value '$(CONFIG)')
endif

# The HOST compiler settings are used to compile the protoc plugins.
# In most cases, you won't have to change anything, but if you are
# cross-compiling, you can override these variables from GNU make's
# command line: make CC=cross-gcc HOST_CC=gcc

HOST_CC ?= $(CC)
HOST_CXX ?= $(CXX)
HOST_LD ?= $(LD)
HOST_LDXX ?= $(LDXX)

CFLAGS += -std=c11
CXXFLAGS += -std=c++17
ifeq ($(SYSTEM),Darwin)
CXXFLAGS += -stdlib=libc++
LDFLAGS += -framework CoreFoundation
endif
CFLAGS += -g
CPPFLAGS += -g -Wall -Wextra -DOSATOMIC_USE_INLINED=1 -Ithird_party/abseil-cpp -Ithird_party/re2 -Ithird_party/upb -Isrc/core/ext/upb-gen -Isrc/core/ext/upbdefs-gen -Ithird_party/utf8_range -Ithird_party/xxhash -Ithird_party/cares/cares/include -Ithird_party/cares -Ithird_party/cares/cares -Ithird_party/address_sorting/include
COREFLAGS += -fno-exceptions
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
LIBS = m pthread ws2_32 crypt32 iphlpapi dbghelp bcrypt
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
# Make sure that you enable building shared libraries and set your prefix to
# something useful like /usr/local/cross
# You will also need to set GRPC_CROSS_LDOPTS and GRPC_CROSS_AROPTS to hold
# additional required arguments for LD and AR (examples below)
# Then you can do a make from the cross-compiling fresh clone!
#
ifeq ($(GRPC_CROSS_COMPILE),true)
LDFLAGS += $(GRPC_CROSS_LDOPTS) # e.g. -L/usr/local/lib -L/usr/local/cross/lib
ARFLAGS += $(GRPC_CROSS_AROPTS) # e.g., rc --target=elf32-little
USE_BUILT_PROTOC = false
endif

# V=1 can be used to print commands run by make
ifeq ($(V),1)
E = @:
Q =
else
E = @echo
Q = @
endif

CORE_VERSION = 46.0.0
CPP_VERSION = 1.72.0-dev

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

HOST_CPPFLAGS += $(CPPFLAGS)
HOST_CFLAGS += $(CFLAGS)
HOST_CXXFLAGS += $(CXXFLAGS)
HOST_LDFLAGS += $(LDFLAGS)
HOST_LDLIBS += $(LDLIBS)

# These are automatically computed variables.
# There shouldn't be any need to change anything from now on.

-include cache.mk

CACHE_MK =

ifeq ($(SYSTEM),MINGW32)
EXECUTABLE_SUFFIX = .exe
SHARED_EXT_CORE = dll
SHARED_EXT_CPP = dll

SHARED_PREFIX =
SHARED_VERSION_CORE = -46
SHARED_VERSION_CPP = -1
else ifeq ($(SYSTEM),Darwin)
EXECUTABLE_SUFFIX =
SHARED_EXT_CORE = dylib
SHARED_EXT_CPP = dylib
SHARED_PREFIX = lib
SHARED_VERSION_CORE =
SHARED_VERSION_CPP =
else
EXECUTABLE_SUFFIX =
SHARED_EXT_CORE = so.$(CORE_VERSION)
SHARED_EXT_CPP = so.$(CPP_VERSION)
SHARED_PREFIX = lib
SHARED_VERSION_CORE =
SHARED_VERSION_CPP =
endif

ifeq ($(wildcard .git),)
IS_GIT_FOLDER = false
else
IS_GIT_FOLDER = true
endif

# Setup zlib dependency

ifeq ($(wildcard third_party/zlib/zlib.h),)
HAS_EMBEDDED_ZLIB = false
else
HAS_EMBEDDED_ZLIB = true
endif

# for zlib, we support building both from submodule
# and from system-installed zlib. In some builds,
# embedding zlib is not desirable.
# By default we use the system zlib (to match legacy behavior)
EMBED_ZLIB ?= false

ifeq ($(EMBED_ZLIB),true)
ZLIB_DEP = $(LIBDIR)/$(CONFIG)/libz.a
ZLIB_MERGE_LIBS = $(LIBDIR)/$(CONFIG)/libz.a
ZLIB_MERGE_OBJS = $(LIBZ_OBJS)
CPPFLAGS += -Ithird_party/zlib
else
LIBS += z
endif

# Setup boringssl dependency

ifeq ($(wildcard third_party/boringssl-with-bazel/src/include/openssl/ssl.h),)
HAS_EMBEDDED_OPENSSL = false
else
HAS_EMBEDDED_OPENSSL = true
endif

ifeq ($(HAS_EMBEDDED_OPENSSL),true)
EMBED_OPENSSL ?= true
else
# only support building boringssl from submodule
DEP_MISSING += openssl
EMBED_OPENSSL ?= broken
endif

ifeq ($(EMBED_OPENSSL),true)
OPENSSL_DEP += $(LIBDIR)/$(CONFIG)/libboringssl.a
OPENSSL_MERGE_LIBS += $(LIBDIR)/$(CONFIG)/libboringssl.a
OPENSSL_MERGE_OBJS += $(LIBBORINGSSL_OBJS)
# need to prefix these to ensure overriding system libraries
CPPFLAGS := -Ithird_party/boringssl-with-bazel/src/include $(CPPFLAGS)  
ifeq ($(DISABLE_ALPN),true)
CPPFLAGS += -DTSI_OPENSSL_ALPN_SUPPORT=0
LIBS_SECURE = $(OPENSSL_LIBS)
endif # DISABLE_ALPN
endif # EMBED_OPENSSL

LDLIBS_SECURE += $(addprefix -l, $(LIBS_SECURE))

ifeq ($(MAKECMDGOALS),clean)
NO_DEPS = true
endif

ifeq ($(DEP_MISSING),)
all: static shared

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

openssl_dep_message:
	@echo
	@echo "DEPENDENCY ERROR"
	@echo
	@echo "The target you are trying to run requires an OpenSSL implementation."
	@echo "Your system doesn't have one, and either the third_party directory"
	@echo "doesn't have it, or your compiler can't build BoringSSL."
	@echo
	@echo "Please consult BUILDING.md to get more information."
	@echo
	@echo "If you need information about why these tests failed, run:"
	@echo
	@echo "  make run_dep_checks"
	@echo

systemtap_dep_error:
	@echo
	@echo "DEPENDENCY ERROR"
	@echo
	@echo "Under the '$(CONFIG)' configuration, the target you are trying "
	@echo "to build requires systemtap 2.7+ (on Linux) or dtrace (on other "
	@echo "platforms such as Solaris and *BSD). "
	@echo
	@echo "Please consult BUILDING.md to get more information."
	@echo

install_not_supported_message:
	@echo
	@echo "Installing via 'make' is no longer supported. Use cmake or bazel instead."
	@echo
	@echo "Please consult BUILDING.md to get more information."
	@echo

install_not_supported_error: install_not_supported_message stop

stop:
	@false

run_dep_checks:
	@echo "run_dep_checks target has been deprecated."

static: static_c static_cxx

static_c: cache.mk  $(LIBDIR)/$(CONFIG)/libgrpc.a

static_cxx: cache.mk 

shared: shared_c shared_cxx

shared_c: cache.mk $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)
shared_cxx: cache.mk

privatelibs: privatelibs_c privatelibs_cxx

privatelibs_c:  $(LIBDIR)/$(CONFIG)/libcares.a $(LIBDIR)/$(CONFIG)/libz.a
ifeq ($(EMBED_OPENSSL),true)
privatelibs_cxx: 
else
privatelibs_cxx: 
endif


strip: strip-static strip-shared

strip-static: strip-static_c strip-static_cxx

strip-shared: strip-shared_c strip-shared_cxx

strip-static_c: static_c
ifeq ($(CONFIG),opt)
	$(E) "[STRIP]   Stripping libgrpc.a"
	$(Q) $(STRIP) $(LIBDIR)/$(CONFIG)/libgrpc.a
endif

strip-static_cxx: static_cxx
ifeq ($(CONFIG),opt)
endif

strip-shared_c: shared_c
ifeq ($(CONFIG),opt)
	$(E) "[STRIP]   Stripping $(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)"
	$(Q) $(STRIP) $(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE)
endif

strip-shared_cxx: shared_cxx
ifeq ($(CONFIG),opt)
endif

cache.mk::
	$(E) "[MAKE]    Generating $@"
	$(Q) echo "$(CACHE_MK)" | tr , '\n' >$@

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

$(OBJDIR)/$(CONFIG)/src/core/%.o : src/core/%.cc
	$(E) "[CXX]     Compiling $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(CXX) $(CPPFLAGS) $(CXXFLAGS) $(COREFLAGS) -MMD -MF $(addsuffix .dep, $(basename $@)) -c -o $@ $<

$(OBJDIR)/$(CONFIG)/%.o : %.cc
	$(E) "[CXX]     Compiling $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MF $(addsuffix .dep, $(basename $@)) -c -o $@ $<

$(OBJDIR)/$(CONFIG)/%.o : %.cpp
	$(E) "[CXX]     Compiling $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MF $(addsuffix .dep, $(basename $@)) -c -o $@ $<

install: install_not_supported_error

install_c: install_not_supported_error

install_cxx: install_not_supported_error

install-static: install_not_supported_error

install-certs: install_not_supported_error

clean:
	$(E) "[CLEAN]   Cleaning build directories."
	$(Q) $(RM) -rf $(OBJDIR) $(LIBDIR) $(BINDIR) $(GENDIR) cache.mk


# The various libraries


# start of build recipe for library "grpc" (generated by makelib(lib) template function)
# deps: ['cares', 'libssl', 'z']
# transitive_deps: ['cares', 'libssl', 'z']
LIBGRPC_SRC = \
    src/core/call/call_arena_allocator.cc \
    src/core/call/call_filters.cc \
    src/core/call/call_spine.cc \
    src/core/call/call_state.cc \
    src/core/call/client_call.cc \
    src/core/call/interception_chain.cc \
    src/core/call/message.cc \
    src/core/call/metadata.cc \
    src/core/call/metadata_batch.cc \
    src/core/call/metadata_info.cc \
    src/core/call/parsed_metadata.cc \
    src/core/call/request_buffer.cc \
    src/core/call/security_context.cc \
    src/core/call/server_call.cc \
    src/core/call/status_util.cc \
    src/core/channelz/channel_trace.cc \
    src/core/channelz/channelz.cc \
    src/core/channelz/channelz_registry.cc \
    src/core/client_channel/backup_poller.cc \
    src/core/client_channel/client_channel.cc \
    src/core/client_channel/client_channel_factory.cc \
    src/core/client_channel/client_channel_filter.cc \
    src/core/client_channel/client_channel_plugin.cc \
    src/core/client_channel/client_channel_service_config.cc \
    src/core/client_channel/direct_channel.cc \
    src/core/client_channel/dynamic_filters.cc \
    src/core/client_channel/global_subchannel_pool.cc \
    src/core/client_channel/lb_metadata.cc \
    src/core/client_channel/load_balanced_call_destination.cc \
    src/core/client_channel/local_subchannel_pool.cc \
    src/core/client_channel/retry_filter.cc \
    src/core/client_channel/retry_filter_legacy_call_data.cc \
    src/core/client_channel/retry_interceptor.cc \
    src/core/client_channel/retry_service_config.cc \
    src/core/client_channel/retry_throttle.cc \
    src/core/client_channel/subchannel.cc \
    src/core/client_channel/subchannel_pool_interface.cc \
    src/core/client_channel/subchannel_stream_client.cc \
    src/core/config/config_vars.cc \
    src/core/config/config_vars_non_generated.cc \
    src/core/config/core_configuration.cc \
    src/core/config/load_config.cc \
    src/core/credentials/call/call_creds_util.cc \
    src/core/credentials/call/composite/composite_call_credentials.cc \
    src/core/credentials/call/external/aws_external_account_credentials.cc \
    src/core/credentials/call/external/aws_request_signer.cc \
    src/core/credentials/call/external/external_account_credentials.cc \
    src/core/credentials/call/external/file_external_account_credentials.cc \
    src/core/credentials/call/external/url_external_account_credentials.cc \
    src/core/credentials/call/gcp_service_account_identity/gcp_service_account_identity_credentials.cc \
    src/core/credentials/call/iam/iam_credentials.cc \
    src/core/credentials/call/json_util.cc \
    src/core/credentials/call/jwt/json_token.cc \
    src/core/credentials/call/jwt/jwt_credentials.cc \
    src/core/credentials/call/jwt/jwt_verifier.cc \
    src/core/credentials/call/oauth2/oauth2_credentials.cc \
    src/core/credentials/call/plugin/plugin_credentials.cc \
    src/core/credentials/call/token_fetcher/token_fetcher_credentials.cc \
    src/core/credentials/transport/alts/alts_credentials.cc \
    src/core/credentials/transport/alts/alts_security_connector.cc \
    src/core/credentials/transport/alts/check_gcp_environment.cc \
    src/core/credentials/transport/alts/check_gcp_environment_linux.cc \
    src/core/credentials/transport/alts/check_gcp_environment_no_op.cc \
    src/core/credentials/transport/alts/check_gcp_environment_windows.cc \
    src/core/credentials/transport/alts/grpc_alts_credentials_client_options.cc \
    src/core/credentials/transport/alts/grpc_alts_credentials_options.cc \
    src/core/credentials/transport/alts/grpc_alts_credentials_server_options.cc \
    src/core/credentials/transport/channel_creds_registry_init.cc \
    src/core/credentials/transport/composite/composite_channel_credentials.cc \
    src/core/credentials/transport/fake/fake_credentials.cc \
    src/core/credentials/transport/fake/fake_security_connector.cc \
    src/core/credentials/transport/google_default/credentials_generic.cc \
    src/core/credentials/transport/google_default/google_default_credentials.cc \
    src/core/credentials/transport/insecure/insecure_credentials.cc \
    src/core/credentials/transport/insecure/insecure_security_connector.cc \
    src/core/credentials/transport/local/local_credentials.cc \
    src/core/credentials/transport/local/local_security_connector.cc \
    src/core/credentials/transport/security_connector.cc \
    src/core/credentials/transport/ssl/ssl_credentials.cc \
    src/core/credentials/transport/ssl/ssl_security_connector.cc \
    src/core/credentials/transport/tls/certificate_provider_registry.cc \
    src/core/credentials/transport/tls/grpc_tls_certificate_distributor.cc \
    src/core/credentials/transport/tls/grpc_tls_certificate_match.cc \
    src/core/credentials/transport/tls/grpc_tls_certificate_provider.cc \
    src/core/credentials/transport/tls/grpc_tls_certificate_verifier.cc \
    src/core/credentials/transport/tls/grpc_tls_credentials_options.cc \
    src/core/credentials/transport/tls/grpc_tls_crl_provider.cc \
    src/core/credentials/transport/tls/load_system_roots_fallback.cc \
    src/core/credentials/transport/tls/load_system_roots_supported.cc \
    src/core/credentials/transport/tls/load_system_roots_windows.cc \
    src/core/credentials/transport/tls/ssl_utils.cc \
    src/core/credentials/transport/tls/tls_credentials.cc \
    src/core/credentials/transport/tls/tls_security_connector.cc \
    src/core/credentials/transport/tls/tls_utils.cc \
    src/core/credentials/transport/transport_credentials.cc \
    src/core/credentials/transport/xds/xds_credentials.cc \
    src/core/ext/filters/backend_metrics/backend_metric_filter.cc \
    src/core/ext/filters/census/grpc_context.cc \
    src/core/ext/filters/channel_idle/idle_filter_state.cc \
    src/core/ext/filters/channel_idle/legacy_channel_idle_filter.cc \
    src/core/ext/filters/fault_injection/fault_injection_filter.cc \
    src/core/ext/filters/fault_injection/fault_injection_service_config_parser.cc \
    src/core/ext/filters/gcp_authentication/gcp_authentication_filter.cc \
    src/core/ext/filters/gcp_authentication/gcp_authentication_service_config_parser.cc \
    src/core/ext/filters/http/client/http_client_filter.cc \
    src/core/ext/filters/http/client_authority_filter.cc \
    src/core/ext/filters/http/http_filters_plugin.cc \
    src/core/ext/filters/http/message_compress/compression_filter.cc \
    src/core/ext/filters/http/server/http_server_filter.cc \
    src/core/ext/filters/message_size/message_size_filter.cc \
    src/core/ext/filters/rbac/rbac_filter.cc \
    src/core/ext/filters/rbac/rbac_service_config_parser.cc \
    src/core/ext/filters/stateful_session/stateful_session_filter.cc \
    src/core/ext/filters/stateful_session/stateful_session_service_config_parser.cc \
    src/core/ext/transport/chttp2/alpn/alpn.cc \
    src/core/ext/transport/chttp2/client/chttp2_connector.cc \
    src/core/ext/transport/chttp2/server/chttp2_server.cc \
    src/core/ext/transport/chttp2/transport/bin_decoder.cc \
    src/core/ext/transport/chttp2/transport/bin_encoder.cc \
    src/core/ext/transport/chttp2/transport/call_tracer_wrapper.cc \
    src/core/ext/transport/chttp2/transport/chttp2_transport.cc \
    src/core/ext/transport/chttp2/transport/decode_huff.cc \
    src/core/ext/transport/chttp2/transport/flow_control.cc \
    src/core/ext/transport/chttp2/transport/frame.cc \
    src/core/ext/transport/chttp2/transport/frame_data.cc \
    src/core/ext/transport/chttp2/transport/frame_goaway.cc \
    src/core/ext/transport/chttp2/transport/frame_ping.cc \
    src/core/ext/transport/chttp2/transport/frame_rst_stream.cc \
    src/core/ext/transport/chttp2/transport/frame_security.cc \
    src/core/ext/transport/chttp2/transport/frame_settings.cc \
    src/core/ext/transport/chttp2/transport/frame_window_update.cc \
    src/core/ext/transport/chttp2/transport/hpack_encoder.cc \
    src/core/ext/transport/chttp2/transport/hpack_encoder_table.cc \
    src/core/ext/transport/chttp2/transport/hpack_parse_result.cc \
    src/core/ext/transport/chttp2/transport/hpack_parser.cc \
    src/core/ext/transport/chttp2/transport/hpack_parser_table.cc \
    src/core/ext/transport/chttp2/transport/http2_settings.cc \
    src/core/ext/transport/chttp2/transport/huffsyms.cc \
    src/core/ext/transport/chttp2/transport/parsing.cc \
    src/core/ext/transport/chttp2/transport/ping_abuse_policy.cc \
    src/core/ext/transport/chttp2/transport/ping_callbacks.cc \
    src/core/ext/transport/chttp2/transport/ping_rate_policy.cc \
    src/core/ext/transport/chttp2/transport/stream_lists.cc \
    src/core/ext/transport/chttp2/transport/varint.cc \
    src/core/ext/transport/chttp2/transport/write_size_policy.cc \
    src/core/ext/transport/chttp2/transport/writing.cc \
    src/core/ext/transport/inproc/inproc_transport.cc \
    src/core/ext/transport/inproc/legacy_inproc_transport.cc \
    src/core/ext/upb-gen/envoy/admin/v3/certs.upb_minitable.c \
    src/core/ext/upb-gen/envoy/admin/v3/clusters.upb_minitable.c \
    src/core/ext/upb-gen/envoy/admin/v3/config_dump.upb_minitable.c \
    src/core/ext/upb-gen/envoy/admin/v3/config_dump_shared.upb_minitable.c \
    src/core/ext/upb-gen/envoy/admin/v3/init_dump.upb_minitable.c \
    src/core/ext/upb-gen/envoy/admin/v3/listeners.upb_minitable.c \
    src/core/ext/upb-gen/envoy/admin/v3/memory.upb_minitable.c \
    src/core/ext/upb-gen/envoy/admin/v3/metrics.upb_minitable.c \
    src/core/ext/upb-gen/envoy/admin/v3/mutex_stats.upb_minitable.c \
    src/core/ext/upb-gen/envoy/admin/v3/server_info.upb_minitable.c \
    src/core/ext/upb-gen/envoy/admin/v3/tap.upb_minitable.c \
    src/core/ext/upb-gen/envoy/annotations/deprecation.upb_minitable.c \
    src/core/ext/upb-gen/envoy/annotations/resource.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/accesslog/v3/accesslog.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/bootstrap/v3/bootstrap.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/cluster/v3/circuit_breaker.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/cluster/v3/cluster.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/cluster/v3/filter.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/cluster/v3/outlier_detection.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/common/matcher/v3/matcher.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/core/v3/address.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/core/v3/backoff.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/core/v3/base.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/core/v3/config_source.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/core/v3/event_service_config.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/core/v3/extension.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/core/v3/grpc_method_list.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/core/v3/grpc_service.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/core/v3/health_check.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/core/v3/http_service.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/core/v3/http_uri.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/core/v3/protocol.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/core/v3/proxy_protocol.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/core/v3/resolver.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/core/v3/socket_cmsg_headers.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/core/v3/socket_option.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/core/v3/substitution_format_string.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/core/v3/udp_socket_config.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/endpoint/v3/endpoint.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/endpoint/v3/endpoint_components.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/endpoint/v3/load_report.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/listener/v3/api_listener.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/listener/v3/listener.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/listener/v3/listener_components.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/listener/v3/quic_config.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/listener/v3/udp_listener_config.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/metrics/v3/metrics_service.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/metrics/v3/stats.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/overload/v3/overload.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/rbac/v3/rbac.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/route/v3/route.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/route/v3/route_components.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/route/v3/scoped_route.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/tap/v3/common.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/trace/v3/datadog.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/trace/v3/dynamic_ot.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/trace/v3/http_tracer.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/trace/v3/lightstep.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/trace/v3/opentelemetry.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/trace/v3/service.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/trace/v3/skywalking.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/trace/v3/trace.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/trace/v3/xray.upb_minitable.c \
    src/core/ext/upb-gen/envoy/config/trace/v3/zipkin.upb_minitable.c \
    src/core/ext/upb-gen/envoy/data/accesslog/v3/accesslog.upb_minitable.c \
    src/core/ext/upb-gen/envoy/extensions/clusters/aggregate/v3/cluster.upb_minitable.c \
    src/core/ext/upb-gen/envoy/extensions/filters/common/fault/v3/fault.upb_minitable.c \
    src/core/ext/upb-gen/envoy/extensions/filters/http/fault/v3/fault.upb_minitable.c \
    src/core/ext/upb-gen/envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.upb_minitable.c \
    src/core/ext/upb-gen/envoy/extensions/filters/http/rbac/v3/rbac.upb_minitable.c \
    src/core/ext/upb-gen/envoy/extensions/filters/http/router/v3/router.upb_minitable.c \
    src/core/ext/upb-gen/envoy/extensions/filters/http/stateful_session/v3/stateful_session.upb_minitable.c \
    src/core/ext/upb-gen/envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.upb_minitable.c \
    src/core/ext/upb-gen/envoy/extensions/http/stateful_session/cookie/v3/cookie.upb_minitable.c \
    src/core/ext/upb-gen/envoy/extensions/load_balancing_policies/client_side_weighted_round_robin/v3/client_side_weighted_round_robin.upb_minitable.c \
    src/core/ext/upb-gen/envoy/extensions/load_balancing_policies/common/v3/common.upb_minitable.c \
    src/core/ext/upb-gen/envoy/extensions/load_balancing_policies/pick_first/v3/pick_first.upb_minitable.c \
    src/core/ext/upb-gen/envoy/extensions/load_balancing_policies/ring_hash/v3/ring_hash.upb_minitable.c \
    src/core/ext/upb-gen/envoy/extensions/load_balancing_policies/wrr_locality/v3/wrr_locality.upb_minitable.c \
    src/core/ext/upb-gen/envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect.upb_minitable.c \
    src/core/ext/upb-gen/envoy/extensions/transport_sockets/tls/v3/cert.upb_minitable.c \
    src/core/ext/upb-gen/envoy/extensions/transport_sockets/tls/v3/common.upb_minitable.c \
    src/core/ext/upb-gen/envoy/extensions/transport_sockets/tls/v3/secret.upb_minitable.c \
    src/core/ext/upb-gen/envoy/extensions/transport_sockets/tls/v3/tls.upb_minitable.c \
    src/core/ext/upb-gen/envoy/extensions/transport_sockets/tls/v3/tls_spiffe_validator_config.upb_minitable.c \
    src/core/ext/upb-gen/envoy/extensions/upstreams/http/v3/http_protocol_options.upb_minitable.c \
    src/core/ext/upb-gen/envoy/service/discovery/v3/ads.upb_minitable.c \
    src/core/ext/upb-gen/envoy/service/discovery/v3/discovery.upb_minitable.c \
    src/core/ext/upb-gen/envoy/service/load_stats/v3/lrs.upb_minitable.c \
    src/core/ext/upb-gen/envoy/service/status/v3/csds.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/http/v3/cookie.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/http/v3/path_transformation.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/matcher/v3/address.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/matcher/v3/filter_state.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/matcher/v3/http_inputs.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/matcher/v3/metadata.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/matcher/v3/node.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/matcher/v3/number.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/matcher/v3/path.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/matcher/v3/regex.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/matcher/v3/status_code_input.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/matcher/v3/string.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/matcher/v3/struct.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/matcher/v3/value.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/metadata/v3/metadata.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/tracing/v3/custom_tag.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/v3/hash_policy.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/v3/http.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/v3/http_status.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/v3/percent.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/v3/range.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/v3/ratelimit_strategy.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/v3/ratelimit_unit.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/v3/semantic_version.upb_minitable.c \
    src/core/ext/upb-gen/envoy/type/v3/token_bucket.upb_minitable.c \
    src/core/ext/upb-gen/google/api/annotations.upb_minitable.c \
    src/core/ext/upb-gen/google/api/expr/v1alpha1/checked.upb_minitable.c \
    src/core/ext/upb-gen/google/api/expr/v1alpha1/syntax.upb_minitable.c \
    src/core/ext/upb-gen/google/api/http.upb_minitable.c \
    src/core/ext/upb-gen/google/api/httpbody.upb_minitable.c \
    src/core/ext/upb-gen/google/protobuf/any.upb_minitable.c \
    src/core/ext/upb-gen/google/protobuf/descriptor.upb_minitable.c \
    src/core/ext/upb-gen/google/protobuf/duration.upb_minitable.c \
    src/core/ext/upb-gen/google/protobuf/empty.upb_minitable.c \
    src/core/ext/upb-gen/google/protobuf/struct.upb_minitable.c \
    src/core/ext/upb-gen/google/protobuf/timestamp.upb_minitable.c \
    src/core/ext/upb-gen/google/protobuf/wrappers.upb_minitable.c \
    src/core/ext/upb-gen/google/rpc/status.upb_minitable.c \
    src/core/ext/upb-gen/src/proto/grpc/gcp/altscontext.upb_minitable.c \
    src/core/ext/upb-gen/src/proto/grpc/gcp/handshaker.upb_minitable.c \
    src/core/ext/upb-gen/src/proto/grpc/gcp/transport_security_common.upb_minitable.c \
    src/core/ext/upb-gen/src/proto/grpc/health/v1/health.upb_minitable.c \
    src/core/ext/upb-gen/src/proto/grpc/lb/v1/load_balancer.upb_minitable.c \
    src/core/ext/upb-gen/src/proto/grpc/lookup/v1/rls.upb_minitable.c \
    src/core/ext/upb-gen/src/proto/grpc/lookup/v1/rls_config.upb_minitable.c \
    src/core/ext/upb-gen/udpa/annotations/migrate.upb_minitable.c \
    src/core/ext/upb-gen/udpa/annotations/security.upb_minitable.c \
    src/core/ext/upb-gen/udpa/annotations/sensitive.upb_minitable.c \
    src/core/ext/upb-gen/udpa/annotations/status.upb_minitable.c \
    src/core/ext/upb-gen/udpa/annotations/versioning.upb_minitable.c \
    src/core/ext/upb-gen/validate/validate.upb_minitable.c \
    src/core/ext/upb-gen/xds/annotations/v3/migrate.upb_minitable.c \
    src/core/ext/upb-gen/xds/annotations/v3/security.upb_minitable.c \
    src/core/ext/upb-gen/xds/annotations/v3/sensitive.upb_minitable.c \
    src/core/ext/upb-gen/xds/annotations/v3/status.upb_minitable.c \
    src/core/ext/upb-gen/xds/annotations/v3/versioning.upb_minitable.c \
    src/core/ext/upb-gen/xds/core/v3/authority.upb_minitable.c \
    src/core/ext/upb-gen/xds/core/v3/cidr.upb_minitable.c \
    src/core/ext/upb-gen/xds/core/v3/collection_entry.upb_minitable.c \
    src/core/ext/upb-gen/xds/core/v3/context_params.upb_minitable.c \
    src/core/ext/upb-gen/xds/core/v3/extension.upb_minitable.c \
    src/core/ext/upb-gen/xds/core/v3/resource.upb_minitable.c \
    src/core/ext/upb-gen/xds/core/v3/resource_locator.upb_minitable.c \
    src/core/ext/upb-gen/xds/core/v3/resource_name.upb_minitable.c \
    src/core/ext/upb-gen/xds/data/orca/v3/orca_load_report.upb_minitable.c \
    src/core/ext/upb-gen/xds/service/orca/v3/orca.upb_minitable.c \
    src/core/ext/upb-gen/xds/type/matcher/v3/cel.upb_minitable.c \
    src/core/ext/upb-gen/xds/type/matcher/v3/domain.upb_minitable.c \
    src/core/ext/upb-gen/xds/type/matcher/v3/http_inputs.upb_minitable.c \
    src/core/ext/upb-gen/xds/type/matcher/v3/ip.upb_minitable.c \
    src/core/ext/upb-gen/xds/type/matcher/v3/matcher.upb_minitable.c \
    src/core/ext/upb-gen/xds/type/matcher/v3/range.upb_minitable.c \
    src/core/ext/upb-gen/xds/type/matcher/v3/regex.upb_minitable.c \
    src/core/ext/upb-gen/xds/type/matcher/v3/string.upb_minitable.c \
    src/core/ext/upb-gen/xds/type/v3/cel.upb_minitable.c \
    src/core/ext/upb-gen/xds/type/v3/range.upb_minitable.c \
    src/core/ext/upb-gen/xds/type/v3/typed_struct.upb_minitable.c \
    src/core/ext/upbdefs-gen/envoy/admin/v3/certs.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/admin/v3/clusters.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/admin/v3/config_dump.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/admin/v3/config_dump_shared.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/admin/v3/init_dump.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/admin/v3/listeners.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/admin/v3/memory.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/admin/v3/metrics.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/admin/v3/mutex_stats.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/admin/v3/server_info.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/admin/v3/tap.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/annotations/deprecation.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/annotations/resource.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/accesslog/v3/accesslog.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/bootstrap/v3/bootstrap.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/cluster/v3/circuit_breaker.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/cluster/v3/cluster.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/cluster/v3/filter.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/cluster/v3/outlier_detection.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/common/matcher/v3/matcher.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/core/v3/address.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/core/v3/backoff.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/core/v3/base.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/core/v3/config_source.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/core/v3/event_service_config.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/core/v3/extension.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/core/v3/grpc_method_list.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/core/v3/grpc_service.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/core/v3/health_check.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/core/v3/http_service.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/core/v3/http_uri.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/core/v3/protocol.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/core/v3/proxy_protocol.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/core/v3/resolver.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/core/v3/socket_cmsg_headers.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/core/v3/socket_option.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/core/v3/substitution_format_string.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/core/v3/udp_socket_config.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/endpoint/v3/endpoint.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/endpoint/v3/endpoint_components.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/endpoint/v3/load_report.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/listener/v3/api_listener.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/listener/v3/listener.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/listener/v3/listener_components.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/listener/v3/quic_config.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/listener/v3/udp_listener_config.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/metrics/v3/metrics_service.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/metrics/v3/stats.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/overload/v3/overload.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/rbac/v3/rbac.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/route/v3/route.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/route/v3/route_components.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/route/v3/scoped_route.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/tap/v3/common.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/trace/v3/datadog.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/trace/v3/dynamic_ot.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/trace/v3/http_tracer.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/trace/v3/lightstep.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/trace/v3/opentelemetry.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/trace/v3/service.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/trace/v3/skywalking.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/trace/v3/trace.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/trace/v3/xray.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/config/trace/v3/zipkin.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/data/accesslog/v3/accesslog.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/extensions/clusters/aggregate/v3/cluster.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/extensions/filters/common/fault/v3/fault.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/extensions/filters/http/fault/v3/fault.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/extensions/filters/http/rbac/v3/rbac.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/extensions/filters/http/router/v3/router.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/extensions/filters/http/stateful_session/v3/stateful_session.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/extensions/http/stateful_session/cookie/v3/cookie.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/extensions/transport_sockets/tls/v3/cert.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/extensions/transport_sockets/tls/v3/common.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/extensions/transport_sockets/tls/v3/secret.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/extensions/transport_sockets/tls/v3/tls.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/extensions/transport_sockets/tls/v3/tls_spiffe_validator_config.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/extensions/upstreams/http/v3/http_protocol_options.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/service/discovery/v3/ads.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/service/discovery/v3/discovery.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/service/load_stats/v3/lrs.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/service/status/v3/csds.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/http/v3/cookie.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/http/v3/path_transformation.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/matcher/v3/address.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/matcher/v3/filter_state.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/matcher/v3/http_inputs.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/matcher/v3/metadata.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/matcher/v3/node.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/matcher/v3/number.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/matcher/v3/path.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/matcher/v3/regex.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/matcher/v3/status_code_input.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/matcher/v3/string.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/matcher/v3/struct.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/matcher/v3/value.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/metadata/v3/metadata.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/tracing/v3/custom_tag.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/v3/hash_policy.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/v3/http.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/v3/http_status.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/v3/percent.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/v3/range.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/v3/ratelimit_strategy.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/v3/ratelimit_unit.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/v3/semantic_version.upbdefs.c \
    src/core/ext/upbdefs-gen/envoy/type/v3/token_bucket.upbdefs.c \
    src/core/ext/upbdefs-gen/google/api/annotations.upbdefs.c \
    src/core/ext/upbdefs-gen/google/api/expr/v1alpha1/checked.upbdefs.c \
    src/core/ext/upbdefs-gen/google/api/expr/v1alpha1/syntax.upbdefs.c \
    src/core/ext/upbdefs-gen/google/api/http.upbdefs.c \
    src/core/ext/upbdefs-gen/google/api/httpbody.upbdefs.c \
    src/core/ext/upbdefs-gen/google/protobuf/any.upbdefs.c \
    src/core/ext/upbdefs-gen/google/protobuf/descriptor.upbdefs.c \
    src/core/ext/upbdefs-gen/google/protobuf/duration.upbdefs.c \
    src/core/ext/upbdefs-gen/google/protobuf/empty.upbdefs.c \
    src/core/ext/upbdefs-gen/google/protobuf/struct.upbdefs.c \
    src/core/ext/upbdefs-gen/google/protobuf/timestamp.upbdefs.c \
    src/core/ext/upbdefs-gen/google/protobuf/wrappers.upbdefs.c \
    src/core/ext/upbdefs-gen/google/rpc/status.upbdefs.c \
    src/core/ext/upbdefs-gen/src/proto/grpc/lookup/v1/rls_config.upbdefs.c \
    src/core/ext/upbdefs-gen/udpa/annotations/migrate.upbdefs.c \
    src/core/ext/upbdefs-gen/udpa/annotations/security.upbdefs.c \
    src/core/ext/upbdefs-gen/udpa/annotations/sensitive.upbdefs.c \
    src/core/ext/upbdefs-gen/udpa/annotations/status.upbdefs.c \
    src/core/ext/upbdefs-gen/udpa/annotations/versioning.upbdefs.c \
    src/core/ext/upbdefs-gen/validate/validate.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/annotations/v3/migrate.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/annotations/v3/security.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/annotations/v3/sensitive.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/annotations/v3/status.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/annotations/v3/versioning.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/core/v3/authority.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/core/v3/cidr.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/core/v3/collection_entry.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/core/v3/context_params.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/core/v3/extension.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/core/v3/resource.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/core/v3/resource_locator.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/core/v3/resource_name.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/type/matcher/v3/cel.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/type/matcher/v3/domain.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/type/matcher/v3/http_inputs.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/type/matcher/v3/ip.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/type/matcher/v3/matcher.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/type/matcher/v3/range.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/type/matcher/v3/regex.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/type/matcher/v3/string.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/type/v3/cel.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/type/v3/range.upbdefs.c \
    src/core/ext/upbdefs-gen/xds/type/v3/typed_struct.upbdefs.c \
    src/core/filter/auth/client_auth_filter.cc \
    src/core/filter/auth/server_auth_filter.cc \
    src/core/filter/blackboard.cc \
    src/core/handshaker/endpoint_info/endpoint_info_handshaker.cc \
    src/core/handshaker/handshaker.cc \
    src/core/handshaker/handshaker_registry.cc \
    src/core/handshaker/http_connect/http_connect_handshaker.cc \
    src/core/handshaker/http_connect/http_proxy_mapper.cc \
    src/core/handshaker/http_connect/xds_http_proxy_mapper.cc \
    src/core/handshaker/proxy_mapper_registry.cc \
    src/core/handshaker/security/secure_endpoint.cc \
    src/core/handshaker/security/security_handshaker.cc \
    src/core/handshaker/tcp_connect/tcp_connect_handshaker.cc \
    src/core/lib/address_utils/parse_address.cc \
    src/core/lib/address_utils/sockaddr_utils.cc \
    src/core/lib/channel/channel_args.cc \
    src/core/lib/channel/channel_args_preconditioning.cc \
    src/core/lib/channel/channel_stack.cc \
    src/core/lib/channel/channel_stack_builder.cc \
    src/core/lib/channel/channel_stack_builder_impl.cc \
    src/core/lib/channel/connected_channel.cc \
    src/core/lib/channel/promise_based_filter.cc \
    src/core/lib/compression/compression.cc \
    src/core/lib/compression/compression_internal.cc \
    src/core/lib/compression/message_compress.cc \
    src/core/lib/debug/trace.cc \
    src/core/lib/debug/trace_flags.cc \
    src/core/lib/event_engine/ares_resolver.cc \
    src/core/lib/event_engine/cf_engine/cf_engine.cc \
    src/core/lib/event_engine/cf_engine/cfstream_endpoint.cc \
    src/core/lib/event_engine/cf_engine/dispatch_thread_pool.cc \
    src/core/lib/event_engine/cf_engine/dns_service_resolver.cc \
    src/core/lib/event_engine/channel_args_endpoint_config.cc \
    src/core/lib/event_engine/default_event_engine.cc \
    src/core/lib/event_engine/default_event_engine_factory.cc \
    src/core/lib/event_engine/event_engine.cc \
    src/core/lib/event_engine/forkable.cc \
    src/core/lib/event_engine/posix_engine/ev_epoll1_linux.cc \
    src/core/lib/event_engine/posix_engine/ev_poll_posix.cc \
    src/core/lib/event_engine/posix_engine/event_poller_posix_default.cc \
    src/core/lib/event_engine/posix_engine/internal_errqueue.cc \
    src/core/lib/event_engine/posix_engine/lockfree_event.cc \
    src/core/lib/event_engine/posix_engine/native_posix_dns_resolver.cc \
    src/core/lib/event_engine/posix_engine/posix_endpoint.cc \
    src/core/lib/event_engine/posix_engine/posix_engine.cc \
    src/core/lib/event_engine/posix_engine/posix_engine_listener.cc \
    src/core/lib/event_engine/posix_engine/posix_engine_listener_utils.cc \
    src/core/lib/event_engine/posix_engine/set_socket_dualstack.cc \
    src/core/lib/event_engine/posix_engine/tcp_socket_utils.cc \
    src/core/lib/event_engine/posix_engine/timer.cc \
    src/core/lib/event_engine/posix_engine/timer_heap.cc \
    src/core/lib/event_engine/posix_engine/timer_manager.cc \
    src/core/lib/event_engine/posix_engine/traced_buffer_list.cc \
    src/core/lib/event_engine/posix_engine/wakeup_fd_eventfd.cc \
    src/core/lib/event_engine/posix_engine/wakeup_fd_pipe.cc \
    src/core/lib/event_engine/posix_engine/wakeup_fd_posix_default.cc \
    src/core/lib/event_engine/resolved_address.cc \
    src/core/lib/event_engine/shim.cc \
    src/core/lib/event_engine/slice.cc \
    src/core/lib/event_engine/slice_buffer.cc \
    src/core/lib/event_engine/tcp_socket_utils.cc \
    src/core/lib/event_engine/thread_local.cc \
    src/core/lib/event_engine/thread_pool/thread_count.cc \
    src/core/lib/event_engine/thread_pool/thread_pool_factory.cc \
    src/core/lib/event_engine/thread_pool/work_stealing_thread_pool.cc \
    src/core/lib/event_engine/thready_event_engine/thready_event_engine.cc \
    src/core/lib/event_engine/time_util.cc \
    src/core/lib/event_engine/utils.cc \
    src/core/lib/event_engine/windows/grpc_polled_fd_windows.cc \
    src/core/lib/event_engine/windows/iocp.cc \
    src/core/lib/event_engine/windows/native_windows_dns_resolver.cc \
    src/core/lib/event_engine/windows/win_socket.cc \
    src/core/lib/event_engine/windows/windows_endpoint.cc \
    src/core/lib/event_engine/windows/windows_engine.cc \
    src/core/lib/event_engine/windows/windows_listener.cc \
    src/core/lib/event_engine/work_queue/basic_work_queue.cc \
    src/core/lib/experiments/config.cc \
    src/core/lib/experiments/experiments.cc \
    src/core/lib/iomgr/buffer_list.cc \
    src/core/lib/iomgr/call_combiner.cc \
    src/core/lib/iomgr/cfstream_handle.cc \
    src/core/lib/iomgr/closure.cc \
    src/core/lib/iomgr/combiner.cc \
    src/core/lib/iomgr/dualstack_socket_posix.cc \
    src/core/lib/iomgr/endpoint.cc \
    src/core/lib/iomgr/endpoint_cfstream.cc \
    src/core/lib/iomgr/endpoint_pair_posix.cc \
    src/core/lib/iomgr/endpoint_pair_windows.cc \
    src/core/lib/iomgr/error.cc \
    src/core/lib/iomgr/error_cfstream.cc \
    src/core/lib/iomgr/ev_apple.cc \
    src/core/lib/iomgr/ev_epoll1_linux.cc \
    src/core/lib/iomgr/ev_poll_posix.cc \
    src/core/lib/iomgr/ev_posix.cc \
    src/core/lib/iomgr/event_engine_shims/closure.cc \
    src/core/lib/iomgr/event_engine_shims/endpoint.cc \
    src/core/lib/iomgr/event_engine_shims/tcp_client.cc \
    src/core/lib/iomgr/exec_ctx.cc \
    src/core/lib/iomgr/executor.cc \
    src/core/lib/iomgr/fork_posix.cc \
    src/core/lib/iomgr/fork_windows.cc \
    src/core/lib/iomgr/internal_errqueue.cc \
    src/core/lib/iomgr/iocp_windows.cc \
    src/core/lib/iomgr/iomgr.cc \
    src/core/lib/iomgr/iomgr_internal.cc \
    src/core/lib/iomgr/iomgr_posix.cc \
    src/core/lib/iomgr/iomgr_posix_cfstream.cc \
    src/core/lib/iomgr/iomgr_windows.cc \
    src/core/lib/iomgr/lockfree_event.cc \
    src/core/lib/iomgr/polling_entity.cc \
    src/core/lib/iomgr/pollset.cc \
    src/core/lib/iomgr/pollset_set.cc \
    src/core/lib/iomgr/pollset_set_windows.cc \
    src/core/lib/iomgr/pollset_windows.cc \
    src/core/lib/iomgr/resolve_address.cc \
    src/core/lib/iomgr/resolve_address_posix.cc \
    src/core/lib/iomgr/resolve_address_windows.cc \
    src/core/lib/iomgr/sockaddr_utils_posix.cc \
    src/core/lib/iomgr/socket_factory_posix.cc \
    src/core/lib/iomgr/socket_mutator.cc \
    src/core/lib/iomgr/socket_utils_common_posix.cc \
    src/core/lib/iomgr/socket_utils_linux.cc \
    src/core/lib/iomgr/socket_utils_posix.cc \
    src/core/lib/iomgr/socket_utils_windows.cc \
    src/core/lib/iomgr/socket_windows.cc \
    src/core/lib/iomgr/systemd_utils.cc \
    src/core/lib/iomgr/tcp_client.cc \
    src/core/lib/iomgr/tcp_client_cfstream.cc \
    src/core/lib/iomgr/tcp_client_posix.cc \
    src/core/lib/iomgr/tcp_client_windows.cc \
    src/core/lib/iomgr/tcp_posix.cc \
    src/core/lib/iomgr/tcp_server.cc \
    src/core/lib/iomgr/tcp_server_posix.cc \
    src/core/lib/iomgr/tcp_server_utils_posix_common.cc \
    src/core/lib/iomgr/tcp_server_utils_posix_ifaddrs.cc \
    src/core/lib/iomgr/tcp_server_utils_posix_noifaddrs.cc \
    src/core/lib/iomgr/tcp_server_windows.cc \
    src/core/lib/iomgr/tcp_windows.cc \
    src/core/lib/iomgr/timer.cc \
    src/core/lib/iomgr/timer_generic.cc \
    src/core/lib/iomgr/timer_heap.cc \
    src/core/lib/iomgr/timer_manager.cc \
    src/core/lib/iomgr/unix_sockets_posix.cc \
    src/core/lib/iomgr/unix_sockets_posix_noop.cc \
    src/core/lib/iomgr/vsock.cc \
    src/core/lib/iomgr/wakeup_fd_eventfd.cc \
    src/core/lib/iomgr/wakeup_fd_nospecial.cc \
    src/core/lib/iomgr/wakeup_fd_pipe.cc \
    src/core/lib/iomgr/wakeup_fd_posix.cc \
    src/core/lib/promise/activity.cc \
    src/core/lib/promise/party.cc \
    src/core/lib/promise/sleep.cc \
    src/core/lib/resource_quota/api.cc \
    src/core/lib/resource_quota/arena.cc \
    src/core/lib/resource_quota/connection_quota.cc \
    src/core/lib/resource_quota/memory_quota.cc \
    src/core/lib/resource_quota/periodic_update.cc \
    src/core/lib/resource_quota/resource_quota.cc \
    src/core/lib/resource_quota/thread_quota.cc \
    src/core/lib/security/authorization/audit_logging.cc \
    src/core/lib/security/authorization/authorization_policy_provider_vtable.cc \
    src/core/lib/security/authorization/evaluate_args.cc \
    src/core/lib/security/authorization/grpc_authorization_engine.cc \
    src/core/lib/security/authorization/grpc_server_authz_filter.cc \
    src/core/lib/security/authorization/matchers.cc \
    src/core/lib/security/authorization/rbac_policy.cc \
    src/core/lib/security/authorization/stdout_logger.cc \
    src/core/lib/slice/percent_encoding.cc \
    src/core/lib/slice/slice.cc \
    src/core/lib/slice/slice_buffer.cc \
    src/core/lib/slice/slice_string_helpers.cc \
    src/core/lib/surface/byte_buffer.cc \
    src/core/lib/surface/byte_buffer_reader.cc \
    src/core/lib/surface/call.cc \
    src/core/lib/surface/call_details.cc \
    src/core/lib/surface/call_log_batch.cc \
    src/core/lib/surface/call_utils.cc \
    src/core/lib/surface/channel.cc \
    src/core/lib/surface/channel_create.cc \
    src/core/lib/surface/channel_init.cc \
    src/core/lib/surface/channel_stack_type.cc \
    src/core/lib/surface/completion_queue.cc \
    src/core/lib/surface/completion_queue_factory.cc \
    src/core/lib/surface/connection_context.cc \
    src/core/lib/surface/event_string.cc \
    src/core/lib/surface/filter_stack_call.cc \
    src/core/lib/surface/init.cc \
    src/core/lib/surface/init_internally.cc \
    src/core/lib/surface/lame_client.cc \
    src/core/lib/surface/legacy_channel.cc \
    src/core/lib/surface/metadata_array.cc \
    src/core/lib/surface/validate_metadata.cc \
    src/core/lib/surface/version.cc \
    src/core/lib/transport/bdp_estimator.cc \
    src/core/lib/transport/call_final_info.cc \
    src/core/lib/transport/connectivity_state.cc \
    src/core/lib/transport/error_utils.cc \
    src/core/lib/transport/status_conversion.cc \
    src/core/lib/transport/timeout_encoding.cc \
    src/core/lib/transport/transport.cc \
    src/core/lib/transport/transport_op_string.cc \
    src/core/load_balancing/address_filtering.cc \
    src/core/load_balancing/backend_metric_parser.cc \
    src/core/load_balancing/child_policy_handler.cc \
    src/core/load_balancing/endpoint_list.cc \
    src/core/load_balancing/grpclb/client_load_reporting_filter.cc \
    src/core/load_balancing/grpclb/grpclb.cc \
    src/core/load_balancing/grpclb/grpclb_balancer_addresses.cc \
    src/core/load_balancing/grpclb/grpclb_client_stats.cc \
    src/core/load_balancing/grpclb/load_balancer_api.cc \
    src/core/load_balancing/health_check_client.cc \
    src/core/load_balancing/lb_policy.cc \
    src/core/load_balancing/lb_policy_registry.cc \
    src/core/load_balancing/oob_backend_metric.cc \
    src/core/load_balancing/outlier_detection/outlier_detection.cc \
    src/core/load_balancing/pick_first/pick_first.cc \
    src/core/load_balancing/priority/priority.cc \
    src/core/load_balancing/ring_hash/ring_hash.cc \
    src/core/load_balancing/rls/rls.cc \
    src/core/load_balancing/round_robin/round_robin.cc \
    src/core/load_balancing/weighted_round_robin/static_stride_scheduler.cc \
    src/core/load_balancing/weighted_round_robin/weighted_round_robin.cc \
    src/core/load_balancing/weighted_target/weighted_target.cc \
    src/core/load_balancing/xds/cds.cc \
    src/core/load_balancing/xds/xds_cluster_impl.cc \
    src/core/load_balancing/xds/xds_cluster_manager.cc \
    src/core/load_balancing/xds/xds_override_host.cc \
    src/core/load_balancing/xds/xds_wrr_locality.cc \
    src/core/plugin_registry/grpc_plugin_registry.cc \
    src/core/plugin_registry/grpc_plugin_registry_extra.cc \
    src/core/resolver/dns/c_ares/dns_resolver_ares.cc \
    src/core/resolver/dns/c_ares/grpc_ares_ev_driver_posix.cc \
    src/core/resolver/dns/c_ares/grpc_ares_ev_driver_windows.cc \
    src/core/resolver/dns/c_ares/grpc_ares_wrapper.cc \
    src/core/resolver/dns/c_ares/grpc_ares_wrapper_posix.cc \
    src/core/resolver/dns/c_ares/grpc_ares_wrapper_windows.cc \
    src/core/resolver/dns/dns_resolver_plugin.cc \
    src/core/resolver/dns/event_engine/event_engine_client_channel_resolver.cc \
    src/core/resolver/dns/event_engine/service_config_helper.cc \
    src/core/resolver/dns/native/dns_resolver.cc \
    src/core/resolver/endpoint_addresses.cc \
    src/core/resolver/fake/fake_resolver.cc \
    src/core/resolver/google_c2p/google_c2p_resolver.cc \
    src/core/resolver/polling_resolver.cc \
    src/core/resolver/resolver.cc \
    src/core/resolver/resolver_registry.cc \
    src/core/resolver/sockaddr/sockaddr_resolver.cc \
    src/core/resolver/xds/xds_config.cc \
    src/core/resolver/xds/xds_dependency_manager.cc \
    src/core/resolver/xds/xds_resolver.cc \
    src/core/server/server.cc \
    src/core/server/server_call_tracer_filter.cc \
    src/core/server/server_config_selector_filter.cc \
    src/core/server/xds_channel_stack_modifier.cc \
    src/core/server/xds_server_config_fetcher.cc \
    src/core/service_config/service_config_channel_arg_filter.cc \
    src/core/service_config/service_config_impl.cc \
    src/core/service_config/service_config_parser.cc \
    src/core/telemetry/call_tracer.cc \
    src/core/telemetry/histogram_view.cc \
    src/core/telemetry/metrics.cc \
    src/core/telemetry/stats.cc \
    src/core/telemetry/stats_data.cc \
    src/core/transport/auth_context.cc \
    src/core/tsi/alts/crypt/aes_gcm.cc \
    src/core/tsi/alts/crypt/gsec.cc \
    src/core/tsi/alts/frame_protector/alts_counter.cc \
    src/core/tsi/alts/frame_protector/alts_crypter.cc \
    src/core/tsi/alts/frame_protector/alts_frame_protector.cc \
    src/core/tsi/alts/frame_protector/alts_record_protocol_crypter_common.cc \
    src/core/tsi/alts/frame_protector/alts_seal_privacy_integrity_crypter.cc \
    src/core/tsi/alts/frame_protector/alts_unseal_privacy_integrity_crypter.cc \
    src/core/tsi/alts/frame_protector/frame_handler.cc \
    src/core/tsi/alts/handshaker/alts_handshaker_client.cc \
    src/core/tsi/alts/handshaker/alts_shared_resource.cc \
    src/core/tsi/alts/handshaker/alts_tsi_handshaker.cc \
    src/core/tsi/alts/handshaker/alts_tsi_utils.cc \
    src/core/tsi/alts/handshaker/transport_security_common_api.cc \
    src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_integrity_only_record_protocol.cc \
    src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_privacy_integrity_record_protocol.cc \
    src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol_common.cc \
    src/core/tsi/alts/zero_copy_frame_protector/alts_iovec_record_protocol.cc \
    src/core/tsi/alts/zero_copy_frame_protector/alts_zero_copy_grpc_protector.cc \
    src/core/tsi/fake_transport_security.cc \
    src/core/tsi/local_transport_security.cc \
    src/core/tsi/ssl/key_logging/ssl_key_logging.cc \
    src/core/tsi/ssl/session_cache/ssl_session_boringssl.cc \
    src/core/tsi/ssl/session_cache/ssl_session_cache.cc \
    src/core/tsi/ssl/session_cache/ssl_session_openssl.cc \
    src/core/tsi/ssl_transport_security.cc \
    src/core/tsi/ssl_transport_security_utils.cc \
    src/core/tsi/transport_security.cc \
    src/core/tsi/transport_security_grpc.cc \
    src/core/util/alloc.cc \
    src/core/util/backoff.cc \
    src/core/util/crash.cc \
    src/core/util/dump_args.cc \
    src/core/util/event_log.cc \
    src/core/util/examine_stack.cc \
    src/core/util/fork.cc \
    src/core/util/gcp_metadata_query.cc \
    src/core/util/gethostname_fallback.cc \
    src/core/util/gethostname_host_name_max.cc \
    src/core/util/gethostname_sysconf.cc \
    src/core/util/glob.cc \
    src/core/util/gpr_time.cc \
    src/core/util/grpc_if_nametoindex_posix.cc \
    src/core/util/grpc_if_nametoindex_unsupported.cc \
    src/core/util/host_port.cc \
    src/core/util/http_client/format_request.cc \
    src/core/util/http_client/httpcli.cc \
    src/core/util/http_client/httpcli_security_connector.cc \
    src/core/util/http_client/parser.cc \
    src/core/util/iphone/cpu.cc \
    src/core/util/json/json_object_loader.cc \
    src/core/util/json/json_reader.cc \
    src/core/util/json/json_util.cc \
    src/core/util/json/json_writer.cc \
    src/core/util/latent_see.cc \
    src/core/util/linux/cpu.cc \
    src/core/util/linux/env.cc \
    src/core/util/load_file.cc \
    src/core/util/log.cc \
    src/core/util/matchers.cc \
    src/core/util/mpscq.cc \
    src/core/util/msys/tmpfile.cc \
    src/core/util/per_cpu.cc \
    src/core/util/posix/cpu.cc \
    src/core/util/posix/directory_reader.cc \
    src/core/util/posix/env.cc \
    src/core/util/posix/stat.cc \
    src/core/util/posix/string.cc \
    src/core/util/posix/sync.cc \
    src/core/util/posix/thd.cc \
    src/core/util/posix/time.cc \
    src/core/util/posix/tmpfile.cc \
    src/core/util/random_early_detection.cc \
    src/core/util/ref_counted_string.cc \
    src/core/util/status_helper.cc \
    src/core/util/strerror.cc \
    src/core/util/string.cc \
    src/core/util/sync.cc \
    src/core/util/sync_abseil.cc \
    src/core/util/tchar.cc \
    src/core/util/time.cc \
    src/core/util/time_averaged_stats.cc \
    src/core/util/time_precise.cc \
    src/core/util/time_util.cc \
    src/core/util/uri.cc \
    src/core/util/uuid_v4.cc \
    src/core/util/validation_errors.cc \
    src/core/util/windows/cpu.cc \
    src/core/util/windows/directory_reader.cc \
    src/core/util/windows/env.cc \
    src/core/util/windows/stat.cc \
    src/core/util/windows/string.cc \
    src/core/util/windows/string_util.cc \
    src/core/util/windows/sync.cc \
    src/core/util/windows/thd.cc \
    src/core/util/windows/time.cc \
    src/core/util/windows/tmpfile.cc \
    src/core/util/work_serializer.cc \
    src/core/xds/grpc/certificate_provider_store.cc \
    src/core/xds/grpc/file_watcher_certificate_provider_factory.cc \
    src/core/xds/grpc/xds_audit_logger_registry.cc \
    src/core/xds/grpc/xds_bootstrap_grpc.cc \
    src/core/xds/grpc/xds_certificate_provider.cc \
    src/core/xds/grpc/xds_client_grpc.cc \
    src/core/xds/grpc/xds_cluster.cc \
    src/core/xds/grpc/xds_cluster_parser.cc \
    src/core/xds/grpc/xds_cluster_specifier_plugin.cc \
    src/core/xds/grpc/xds_common_types.cc \
    src/core/xds/grpc/xds_common_types_parser.cc \
    src/core/xds/grpc/xds_endpoint.cc \
    src/core/xds/grpc/xds_endpoint_parser.cc \
    src/core/xds/grpc/xds_health_status.cc \
    src/core/xds/grpc/xds_http_fault_filter.cc \
    src/core/xds/grpc/xds_http_filter_registry.cc \
    src/core/xds/grpc/xds_http_gcp_authn_filter.cc \
    src/core/xds/grpc/xds_http_rbac_filter.cc \
    src/core/xds/grpc/xds_http_stateful_session_filter.cc \
    src/core/xds/grpc/xds_lb_policy_registry.cc \
    src/core/xds/grpc/xds_listener.cc \
    src/core/xds/grpc/xds_listener_parser.cc \
    src/core/xds/grpc/xds_metadata.cc \
    src/core/xds/grpc/xds_metadata_parser.cc \
    src/core/xds/grpc/xds_route_config.cc \
    src/core/xds/grpc/xds_route_config_parser.cc \
    src/core/xds/grpc/xds_routing.cc \
    src/core/xds/grpc/xds_server_grpc.cc \
    src/core/xds/grpc/xds_transport_grpc.cc \
    src/core/xds/xds_client/lrs_client.cc \
    src/core/xds/xds_client/xds_api.cc \
    src/core/xds/xds_client/xds_backend_metric_propagation.cc \
    src/core/xds/xds_client/xds_bootstrap.cc \
    src/core/xds/xds_client/xds_client.cc \
    third_party/abseil-cpp/absl/base/internal/cycleclock.cc \
    third_party/abseil-cpp/absl/base/internal/low_level_alloc.cc \
    third_party/abseil-cpp/absl/base/internal/raw_logging.cc \
    third_party/abseil-cpp/absl/base/internal/spinlock.cc \
    third_party/abseil-cpp/absl/base/internal/spinlock_wait.cc \
    third_party/abseil-cpp/absl/base/internal/strerror.cc \
    third_party/abseil-cpp/absl/base/internal/sysinfo.cc \
    third_party/abseil-cpp/absl/base/internal/thread_identity.cc \
    third_party/abseil-cpp/absl/base/internal/throw_delegate.cc \
    third_party/abseil-cpp/absl/base/internal/unscaledcycleclock.cc \
    third_party/abseil-cpp/absl/base/log_severity.cc \
    third_party/abseil-cpp/absl/container/internal/hashtablez_sampler.cc \
    third_party/abseil-cpp/absl/container/internal/hashtablez_sampler_force_weak_definition.cc \
    third_party/abseil-cpp/absl/container/internal/raw_hash_set.cc \
    third_party/abseil-cpp/absl/crc/crc32c.cc \
    third_party/abseil-cpp/absl/crc/internal/cpu_detect.cc \
    third_party/abseil-cpp/absl/crc/internal/crc.cc \
    third_party/abseil-cpp/absl/crc/internal/crc_cord_state.cc \
    third_party/abseil-cpp/absl/crc/internal/crc_memcpy_fallback.cc \
    third_party/abseil-cpp/absl/crc/internal/crc_memcpy_x86_arm_combined.cc \
    third_party/abseil-cpp/absl/crc/internal/crc_non_temporal_memcpy.cc \
    third_party/abseil-cpp/absl/crc/internal/crc_x86_arm_combined.cc \
    third_party/abseil-cpp/absl/debugging/internal/address_is_readable.cc \
    third_party/abseil-cpp/absl/debugging/internal/decode_rust_punycode.cc \
    third_party/abseil-cpp/absl/debugging/internal/demangle.cc \
    third_party/abseil-cpp/absl/debugging/internal/demangle_rust.cc \
    third_party/abseil-cpp/absl/debugging/internal/elf_mem_image.cc \
    third_party/abseil-cpp/absl/debugging/internal/examine_stack.cc \
    third_party/abseil-cpp/absl/debugging/internal/utf8_for_code_point.cc \
    third_party/abseil-cpp/absl/debugging/internal/vdso_support.cc \
    third_party/abseil-cpp/absl/debugging/stacktrace.cc \
    third_party/abseil-cpp/absl/debugging/symbolize.cc \
    third_party/abseil-cpp/absl/flags/commandlineflag.cc \
    third_party/abseil-cpp/absl/flags/internal/commandlineflag.cc \
    third_party/abseil-cpp/absl/flags/internal/flag.cc \
    third_party/abseil-cpp/absl/flags/internal/private_handle_accessor.cc \
    third_party/abseil-cpp/absl/flags/internal/program_name.cc \
    third_party/abseil-cpp/absl/flags/marshalling.cc \
    third_party/abseil-cpp/absl/flags/reflection.cc \
    third_party/abseil-cpp/absl/flags/usage_config.cc \
    third_party/abseil-cpp/absl/hash/internal/city.cc \
    third_party/abseil-cpp/absl/hash/internal/hash.cc \
    third_party/abseil-cpp/absl/hash/internal/low_level_hash.cc \
    third_party/abseil-cpp/absl/log/globals.cc \
    third_party/abseil-cpp/absl/log/internal/check_op.cc \
    third_party/abseil-cpp/absl/log/internal/conditions.cc \
    third_party/abseil-cpp/absl/log/internal/fnmatch.cc \
    third_party/abseil-cpp/absl/log/internal/globals.cc \
    third_party/abseil-cpp/absl/log/internal/log_format.cc \
    third_party/abseil-cpp/absl/log/internal/log_message.cc \
    third_party/abseil-cpp/absl/log/internal/log_sink_set.cc \
    third_party/abseil-cpp/absl/log/internal/nullguard.cc \
    third_party/abseil-cpp/absl/log/internal/proto.cc \
    third_party/abseil-cpp/absl/log/internal/vlog_config.cc \
    third_party/abseil-cpp/absl/log/log_entry.cc \
    third_party/abseil-cpp/absl/log/log_sink.cc \
    third_party/abseil-cpp/absl/numeric/int128.cc \
    third_party/abseil-cpp/absl/profiling/internal/exponential_biased.cc \
    third_party/abseil-cpp/absl/random/discrete_distribution.cc \
    third_party/abseil-cpp/absl/random/gaussian_distribution.cc \
    third_party/abseil-cpp/absl/random/internal/pool_urbg.cc \
    third_party/abseil-cpp/absl/random/internal/randen.cc \
    third_party/abseil-cpp/absl/random/internal/randen_detect.cc \
    third_party/abseil-cpp/absl/random/internal/randen_hwaes.cc \
    third_party/abseil-cpp/absl/random/internal/randen_round_keys.cc \
    third_party/abseil-cpp/absl/random/internal/randen_slow.cc \
    third_party/abseil-cpp/absl/random/internal/seed_material.cc \
    third_party/abseil-cpp/absl/random/seed_gen_exception.cc \
    third_party/abseil-cpp/absl/random/seed_sequences.cc \
    third_party/abseil-cpp/absl/status/internal/status_internal.cc \
    third_party/abseil-cpp/absl/status/status.cc \
    third_party/abseil-cpp/absl/status/status_payload_printer.cc \
    third_party/abseil-cpp/absl/status/statusor.cc \
    third_party/abseil-cpp/absl/strings/ascii.cc \
    third_party/abseil-cpp/absl/strings/charconv.cc \
    third_party/abseil-cpp/absl/strings/cord.cc \
    third_party/abseil-cpp/absl/strings/cord_analysis.cc \
    third_party/abseil-cpp/absl/strings/cord_buffer.cc \
    third_party/abseil-cpp/absl/strings/escaping.cc \
    third_party/abseil-cpp/absl/strings/internal/charconv_bigint.cc \
    third_party/abseil-cpp/absl/strings/internal/charconv_parse.cc \
    third_party/abseil-cpp/absl/strings/internal/cord_internal.cc \
    third_party/abseil-cpp/absl/strings/internal/cord_rep_btree.cc \
    third_party/abseil-cpp/absl/strings/internal/cord_rep_btree_navigator.cc \
    third_party/abseil-cpp/absl/strings/internal/cord_rep_btree_reader.cc \
    third_party/abseil-cpp/absl/strings/internal/cord_rep_consume.cc \
    third_party/abseil-cpp/absl/strings/internal/cord_rep_crc.cc \
    third_party/abseil-cpp/absl/strings/internal/cordz_functions.cc \
    third_party/abseil-cpp/absl/strings/internal/cordz_handle.cc \
    third_party/abseil-cpp/absl/strings/internal/cordz_info.cc \
    third_party/abseil-cpp/absl/strings/internal/damerau_levenshtein_distance.cc \
    third_party/abseil-cpp/absl/strings/internal/escaping.cc \
    third_party/abseil-cpp/absl/strings/internal/memutil.cc \
    third_party/abseil-cpp/absl/strings/internal/ostringstream.cc \
    third_party/abseil-cpp/absl/strings/internal/str_format/arg.cc \
    third_party/abseil-cpp/absl/strings/internal/str_format/bind.cc \
    third_party/abseil-cpp/absl/strings/internal/str_format/extension.cc \
    third_party/abseil-cpp/absl/strings/internal/str_format/float_conversion.cc \
    third_party/abseil-cpp/absl/strings/internal/str_format/output.cc \
    third_party/abseil-cpp/absl/strings/internal/str_format/parser.cc \
    third_party/abseil-cpp/absl/strings/internal/stringify_sink.cc \
    third_party/abseil-cpp/absl/strings/internal/utf8.cc \
    third_party/abseil-cpp/absl/strings/match.cc \
    third_party/abseil-cpp/absl/strings/numbers.cc \
    third_party/abseil-cpp/absl/strings/str_cat.cc \
    third_party/abseil-cpp/absl/strings/str_replace.cc \
    third_party/abseil-cpp/absl/strings/str_split.cc \
    third_party/abseil-cpp/absl/strings/string_view.cc \
    third_party/abseil-cpp/absl/strings/substitute.cc \
    third_party/abseil-cpp/absl/synchronization/barrier.cc \
    third_party/abseil-cpp/absl/synchronization/blocking_counter.cc \
    third_party/abseil-cpp/absl/synchronization/internal/create_thread_identity.cc \
    third_party/abseil-cpp/absl/synchronization/internal/futex_waiter.cc \
    third_party/abseil-cpp/absl/synchronization/internal/graphcycles.cc \
    third_party/abseil-cpp/absl/synchronization/internal/kernel_timeout.cc \
    third_party/abseil-cpp/absl/synchronization/internal/per_thread_sem.cc \
    third_party/abseil-cpp/absl/synchronization/internal/pthread_waiter.cc \
    third_party/abseil-cpp/absl/synchronization/internal/sem_waiter.cc \
    third_party/abseil-cpp/absl/synchronization/internal/stdcpp_waiter.cc \
    third_party/abseil-cpp/absl/synchronization/internal/waiter_base.cc \
    third_party/abseil-cpp/absl/synchronization/internal/win32_waiter.cc \
    third_party/abseil-cpp/absl/synchronization/mutex.cc \
    third_party/abseil-cpp/absl/synchronization/notification.cc \
    third_party/abseil-cpp/absl/time/civil_time.cc \
    third_party/abseil-cpp/absl/time/clock.cc \
    third_party/abseil-cpp/absl/time/duration.cc \
    third_party/abseil-cpp/absl/time/format.cc \
    third_party/abseil-cpp/absl/time/internal/cctz/src/civil_time_detail.cc \
    third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_fixed.cc \
    third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_format.cc \
    third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_if.cc \
    third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_impl.cc \
    third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_info.cc \
    third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_libc.cc \
    third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_lookup.cc \
    third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_posix.cc \
    third_party/abseil-cpp/absl/time/internal/cctz/src/zone_info_source.cc \
    third_party/abseil-cpp/absl/time/time.cc \
    third_party/abseil-cpp/absl/types/bad_optional_access.cc \
    third_party/abseil-cpp/absl/types/bad_variant_access.cc \
    third_party/address_sorting/address_sorting.c \
    third_party/address_sorting/address_sorting_posix.c \
    third_party/address_sorting/address_sorting_windows.c \
    third_party/re2/re2/bitstate.cc \
    third_party/re2/re2/compile.cc \
    third_party/re2/re2/dfa.cc \
    third_party/re2/re2/filtered_re2.cc \
    third_party/re2/re2/mimics_pcre.cc \
    third_party/re2/re2/nfa.cc \
    third_party/re2/re2/onepass.cc \
    third_party/re2/re2/parse.cc \
    third_party/re2/re2/perl_groups.cc \
    third_party/re2/re2/prefilter.cc \
    third_party/re2/re2/prefilter_tree.cc \
    third_party/re2/re2/prog.cc \
    third_party/re2/re2/re2.cc \
    third_party/re2/re2/regexp.cc \
    third_party/re2/re2/set.cc \
    third_party/re2/re2/simplify.cc \
    third_party/re2/re2/stringpiece.cc \
    third_party/re2/re2/tostring.cc \
    third_party/re2/re2/unicode_casefold.cc \
    third_party/re2/re2/unicode_groups.cc \
    third_party/re2/util/rune.cc \
    third_party/re2/util/strutil.cc \
    third_party/upb/upb/base/status.c \
    third_party/upb/upb/hash/common.c \
    third_party/upb/upb/json/decode.c \
    third_party/upb/upb/json/encode.c \
    third_party/upb/upb/lex/atoi.c \
    third_party/upb/upb/lex/round_trip.c \
    third_party/upb/upb/lex/strtod.c \
    third_party/upb/upb/lex/unicode.c \
    third_party/upb/upb/mem/alloc.c \
    third_party/upb/upb/mem/arena.c \
    third_party/upb/upb/message/accessors.c \
    third_party/upb/upb/message/array.c \
    third_party/upb/upb/message/compat.c \
    third_party/upb/upb/message/copy.c \
    third_party/upb/upb/message/internal/extension.c \
    third_party/upb/upb/message/internal/iterator.c \
    third_party/upb/upb/message/internal/message.c \
    third_party/upb/upb/message/map.c \
    third_party/upb/upb/message/map_sorter.c \
    third_party/upb/upb/message/merge.c \
    third_party/upb/upb/message/message.c \
    third_party/upb/upb/mini_descriptor/build_enum.c \
    third_party/upb/upb/mini_descriptor/decode.c \
    third_party/upb/upb/mini_descriptor/internal/base92.c \
    third_party/upb/upb/mini_descriptor/internal/encode.c \
    third_party/upb/upb/mini_descriptor/link.c \
    third_party/upb/upb/mini_table/extension_registry.c \
    third_party/upb/upb/mini_table/internal/message.c \
    third_party/upb/upb/mini_table/message.c \
    third_party/upb/upb/reflection/def_pool.c \
    third_party/upb/upb/reflection/def_type.c \
    third_party/upb/upb/reflection/desc_state.c \
    third_party/upb/upb/reflection/enum_def.c \
    third_party/upb/upb/reflection/enum_reserved_range.c \
    third_party/upb/upb/reflection/enum_value_def.c \
    third_party/upb/upb/reflection/extension_range.c \
    third_party/upb/upb/reflection/field_def.c \
    third_party/upb/upb/reflection/file_def.c \
    third_party/upb/upb/reflection/internal/def_builder.c \
    third_party/upb/upb/reflection/internal/strdup2.c \
    third_party/upb/upb/reflection/message.c \
    third_party/upb/upb/reflection/message_def.c \
    third_party/upb/upb/reflection/message_reserved_range.c \
    third_party/upb/upb/reflection/method_def.c \
    third_party/upb/upb/reflection/oneof_def.c \
    third_party/upb/upb/reflection/service_def.c \
    third_party/upb/upb/text/encode.c \
    third_party/upb/upb/text/internal/encode.c \
    third_party/upb/upb/wire/decode.c \
    third_party/upb/upb/wire/encode.c \
    third_party/upb/upb/wire/eps_copy_input_stream.c \
    third_party/upb/upb/wire/internal/decode_fast.c \
    third_party/upb/upb/wire/reader.c \
    third_party/utf8_range/utf8_range.c \

PUBLIC_HEADERS_C += \
    include/grpc/byte_buffer.h \
    include/grpc/byte_buffer_reader.h \
    include/grpc/census.h \
    include/grpc/compression.h \
    include/grpc/credentials.h \
    include/grpc/event_engine/endpoint_config.h \
    include/grpc/event_engine/event_engine.h \
    include/grpc/event_engine/extensible.h \
    include/grpc/event_engine/internal/memory_allocator_impl.h \
    include/grpc/event_engine/internal/slice_cast.h \
    include/grpc/event_engine/memory_allocator.h \
    include/grpc/event_engine/memory_request.h \
    include/grpc/event_engine/port.h \
    include/grpc/event_engine/slice.h \
    include/grpc/event_engine/slice_buffer.h \
    include/grpc/fork.h \
    include/grpc/grpc.h \
    include/grpc/grpc_audit_logging.h \
    include/grpc/grpc_crl_provider.h \
    include/grpc/grpc_posix.h \
    include/grpc/grpc_security.h \
    include/grpc/grpc_security_constants.h \
    include/grpc/impl/channel_arg_names.h \
    include/grpc/impl/codegen/byte_buffer.h \
    include/grpc/impl/codegen/byte_buffer_reader.h \
    include/grpc/impl/codegen/compression_types.h \
    include/grpc/impl/codegen/connectivity_state.h \
    include/grpc/impl/codegen/grpc_types.h \
    include/grpc/impl/codegen/propagation_bits.h \
    include/grpc/impl/codegen/slice.h \
    include/grpc/impl/codegen/status.h \
    include/grpc/impl/compression_types.h \
    include/grpc/impl/connectivity_state.h \
    include/grpc/impl/grpc_types.h \
    include/grpc/impl/propagation_bits.h \
    include/grpc/impl/slice_type.h \
    include/grpc/load_reporting.h \
    include/grpc/passive_listener.h \
    include/grpc/slice.h \
    include/grpc/slice_buffer.h \
    include/grpc/status.h \
    include/grpc/support/workaround_list.h \

LIBGRPC_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBGRPC_SRC))))


ifeq ($(NO_SECURE),true)

# You can't build secure libraries if you don't have OpenSSL.

$(LIBDIR)/$(CONFIG)/libgrpc.a: openssl_dep_error

$(LIBDIR)/$(CONFIG)/$(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): openssl_dep_error

else

# static library for "grpc"
$(LIBDIR)/$(CONFIG)/libgrpc.a: $(LIBDIR)/$(CONFIG)/libcares.a $(OPENSSL_DEP) $(ZLIB_DEP) $(LIBGRPC_OBJS) $(LIBCARES_OBJS) $(OPENSSL_MERGE_OBJS) $(ZLIB_MERGE_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libgrpc.a
	$(Q) $(AR) $(ARFLAGS) $(LIBDIR)/$(CONFIG)/libgrpc.a $(LIBGRPC_OBJS) $(LIBCARES_OBJS) $(OPENSSL_MERGE_OBJS) $(ZLIB_MERGE_OBJS)
ifeq ($(SYSTEM),Darwin)
	$(Q) $(RANLIB) $(RANLIBFLAGS) $(LIBDIR)/$(CONFIG)/libgrpc.a
endif

# shared library for "grpc"
ifeq ($(SYSTEM),MINGW32)
$(LIBDIR)/$(CONFIG)/grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): $(LIBGRPC_OBJS) $(LIBDIR)/$(CONFIG)/libcares.a $(OPENSSL_DEP) $(ZLIB_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,--output-def=$(LIBDIR)/$(CONFIG)/grpc$(SHARED_VERSION_CORE).def -Wl,--out-implib=$(LIBDIR)/$(CONFIG)/libgrpc$(SHARED_VERSION_CORE)-dll.a -o $(LIBDIR)/$(CONFIG)/grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGRPC_OBJS) $(LIBDIR)/$(CONFIG)/libcares.a $(OPENSSL_MERGE_LIBS) $(ZLIB_MERGE_LIBS) $(LDLIBS_SECURE) $(LDLIBS)
else
$(LIBDIR)/$(CONFIG)/libgrpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE): $(LIBGRPC_OBJS) $(LIBDIR)/$(CONFIG)/libcares.a $(OPENSSL_DEP) $(ZLIB_DEP)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
ifeq ($(SYSTEM),Darwin)
	$(Q) $(LDXX) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -install_name $(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) -dynamiclib -o $(LIBDIR)/$(CONFIG)/libgrpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGRPC_OBJS) $(LIBDIR)/$(CONFIG)/libcares.a $(OPENSSL_MERGE_LIBS) $(ZLIB_MERGE_LIBS) $(LDLIBS_SECURE) $(LDLIBS)
else
	$(Q) $(LDXX) $(LDFLAGS) -L$(LIBDIR)/$(CONFIG) -shared -Wl,-soname,libgrpc.so.46 -o $(LIBDIR)/$(CONFIG)/libgrpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBGRPC_OBJS) $(LIBDIR)/$(CONFIG)/libcares.a $(OPENSSL_MERGE_LIBS) $(ZLIB_MERGE_LIBS) $(LDLIBS_SECURE) $(LDLIBS)
	$(Q) ln -sf $(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/libgrpc$(SHARED_VERSION_CORE).so.46
	$(Q) ln -sf $(SHARED_PREFIX)grpc$(SHARED_VERSION_CORE).$(SHARED_EXT_CORE) $(LIBDIR)/$(CONFIG)/libgrpc$(SHARED_VERSION_CORE).so
endif
endif

endif  # corresponds to the "ifeq ($(NO_SECURE),true)" above

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBGRPC_OBJS:.o=.dep)
endif
endif
# end of build recipe for library "grpc"


# start of build recipe for library "boringssl" (generated by makelib(lib) template function)
# deps: []
# transitive_deps: []
LIBBORINGSSL_SRC = \
    third_party/boringssl-with-bazel/src/crypto/asn1/a_bitstr.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/a_bool.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/a_d2i_fp.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/a_dup.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/a_gentm.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/a_i2d_fp.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/a_int.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/a_mbstr.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/a_object.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/a_octet.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/a_strex.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/a_strnid.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/a_time.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/a_type.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/a_utctm.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/asn1_lib.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/asn1_par.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/asn_pack.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/f_int.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/f_string.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/posix_time.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/tasn_dec.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/tasn_enc.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/tasn_fre.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/tasn_new.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/tasn_typ.cc \
    third_party/boringssl-with-bazel/src/crypto/asn1/tasn_utl.cc \
    third_party/boringssl-with-bazel/src/crypto/base64/base64.cc \
    third_party/boringssl-with-bazel/src/crypto/bio/bio.cc \
    third_party/boringssl-with-bazel/src/crypto/bio/bio_mem.cc \
    third_party/boringssl-with-bazel/src/crypto/bio/connect.cc \
    third_party/boringssl-with-bazel/src/crypto/bio/errno.cc \
    third_party/boringssl-with-bazel/src/crypto/bio/fd.cc \
    third_party/boringssl-with-bazel/src/crypto/bio/file.cc \
    third_party/boringssl-with-bazel/src/crypto/bio/hexdump.cc \
    third_party/boringssl-with-bazel/src/crypto/bio/pair.cc \
    third_party/boringssl-with-bazel/src/crypto/bio/printf.cc \
    third_party/boringssl-with-bazel/src/crypto/bio/socket.cc \
    third_party/boringssl-with-bazel/src/crypto/bio/socket_helper.cc \
    third_party/boringssl-with-bazel/src/crypto/blake2/blake2.cc \
    third_party/boringssl-with-bazel/src/crypto/bn/bn_asn1.cc \
    third_party/boringssl-with-bazel/src/crypto/bn/convert.cc \
    third_party/boringssl-with-bazel/src/crypto/buf/buf.cc \
    third_party/boringssl-with-bazel/src/crypto/bytestring/asn1_compat.cc \
    third_party/boringssl-with-bazel/src/crypto/bytestring/ber.cc \
    third_party/boringssl-with-bazel/src/crypto/bytestring/cbb.cc \
    third_party/boringssl-with-bazel/src/crypto/bytestring/cbs.cc \
    third_party/boringssl-with-bazel/src/crypto/bytestring/unicode.cc \
    third_party/boringssl-with-bazel/src/crypto/chacha/chacha.cc \
    third_party/boringssl-with-bazel/src/crypto/cipher/derive_key.cc \
    third_party/boringssl-with-bazel/src/crypto/cipher/e_aesctrhmac.cc \
    third_party/boringssl-with-bazel/src/crypto/cipher/e_aesgcmsiv.cc \
    third_party/boringssl-with-bazel/src/crypto/cipher/e_chacha20poly1305.cc \
    third_party/boringssl-with-bazel/src/crypto/cipher/e_des.cc \
    third_party/boringssl-with-bazel/src/crypto/cipher/e_null.cc \
    third_party/boringssl-with-bazel/src/crypto/cipher/e_rc2.cc \
    third_party/boringssl-with-bazel/src/crypto/cipher/e_rc4.cc \
    third_party/boringssl-with-bazel/src/crypto/cipher/e_tls.cc \
    third_party/boringssl-with-bazel/src/crypto/cipher/get_cipher.cc \
    third_party/boringssl-with-bazel/src/crypto/cipher/tls_cbc.cc \
    third_party/boringssl-with-bazel/src/crypto/conf/conf.cc \
    third_party/boringssl-with-bazel/src/crypto/cpu_aarch64_apple.cc \
    third_party/boringssl-with-bazel/src/crypto/cpu_aarch64_fuchsia.cc \
    third_party/boringssl-with-bazel/src/crypto/cpu_aarch64_linux.cc \
    third_party/boringssl-with-bazel/src/crypto/cpu_aarch64_openbsd.cc \
    third_party/boringssl-with-bazel/src/crypto/cpu_aarch64_sysreg.cc \
    third_party/boringssl-with-bazel/src/crypto/cpu_aarch64_win.cc \
    third_party/boringssl-with-bazel/src/crypto/cpu_arm_freebsd.cc \
    third_party/boringssl-with-bazel/src/crypto/cpu_arm_linux.cc \
    third_party/boringssl-with-bazel/src/crypto/cpu_intel.cc \
    third_party/boringssl-with-bazel/src/crypto/crypto.cc \
    third_party/boringssl-with-bazel/src/crypto/curve25519/curve25519.cc \
    third_party/boringssl-with-bazel/src/crypto/curve25519/curve25519_64_adx.cc \
    third_party/boringssl-with-bazel/src/crypto/curve25519/spake25519.cc \
    third_party/boringssl-with-bazel/src/crypto/des/des.cc \
    third_party/boringssl-with-bazel/src/crypto/dh/dh_asn1.cc \
    third_party/boringssl-with-bazel/src/crypto/dh/params.cc \
    third_party/boringssl-with-bazel/src/crypto/digest/digest_extra.cc \
    third_party/boringssl-with-bazel/src/crypto/dsa/dsa.cc \
    third_party/boringssl-with-bazel/src/crypto/dsa/dsa_asn1.cc \
    third_party/boringssl-with-bazel/src/crypto/ec/ec_asn1.cc \
    third_party/boringssl-with-bazel/src/crypto/ec/ec_derive.cc \
    third_party/boringssl-with-bazel/src/crypto/ec/hash_to_curve.cc \
    third_party/boringssl-with-bazel/src/crypto/ecdh/ecdh.cc \
    third_party/boringssl-with-bazel/src/crypto/ecdsa/ecdsa_asn1.cc \
    third_party/boringssl-with-bazel/src/crypto/engine/engine.cc \
    third_party/boringssl-with-bazel/src/crypto/err/err.cc \
    third_party/boringssl-with-bazel/src/crypto/evp/evp.cc \
    third_party/boringssl-with-bazel/src/crypto/evp/evp_asn1.cc \
    third_party/boringssl-with-bazel/src/crypto/evp/evp_ctx.cc \
    third_party/boringssl-with-bazel/src/crypto/evp/p_dh.cc \
    third_party/boringssl-with-bazel/src/crypto/evp/p_dh_asn1.cc \
    third_party/boringssl-with-bazel/src/crypto/evp/p_dsa_asn1.cc \
    third_party/boringssl-with-bazel/src/crypto/evp/p_ec.cc \
    third_party/boringssl-with-bazel/src/crypto/evp/p_ec_asn1.cc \
    third_party/boringssl-with-bazel/src/crypto/evp/p_ed25519.cc \
    third_party/boringssl-with-bazel/src/crypto/evp/p_ed25519_asn1.cc \
    third_party/boringssl-with-bazel/src/crypto/evp/p_hkdf.cc \
    third_party/boringssl-with-bazel/src/crypto/evp/p_rsa.cc \
    third_party/boringssl-with-bazel/src/crypto/evp/p_rsa_asn1.cc \
    third_party/boringssl-with-bazel/src/crypto/evp/p_x25519.cc \
    third_party/boringssl-with-bazel/src/crypto/evp/p_x25519_asn1.cc \
    third_party/boringssl-with-bazel/src/crypto/evp/pbkdf.cc \
    third_party/boringssl-with-bazel/src/crypto/evp/print.cc \
    third_party/boringssl-with-bazel/src/crypto/evp/scrypt.cc \
    third_party/boringssl-with-bazel/src/crypto/evp/sign.cc \
    third_party/boringssl-with-bazel/src/crypto/ex_data.cc \
    third_party/boringssl-with-bazel/src/crypto/fipsmodule/bcm.cc \
    third_party/boringssl-with-bazel/src/crypto/fipsmodule/fips_shared_support.cc \
    third_party/boringssl-with-bazel/src/crypto/hpke/hpke.cc \
    third_party/boringssl-with-bazel/src/crypto/hrss/hrss.cc \
    third_party/boringssl-with-bazel/src/crypto/kyber/kyber.cc \
    third_party/boringssl-with-bazel/src/crypto/lhash/lhash.cc \
    third_party/boringssl-with-bazel/src/crypto/md4/md4.cc \
    third_party/boringssl-with-bazel/src/crypto/md5/md5.cc \
    third_party/boringssl-with-bazel/src/crypto/mem.cc \
    third_party/boringssl-with-bazel/src/crypto/mldsa/mldsa.cc \
    third_party/boringssl-with-bazel/src/crypto/mlkem/mlkem.cc \
    third_party/boringssl-with-bazel/src/crypto/obj/obj.cc \
    third_party/boringssl-with-bazel/src/crypto/obj/obj_xref.cc \
    third_party/boringssl-with-bazel/src/crypto/pem/pem_all.cc \
    third_party/boringssl-with-bazel/src/crypto/pem/pem_info.cc \
    third_party/boringssl-with-bazel/src/crypto/pem/pem_lib.cc \
    third_party/boringssl-with-bazel/src/crypto/pem/pem_oth.cc \
    third_party/boringssl-with-bazel/src/crypto/pem/pem_pk8.cc \
    third_party/boringssl-with-bazel/src/crypto/pem/pem_pkey.cc \
    third_party/boringssl-with-bazel/src/crypto/pem/pem_x509.cc \
    third_party/boringssl-with-bazel/src/crypto/pem/pem_xaux.cc \
    third_party/boringssl-with-bazel/src/crypto/pkcs7/pkcs7.cc \
    third_party/boringssl-with-bazel/src/crypto/pkcs7/pkcs7_x509.cc \
    third_party/boringssl-with-bazel/src/crypto/pkcs8/p5_pbev2.cc \
    third_party/boringssl-with-bazel/src/crypto/pkcs8/pkcs8.cc \
    third_party/boringssl-with-bazel/src/crypto/pkcs8/pkcs8_x509.cc \
    third_party/boringssl-with-bazel/src/crypto/poly1305/poly1305.cc \
    third_party/boringssl-with-bazel/src/crypto/poly1305/poly1305_arm.cc \
    third_party/boringssl-with-bazel/src/crypto/poly1305/poly1305_vec.cc \
    third_party/boringssl-with-bazel/src/crypto/pool/pool.cc \
    third_party/boringssl-with-bazel/src/crypto/rand/deterministic.cc \
    third_party/boringssl-with-bazel/src/crypto/rand/fork_detect.cc \
    third_party/boringssl-with-bazel/src/crypto/rand/forkunsafe.cc \
    third_party/boringssl-with-bazel/src/crypto/rand/getentropy.cc \
    third_party/boringssl-with-bazel/src/crypto/rand/ios.cc \
    third_party/boringssl-with-bazel/src/crypto/rand/passive.cc \
    third_party/boringssl-with-bazel/src/crypto/rand/rand.cc \
    third_party/boringssl-with-bazel/src/crypto/rand/trusty.cc \
    third_party/boringssl-with-bazel/src/crypto/rand/urandom.cc \
    third_party/boringssl-with-bazel/src/crypto/rand/windows.cc \
    third_party/boringssl-with-bazel/src/crypto/rc4/rc4.cc \
    third_party/boringssl-with-bazel/src/crypto/refcount.cc \
    third_party/boringssl-with-bazel/src/crypto/rsa/rsa_asn1.cc \
    third_party/boringssl-with-bazel/src/crypto/rsa/rsa_crypt.cc \
    third_party/boringssl-with-bazel/src/crypto/rsa/rsa_extra.cc \
    third_party/boringssl-with-bazel/src/crypto/rsa/rsa_print.cc \
    third_party/boringssl-with-bazel/src/crypto/sha/sha1.cc \
    third_party/boringssl-with-bazel/src/crypto/sha/sha256.cc \
    third_party/boringssl-with-bazel/src/crypto/sha/sha512.cc \
    third_party/boringssl-with-bazel/src/crypto/siphash/siphash.cc \
    third_party/boringssl-with-bazel/src/crypto/slhdsa/slhdsa.cc \
    third_party/boringssl-with-bazel/src/crypto/spake2plus/spake2plus.cc \
    third_party/boringssl-with-bazel/src/crypto/stack/stack.cc \
    third_party/boringssl-with-bazel/src/crypto/thread.cc \
    third_party/boringssl-with-bazel/src/crypto/thread_none.cc \
    third_party/boringssl-with-bazel/src/crypto/thread_pthread.cc \
    third_party/boringssl-with-bazel/src/crypto/thread_win.cc \
    third_party/boringssl-with-bazel/src/crypto/trust_token/pmbtoken.cc \
    third_party/boringssl-with-bazel/src/crypto/trust_token/trust_token.cc \
    third_party/boringssl-with-bazel/src/crypto/trust_token/voprf.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/a_digest.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/a_sign.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/a_verify.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/algorithm.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/asn1_gen.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/by_dir.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/by_file.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/i2d_pr.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/name_print.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/policy.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/rsa_pss.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/t_crl.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/t_req.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/t_x509.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/t_x509a.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_akey.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_akeya.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_alt.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_bcons.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_bitst.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_conf.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_cpols.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_crld.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_enum.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_extku.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_genn.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_ia5.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_info.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_int.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_lib.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_ncons.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_ocsp.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_pcons.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_pmaps.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_prn.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_purp.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_skey.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/v3_utl.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x509.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x509_att.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x509_cmp.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x509_d2.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x509_def.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x509_ext.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x509_lu.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x509_obj.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x509_req.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x509_set.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x509_trs.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x509_txt.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x509_v3.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x509_vfy.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x509_vpm.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x509cset.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x509name.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x509rset.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x509spki.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x_algor.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x_all.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x_attrib.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x_crl.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x_exten.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x_name.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x_pubkey.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x_req.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x_sig.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x_spki.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x_val.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x_x509.cc \
    third_party/boringssl-with-bazel/src/crypto/x509/x_x509a.cc \
    third_party/boringssl-with-bazel/src/gen/crypto/err_data.cc \
    third_party/boringssl-with-bazel/src/ssl/bio_ssl.cc \
    third_party/boringssl-with-bazel/src/ssl/d1_both.cc \
    third_party/boringssl-with-bazel/src/ssl/d1_lib.cc \
    third_party/boringssl-with-bazel/src/ssl/d1_pkt.cc \
    third_party/boringssl-with-bazel/src/ssl/d1_srtp.cc \
    third_party/boringssl-with-bazel/src/ssl/dtls_method.cc \
    third_party/boringssl-with-bazel/src/ssl/dtls_record.cc \
    third_party/boringssl-with-bazel/src/ssl/encrypted_client_hello.cc \
    third_party/boringssl-with-bazel/src/ssl/extensions.cc \
    third_party/boringssl-with-bazel/src/ssl/handoff.cc \
    third_party/boringssl-with-bazel/src/ssl/handshake.cc \
    third_party/boringssl-with-bazel/src/ssl/handshake_client.cc \
    third_party/boringssl-with-bazel/src/ssl/handshake_server.cc \
    third_party/boringssl-with-bazel/src/ssl/s3_both.cc \
    third_party/boringssl-with-bazel/src/ssl/s3_lib.cc \
    third_party/boringssl-with-bazel/src/ssl/s3_pkt.cc \
    third_party/boringssl-with-bazel/src/ssl/ssl_aead_ctx.cc \
    third_party/boringssl-with-bazel/src/ssl/ssl_asn1.cc \
    third_party/boringssl-with-bazel/src/ssl/ssl_buffer.cc \
    third_party/boringssl-with-bazel/src/ssl/ssl_cert.cc \
    third_party/boringssl-with-bazel/src/ssl/ssl_cipher.cc \
    third_party/boringssl-with-bazel/src/ssl/ssl_credential.cc \
    third_party/boringssl-with-bazel/src/ssl/ssl_file.cc \
    third_party/boringssl-with-bazel/src/ssl/ssl_key_share.cc \
    third_party/boringssl-with-bazel/src/ssl/ssl_lib.cc \
    third_party/boringssl-with-bazel/src/ssl/ssl_privkey.cc \
    third_party/boringssl-with-bazel/src/ssl/ssl_session.cc \
    third_party/boringssl-with-bazel/src/ssl/ssl_stat.cc \
    third_party/boringssl-with-bazel/src/ssl/ssl_transcript.cc \
    third_party/boringssl-with-bazel/src/ssl/ssl_versions.cc \
    third_party/boringssl-with-bazel/src/ssl/ssl_x509.cc \
    third_party/boringssl-with-bazel/src/ssl/t1_enc.cc \
    third_party/boringssl-with-bazel/src/ssl/tls13_both.cc \
    third_party/boringssl-with-bazel/src/ssl/tls13_client.cc \
    third_party/boringssl-with-bazel/src/ssl/tls13_enc.cc \
    third_party/boringssl-with-bazel/src/ssl/tls13_server.cc \
    third_party/boringssl-with-bazel/src/ssl/tls_method.cc \
    third_party/boringssl-with-bazel/src/ssl/tls_record.cc \


LIBBORINGSSL_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBBORINGSSL_SRC))))

$(LIBBORINGSSL_OBJS): CFLAGS += -g
$(LIBBORINGSSL_OBJS): CPPFLAGS += -Ithird_party/boringssl-with-bazel/src/include -fvisibility=hidden -DOPENSSL_NO_ASM -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX
$(LIBBORINGSSL_OBJS): CXXFLAGS += -fno-exceptions

# static library for "boringssl"
$(LIBDIR)/$(CONFIG)/libboringssl.a: $(LIBBORINGSSL_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libboringssl.a
	$(Q) $(AR) $(ARFLAGS) $(LIBDIR)/$(CONFIG)/libboringssl.a $(LIBBORINGSSL_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) $(RANLIB) $(RANLIBFLAGS) $(LIBDIR)/$(CONFIG)/libboringssl.a
endif

# shared library for "boringssl"

ifneq ($(NO_DEPS),true)
-include $(LIBBORINGSSL_OBJS:.o=.dep)
endif
# end of build recipe for library "boringssl"


# start of build recipe for library "cares" (generated by makelib(lib) template function)
# deps: []
# transitive_deps: []
LIBCARES_SRC = \
    third_party/cares/cares/src/lib/ares__addrinfo2hostent.c \
    third_party/cares/cares/src/lib/ares__addrinfo_localhost.c \
    third_party/cares/cares/src/lib/ares__close_sockets.c \
    third_party/cares/cares/src/lib/ares__get_hostent.c \
    third_party/cares/cares/src/lib/ares__parse_into_addrinfo.c \
    third_party/cares/cares/src/lib/ares__read_line.c \
    third_party/cares/cares/src/lib/ares__readaddrinfo.c \
    third_party/cares/cares/src/lib/ares__sortaddrinfo.c \
    third_party/cares/cares/src/lib/ares__timeval.c \
    third_party/cares/cares/src/lib/ares_android.c \
    third_party/cares/cares/src/lib/ares_cancel.c \
    third_party/cares/cares/src/lib/ares_create_query.c \
    third_party/cares/cares/src/lib/ares_data.c \
    third_party/cares/cares/src/lib/ares_destroy.c \
    third_party/cares/cares/src/lib/ares_expand_name.c \
    third_party/cares/cares/src/lib/ares_expand_string.c \
    third_party/cares/cares/src/lib/ares_fds.c \
    third_party/cares/cares/src/lib/ares_free_hostent.c \
    third_party/cares/cares/src/lib/ares_free_string.c \
    third_party/cares/cares/src/lib/ares_freeaddrinfo.c \
    third_party/cares/cares/src/lib/ares_getaddrinfo.c \
    third_party/cares/cares/src/lib/ares_getenv.c \
    third_party/cares/cares/src/lib/ares_gethostbyaddr.c \
    third_party/cares/cares/src/lib/ares_gethostbyname.c \
    third_party/cares/cares/src/lib/ares_getnameinfo.c \
    third_party/cares/cares/src/lib/ares_getsock.c \
    third_party/cares/cares/src/lib/ares_init.c \
    third_party/cares/cares/src/lib/ares_library_init.c \
    third_party/cares/cares/src/lib/ares_llist.c \
    third_party/cares/cares/src/lib/ares_mkquery.c \
    third_party/cares/cares/src/lib/ares_nowarn.c \
    third_party/cares/cares/src/lib/ares_options.c \
    third_party/cares/cares/src/lib/ares_parse_a_reply.c \
    third_party/cares/cares/src/lib/ares_parse_aaaa_reply.c \
    third_party/cares/cares/src/lib/ares_parse_caa_reply.c \
    third_party/cares/cares/src/lib/ares_parse_mx_reply.c \
    third_party/cares/cares/src/lib/ares_parse_naptr_reply.c \
    third_party/cares/cares/src/lib/ares_parse_ns_reply.c \
    third_party/cares/cares/src/lib/ares_parse_ptr_reply.c \
    third_party/cares/cares/src/lib/ares_parse_soa_reply.c \
    third_party/cares/cares/src/lib/ares_parse_srv_reply.c \
    third_party/cares/cares/src/lib/ares_parse_txt_reply.c \
    third_party/cares/cares/src/lib/ares_parse_uri_reply.c \
    third_party/cares/cares/src/lib/ares_platform.c \
    third_party/cares/cares/src/lib/ares_process.c \
    third_party/cares/cares/src/lib/ares_query.c \
    third_party/cares/cares/src/lib/ares_rand.c \
    third_party/cares/cares/src/lib/ares_search.c \
    third_party/cares/cares/src/lib/ares_send.c \
    third_party/cares/cares/src/lib/ares_strcasecmp.c \
    third_party/cares/cares/src/lib/ares_strdup.c \
    third_party/cares/cares/src/lib/ares_strerror.c \
    third_party/cares/cares/src/lib/ares_strsplit.c \
    third_party/cares/cares/src/lib/ares_timeout.c \
    third_party/cares/cares/src/lib/ares_version.c \
    third_party/cares/cares/src/lib/ares_writev.c \
    third_party/cares/cares/src/lib/bitncmp.c \
    third_party/cares/cares/src/lib/inet_net_pton.c \
    third_party/cares/cares/src/lib/inet_ntop.c \
    third_party/cares/cares/src/lib/windows_port.c \


LIBCARES_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBCARES_SRC))))

$(LIBCARES_OBJS): CFLAGS += -g
$(LIBCARES_OBJS): CPPFLAGS += -Ithird_party/cares/cares/include -Ithird_party/cares -Ithird_party/cares/cares -fvisibility=hidden -D_GNU_SOURCE $(if $(subst Darwin,,$(SYSTEM)),,-Ithird_party/cares/config_darwin) $(if $(subst FreeBSD,,$(SYSTEM)),,-Ithird_party/cares/config_freebsd) $(if $(subst Linux,,$(SYSTEM)),,-Ithird_party/cares/config_linux) $(if $(subst OpenBSD,,$(SYSTEM)),,-Ithird_party/cares/config_openbsd) -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX $(if $(subst MINGW32,,$(SYSTEM)),-DHAVE_CONFIG_H,)

# static library for "cares"
$(LIBDIR)/$(CONFIG)/libcares.a: $(LIBCARES_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libcares.a
	$(Q) $(AR) $(ARFLAGS) $(LIBDIR)/$(CONFIG)/libcares.a $(LIBCARES_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) $(RANLIB) $(RANLIBFLAGS) $(LIBDIR)/$(CONFIG)/libcares.a
endif

# shared library for "cares"

ifneq ($(NO_DEPS),true)
-include $(LIBCARES_OBJS:.o=.dep)
endif
# end of build recipe for library "cares"


# start of build recipe for library "z" (generated by makelib(lib) template function)
# deps: []
# transitive_deps: []
LIBZ_SRC = \
    third_party/zlib/adler32.c \
    third_party/zlib/compress.c \
    third_party/zlib/crc32.c \
    third_party/zlib/deflate.c \
    third_party/zlib/infback.c \
    third_party/zlib/inffast.c \
    third_party/zlib/inflate.c \
    third_party/zlib/inftrees.c \
    third_party/zlib/trees.c \
    third_party/zlib/uncompr.c \
    third_party/zlib/zutil.c \

PUBLIC_HEADERS_C += \

LIBZ_OBJS = $(addprefix $(OBJDIR)/$(CONFIG)/, $(addsuffix .o, $(basename $(LIBZ_SRC))))

$(LIBZ_OBJS): CFLAGS += -fvisibility=hidden
$(LIBZ_OBJS): CPPFLAGS += -DHAVE_UNISTD_H

# static library for "z"
$(LIBDIR)/$(CONFIG)/libz.a: $(ZLIB_MERGE_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) rm -f $(LIBDIR)/$(CONFIG)/libz.a
	$(Q) $(AR) $(ARFLAGS) $(LIBDIR)/$(CONFIG)/libz.a $(LIBZ_OBJS) 
ifeq ($(SYSTEM),Darwin)
	$(Q) $(RANLIB) $(RANLIBFLAGS) $(LIBDIR)/$(CONFIG)/libz.a
endif

# shared library for "z"

ifneq ($(NO_DEPS),true)
-include $(LIBZ_OBJS:.o=.dep)
endif
# end of build recipe for library "z"




.PHONY: all strip tools dep_error openssl_dep_error openssl_dep_message git_update stop buildtests buildtests_c buildtests_cxx test test_c test_cxx install install_c install_cxx install-static install-certs strip strip-shared strip-static strip_c strip-shared_c strip-static_c strip_cxx strip-shared_cxx strip-static_cxx dep_c dep_cxx bins_dep_c bins_dep_cxx clean

.PHONY: printvars
printvars:
	@$(foreach V,$(sort $(.VARIABLES)),                 	  $(if $(filter-out environment% default automatic, 	  $(origin $V)),$(warning $V=$($V) ($(value $V)))))
