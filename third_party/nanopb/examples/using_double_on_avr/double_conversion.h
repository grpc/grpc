/* AVR-GCC does not have real double datatype. Instead its double
 * is equal to float, i.e. 32 bit value. If you need to communicate
 * with other systems that use double in their .proto files, you
 * need to do some conversion.
 *
 * These functions use bitwise operations to mangle floats into doubles
 * and then store them in uint64_t datatype.
 */

#ifndef DOUBLE_CONVERSION
#define DOUBLE_CONVERSION

#include <stdint.h>

/* Convert native 4-byte float into a 8-byte double. */
extern uint64_t float_to_double(float value);

/* Convert 8-byte double into native 4-byte float.
 * Values are rounded to nearest, 0.5 away from zero.
 * Overflowing values are converted to Inf or -Inf.
 */
extern float double_to_float(uint64_t value);


#endif

