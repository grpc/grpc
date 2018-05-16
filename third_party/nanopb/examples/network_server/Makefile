# Include the nanopb provided Makefile rules
include ../../extra/nanopb.mk

# Compiler flags to enable all warnings & debug info
CFLAGS = -ansi -Wall -Werror -g -O0
CFLAGS += -I$(NANOPB_DIR)

all: server client

.SUFFIXES:

clean:
	rm -f server client fileproto.pb.c fileproto.pb.h

%: %.c common.c fileproto.pb.c
	$(CC) $(CFLAGS) -o $@ $^ $(NANOPB_CORE)

