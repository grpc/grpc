#include <grpc/grpc.h>
#include <grpc/support/alloc.h>

#include <string.h>

void grpc_call_details_init(grpc_call_details *cd) {
  memset(cd, 0, sizeof(*cd));
}

void grpc_call_details_destroy(grpc_call_details *cd) {
  gpr_free(cd->method);
  gpr_free(cd->host);
}
