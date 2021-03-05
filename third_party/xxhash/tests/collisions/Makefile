#  Brute force collision tester for 64-bit hashes
#  Part of xxHash project
#  Copyright (C) 2019-2020 Yann Collet
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
#  You can contact the author at:
#  - xxHash homepage: https://www.xxhash.com
#  - xxHash source repository: https://github.com/Cyan4973/xxHash
#

SRC_DIRS = ./ ../../ allcodecs/
VPATH = $(SRC_DIRS)
CPPFLAGS += $(addprefix -I ,$(SRC_DIRS))
CFLAGS   ?= -std=c99 \
            -Wall -Wextra -Wconversion
CXXFLAGS ?= -Wall -Wextra -Wconversion -std=c++11
LDFLAGS  += -pthread
TESTHASHES = 110000000

HASH_SRC := $(sort $(wildcard allcodecs/*.c allcodecs/*.cc))
HASH_OBJ := $(patsubst %.c,%.o,$(HASH_SRC))


.PHONY: default
default: release

.PHONY: all
all: release

collisionsTest: main.o pool.o threading.o sort.o $(HASH_OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

main.o: hashes.h xxhash.h

release: CXXFLAGS += -O3
release: CFLAGS += -O3
release: collisionsTest

debug: CXXFLAGS += -g3 -O0 -DDEBUG
debug: CFLAGS += -g3 -O0 -DDEBUG
debug: collisionsTest

.PHONY: check
check: test

.PHONY: test
test: debug
	@echo ""
	@echo "## $(TESTHASHES) hashes with original and 0 threads"
	@time ./collisionsTest --nbh=$(TESTHASHES)
	@echo ""
	@echo "## $(TESTHASHES) hashes with original and 4 threads"
	@time ./collisionsTest --nbh=$(TESTHASHES) --threadlog=2
	@echo ""

.PHONY: clean
clean:
	$(RM) *.o allcodecs/*.o
	$(RM) collisionsTest
