CFLAGS += -Wall -Wextra -Wundef -g

NM = nm
GREP = grep

# Define *.exe as extension for Windows systems
ifneq (,$(filter Windows%,$(OS)))
EXT =.exe
else
EXT =
endif

ifneq (,$(filter %UTF-8,$(LANG)))
ENABLE_UNICODE ?= 1
else
ENABLE_UNICODE ?= 0
endif

.PHONY: default
default: all

.PHONY: all
all: test

.PHONY: test
test: test_multiInclude test_unicode

.PHONY: test_multiInclude
test_multiInclude:
	@$(MAKE) clean
	# compile without xxhash.o, ensure symbols exist within target
	# Note: built using only default rules
	$(MAKE) multiInclude
	@$(MAKE) clean
	# compile with xxhash.o, to detect duplicated symbols
	$(MAKE) multiInclude_withxxhash
	@$(MAKE) clean
	# Note: XXH_INLINE_ALL with XXH_NAMESPACE is currently disabled
	# compile with XXH_NAMESPACE
	# CPPFLAGS=-DXXH_NAMESPACE=TESTN_ $(MAKE) multiInclude_withxxhash
	# no symbol prefixed TESTN_ should exist
	# ! $(NM) multiInclude_withxxhash | $(GREP) TESTN_
	#$(MAKE) clean
	# compile with XXH_NAMESPACE and without xxhash.o
	# CPPFLAGS=-DXXH_NAMESPACE=TESTN_ $(MAKE) multiInclude
	# no symbol prefixed TESTN_ should exist
	# ! $(NM) multiInclude | $(GREP) TESTN_
	#@$(MAKE) clean

.PHONY: test_ppc_redefine
test_ppc_redefine: ppc_define.c
	@$(MAKE) clean
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $^

xxhsum$(EXT): ../xxhash.c ../xxhash.h ../xxhsum.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -DXXH_INLINE_ALL ../xxhsum.c -o $@

# Make sure that Unicode filenames work.
# https://github.com/Cyan4973/xxHash/issues/293
.PHONY: test_unicode
ifeq (0,$(ENABLE_UNICODE))
test_unicode:
	@echo "Skipping Unicode test, your terminal doesn't appear to support UTF-8."
	@echo "Try with ENABLE_UNICODE=1"
else
test_unicode: xxhsum$(EXT) generate_unicode_test.c
	# Generate a Unicode filename test dynamically
	# to keep UTF-8 out of the source tree.
	$(CC) $(CFLAGS) $(LDFLAGS) generate_unicode_test.c -o generate_unicode_test$(EXT)
	./generate_unicode_test$(EXT)
	$(SHELL) ./unicode_test.sh
endif

xxhash.o: ../xxhash.c ../xxhash.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -c -o $@ $<

multiInclude_withxxhash: multiInclude.o xxhash.o
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^

clean:
	@$(RM) *.o
	@$(RM) multiInclude multiInclude_withxxhash
	@$(RM) *.unicode generate_unicode_test$(EXT) unicode_test.* xxhsum$(EXT)
