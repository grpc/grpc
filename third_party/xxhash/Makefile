# ################################################################
# xxHash Makefile
# Copyright (C) 2012-2020 Yann Collet
#
# GPL v2 License
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#
# You can contact the author at:
#   - xxHash homepage: https://www.xxhash.com
#   - xxHash source repository: https://github.com/Cyan4973/xxHash
# ################################################################
# xxhsum: provides 32/64 bits hash of one or multiple files, or stdin
# ################################################################
Q = $(if $(filter 1,$(V) $(VERBOSE)),,@)

# Version numbers
SED ?= sed
SED_ERE_OPT ?= -E
LIBVER_MAJOR_SCRIPT:=`$(SED) -n '/define XXH_VERSION_MAJOR/s/.*[[:blank:]]\([0-9][0-9]*\).*/\1/p' < xxhash.h`
LIBVER_MINOR_SCRIPT:=`$(SED) -n '/define XXH_VERSION_MINOR/s/.*[[:blank:]]\([0-9][0-9]*\).*/\1/p' < xxhash.h`
LIBVER_PATCH_SCRIPT:=`$(SED) -n '/define XXH_VERSION_RELEASE/s/.*[[:blank:]]\([0-9][0-9]*\).*/\1/p' < xxhash.h`
LIBVER_MAJOR := $(shell echo $(LIBVER_MAJOR_SCRIPT))
LIBVER_MINOR := $(shell echo $(LIBVER_MINOR_SCRIPT))
LIBVER_PATCH := $(shell echo $(LIBVER_PATCH_SCRIPT))
LIBVER := $(LIBVER_MAJOR).$(LIBVER_MINOR).$(LIBVER_PATCH)

CFLAGS ?= -O3
DEBUGFLAGS+=-Wall -Wextra -Wconversion -Wcast-qual -Wcast-align -Wshadow \
            -Wstrict-aliasing=1 -Wswitch-enum -Wdeclaration-after-statement \
            -Wstrict-prototypes -Wundef -Wpointer-arith -Wformat-security \
            -Wvla -Wformat=2 -Winit-self -Wfloat-equal -Wwrite-strings \
            -Wredundant-decls -Wstrict-overflow=2
CFLAGS += $(DEBUGFLAGS) $(MOREFLAGS)
FLAGS   = $(CFLAGS) $(CPPFLAGS)
XXHSUM_VERSION = $(LIBVER)
UNAME := $(shell uname)

# Define *.exe as extension for Windows systems
ifneq (,$(filter Windows%,$(OS)))
EXT =.exe
else
EXT =
endif

# OS X linker doesn't support -soname, and use different extension
# see: https://developer.apple.com/library/mac/documentation/DeveloperTools/Conceptual/DynamicLibraries/100-Articles/DynamicLibraryDesignGuidelines.html
ifeq ($(UNAME), Darwin)
	SHARED_EXT = dylib
	SHARED_EXT_MAJOR = $(LIBVER_MAJOR).$(SHARED_EXT)
	SHARED_EXT_VER = $(LIBVER).$(SHARED_EXT)
	SONAME_FLAGS = -install_name $(LIBDIR)/libxxhash.$(SHARED_EXT_MAJOR) -compatibility_version $(LIBVER_MAJOR) -current_version $(LIBVER)
else
	SONAME_FLAGS = -Wl,-soname=libxxhash.$(SHARED_EXT).$(LIBVER_MAJOR)
	SHARED_EXT = so
	SHARED_EXT_MAJOR = $(SHARED_EXT).$(LIBVER_MAJOR)
	SHARED_EXT_VER = $(SHARED_EXT).$(LIBVER)
endif

LIBXXH = libxxhash.$(SHARED_EXT_VER)

XXHSUM_SRC_DIR = cli
XXHSUM_SPLIT_SRCS = $(XXHSUM_SRC_DIR)/xsum_os_specific.c \
                    $(XXHSUM_SRC_DIR)/xsum_output.c \
                    $(XXHSUM_SRC_DIR)/xsum_sanity_check.c
