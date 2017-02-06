# Include the nanopb provided Makefile rules
include ../../extra/nanopb.mk

# Compiler flags to enable all warnings & debug info
CFLAGS = -Wall -Werror -g -O0
CFLAGS += -I$(NANOPB_DIR)

all: run_tests

.SUFFIXES:

clean:
	rm -f test_conversions encode_double decode_double doubleproto.pb.c doubleproto.pb.h

test_conversions: test_conversions.c double_conversion.c
	$(CC) $(CFLAGS) -o $@ $^

%: %.c double_conversion.c doubleproto.pb.c
	$(CC) $(CFLAGS) -o $@ $^ $(NANOPB_CORE)

run_tests: test_conversions encode_double decode_double
	./test_conversions
	./encode_double | ./decode_double
    
