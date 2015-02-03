#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "ext/spl/spl_exceptions.h"
#include "php_grpc.h"

#include <string.h>

#include "byte_buffer.h"

#include "grpc/grpc.h"
#include "grpc/support/slice.h"

grpc_byte_buffer *string_to_byte_buffer(char *string, size_t length) {
  gpr_slice slice = gpr_slice_from_copied_buffer(string, length);
  grpc_byte_buffer *buffer = grpc_byte_buffer_create(&slice, 1);
  gpr_slice_unref(slice);
  return buffer;
}

void byte_buffer_to_string(grpc_byte_buffer *buffer, char **out_string,
                           size_t *out_length) {
  size_t length = grpc_byte_buffer_length(buffer);
  char *string = ecalloc(length + 1, sizeof(char));
  size_t offset = 0;
  grpc_byte_buffer_reader *reader = grpc_byte_buffer_reader_create(buffer);
  gpr_slice next;
  while (grpc_byte_buffer_reader_next(reader, &next) != 0) {
    memcpy(string + offset, GPR_SLICE_START_PTR(next), GPR_SLICE_LENGTH(next));
    offset += GPR_SLICE_LENGTH(next);
  }
  *out_string = string;
  *out_length = length;
}