XXHSUM_SPLIT_OBJS = $(XXHSUM_SPLIT_SRCS:.c=.o)
XXHSUM_HEADERS = $(XXHSUM_SRC_DIR)/xsum_config.h \
                 $(XXHSUM_SRC_DIR)/xsum_arch.h \
                 $(XXHSUM_SRC_DIR)/xsum_os_specific.h \
                 $(XXHSUM_SRC_DIR)/xsum_output.h \
                 $(XXHSUM_SRC_DIR)/xsum_sanity_check.h

## generate CLI and libraries in release mode (default for `make`)
.PHONY: default
default: DEBUGFLAGS=
default: lib xxhsum_and_links

.PHONY: all
all: lib xxhsum xxhsum_inlinedXXH

## xxhsum is the command line interface (CLI)
ifeq ($(DISPATCH),1)
xxhsum: CPPFLAGS += -DXXHSUM_DISPATCH=1
xxhsum: xxh_x86dispatch.o
endif
xxhsum: xxhash.o xxhsum.o $(XXHSUM_SPLIT_OBJS)
	$(CC) $(FLAGS) $^ $(LDFLAGS) -o $@$(EXT)

xxhsum32: CFLAGS += -m32  ## generate CLI in 32-bits mode
xxhsum32: xxhash.c xxhsum.c $(XXHSUM_SPLIT_SRCS) ## do not generate object (avoid mixing different ABI)
	$(CC) $(FLAGS) $^ $(LDFLAGS) -o $@$(EXT)

## dispatch only works for x86/x64 systems
dispatch: CPPFLAGS += -DXXHSUM_DISPATCH=1
dispatch: xxhash.o xxh_x86dispatch.o xxhsum.c $(XXHSUM_SPLIT_SRCS)
	$(CC) $(FLAGS) $^ $(LDFLAGS) -o $@$(EXT)

xxhash.o: xxhash.c xxhash.h
xxhsum.o: xxhsum.c $(XXHSUM_HEADERS) \
    xxhash.h xxh_x86dispatch.h
xxh_x86dispatch.o: xxh_x86dispatch.c xxh_x86dispatch.h xxhash.h

.PHONY: xxhsum_and_links
xxhsum_and_links: xxhsum xxh32sum xxh64sum xxh128sum

xxh32sum xxh64sum xxh128sum: xxhsum
	ln -sf $<$(EXT) $@$(EXT)

xxhsum_inlinedXXH: CPPFLAGS += -DXXH_INLINE_ALL
xxhsum_inlinedXXH: xxhsum.c $(XXHSUM_SPLIT_SRCS)
	$(CC) $(FLAGS) $< -o $@$(EXT)


# library

libxxhash.a: ARFLAGS = rcs
libxxhash.a: xxhash.o
	$(AR) $(ARFLAGS) $@ $^

$(LIBXXH): LDFLAGS += -shared
ifeq (,$(filter Windows%,$(OS)))
$(LIBXXH): CFLAGS += -fPIC
endif
ifeq ($(DISPATCH),1)
$(LIBXXH): xxh_x86dispatch.c
endif
$(LIBXXH): xxhash.c
	$(CC) $(FLAGS) $^ $(LDFLAGS) $(SONAME_FLAGS) -o $@
	ln -sf $@ libxxhash.$(SHARED_EXT_MAJOR)
	ln -sf $@ libxxhash.$(SHARED_EXT)

.PHONY: libxxhash
libxxhash:  ## generate dynamic xxhash library
libxxhash: $(LIBXXH)

.PHONY: lib
lib:  ## generate static and dynamic xxhash libraries
lib: libxxhash.a libxxhash

# helper targets

AWK = awk
GREP = grep
SORT = sort

.PHONY: list
list:  ## list all Makefile targets
	$(Q)$(MAKE) -pRrq -f $(lastword $(MAKEFILE_LIST)) : 2>/dev/null | $(AWK) -v RS= -F: '/^# File/,/^# Finished Make data base/ {if ($$1 !~ "^[#.]") {print $$1}}' | $(SORT) | egrep -v -e '^[^[:alnum:]]' -e '^$@$$' | xargs

