# Copyright 2023 gRPC authors.
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

# Auto generated by tools/codegen/core/gen_experiments.py

"""Dictionary of tags to experiments so we know when to test different experiments."""

EXPERIMENT_ENABLES = {
    "call_status_override_on_cancellation": "call_status_override_on_cancellation",
    "canary_client_privacy": "canary_client_privacy",
    "client_privacy": "client_privacy",
    "event_engine_client": "event_engine_client",
    "event_engine_dns": "event_engine_dns",
    "event_engine_listener": "event_engine_listener",
    "free_large_allocator": "free_large_allocator",
    "http2_stats_fix": "http2_stats_fix",
    "keepalive_fix": "keepalive_fix",
    "keepalive_server_fix": "keepalive_server_fix",
    "max_pings_wo_data_throttle": "max_pings_wo_data_throttle",
    "monitoring_experiment": "monitoring_experiment",
    "multiping": "multiping",
    "peer_state_based_framing": "peer_state_based_framing",
    "pick_first_new": "pick_first_new",
    "promise_based_inproc_transport": "promise_based_inproc_transport",
    "rstpit": "rstpit",
    "schedule_cancellation_over_write": "schedule_cancellation_over_write",
    "server_privacy": "server_privacy",
    "tcp_frame_size_tuning": "tcp_frame_size_tuning",
    "tcp_rcv_lowat": "tcp_rcv_lowat",
    "trace_record_callops": "trace_record_callops",
    "unconstrained_max_quota_buffer_size": "unconstrained_max_quota_buffer_size",
    "work_serializer_clears_time_cache": "work_serializer_clears_time_cache",
    "work_serializer_dispatch": "event_engine_client,work_serializer_dispatch",
}

EXPERIMENT_POLLERS = [
    "event_engine_client",
    "event_engine_dns",
    "event_engine_listener",
]

EXPERIMENTS = {
    "windows": {
        "dbg": {
        },
        "off": {
            "endpoint_test": [
                "tcp_frame_size_tuning",
                "tcp_rcv_lowat",
            ],
            "flow_control_test": [
                "multiping",
                "peer_state_based_framing",
                "rstpit",
                "tcp_frame_size_tuning",
                "tcp_rcv_lowat",
            ],
            "resource_quota_test": [
                "free_large_allocator",
                "unconstrained_max_quota_buffer_size",
            ],
        },
        "on": {
            "cancel_ares_query_test": [
                "event_engine_dns",
            ],
            "core_end2end_test": [
                "event_engine_client",
                "event_engine_listener",
            ],
            "cpp_lb_end2end_test": [
                "pick_first_new",
            ],
            "event_engine_client_test": [
                "event_engine_client",
            ],
            "event_engine_listener_test": [
                "event_engine_listener",
            ],
            "lb_unit_test": [
                "pick_first_new",
            ],
            "resolver_component_tests_runner_invoker": [
                "event_engine_dns",
            ],
            "xds_end2end_test": [
                "pick_first_new",
            ],
        },
    },
    "ios": {
        "dbg": {
        },
        "off": {
            "endpoint_test": [
                "tcp_frame_size_tuning",
                "tcp_rcv_lowat",
            ],
            "flow_control_test": [
                "multiping",
                "peer_state_based_framing",
                "rstpit",
                "tcp_frame_size_tuning",
                "tcp_rcv_lowat",
            ],
            "resource_quota_test": [
                "free_large_allocator",
                "unconstrained_max_quota_buffer_size",
            ],
        },
        "on": {
            "cpp_lb_end2end_test": [
                "pick_first_new",
            ],
            "lb_unit_test": [
                "pick_first_new",
            ],
            "xds_end2end_test": [
                "pick_first_new",
            ],
        },
    },
    "posix": {
        "dbg": {
        },
        "off": {
            "core_end2end_test": [
                "event_engine_client",
            ],
            "endpoint_test": [
                "tcp_frame_size_tuning",
                "tcp_rcv_lowat",
            ],
            "event_engine_client_test": [
                "event_engine_client",
            ],
            "flow_control_test": [
                "multiping",
                "peer_state_based_framing",
                "rstpit",
                "tcp_frame_size_tuning",
                "tcp_rcv_lowat",
            ],
            "resource_quota_test": [
                "free_large_allocator",
                "unconstrained_max_quota_buffer_size",
            ],
        },
        "on": {
            "cancel_ares_query_test": [
                "event_engine_dns",
            ],
            "core_end2end_test": [
                "event_engine_listener",
                "work_serializer_dispatch",
            ],
            "cpp_end2end_test": [
                "work_serializer_dispatch",
            ],
            "cpp_lb_end2end_test": [
                "pick_first_new",
            ],
            "event_engine_listener_test": [
                "event_engine_listener",
            ],
            "lb_unit_test": [
                "pick_first_new",
                "work_serializer_dispatch",
            ],
            "resolver_component_tests_runner_invoker": [
                "event_engine_dns",
            ],
            "xds_end2end_test": [
                "pick_first_new",
                "work_serializer_dispatch",
            ],
        },
    },
}
