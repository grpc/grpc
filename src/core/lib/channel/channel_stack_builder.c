/*
 *
 * Copyright 2016, Google Inc.
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

#include "src/core/lib/channel/channel_stack_builder.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

grpc_tracer_flag grpc_trace_channel_stack_builder =
    GRPC_TRACER_INITIALIZER(false);

typedef struct filter_node {
  struct filter_node *next;
  struct filter_node *prev;
  const grpc_channel_filter *filter;
  grpc_post_filter_create_init_func init;
  void *init_arg;
} filter_node;

struct grpc_channel_stack_builder {
  // sentinel nodes for filters that have been added
  filter_node begin;
  filter_node end;
  // various set/get-able parameters
  grpc_channel_args *args;
  grpc_transport *transport;
  char *target;
  const char *name;
};

struct grpc_channel_stack_builder_iterator {
  grpc_channel_stack_builder *builder;
  filter_node *node;
};

grpc_channel_stack_builder *grpc_channel_stack_builder_create(void) {
  grpc_channel_stack_builder *b = gpr_zalloc(sizeof(*b));

  b->begin.filter = NULL;
  b->end.filter = NULL;
  b->begin.next = &b->end;
  b->begin.prev = &b->end;
  b->end.next = &b->begin;
  b->end.prev = &b->begin;

  return b;
}

void grpc_channel_stack_builder_set_target(grpc_channel_stack_builder *b,
                                           const char *target) {
  gpr_free(b->target);
  b->target = gpr_strdup(target);
}

const char *grpc_channel_stack_builder_get_target(
    grpc_channel_stack_builder *b) {
  return b->target;
}

static grpc_channel_stack_builder_iterator *create_iterator_at_filter_node(
    grpc_channel_stack_builder *builder, filter_node *node) {
  grpc_channel_stack_builder_iterator *it = gpr_malloc(sizeof(*it));
  it->builder = builder;
  it->node = node;
  return it;
}

void grpc_channel_stack_builder_iterator_destroy(
    grpc_channel_stack_builder_iterator *it) {
  gpr_free(it);
}

grpc_channel_stack_builder_iterator *
grpc_channel_stack_builder_create_iterator_at_first(
    grpc_channel_stack_builder *builder) {
  return create_iterator_at_filter_node(builder, &builder->begin);
}

grpc_channel_stack_builder_iterator *
grpc_channel_stack_builder_create_iterator_at_last(
    grpc_channel_stack_builder *builder) {
  return create_iterator_at_filter_node(builder, &builder->end);
}

bool grpc_channel_stack_builder_iterator_is_end(
    grpc_channel_stack_builder_iterator *iterator) {
  return iterator->node == &iterator->builder->end;
}

const char *grpc_channel_stack_builder_iterator_filter_name(
    grpc_channel_stack_builder_iterator *iterator) {
  if (iterator->node->filter == NULL) return NULL;
  return iterator->node->filter->name;
}

bool grpc_channel_stack_builder_move_next(
    grpc_channel_stack_builder_iterator *iterator) {
  if (iterator->node == &iterator->builder->end) return false;
  iterator->node = iterator->node->next;
  return true;
}

bool grpc_channel_stack_builder_move_prev(
    grpc_channel_stack_builder_iterator *iterator) {
  if (iterator->node == &iterator->builder->begin) return false;
  iterator->node = iterator->node->prev;
  return true;
}

bool grpc_channel_stack_builder_move_prev(
    grpc_channel_stack_builder_iterator *iterator);

void grpc_channel_stack_builder_set_name(grpc_channel_stack_builder *builder,
                                         const char *name) {
  GPR_ASSERT(builder->name == NULL);
  builder->name = name;
}

void grpc_channel_stack_builder_set_channel_arguments(
    grpc_exec_ctx *exec_ctx, grpc_channel_stack_builder *builder,
    const grpc_channel_args *args) {
  if (builder->args != NULL) {
    grpc_channel_args_destroy(exec_ctx, builder->args);
  }
  builder->args = grpc_channel_args_copy(args);
}

void grpc_channel_stack_builder_set_transport(
    grpc_channel_stack_builder *builder, grpc_transport *transport) {
  GPR_ASSERT(builder->transport == NULL);
  builder->transport = transport;
}

grpc_transport *grpc_channel_stack_builder_get_transport(
    grpc_channel_stack_builder *builder) {
  return builder->transport;
}

const grpc_channel_args *grpc_channel_stack_builder_get_channel_arguments(
    grpc_channel_stack_builder *builder) {
  return builder->args;
}

bool grpc_channel_stack_builder_append_filter(
    grpc_channel_stack_builder *builder, const grpc_channel_filter *filter,
    grpc_post_filter_create_init_func post_init_func, void *user_data) {
  grpc_channel_stack_builder_iterator *it =
      grpc_channel_stack_builder_create_iterator_at_last(builder);
  bool ok = grpc_channel_stack_builder_add_filter_before(
      it, filter, post_init_func, user_data);
  grpc_channel_stack_builder_iterator_destroy(it);
  return ok;
}

bool grpc_channel_stack_builder_prepend_filter(
    grpc_channel_stack_builder *builder, const grpc_channel_filter *filter,
    grpc_post_filter_create_init_func post_init_func, void *user_data) {
  grpc_channel_stack_builder_iterator *it =
      grpc_channel_stack_builder_create_iterator_at_first(builder);
  bool ok = grpc_channel_stack_builder_add_filter_after(
      it, filter, post_init_func, user_data);
  grpc_channel_stack_builder_iterator_destroy(it);
  return ok;
}

static void add_after(filter_node *before, const grpc_channel_filter *filter,
                      grpc_post_filter_create_init_func post_init_func,
                      void *user_data) {
  filter_node *new = gpr_malloc(sizeof(*new));
  new->next = before->next;
  new->prev = before;
  new->next->prev = new->prev->next = new;
  new->filter = filter;
  new->init = post_init_func;
  new->init_arg = user_data;
}

bool grpc_channel_stack_builder_add_filter_before(
    grpc_channel_stack_builder_iterator *iterator,
    const grpc_channel_filter *filter,
    grpc_post_filter_create_init_func post_init_func, void *user_data) {
  if (iterator->node == &iterator->builder->begin) return false;
  add_after(iterator->node->prev, filter, post_init_func, user_data);
  return true;
}

bool grpc_channel_stack_builder_add_filter_after(
    grpc_channel_stack_builder_iterator *iterator,
    const grpc_channel_filter *filter,
    grpc_post_filter_create_init_func post_init_func, void *user_data) {
  if (iterator->node == &iterator->builder->end) return false;
  add_after(iterator->node, filter, post_init_func, user_data);
  return true;
}

void grpc_channel_stack_builder_destroy(grpc_exec_ctx *exec_ctx,
                                        grpc_channel_stack_builder *builder) {
  filter_node *p = builder->begin.next;
  while (p != &builder->end) {
    filter_node *next = p->next;
    gpr_free(p);
    p = next;
  }
  if (builder->args != NULL) {
    grpc_channel_args_destroy(exec_ctx, builder->args);
  }
  gpr_free(builder->target);
  gpr_free(builder);
}

grpc_error *grpc_channel_stack_builder_finish(
    grpc_exec_ctx *exec_ctx, grpc_channel_stack_builder *builder,
    size_t prefix_bytes, int initial_refs, grpc_iomgr_cb_func destroy,
    void *destroy_arg, void **result) {
  // count the number of filters
  size_t num_filters = 0;
  for (filter_node *p = builder->begin.next; p != &builder->end; p = p->next) {
    num_filters++;
  }

  // create an array of filters
  const grpc_channel_filter **filters =
      gpr_malloc(sizeof(*filters) * num_filters);
  size_t i = 0;
  for (filter_node *p = builder->begin.next; p != &builder->end; p = p->next) {
    filters[i++] = p->filter;
  }

  // calculate the size of the channel stack
  size_t channel_stack_size = grpc_channel_stack_size(filters, num_filters);

  // allocate memory, with prefix_bytes followed by channel_stack_size
  *result = gpr_zalloc(prefix_bytes + channel_stack_size);
  // fetch a pointer to the channel stack
  grpc_channel_stack *channel_stack =
      (grpc_channel_stack *)((char *)(*result) + prefix_bytes);
  // and initialize it
  grpc_error *error = grpc_channel_stack_init(
      exec_ctx, initial_refs, destroy,
      destroy_arg == NULL ? *result : destroy_arg, filters, num_filters,
      builder->args, builder->transport, builder->name, channel_stack);

  if (error != GRPC_ERROR_NONE) {
    grpc_channel_stack_destroy(exec_ctx, channel_stack);
    gpr_free(*result);
    *result = NULL;
  } else {
    // run post-initialization functions
    i = 0;
    for (filter_node *p = builder->begin.next; p != &builder->end;
         p = p->next) {
      if (p->init != NULL) {
        p->init(channel_stack, grpc_channel_stack_element(channel_stack, i),
                p->init_arg);
      }
      i++;
    }
  }

  grpc_channel_stack_builder_destroy(exec_ctx, builder);
  gpr_free((grpc_channel_filter **)filters);

  return error;
}