.PHONY: help
help:  ## list documented targets
	$(Q)$(GREP) -E '^[0-9a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
	$(SORT) | \
	$(AWK) 'BEGIN {FS = ":.*?## "}; {printf "\033[36m%-30s\033[0m %s\n", $$1, $$2}'

.PHONY: clean
clean:  ## remove all build artifacts
	$(Q)$(RM) -r *.dSYM   # Mac OS-X specific
	$(Q)$(RM) core *.o *.obj *.$(SHARED_EXT) *.$(SHARED_EXT).* *.a libxxhash.pc
	$(Q)$(RM) xxhsum$(EXT) xxhsum32$(EXT) xxhsum_inlinedXXH$(EXT) dispatch$(EXT)
	$(Q)$(RM) xxh32sum$(EXT) xxh64sum$(EXT) xxh128sum$(EXT)
	$(Q)$(RM) $(XXHSUM_SRC_DIR)/*.o $(XXHSUM_SRC_DIR)/*.obj
	@echo cleaning completed


# =================================================
# tests
# =================================================

# make check can be run with cross-compiled binaries on emulated environments (qemu user mode)
# by setting $(RUN_ENV) to the target emulation environment
.PHONY: check
check: xxhsum   ## basic tests for xxhsum CLI, set RUN_ENV for emulated environments
	# stdin
	$(RUN_ENV) ./xxhsum$(EXT) < xxhash.c
	# multiple files
	$(RUN_ENV) ./xxhsum$(EXT) xxhash.* xxhsum.*
	# internal bench
	$(RUN_ENV) ./xxhsum$(EXT) -bi0
	# long bench command
	$(RUN_ENV) ./xxhsum$(EXT) --benchmark-all -i0
	# bench multiple variants
	$(RUN_ENV) ./xxhsum$(EXT) -b1,2,3 -i0
	# file bench
	$(RUN_ENV) ./xxhsum$(EXT) -bi0 xxhash.c
	# 32-bit
	$(RUN_ENV) ./xxhsum$(EXT) -H0 xxhash.c
	# 128-bit
	$(RUN_ENV) ./xxhsum$(EXT) -H2 xxhash.c
	# request incorrect variant
	$(RUN_ENV) ./xxhsum$(EXT) -H9 xxhash.c ; test $$? -eq 1
	@printf "\n .......   checks completed successfully   ....... \n"

.PHONY: test-unicode
test-unicode:
	$(MAKE) -C tests test_unicode

.PHONY: test-mem
VALGRIND = valgrind --leak-check=yes --error-exitcode=1
test-mem: RUN_ENV = $(VALGRIND)
test-mem: xxhsum check

.PHONY: test32
test32: clean xxhsum32
	@echo ---- test 32-bit ----
	./xxhsum32 -bi1 xxhash.c

.PHONY: test-xxhsum-c
test-xxhsum-c: xxhsum
	# xxhsum to/from pipe
	./xxhsum xxh* | ./xxhsum -c -
	./xxhsum -H0 xxh* | ./xxhsum -c -
	# xxhsum -q does not display "Loading" message into stderr (#251)
	! ./xxhsum -q xxh* 2>&1 | grep Loading
	# xxhsum does not display "Loading" message into stderr either
	! ./xxhsum xxh* 2>&1 | grep Loading
	# Check that xxhsum do display filename that it failed to open.
	LC_ALL=C ./xxhsum nonexistent 2>&1 | grep "Error: Could not open 'nonexistent'"
	# xxhsum to/from file, shell redirection
	./xxhsum xxh* > .test.xxh64
	./xxhsum --tag xxh* > .test.xxh64_tag
	./xxhsum --little-endian xxh* > .test.le_xxh64
	./xxhsum --tag --little-endian xxh* > .test.le_xxh64_tag
	./xxhsum -H0 xxh* > .test.xxh32
	./xxhsum -H0 --tag xxh* > .test.xxh32_tag
	./xxhsum -H0 --little-endian xxh* > .test.le_xxh32
	./xxhsum -H0 --tag --little-endian xxh* > .test.le_xxh32_tag
	./xxhsum -H2 xxh* > .test.xxh128
	./xxhsum -H2 --tag xxh* > .test.xxh128_tag
	./xxhsum -H2 --little-endian xxh* > .test.le_xxh128
	./xxhsum -H2 --tag --little-endian xxh* > .test.le_xxh128_tag
	./xxhsum -c .test.xxh*
	./xxhsum -c --little-endian .test.le_xxh*
	./xxhsum -c .test.*_tag
	# read list of files from stdin
	./xxhsum -c < .test.xxh64
	./xxhsum -c < .test.xxh32
	cat .test.xxh* | ./xxhsum -c -
	# check variant with '*' marker as second separator
	$(SED) 's/  / \*/' .test.xxh32 | ./xxhsum -c
	# bsd-style output
	./xxhsum --tag xxhsum* | $(GREP) XXH64
	./xxhsum --tag -H0 xxhsum* | $(GREP) XXH32
	./xxhsum --tag -H1 xxhsum* | $(GREP) XXH64
	./xxhsum --tag -H2 xxhsum* | $(GREP) XXH128
	./xxhsum --tag -H32 xxhsum* | $(GREP) XXH32
	./xxhsum --tag -H64 xxhsum* | $(GREP) XXH64
	./xxhsum --tag -H128 xxhsum* | $(GREP) XXH128
	./xxhsum --tag -H0 --little-endian xxhsum* | $(GREP) XXH32_LE
	./xxhsum --tag -H1 --little-endian xxhsum* | $(GREP) XXH64_LE
	./xxhsum --tag -H2 --little-endian xxhsum* | $(GREP) XXH128_LE
	./xxhsum --tag -H32 --little-endian xxhsum* | $(GREP) XXH32_LE
	./xxhsum --tag -H64 --little-endian xxhsum* | $(GREP) XXH64_LE
	./xxhsum --tag -H128 --little-endian xxhsum* | $(GREP) XXH128_LE
	# check bsd-style
	./xxhsum --tag xxhsum* | ./xxhsum -c
	./xxhsum --tag -H32 --little-endian xxhsum* | ./xxhsum -c
	# xxhsum -c warns improperly format lines.
	echo '12345678 ' >>.test.xxh32
	./xxhsum -c .test.xxh32 | $(GREP) improperly
	echo '123456789  file' >>.test.xxh64
	./xxhsum -c .test.xxh64 | $(GREP) improperly
	# Expects "FAILED"
	echo "0000000000000000  LICENSE" | ./xxhsum -c -; test $$? -eq 1
	echo "00000000  LICENSE" | ./xxhsum -c -; test $$? -eq 1
	# Expects "FAILED open or read"
	echo "0000000000000000  test-expects-file-not-found" | ./xxhsum -c -; test $$? -eq 1
	echo "00000000  test-expects-file-not-found" | ./xxhsum -c -; test $$? -eq 1
	@$(RM) .test.*

