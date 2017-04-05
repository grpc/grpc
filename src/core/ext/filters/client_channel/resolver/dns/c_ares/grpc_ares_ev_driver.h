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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_DNS_C_ARES_GRPC_ARES_EV_DRIVER_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_DNS_C_ARES_GRPC_ARES_EV_DRIVER_H

#include <ares.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/pollset_set.h"

typedef struct grpc_ares_ev_driver grpc_ares_ev_driver;

/* Start \a ev_driver. It will keep working until all IO on its ares_channel is
   done, or grpc_ares_ev_driver_destroy() is called. It may notify the callbacks
   bound to its ares_channel when necessary. */
void grpc_ares_ev_driver_start(grpc_exec_ctx *exec_ctx,
                               grpc_ares_ev_driver *ev_driver);

/* Returns the ares_channel owned by \a ev_driver. To bind a c-ares query to
   \a ev_driver, use the ares_channel owned by \a ev_driver as the arg of the
   query. */
ares_channel *grpc_ares_ev_driver_get_channel(grpc_ares_ev_driver *ev_driver);

/* Creates a new grpc_ares_ev_driver. Returns GRPC_ERROR_NONE if \a ev_driver is
   created successfully. */
grpc_error *grpc_ares_ev_driver_create(grpc_ares_ev_driver **ev_driver,
                                       grpc_pollset_set *pollset_set);

/* Destroys \a ev_driver asynchronously. Pending lookups made on \a ev_driver
   will be cancelled and their on_done callbacks will be invoked with a status
   of ARES_ECANCELLED. */
void grpc_ares_ev_driver_destroy(grpc_ares_ev_driver *ev_driver);

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_DNS_C_ARES_GRPC_ARES_EV_DRIVER_H \
          */
