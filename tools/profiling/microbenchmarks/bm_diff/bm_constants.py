#!/usr/bin/env python3
#
# Copyright 2017 gRPC authors.
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
""" Configurable constants for the bm_*.py family """

_AVAILABLE_BENCHMARK_TESTS = [
    "bm_fullstack_unary_ping_pong",
    "bm_fullstack_streaming_ping_pong",
    "bm_fullstack_streaming_pump",
    "bm_closure",
    "bm_cq",
    "bm_call_create",
    "bm_chttp2_hpack",
    "bm_chttp2_transport",
    "bm_pollset",
]

_INTERESTING = (
    "cpu_time",
    "real_time",
    "locks_per_iteration",
    "allocs_per_iteration",
    "writes_per_iteration",
    "atm_cas_per_iteration",
    "atm_add_per_iteration",
    "nows_per_iteration",
    "cli_transport_stalls_per_iteration",
    "cli_stream_stalls_per_iteration",
    "svr_transport_stalls_per_iteration",
    "svr_stream_stalls_per_iteration",
    "http2_pings_sent_per_iteration",
)
