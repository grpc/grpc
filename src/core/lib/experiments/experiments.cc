// Copyright 2022 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Automatically generated by tools/codegen/core/gen_experiments.py

#include <grpc/support/port_platform.h>

#include "src/core/lib/experiments/experiments.h"

namespace {
const char* const description_tcp_frame_size_tuning =
    "If set, enables TCP to use RPC size estimation made by higher layers. TCP "
    "would not indicate completion of a read operation until a specified "
    "number of bytes have been read over the socket. Buffers are also "
    "allocated according to estimated RPC sizes.";
const char* const description_tcp_read_chunks =
    "Allocate only 8kb or 64kb chunks for TCP reads to reduce pressure on "
    "malloc to recycle arbitrary large blocks.";
const char* const description_tcp_rcv_lowat =
    "Use SO_RCVLOWAT to avoid wakeups on the read path.";
const char* const description_peer_state_based_framing =
    "If set, the max sizes of frames sent to lower layers is controlled based "
    "on the peer's memory pressure which is reflected in its max http2 frame "
    "size.";
const char* const description_flow_control_fixes =
    "Various fixes for flow control, max frame size setting.";
const char* const description_memory_pressure_controller =
    "New memory pressure controller";
const char* const description_periodic_resource_quota_reclamation =
    "Periodically return memory to the resource quota";
const char* const description_unconstrained_max_quota_buffer_size =
    "Discard the cap on the max free pool size for one memory allocator";
}  // namespace

namespace grpc_core {

const ExperimentMetadata g_experiment_metadata[] = {
    {"tcp_frame_size_tuning", description_tcp_frame_size_tuning, false},
    {"tcp_read_chunks", description_tcp_read_chunks, false},
    {"tcp_rcv_lowat", description_tcp_rcv_lowat, false},
    {"peer_state_based_framing", description_peer_state_based_framing, false},
    {"flow_control_fixes", description_flow_control_fixes, false},
    {"memory_pressure_controller", description_memory_pressure_controller,
     false},
    {"periodic_resource_quota_reclamation",
     description_periodic_resource_quota_reclamation, false},
    {"unconstrained_max_quota_buffer_size",
     description_unconstrained_max_quota_buffer_size, false},
};

}  // namespace grpc_core
