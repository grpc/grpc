/*
 *
 * Copyright 2017, Google Inc.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_GRPCLB_CHANNEL_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_GRPCLB_CHANNEL_H

#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/lib/slice/slice_hash_table.h"

/** Create the channel used for communicating with an LB service.
 * Note that an LB *service* may be comprised of several LB *servers*.
 *
 * \a lb_service_target_addresses is the target URI containing the addresses
 * from resolving the LB service's name (eg, ipv4:10.0.0.1:1234,10.2.3.4:9876).
 * \a client_channel_factory will be used for the creation of the LB channel,
 * alongside the channel args passed in \a args. */
grpc_channel *grpc_lb_policy_grpclb_create_lb_channel(
    grpc_exec_ctx *exec_ctx, const char *lb_service_target_addresses,
    grpc_client_channel_factory *client_channel_factory,
    grpc_channel_args *args);

grpc_channel_args *get_lb_channel_args(grpc_exec_ctx *exec_ctx,
                                       grpc_slice_hash_table *targets_info,
                                       const grpc_channel_args *args);

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_GRPCLB_CHANNEL_H \
          */
