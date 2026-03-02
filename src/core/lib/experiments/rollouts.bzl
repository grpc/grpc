# Copyright 2026 gRPC authors.
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

"""Rollout configurations for gRPC experiments."""

ROLLOUTS = [
    {
        "name": "call_tracer_in_transport",
        "default": True,
    },
    {
        "name": "call_tracer_send_initial_metadata_is_an_annotation",
        "default": True,
    },
    {
        "name": "chaotic_good_connect_deadline",
        "default": True,
    },
    {
        "name": "chaotic_good_framing_layer",
        "default": True,
    },
    {
        "name": "error_flatten",
        "default": False,
    },
    {
        "name": "event_engine_callback_cq",
        "default": True,
    },
    {
        "name": "event_engine_client",
        "default": True,
    },
    {
        "name": "event_engine_dns",
        "default": True,
    },
    {
        "name": "event_engine_dns_non_client_channel",
        "default": True,
    },
    {
        "name": "event_engine_for_all_other_endpoints",
        "default": True,
    },
    {
        "name": "event_engine_fork",
        "default": True,
    },
    {
        "name": "event_engine_listener",
        "default": True,
    },
    {
        "name": "event_engine_poller_for_python",
        "default": True,
    },
    {
        "name": "fail_recv_metadata_on_deadline_exceeded",
        "default": False,
    },
    {
        "name": "free_large_allocator",
        "default": False,
    },
    {
        "name": "fuse_filters",
        "default": False,
    },
    {
        "name": "keep_alive_ping_timer_batch",
        "default": False,
    },
    {
        "name": "local_connector_secure",
        "default": False,
    },
    {
        "name": "max_inflight_pings_strict_limit",
        "default": True,
    },
    {
        "name": "metadata_publish_to_app_tag",
        "default": True,
    },
    {
        "name": "monitoring_experiment",
        "default": True,
    },
    {
        "name": "pick_first_ready_to_connecting",
        "default": True,
    },
    {
        "name": "pollset_alternative",
        "default": False,
    },
    {
        "name": "prioritize_finished_requests",
        "default": False,
    },
    {
        "name": "promise_based_http2_client_transport",
        "default": False,
    },
    {
        "name": "promise_based_http2_server_transport",
        "default": False,
    },
    {
        "name": "promise_filter_send_cancel_metadata",
        "default": False,
    },
    {
        "name": "rr_wrr_connect_from_random_index",
        "default": True,
    },
    {
        "name": "schedule_cancellation_over_write",
        "default": False,
    },
    {
        "name": "skip_clear_peer_on_cancellation",
        "default": False,
    },
    {
        "name": "sleep_promise_exec_ctx_removal",
        "default": False,
    },
    {
        "name": "sleep_use_non_owning_waker",
        "default": True,
    },
    {
        "name": "tcp_frame_size_tuning",
        "default": False,
    },
    {
        "name": "tcp_rcv_lowat",
        "default": False,
    },
    {
        "name": "tsi_frame_protector_without_locks",
        "default": False,
    },
    {
        "name": "unconstrained_max_quota_buffer_size",
        "default": False,
    },
    {
        "name": "use_call_event_engine_in_completion_queue",
        "default": False,
    },
    {
        "name": "xds_channel_filter_chain_per_route",
        "default": True,
    },
]
