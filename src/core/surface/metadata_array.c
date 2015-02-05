#include <grpc/grpc.h>
#include <grpc/support/alloc.h>

#include <string.h>

void grpc_metadata_array_init(grpc_metadata_array *array) {
  memset(array, 0, sizeof(*array));
}

void grpc_metadata_array_destroy(grpc_metadata_array *array) {
  gpr_free(array->metadata);
}
