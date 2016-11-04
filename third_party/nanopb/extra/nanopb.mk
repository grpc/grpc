# This is an include file for Makefiles. It provides rules for building
# .pb.c and .pb.h files out of .proto, as well the path to nanopb core.

# Path to the nanopb root directory
NANOPB_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../)

# Files for the nanopb core
NANOPB_CORE = $(NANOPB_DIR)/pb_encode.c $(NANOPB_DIR)/pb_decode.c $(NANOPB_DIR)/pb_common.c

# Check if we are running on Windows
ifdef windir
WINDOWS = 1
endif
ifdef WINDIR
WINDOWS = 1
endif

# Check whether to use binary version of nanopb_generator or the
# system-supplied python interpreter.
ifneq "$(wildcard $(NANOPB_DIR)/generator-bin)" ""
	# Binary package
	PROTOC = $(NANOPB_DIR)/generator-bin/protoc
	PROTOC_OPTS = 
else
	# Source only or git checkout
	PROTOC = protoc
	ifdef WINDOWS
		PROTOC_OPTS = --plugin=protoc-gen-nanopb=$(NANOPB_DIR)/generator/protoc-gen-nanopb.bat
	else
		PROTOC_OPTS = --plugin=protoc-gen-nanopb=$(NANOPB_DIR)/generator/protoc-gen-nanopb
	endif
endif

# Rule for building .pb.c and .pb.h
%.pb.c %.pb.h: %.proto $(wildcard %.options)
	$(PROTOC) $(PROTOC_OPTS) --nanopb_out=. $<

