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

#ifndef GRPC_CORE_EXT_RESOLVER_DNS_C_ARES_GRPC_ARES_EV_DRIVER_H
#define GRPC_CORE_EXT_RESOLVER_DNS_C_ARES_GRPC_ARES_EV_DRIVER_H

#include <grpc/support/port_platform.h>
#ifndef GRPC_NATIVE_ADDRESS_RESOLVE

#include <ares.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/pollset_set.h"

typedef struct grpc_ares_ev_driver grpc_ares_ev_driver;

void grpc_ares_notify_on_event(grpc_exec_ctx *exec_ctx,
                               grpc_ares_ev_driver *ev_driver);
void grpc_ares_gethostbyname(grpc_ares_ev_driver *ev_driver, const char *host,
                             ares_host_callback on_done_cb, void *arg);
ares_channel *grpc_ares_ev_driver_get_channel(grpc_ares_ev_driver *ev_driver);

grpc_error *grpc_ares_ev_driver_create(grpc_ares_ev_driver **ev_driver,
                                       grpc_pollset_set *pollset_set);
void grpc_ares_ev_driver_destroy(grpc_ares_ev_driver *ev_driver);

#endif /* GRPC_NATIVE_ADDRESS_RESOLVE */

#endif /* GRPC_CORE_EXT_RESOLVER_DNS_C_ARES_GRPC_ARES_EV_DRIVER_H */
