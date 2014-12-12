/* This is just a compilation test, to see if we have zlib installed. */

#include <stdlib.h>
#include <zlib.h>

int main() {
  deflateInit(Z_NULL, Z_DEFAULT_COMPRESSION);
  return 0;
}
