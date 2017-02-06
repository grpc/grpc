# Include the nanopb provided Makefile rules
include ../../extra/nanopb.mk

# Compiler flags to enable all warnings & debug info
CFLAGS = -ansi -Wall -Werror -g -O0
CFLAGS += -I$(NANOPB_DIR)

all: encode decode
	./encode 1 | ./decode
	./encode 2 | ./decode
	./encode 3 | ./decode

.SUFFIXES:

clean:
	rm -f encode unionproto.pb.h unionproto.pb.c

%: %.c unionproto.pb.c
	$(CC) $(CFLAGS) -o $@ $^ $(NANOPB_CORE)

