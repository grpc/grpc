/*
 *
 * Copyright 2018 gRPC authors.
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

#include "src/core/ext/tunnel/tunnel_endpoint.h"
#include "src/core/lib/support/memory.h"

struct TunnelEndpoint : public grpc_endpoint {
  grpc_call* call;
  grpc_byte_buffer write_bb;
};

grpc_endpoint* grpc_tunnel_endpoint(grpc_call* call) {
    auto* ep = grpc_core::New<TunnelEndpoint>();
    ep->call = call;
    grpc_call_ref(call);
    grpc_closure_init(&ep->write_done, tun_write_done, ep, grpc_schedule_on_exec_ctx);
    return ep;
}

static void tun_read(grpc_endpoint* ep, grpc_slice_buffer* slices, grpc_closure* cb);

static void tun_write(grpc_endpoint* base_ep, grpc_slice_buffer* slices, grpc_closure* cb){
    auto* ep = static_cast<TunnelEndpoint*>(base_ep);
    ep->write_bb.type = GRPC_BB_RAW;
    ep->write_bb.data.raw.compression = 0;
    grpc_slice_buffer_init(&ep->write_bb.data.raw.slice_buffer);
    grpc_slice_buffer_swap(slices, &ep->write_bb.data.raw.slice_buffer);

    grpc_op ops[1];
    ops[0].op = GRPC_OP_SEND_MESSAGE;
    ops[0].data.send_message.send_message = &ep->write_bb;
    ops[0].flags=0;
    ops[0].reserved=nullptr;
    grpc_call_start_batch_and_execute(ep->call, ops, GPR_ARRAY_SIZE(ops), cb);
}

static void tun_add_to_pollset(grpc_endpoint* ep, grpc_pollset* pollset);
static void tun_add_to_pollset_set(grpc_endpoint* ep, grpc_pollset_set* pollset);
static void tun_delete_from_pollset_set(grpc_endpoint* ep, grpc_pollset_set* pollset);
static void tun_shutdown(grpc_endpoint* ep, grpc_error* why);
static void tun_destroy(grpc_endpoint* ep);
static grpc_resource_user* tun_get_resource_user(grpc_endpoint* ep);
static char* tun_get_peer(grpc_endpoint* ep);
static int tun_get_fd(grpc_endpoint* ep);
