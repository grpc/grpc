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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_INDEX_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_INDEX_H

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/subchannel.h"

/** \file Provides an index of active subchannels so that they can be
    shared amongst channels */

/** Create a key that can be used to uniquely identify a subchannel */
grpc_subchannel_key* grpc_subchannel_key_create(
    const grpc_subchannel_args* args);

/** Destroy a subchannel key */
void grpc_subchannel_key_destroy(grpc_subchannel_key* key);

/** Given a subchannel key, find the subchannel registered for it.
    Returns NULL if no such channel exists.
    Thread-safe. */
grpc_subchannel* grpc_subchannel_index_find(grpc_subchannel_key* key);

/** Register a subchannel against a key.
    Takes ownership of \a constructed.
    Returns the registered subchannel. This may be different from
    \a constructed in the case of a registration race. */
grpc_subchannel* grpc_subchannel_index_register(grpc_subchannel_key* key,
                                                grpc_subchannel* constructed);

/** Remove \a constructed as the registered subchannel for \a key. */
void grpc_subchannel_index_unregister(grpc_subchannel_key* key,
                                      grpc_subchannel* constructed);

int grpc_subchannel_key_compare(const grpc_subchannel_key* a,
                                const grpc_subchannel_key* b);

/** Initialize the subchannel index (global) */
void grpc_subchannel_index_init(void);
/** Shutdown the subchannel index (global) */
void grpc_subchannel_index_shutdown(void);

/** Increment the refcount (non-zero) of subchannel index (global). */
void grpc_subchannel_index_ref(void);

/** Decrement the refcount of subchannel index (global). If the refcount drops
    to zero, unref the subchannel index and destroy its mutex. */
void grpc_subchannel_index_unref(void);

/** \em TEST ONLY.
 * If \a force_creation is true, all key comparisons will be false, resulting in
 * new subchannels always being created. Otherwise, the keys will be compared as
 * usual.
 *
 * This function is *not* threadsafe on purpose: it should *only* be used in
 * test code.
 *
 * Tests using this function \em MUST run tests with and without \a
 * force_creation set. */
void grpc_subchannel_index_test_only_set_force_creation(bool force_creation);

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_INDEX_H */