.PHONY: armtest
armtest: clean
	@echo ---- test ARM compilation ----
	CC=arm-linux-gnueabi-gcc MOREFLAGS="-Werror -static" $(MAKE) xxhsum

.PHONY: clangtest
clangtest: clean
	@echo ---- test clang compilation ----
	CC=clang MOREFLAGS="-Werror -Wconversion -Wno-sign-conversion" $(MAKE) all

.PHONY: cxxtest
cxxtest: clean
	@echo ---- test C++ compilation ----
	CC="$(CXX) -Wno-deprecated" $(MAKE) all CFLAGS="-O3 -Wall -Wextra -Wundef -Wshadow -Wcast-align -Werror -fPIC"

.PHONY: c90test
ifeq ($(NO_C90_TEST),true)
c90test:
	@echo no c90 compatibility test
else
c90test: CPPFLAGS += -DXXH_NO_LONG_LONG
c90test: CFLAGS += -std=c90 -Werror -pedantic
c90test: xxhash.c
	@echo ---- test strict C90 compilation [xxh32 only] ----
	$(RM) xxhash.o
	$(CC) $(FLAGS) $^ $(LDFLAGS) -c
	$(RM) xxhash.o
endif

.PHONY: usan
usan: CC=clang
usan: CXX=clang++
usan:  ## check CLI runtime for undefined behavior, using clang's sanitizer
	@echo ---- check undefined behavior - sanitize ----
	$(MAKE) clean
	$(MAKE) test CC=$(CC) CXX=$(CXX) MOREFLAGS="-g -fsanitize=undefined -fno-sanitize-recover=all"

