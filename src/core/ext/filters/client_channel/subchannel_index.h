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

#include "src/core/ext/filters/client_channel/subchannel.h"

/** \file Provides an index of active subchannels so that they can be
    shared amongst channels */

/** Create a key that can be used to uniquely identify a subchannel */
grpc_subchannel_key *grpc_subchannel_key_create(
    const grpc_subchannel_args *args);

/** Destroy a subchannel key */
void grpc_subchannel_key_destroy(grpc_exec_ctx *exec_ctx,
                                 grpc_subchannel_key *key);

/** Given a subchannel key, find the subchannel registered for it.
    Returns NULL if no such channel exists.
    Thread-safe. */
grpc_subchannel *grpc_subchannel_index_find(grpc_exec_ctx *exec_ctx,
                                            grpc_subchannel_key *key);

/** Register a subchannel against a key.
    Takes ownership of \a constructed.
    Returns the registered subchannel. This may be different from
    \a constructed in the case of a registration race. */
grpc_subchannel *grpc_subchannel_index_register(grpc_exec_ctx *exec_ctx,
                                                grpc_subchannel_key *key,
                                                grpc_subchannel *constructed);

/** Remove \a constructed as the registered subchannel for \a key. */
void grpc_subchannel_index_unregister(grpc_exec_ctx *exec_ctx,
                                      grpc_subchannel_key *key,
                                      grpc_subchannel *constructed);

int grpc_subchannel_key_compare(const grpc_subchannel_key *a,
                                const grpc_subchannel_key *b);

/** Initialize the subchannel index (global) */
void grpc_subchannel_index_init(void);
/** Shutdown the subchannel index (global) */
void grpc_subchannel_index_shutdown(void);

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_INDEX_H */
