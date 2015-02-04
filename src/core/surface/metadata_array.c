#include <grpc/grpc.h>
#include <grpc/support/alloc.h>

#include <string.h>

void grpc_metadata_array_init(grpc_metadata_array *array) {
  memset(array, 0, sizeof(*array));
}

void grpc_metadata_array_destroy(grpc_metadata_array *array) {
  size_t i;
  for (i = 0; i < array->count; i++) {
    gpr_free(array->metadata[i].key);
    gpr_free(array->metadata[i].value);
  }
  gpr_free(array->metadata);
}
