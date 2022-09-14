# Copyright 2022 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Automatically generated by tools/codegen/core/gen_experiments.py

"""Dictionary of tags to experiments so we know when to test different experiments."""

EXPERIMENTS = {
    "dbg": {
        "core_end2end_tests": [
            "new_hpack_huffman_decoder",
        ],
        "hpack_test": [
            "new_hpack_huffman_decoder",
        ],
    },
    "off": {
        "endpoint_test": [
            "tcp_frame_size_tuning",
            "tcp_rcv_lowat",
            "tcp_read_chunks",
        ],
        "flow_control_test": [
            "flow_control_fixes",
            "peer_state_based_framing",
            "tcp_frame_size_tuning",
            "tcp_rcv_lowat",
            "tcp_read_chunks",
        ],
        "resource_quota_test": [
            "memory_pressure_controller",
            "periodic_resource_quota_reclamation",
            "unconstrained_max_quota_buffer_size",
        ],
    },
    "on": {
    },
    "opt": {
    },
}
