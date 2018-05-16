/* Compatibility helpers for the test programs. */

#ifndef _TEST_HELPERS_H_
#define _TEST_HELPERS_H_

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)

#else
#define SET_BINARY_MODE(file)

#endif


#endif