.PHONY: staticAnalyze
SCANBUILD ?= scan-build
staticAnalyze: clean  ## check C source files using $(SCANBUILD) static analyzer
	@echo ---- static analyzer - $(SCANBUILD) ----
	CFLAGS="-g -Werror" $(SCANBUILD) --status-bugs -v $(MAKE) all

CPPCHECK ?= cppcheck
.PHONY: cppcheck
cppcheck:  ## check C source files using $(CPPCHECK) static analyzer
	@echo ---- static analyzer - $(CPPCHECK) ----
	$(CPPCHECK) . --force --enable=warning,portability,performance,style --error-exitcode=1 > /dev/null

.PHONY: namespaceTest
namespaceTest:  ## ensure XXH_NAMESPACE redefines all public symbols
	$(CC) -c xxhash.c
	$(CC) -DXXH_NAMESPACE=TEST_ -c xxhash.c -o xxhash2.o
	$(CC) xxhash.o xxhash2.o xxhsum.c $(XXHSUM_SPLIT_SRCS)  -o xxhsum2  # will fail if one namespace missing (symbol collision)
	$(RM) *.o xxhsum2  # clean

MD2ROFF ?= ronn
MD2ROFF_FLAGS ?= --roff --warnings --manual="User Commands" --organization="xxhsum $(XXHSUM_VERSION)"
xxhsum.1: xxhsum.1.md xxhash.h
	cat $< | $(MD2ROFF) $(MD2ROFF_FLAGS) | $(SED) -n '/^\.\\\".*/!p' > $@

.PHONY: man
man: xxhsum.1  ## generate man page from markdown source

.PHONY: clean-man
clean-man:
	$(RM) xxhsum.1

.PHONY: preview-man
preview-man: man
	man ./xxhsum.1

.PHONY: test
test: DEBUGFLAGS += -DXXH_DEBUGLEVEL=1
test: all namespaceTest check test-xxhsum-c c90test test-tools

.PHONY: test-inline
test-inline:
	$(MAKE) -C tests test_multiInclude

.PHONY: test-all
test-all: CFLAGS += -Werror
test-all: test test32 clangtest cxxtest usan test-inline listL120 trailingWhitespace test-unicode

.PHONY: test-tools
test-tools:
	CFLAGS=-Werror $(MAKE) -C tests/bench
	CFLAGS=-Werror $(MAKE) -C tests/collisions

.PHONY: listL120
listL120:  # extract lines >= 120 characters in *.{c,h}, by Takayuki Matsuoka (note: $$, for Makefile compatibility)
	find . -type f -name '*.c' -o -name '*.h' | while read -r filename; do awk 'length > 120 {print FILENAME "(" FNR "): " $$0}' $$filename; done

.PHONY: trailingWhitespace
trailingWhitespace:
	! $(GREP) -E "`printf '[ \\t]$$'`" xxhsum.1 *.c *.h LICENSE Makefile cmake_unofficial/CMakeLists.txt


# =========================================================
# make install is validated only for the following targets
# =========================================================
ifneq (,$(filter Linux Darwin GNU/kFreeBSD GNU Haiku OpenBSD FreeBSD NetBSD DragonFly SunOS CYGWIN% , $(UNAME)))

