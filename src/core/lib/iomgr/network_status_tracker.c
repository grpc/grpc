
#include "src/core/lib/iomgr/endpoint.h"
#include <grpc/support/alloc.h>

typedef struct endpoint_ll_node {
  grpc_endpoint *ep;
  struct endpoint_ll_node *next;
} endpoint_ll_node;

static endpoint_ll_node *head = NULL;

// TODO(makarandd): Install callback with OS to monitor network status.
void initialize_network_status_monitor() {
}

void destroy_network_status_monitor() {
  for (endpoint_ll_node *curr = head; curr != NULL; ) {
    endpoint_ll_node *next = curr->next;
    gpr_free(curr);
    curr = next;
  }
}

void network_status_register_endpoint(grpc_endpoint *ep) {
  if (head == NULL) {
    head = (endpoint_ll_node *)gpr_malloc(sizeof(endpoint_ll_node));
    head->ep = ep;
    head->next = NULL;
  } else {
    endpoint_ll_node *prev_head = head;
    head = (endpoint_ll_node *)gpr_malloc(sizeof(endpoint_ll_node));
    head->ep = ep;
    head->next = prev_head;
  }
}

// Walk the linked-list from head and execute shutdown. It is possible that
// other threads might be in the process of shutdown as well, but that has
// no side effect.
void network_status_shutdown_all_endpoints() {
  if (head == NULL) {
    return;
  }
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  for (endpoint_ll_node *curr = head; curr != NULL; curr = curr->next) {
    curr->ep->vtable->shutdown(&exec_ctx, curr->ep);
  }
  grpc_exec_ctx_finish(&exec_ctx);
}
