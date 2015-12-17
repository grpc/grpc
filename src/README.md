# Aggregate files

The `aggregate` subdirectory contains C files that `#include` other C files in this directory. Each can be compiled alone into its corresponding library.

This makes it easier for other libraries to build against the C core library statically. In particular, this is the simplest way to do that in the Ruby library.
