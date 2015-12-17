#Overview

This directory contains source code for shared C library. Libraries in other languages in this repository (C++, Ruby,
Python, PHP, NodeJS, Objective-C) are layered on top of this library.

#Status

Beta

# Aggregate files

The `aggregate` subdirectory contains C files that `#include` other C files in this directory. Each can be compiled alone into its corresponding library.

This makes it easier for other libraries to build against the C core library statically. In particular, this is the simplest way to do that in the Ruby library.