DESTDIR     ?=
# directory variables: GNU conventions prefer lowercase
# see https://www.gnu.org/prep/standards/html_node/Makefile-Conventions.html
# support both lower and uppercase (BSD), use uppercase in script
prefix      ?= /usr/local
PREFIX      ?= $(prefix)
exec_prefix ?= $(PREFIX)
EXEC_PREFIX ?= $(exec_prefix)
libdir      ?= $(EXEC_PREFIX)/lib
LIBDIR      ?= $(libdir)
includedir  ?= $(PREFIX)/include
INCLUDEDIR  ?= $(includedir)
bindir      ?= $(EXEC_PREFIX)/bin
BINDIR      ?= $(bindir)
datarootdir ?= $(PREFIX)/share
mandir      ?= $(datarootdir)/man
man1dir     ?= $(mandir)/man1

ifneq (,$(filter $(UNAME),FreeBSD NetBSD DragonFly))
PKGCONFIGDIR ?= $(PREFIX)/libdata/pkgconfig
else
PKGCONFIGDIR ?= $(LIBDIR)/pkgconfig
endif

ifneq (,$(filter $(UNAME),OpenBSD FreeBSD NetBSD DragonFly SunOS))
MANDIR  ?= $(PREFIX)/man/man1
else
MANDIR  ?= $(man1dir)
endif

ifneq (,$(filter $(UNAME),SunOS))
INSTALL ?= ginstall
else
INSTALL ?= install
endif

INSTALL_PROGRAM ?= $(INSTALL)
INSTALL_DATA    ?= $(INSTALL) -m 644


PCLIBDIR ?= $(shell echo "$(LIBDIR)"     | $(SED) -n $(SED_ERE_OPT) -e "s@^$(EXEC_PREFIX)(/|$$)@@p")
PCINCDIR ?= $(shell echo "$(INCLUDEDIR)" | $(SED) -n $(SED_ERE_OPT) -e "s@^$(PREFIX)(/|$$)@@p")
PCEXECDIR?= $(if $(filter $(PREFIX),$(EXEC_PREFIX)),$$\{prefix\},$(EXEC_PREFIX))

