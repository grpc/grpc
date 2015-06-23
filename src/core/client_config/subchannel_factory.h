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

#ifndef GRPC_INTERNAL_CORE_CLIENT_CONFIG_SUBCHANNEL_FACTORY_H
#define GRPC_INTERNAL_CORE_CLIENT_CONFIG_SUBCHANNEL_FACTORY_H

typedef struct grpc_subchannel_factory grpc_subchannel_factory;
typedef struct grpc_subchannel_factory_vtable grpc_subchannel_factory_vtable;

/** Constructor for new configured channels.
    Creating decorators around this type is encouraged to adapt behavior. */
struct grpc_subchannel_factory {
  const grpc_subchannel_factory_vtable *vtable;
};

struct grpc_subchannel_args {
  /* TODO(ctiller): consider making (parent, metadata_context) more opaque
     - these details are not needed at this level of API */
  /** Parent channel element - passed from the master channel */
  grpc_channel_element *parent;
  /** Metadata context for this channel - passed from the master channel */
  grpc_mdctx *metadata_context;
  /** Channel filters for this channel - wrapped factories will likely
      want to mutate this */
  const grpc_channel_filter **filters;
  /** The number of filters in the above array */
  size_t filter_count;
  /** Channel arguments to be supplied to the newly created channel */
  const grpc_channel_args *args;

  struct sockaddr *addr;
};

struct grpc_subchannel_factory_vtable {
  void (*ref)(grpc_subchannel_factory *factory);
  void (*unref)(grpc_subchannel_factory *factory);
  grpc_subchannel *(*create_subchannel)(grpc_subchannel_factory *factory,
                                        grpc_subchannel_args *args);
};

void grpc_subchannel_factory_ref(grpc_subchannel_factory *factory);
void grpc_subchannel_factory_unref(grpc_subchannel_factory *factory);
/** Create a new grpc_subchannel */
void grpc_subchannel_factory_create_subchannel(grpc_subchannel_factory *factory,
                                               grpc_subchannel_args *args);

grpc_subchannel_factory *grpc_default_subchannel_factory();

#endif /* GRPC_INTERNAL_CORE_CLIENT_CONFIG_SUBCHANNEL_FACTORY_H */
