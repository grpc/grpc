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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_DNS_C_ARES_GRPC_ARES_EV_DRIVER_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_DNS_C_ARES_GRPC_ARES_EV_DRIVER_H

#include <ares.h>
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/pollset_set.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grpc_ares_ev_driver grpc_ares_ev_driver;

/* Start \a ev_driver. It will keep working until all IO on its ares_channel is
   done, or grpc_ares_ev_driver_destroy() is called. It may notify the callbacks
   bound to its ares_channel when necessary. */
void grpc_ares_ev_driver_start(grpc_exec_ctx* exec_ctx,
                               grpc_ares_ev_driver* ev_driver);

/* Returns the ares_channel owned by \a ev_driver. To bind a c-ares query to
   \a ev_driver, use the ares_channel owned by \a ev_driver as the arg of the
   query. */
ares_channel* grpc_ares_ev_driver_get_channel(grpc_ares_ev_driver* ev_driver);

/* Creates a new grpc_ares_ev_driver. Returns GRPC_ERROR_NONE if \a ev_driver is
   created successfully. */
grpc_error* grpc_ares_ev_driver_create(grpc_ares_ev_driver** ev_driver,
                                       grpc_pollset_set* pollset_set);

/* Destroys \a ev_driver asynchronously. Pending lookups made on \a ev_driver
   will be cancelled and their on_done callbacks will be invoked with a status
   of ARES_ECANCELLED. */
void grpc_ares_ev_driver_destroy(grpc_ares_ev_driver* ev_driver);

/* Shutdown all the grpc_fds used by \a ev_driver */
void grpc_ares_ev_driver_shutdown(grpc_exec_ctx* exec_ctx,
                                  grpc_ares_ev_driver* ev_driver);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_DNS_C_ARES_GRPC_ARES_EV_DRIVER_H \
        */