ifeq (,$(PCLIBDIR))
# Additional prefix check is required, since the empty string is technically a
# valid PCLIBDIR
ifeq (,$(shell echo "$(LIBDIR)" | $(SED) -n $(SED_ERE_OPT) -e "\\@^$(EXEC_PREFIX)(/|$$)@ p"))
$(error configured libdir ($(LIBDIR)) is outside of exec_prefix ($(EXEC_PREFIX)), can't generate pkg-config file)
endif
endif

ifeq (,$(PCINCDIR))
# Additional prefix check is required, since the empty string is technically a
# valid PCINCDIR
ifeq (,$(shell echo "$(INCLUDEDIR)" | $(SED) -n $(SED_ERE_OPT) -e "\\@^$(PREFIX)(/|$$)@ p"))
$(error configured includedir ($(INCLUDEDIR)) is outside of prefix ($(PREFIX)), can't generate pkg-config file)
endif
endif

libxxhash.pc: libxxhash.pc.in
	@echo creating pkgconfig
	$(Q)$(SED) $(SED_ERE_OPT) -e 's|@PREFIX@|$(PREFIX)|' \
          -e 's|@EXECPREFIX@|$(PCEXECDIR)|' \
          -e 's|@LIBDIR@|$(PCLIBDIR)|' \
          -e 's|@INCLUDEDIR@|$(PCINCDIR)|' \
          -e 's|@VERSION@|$(LIBVER)|' \
          $< > $@


.PHONY: install
install: lib libxxhash.pc xxhsum  ## install libraries, CLI, links and man page
	@echo Installing libxxhash
	$(Q)$(INSTALL) -d -m 755 $(DESTDIR)$(LIBDIR)
	$(Q)$(INSTALL_DATA) libxxhash.a $(DESTDIR)$(LIBDIR)
	$(Q)$(INSTALL_PROGRAM) $(LIBXXH) $(DESTDIR)$(LIBDIR)
	$(Q)ln -sf $(LIBXXH) $(DESTDIR)$(LIBDIR)/libxxhash.$(SHARED_EXT_MAJOR)
	$(Q)ln -sf $(LIBXXH) $(DESTDIR)$(LIBDIR)/libxxhash.$(SHARED_EXT)
	$(Q)$(INSTALL) -d -m 755 $(DESTDIR)$(INCLUDEDIR)   # includes
	$(Q)$(INSTALL_DATA) xxhash.h $(DESTDIR)$(INCLUDEDIR)
	$(Q)$(INSTALL_DATA) xxh3.h $(DESTDIR)$(INCLUDEDIR) # for compatibility, will be removed in v0.9.0
ifeq ($(DISPATCH),1)
	$(Q)$(INSTALL_DATA) xxh_x86dispatch.h $(DESTDIR)$(INCLUDEDIR)
endif
	@echo Installing pkgconfig
	$(Q)$(INSTALL) -d -m 755 $(DESTDIR)$(PKGCONFIGDIR)/
	$(Q)$(INSTALL_DATA) libxxhash.pc $(DESTDIR)$(PKGCONFIGDIR)/
	@echo Installing xxhsum
	$(Q)$(INSTALL) -d -m 755 $(DESTDIR)$(BINDIR)/ $(DESTDIR)$(MANDIR)/
	$(Q)$(INSTALL_PROGRAM) xxhsum $(DESTDIR)$(BINDIR)/xxhsum
	$(Q)ln -sf xxhsum $(DESTDIR)$(BINDIR)/xxh32sum
	$(Q)ln -sf xxhsum $(DESTDIR)$(BINDIR)/xxh64sum
	$(Q)ln -sf xxhsum $(DESTDIR)$(BINDIR)/xxh128sum
	@echo Installing man pages
	$(Q)$(INSTALL_DATA) xxhsum.1 $(DESTDIR)$(MANDIR)/xxhsum.1
	$(Q)ln -sf xxhsum.1 $(DESTDIR)$(MANDIR)/xxh32sum.1
	$(Q)ln -sf xxhsum.1 $(DESTDIR)$(MANDIR)/xxh64sum.1
	$(Q)ln -sf xxhsum.1 $(DESTDIR)$(MANDIR)/xxh128sum.1
	@echo xxhash installation completed

.PHONY: uninstall
uninstall:  ## uninstall libraries, CLI, links and man page
	$(Q)$(RM) $(DESTDIR)$(LIBDIR)/libxxhash.a
	$(Q)$(RM) $(DESTDIR)$(LIBDIR)/libxxhash.$(SHARED_EXT)
	$(Q)$(RM) $(DESTDIR)$(LIBDIR)/libxxhash.$(SHARED_EXT_MAJOR)
	$(Q)$(RM) $(DESTDIR)$(LIBDIR)/$(LIBXXH)
	$(Q)$(RM) $(DESTDIR)$(INCLUDEDIR)/xxhash.h
	$(Q)$(RM) $(DESTDIR)$(INCLUDEDIR)/xxh3.h
	$(Q)$(RM) $(DESTDIR)$(INCLUDEDIR)/xxh_x86dispatch.h
	$(Q)$(RM) $(DESTDIR)$(PKGCONFIGDIR)/libxxhash.pc
	$(Q)$(RM) $(DESTDIR)$(BINDIR)/xxh32sum
	$(Q)$(RM) $(DESTDIR)$(BINDIR)/xxh64sum
	$(Q)$(RM) $(DESTDIR)$(BINDIR)/xxh128sum
	$(Q)$(RM) $(DESTDIR)$(BINDIR)/xxhsum
	$(Q)$(RM) $(DESTDIR)$(MANDIR)/xxh32sum.1
	$(Q)$(RM) $(DESTDIR)$(MANDIR)/xxh64sum.1
	$(Q)$(RM) $(DESTDIR)$(MANDIR)/xxh128sum.1
	$(Q)$(RM) $(DESTDIR)$(MANDIR)/xxhsum.1
	@echo xxhsum successfully uninstalled

endif
