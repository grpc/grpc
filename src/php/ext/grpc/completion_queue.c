/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "completion_queue.h"

#include <stdbool.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

grpc_completion_queue* completion_queue;

void grpc_php_init_completion_queue(TSRMLS_D) {
  completion_queue = grpc_completion_queue_create_for_pluck(NULL);
}

void grpc_php_shutdown_completion_queue(TSRMLS_D) {
  grpc_completion_queue_shutdown(completion_queue);
  grpc_completion_queue_destroy(completion_queue);
}

grpc_completion_queue* callback_queue;

static struct callback_tag_list_item* g_callback_tag_list_head = NULL;
static gpr_mu g_callback_tag_list_mu;

void grpc_php_callback_tag_list_init() {
  gpr_mu_init(&g_callback_tag_list_mu);
  gpr_mu_lock(&g_callback_tag_list_mu);
  g_callback_tag_list_head = gpr_zalloc(sizeof(struct callback_tag_list_item));
  g_callback_tag_list_head->next = g_callback_tag_list_head;
  g_callback_tag_list_head->prev = g_callback_tag_list_head;
  gpr_mu_unlock(&g_callback_tag_list_mu);
}

void grpc_php_callback_tag_list_destroy() {
  gpr_mu_lock(&g_callback_tag_list_mu);
  int i = 0;
  for (struct callback_tag_list_item* n = g_callback_tag_list_head->next;
       n != g_callback_tag_list_head; n = n->next) {
    ++i;
  }
  // GPR_ASSERT(g_callback_tag_list_head->next == g_callback_tag_list_head);
  gpr_free(g_callback_tag_list_head);
  g_callback_tag_list_head = NULL;
  gpr_mu_unlock(&g_callback_tag_list_mu);
  gpr_mu_destroy(&g_callback_tag_list_mu);
}

void grpc_php_callback_tag_list_push(struct callback_tag_list_item* tag) {
  gpr_mu_lock(&g_callback_tag_list_mu);
  tag->prev = g_callback_tag_list_head->prev;
  tag->next = g_callback_tag_list_head;
  g_callback_tag_list_head->prev = tag;
  tag->prev->next = tag;
  gpr_mu_unlock(&g_callback_tag_list_mu);
}

struct callback_tag_list_item* grpc_php_callback_tag_list_pop() {
  gpr_mu_lock(&g_callback_tag_list_mu);
  if (g_callback_tag_list_head->next == g_callback_tag_list_head) {
    gpr_mu_unlock(&g_callback_tag_list_mu);
    return NULL;
  }

  struct callback_tag_list_item* result = g_callback_tag_list_head->next;
  g_callback_tag_list_head->next = result->next;
  result->next->prev = g_callback_tag_list_head;
  gpr_mu_unlock(&g_callback_tag_list_mu);
  result->next = NULL;
  result->prev = NULL;
  return result;
}

void callback_queue_shutdown_callback(
    struct grpc_completion_queue_functor* tag, int succeeded) {}

static struct grpc_completion_queue_functor
    callback_queue_shutdown_functor = {callback_queue_shutdown_callback, false};

void grpc_php_init_completion_queue_for_callback(TSRMLS_D) {
  grpc_php_callback_tag_list_init();
  callback_queue = grpc_completion_queue_create_for_callback(
      &callback_queue_shutdown_functor, NULL);
}

void grpc_php_shutdown_completion_queue_for_callback(TSRMLS_D) {
  grpc_completion_queue_shutdown(callback_queue);
  grpc_completion_queue_destroy(callback_queue);
  grpc_php_callback_tag_list_destroy();
}
