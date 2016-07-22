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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "src/core/lib/iomgr/endpoint.h"

typedef struct endpoint_ll_node {
  grpc_endpoint *ep;
  struct endpoint_ll_node *next;
} endpoint_ll_node;

static endpoint_ll_node *head = NULL;
static gpr_mu g_endpoint_mutex;

void grpc_network_status_shutdown(void) {
  if (head != NULL) {
    gpr_log(GPR_ERROR,
            "Memory leaked as all network endpoints were not shut down");
  }
  gpr_mu_destroy(&g_endpoint_mutex);
}

void grpc_network_status_init(void) {
  gpr_mu_init(&g_endpoint_mutex);
  // TODO(makarandd): Install callback with OS to monitor network status.
}

void grpc_destroy_network_status_monitor() {
  for (endpoint_ll_node *curr = head; curr != NULL;) {
    endpoint_ll_node *next = curr->next;
    gpr_free(curr);
    curr = next;
  }
  gpr_mu_destroy(&g_endpoint_mutex);
}

void grpc_network_status_register_endpoint(grpc_endpoint *ep) {
  gpr_mu_lock(&g_endpoint_mutex);
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
  gpr_mu_unlock(&g_endpoint_mutex);
}

void grpc_network_status_unregister_endpoint(grpc_endpoint *ep) {
  gpr_mu_lock(&g_endpoint_mutex);
  GPR_ASSERT(head);
  bool found = false;
  endpoint_ll_node *prev = head;
  // if we're unregistering the head, just move head to the next
  if (ep == head->ep) {
    head = head->next;
    gpr_free(prev);
    found = true;
  } else {
    for (endpoint_ll_node *curr = head->next; curr != NULL; curr = curr->next) {
      if (ep == curr->ep) {
        prev->next = curr->next;
        gpr_free(curr);
        found = true;
        break;
      }
      prev = curr;
    }
  }
  gpr_mu_unlock(&g_endpoint_mutex);
  GPR_ASSERT(found);
}

// Walk the linked-list from head and execute shutdown. It is possible that
// other threads might be in the process of shutdown as well, but that has
// no side effect since endpoint shutdown is idempotent.
void grpc_network_status_shutdown_all_endpoints() {
  gpr_mu_lock(&g_endpoint_mutex);
  if (head == NULL) {
    gpr_mu_unlock(&g_endpoint_mutex);
    return;
  }
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  for (endpoint_ll_node *curr = head; curr != NULL; curr = curr->next) {
    curr->ep->vtable->shutdown(&exec_ctx, curr->ep);
  }
  gpr_mu_unlock(&g_endpoint_mutex);
  grpc_exec_ctx_finish(&exec_ctx);
}
