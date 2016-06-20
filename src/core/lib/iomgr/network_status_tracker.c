/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/core/lib/iomgr/endpoint.h"
#include <grpc/support/alloc.h>

typedef struct endpoint_ll_node {
  grpc_endpoint *ep;
  struct endpoint_ll_node *next;
} endpoint_ll_node;

static endpoint_ll_node *head = NULL;

// TODO(makarandd): Install callback with OS to monitor network status.
void grpc_initialize_network_status_monitor() {
}

void grpc_destroy_network_status_monitor() {
  for (endpoint_ll_node *curr = head; curr != NULL; ) {
    endpoint_ll_node *next = curr->next;
    gpr_free(curr);
    curr = next;
  }
}

void grpc_network_status_register_endpoint(grpc_endpoint *ep) {
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
void grpc_network_status_shutdown_all_endpoints() {
  if (head == NULL) {
    return;
  }
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  for (endpoint_ll_node *curr = head; curr != NULL; curr = curr->next) {
    curr->ep->vtable->shutdown(&exec_ctx, curr->ep);
  }
  grpc_exec_ctx_finish(&exec_ctx);
}
