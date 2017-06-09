#!/usr/bin/env python2.7
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
  'bm_fullstack_unary_ping_pong', 'bm_fullstack_streaming_ping_pong',
  'bm_fullstack_streaming_pump', 'bm_closure', 'bm_cq', 'bm_call_create',
  'bm_error', 'bm_chttp2_hpack', 'bm_chttp2_transport', 'bm_pollset',
  'bm_metadata'
]

# these tests will only be run twice during the full sweep to prevent
# timeouts in the Jenkins environment. For now, they also skip the counters
# runs, because trickle doesn't need it. If we add more time intensive tests
# that do provide interesting counters data, this will need to be updated.
_TIME_INTENSIVE_BENCHMARK_TESTS = [
  'bm_fullstack_trickle',
]

# This should be the biggest number that does not cause excessive timeouts
_TIME_INTENSIVE_BENCHMARK_LOOP_CAP = 4

_INTERESTING = ('cpu_time', 'real_time', 'locks_per_iteration',
        'allocs_per_iteration', 'writes_per_iteration',
        'atm_cas_per_iteration', 'atm_add_per_iteration',
        'nows_per_iteration', 'cli_transport_stalls', 'cli_stream_stalls', 
        'svr_transport_stalls', 'svr_stream_stalls')
