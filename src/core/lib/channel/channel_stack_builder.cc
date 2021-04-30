/*
 *
 * Copyright 2016 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_stack_builder.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

typedef struct filter_node {
  struct filter_node* next;
  struct filter_node* prev;
  const grpc_channel_filter* filter;
  grpc_post_filter_create_init_func init;
  void* init_arg;
} filter_node;

struct grpc_channel_stack_builder {
  // sentinel nodes for filters that have been added
  filter_node begin;
  filter_node end;
  // various set/get-able parameters
  grpc_channel_args* args;
  grpc_transport* transport;
  grpc_resource_user* resource_user;
  char* target;
  const char* name;
};

struct grpc_channel_stack_builder_iterator {
  grpc_channel_stack_builder* builder;
  filter_node* node;
};

grpc_channel_stack_builder* grpc_channel_stack_builder_create(void) {
  grpc_channel_stack_builder* b =
      static_cast<grpc_channel_stack_builder*>(gpr_zalloc(sizeof(*b)));

  b->begin.filter = nullptr;
  b->end.filter = nullptr;
  b->begin.next = &b->end;
  b->begin.prev = &b->end;
  b->end.next = &b->begin;
  b->end.prev = &b->begin;

  return b;
}

void grpc_channel_stack_builder_set_target(grpc_channel_stack_builder* b,
                                           const char* target) {
  gpr_free(b->target);
  b->target = gpr_strdup(target);
}

const char* grpc_channel_stack_builder_get_target(
    grpc_channel_stack_builder* b) {
  return b->target;
}

static grpc_channel_stack_builder_iterator* create_iterator_at_filter_node(
    grpc_channel_stack_builder* builder, filter_node* node) {
  grpc_channel_stack_builder_iterator* it =
      static_cast<grpc_channel_stack_builder_iterator*>(
          gpr_malloc(sizeof(*it)));
  it->builder = builder;
  it->node = node;
  return it;
}

void grpc_channel_stack_builder_iterator_destroy(
    grpc_channel_stack_builder_iterator* it) {
  gpr_free(it);
}

grpc_channel_stack_builder_iterator*
grpc_channel_stack_builder_create_iterator_at_first(
    grpc_channel_stack_builder* builder) {
  return create_iterator_at_filter_node(builder, &builder->begin);
}

grpc_channel_stack_builder_iterator*
grpc_channel_stack_builder_create_iterator_at_last(
    grpc_channel_stack_builder* builder) {
  return create_iterator_at_filter_node(builder, &builder->end);
}

bool grpc_channel_stack_builder_iterator_is_end(
    grpc_channel_stack_builder_iterator* iterator) {
  return iterator->node == &iterator->builder->end;
}

const char* grpc_channel_stack_builder_iterator_filter_name(
    grpc_channel_stack_builder_iterator* iterator) {
  if (iterator->node->filter == nullptr) return nullptr;
  return iterator->node->filter->name;
}

bool grpc_channel_stack_builder_move_next(
    grpc_channel_stack_builder_iterator* iterator) {
  if (iterator->node == &iterator->builder->end) return false;
  iterator->node = iterator->node->next;
  return true;
}

bool grpc_channel_stack_builder_move_prev(
    grpc_channel_stack_builder_iterator* iterator) {
  if (iterator->node == &iterator->builder->begin) return false;
  iterator->node = iterator->node->prev;
  return true;
}

grpc_channel_stack_builder_iterator* grpc_channel_stack_builder_iterator_find(
    grpc_channel_stack_builder* builder, const char* filter_name) {
  GPR_ASSERT(filter_name != nullptr);
  grpc_channel_stack_builder_iterator* it =
      grpc_channel_stack_builder_create_iterator_at_first(builder);
  while (grpc_channel_stack_builder_move_next(it)) {
    if (grpc_channel_stack_builder_iterator_is_end(it)) break;
    const char* filter_name_at_it =
        grpc_channel_stack_builder_iterator_filter_name(it);
    if (strcmp(filter_name, filter_name_at_it) == 0) break;
  }
  return it;
}

bool grpc_channel_stack_builder_move_prev(
    grpc_channel_stack_builder_iterator* iterator);

void grpc_channel_stack_builder_set_name(grpc_channel_stack_builder* builder,
                                         const char* name) {
  GPR_ASSERT(builder->name == nullptr);
  builder->name = name;
}

void grpc_channel_stack_builder_set_channel_arguments(
    grpc_channel_stack_builder* builder, const grpc_channel_args* args) {
  if (builder->args != nullptr) {
    grpc_channel_args_destroy(builder->args);
  }
  builder->args = grpc_channel_args_copy(args);
}

const grpc_channel_args* grpc_channel_stack_builder_get_channel_arguments(
    grpc_channel_stack_builder* builder) {
  return builder->args;
}

void grpc_channel_stack_builder_set_transport(
    grpc_channel_stack_builder* builder, grpc_transport* transport) {
  GPR_ASSERT(builder->transport == nullptr);
  builder->transport = transport;
}

grpc_transport* grpc_channel_stack_builder_get_transport(
    grpc_channel_stack_builder* builder) {
  return builder->transport;
}

void grpc_channel_stack_builder_set_resource_user(
    grpc_channel_stack_builder* builder, grpc_resource_user* resource_user) {
  GPR_ASSERT(builder->resource_user == nullptr);
  builder->resource_user = resource_user;
}

grpc_resource_user* grpc_channel_stack_builder_get_resource_user(
    grpc_channel_stack_builder* builder) {
  return builder->resource_user;
}

bool grpc_channel_stack_builder_append_filter(
    grpc_channel_stack_builder* builder, const grpc_channel_filter* filter,
    grpc_post_filter_create_init_func post_init_func, void* user_data) {
  grpc_channel_stack_builder_iterator* it =
      grpc_channel_stack_builder_create_iterator_at_last(builder);
  bool ok = grpc_channel_stack_builder_add_filter_before(
      it, filter, post_init_func, user_data);
  grpc_channel_stack_builder_iterator_destroy(it);
  return ok;
}

bool grpc_channel_stack_builder_remove_filter(
    grpc_channel_stack_builder* builder, const char* filter_name) {
  grpc_channel_stack_builder_iterator* it =
      grpc_channel_stack_builder_iterator_find(builder, filter_name);
  if (grpc_channel_stack_builder_iterator_is_end(it)) {
    grpc_channel_stack_builder_iterator_destroy(it);
    return false;
  }
  it->node->prev->next = it->node->next;
  it->node->next->prev = it->node->prev;
  gpr_free(it->node);
  grpc_channel_stack_builder_iterator_destroy(it);
  return true;
}

bool grpc_channel_stack_builder_prepend_filter(
    grpc_channel_stack_builder* builder, const grpc_channel_filter* filter,
    grpc_post_filter_create_init_func post_init_func, void* user_data) {
  grpc_channel_stack_builder_iterator* it =
      grpc_channel_stack_builder_create_iterator_at_first(builder);
  bool ok = grpc_channel_stack_builder_add_filter_after(
      it, filter, post_init_func, user_data);
  grpc_channel_stack_builder_iterator_destroy(it);
  return ok;
}

static void add_after(filter_node* before, const grpc_channel_filter* filter,
                      grpc_post_filter_create_init_func post_init_func,
                      void* user_data) {
  filter_node* new_node =
      static_cast<filter_node*>(gpr_malloc(sizeof(*new_node)));
  new_node->next = before->next;
  new_node->prev = before;
  new_node->next->prev = new_node->prev->next = new_node;
  new_node->filter = filter;
  new_node->init = post_init_func;
  new_node->init_arg = user_data;
}

bool grpc_channel_stack_builder_add_filter_before(
    grpc_channel_stack_builder_iterator* iterator,
    const grpc_channel_filter* filter,
    grpc_post_filter_create_init_func post_init_func, void* user_data) {
  if (iterator->node == &iterator->builder->begin) return false;
  add_after(iterator->node->prev, filter, post_init_func, user_data);
  return true;
}

bool grpc_channel_stack_builder_add_filter_after(
    grpc_channel_stack_builder_iterator* iterator,
    const grpc_channel_filter* filter,
    grpc_post_filter_create_init_func post_init_func, void* user_data) {
  if (iterator->node == &iterator->builder->end) return false;
  add_after(iterator->node, filter, post_init_func, user_data);
  return true;
}

void grpc_channel_stack_builder_destroy(grpc_channel_stack_builder* builder) {
  filter_node* p = builder->begin.next;
  while (p != &builder->end) {
    filter_node* next = p->next;
    gpr_free(p);
    p = next;
  }
  if (builder->args != nullptr) {
    grpc_channel_args_destroy(builder->args);
  }
  gpr_free(builder->target);
  gpr_free(builder);
}

grpc_error_handle grpc_channel_stack_builder_finish(
    grpc_channel_stack_builder* builder, size_t prefix_bytes, int initial_refs,
    grpc_iomgr_cb_func destroy, void* destroy_arg, void** result) {
  // count the number of filters
  size_t num_filters = 0;
  for (filter_node* p = builder->begin.next; p != &builder->end; p = p->next) {
    num_filters++;
  }

  // create an array of filters
  const grpc_channel_filter** filters =
      static_cast<const grpc_channel_filter**>(
          gpr_malloc(sizeof(*filters) * num_filters));
  size_t i = 0;
  for (filter_node* p = builder->begin.next; p != &builder->end; p = p->next) {
    filters[i++] = p->filter;
  }

  // calculate the size of the channel stack
  size_t channel_stack_size = grpc_channel_stack_size(filters, num_filters);

  // allocate memory, with prefix_bytes followed by channel_stack_size
  *result = gpr_zalloc(prefix_bytes + channel_stack_size);
  // fetch a pointer to the channel stack
  grpc_channel_stack* channel_stack = reinterpret_cast<grpc_channel_stack*>(
      static_cast<char*>(*result) + prefix_bytes);
  // and initialize it
  grpc_error_handle error = grpc_channel_stack_init(
      initial_refs, destroy, destroy_arg == nullptr ? *result : destroy_arg,
      filters, num_filters, builder->args, builder->transport, builder->name,
      channel_stack);

  if (error != GRPC_ERROR_NONE) {
    grpc_channel_stack_destroy(channel_stack);
    gpr_free(*result);
    *result = nullptr;
  } else {
    // run post-initialization functions
    i = 0;
    for (filter_node* p = builder->begin.next; p != &builder->end;
         p = p->next) {
      if (p->init != nullptr) {
        p->init(channel_stack, grpc_channel_stack_element(channel_stack, i),
                p->init_arg);
      }
      i++;
    }
  }

  grpc_channel_stack_builder_destroy(builder);
  gpr_free(const_cast<grpc_channel_filter**>(filters));

  return error;
}
