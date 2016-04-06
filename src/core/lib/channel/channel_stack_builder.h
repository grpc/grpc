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

#ifndef GRPC_CORE_LIB_CHANNEL_CHANNEL_STACK_BUILDER_H
#define GRPC_CORE_LIB_CHANNEL_CHANNEL_STACK_BUILDER_H

#include <stdbool.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"

/// grpc_channel_stack_builder offers a programmatic interface to selected
/// and order channel filters
typedef struct grpc_channel_stack_builder grpc_channel_stack_builder;
typedef struct grpc_channel_stack_builder_iterator
    grpc_channel_stack_builder_iterator;

/// Create a new channel stack builder
grpc_channel_stack_builder *grpc_channel_stack_builder_create(void);

/// Assign a name to the channel stack: \a name must be statically allocated
void grpc_channel_stack_builder_set_name(grpc_channel_stack_builder *builder,
                                         const char *name);

/// Set the target uri
void grpc_channel_stack_builder_set_target(grpc_channel_stack_builder *b,
                                           const char *target);

const char *grpc_channel_stack_builder_get_target(
    grpc_channel_stack_builder *b);

/// Attach \a transport to the builder (does not take ownership)
void grpc_channel_stack_builder_set_transport(
    grpc_channel_stack_builder *builder, grpc_transport *transport);

/// Fetch attached transport
grpc_transport *grpc_channel_stack_builder_get_transport(
    grpc_channel_stack_builder *builder);

/// Set channel arguments: copies args
void grpc_channel_stack_builder_set_channel_arguments(
    grpc_channel_stack_builder *builder, const grpc_channel_args *args);

/// Return a borrowed pointer to the channel arguments
const grpc_channel_args *grpc_channel_stack_builder_get_channel_arguments(
    grpc_channel_stack_builder *builder);

/// Begin iterating over already defined filters in the builder at the beginning
grpc_channel_stack_builder_iterator *
grpc_channel_stack_builder_create_iterator_at_first(
    grpc_channel_stack_builder *builder);

/// Begin iterating over already defined filters in the builder at the end
grpc_channel_stack_builder_iterator *
grpc_channel_stack_builder_create_iterator_at_last(
    grpc_channel_stack_builder *builder);

/// Is an iterator at the first element?
bool grpc_channel_stack_builder_iterator_is_first(
    grpc_channel_stack_builder_iterator *iterator);

/// Is an iterator at the end?
bool grpc_channel_stack_builder_iterator_is_end(
    grpc_channel_stack_builder_iterator *iterator);

/// Move an iterator to the next item
bool grpc_channel_stack_builder_move_next(
    grpc_channel_stack_builder_iterator *iterator);

/// Move an iterator to the previous item
bool grpc_channel_stack_builder_move_prev(
    grpc_channel_stack_builder_iterator *iterator);

typedef void (*grpc_post_filter_create_init_func)(
    grpc_channel_stack *channel_stack, grpc_channel_element *elem, void *arg);

/// Add \a filter to the stack, after \a iterator.
/// Call \a post_init_func(..., \a user_data) once the channel stack is
/// created.
bool grpc_channel_stack_builder_add_filter_after(
    grpc_channel_stack_builder_iterator *iterator,
    const grpc_channel_filter *filter,
    grpc_post_filter_create_init_func post_init_func,
    void *user_data) GRPC_MUST_USE_RESULT;

/// Add \a filter to the stack, before \a iterator.
/// Call \a post_init_func(..., \a user_data) once the channel stack is
/// created.
bool grpc_channel_stack_builder_add_filter_before(
    grpc_channel_stack_builder_iterator *iterator,
    const grpc_channel_filter *filter,
    grpc_post_filter_create_init_func post_init_func,
    void *user_data) GRPC_MUST_USE_RESULT;

/// Add \a filter to the beginning of the filter list.
/// Call \a post_init_func(..., \a user_data) once the channel stack is
/// created.
bool grpc_channel_stack_builder_prepend_filter(
    grpc_channel_stack_builder *builder, const grpc_channel_filter *filter,
    grpc_post_filter_create_init_func post_init_func,
    void *user_data) GRPC_MUST_USE_RESULT;

/// Add \a filter to the end of the filter list.
/// Call \a post_init_func(..., \a user_data) once the channel stack is
/// created.
bool grpc_channel_stack_builder_append_filter(
    grpc_channel_stack_builder *builder, const grpc_channel_filter *filter,
    grpc_post_filter_create_init_func post_init_func,
    void *user_data) GRPC_MUST_USE_RESULT;

/// Terminate iteration and destroy \a iterator
void grpc_channel_stack_builder_iterator_destroy(
    grpc_channel_stack_builder_iterator *iterator);

/// Destroy the builder, return the freshly minted channel stack
/// Allocates \a prefix_bytes bytes before the channel stack
/// Returns the base pointer of the allocated block
/// \a initial_refs, \a destroy, \a destroy_arg are as per
/// grpc_channel_stack_init
void *grpc_channel_stack_builder_finish(grpc_exec_ctx *exec_ctx,
                                        grpc_channel_stack_builder *builder,
                                        size_t prefix_bytes, int initial_refs,
                                        grpc_iomgr_cb_func destroy,
                                        void *destroy_arg);

/// Destroy the builder without creating a channel stack
void grpc_channel_stack_builder_destroy(grpc_channel_stack_builder *builder);

extern int grpc_trace_channel_stack_builder;

#endif /* GRPC_CORE_LIB_CHANNEL_CHANNEL_STACK_BUILDER_H */
