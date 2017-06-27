# Copyright 2017, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
""" QPS Scenarios to run """

_SCENARIOS = {
  'large-message-throughput': '{"scenarios":[{"name":"large-message-throughput", "spawn_local_worker_count": -2, "warmup_seconds": 30, "benchmark_seconds": 270, "num_servers": 1, "server_config": {"async_server_threads": 1, "security_params": null, "server_type": "ASYNC_SERVER"}, "num_clients": 1, "client_config": {"client_type": "ASYNC_CLIENT", "security_params": null, "payload_config": {"simple_params": {"resp_size": 1048576, "req_size": 1048576}}, "client_channels": 1, "async_client_threads": 1, "outstanding_rpcs_per_channel": 1, "rpc_type": "UNARY", "load_params": {"closed_loop": {}}, "histogram_params": {"max_possible": 60000000000.0, "resolution": 0.01}}}]}',
  'multi-channel-64-KiB': '{"scenarios":[{"name":"multi-channel-64-KiB", "spawn_local_worker_count": -3, "warmup_seconds": 30, "benchmark_seconds": 270, "num_servers": 1, "server_config": {"async_server_threads": 31, "security_params": null, "server_type": "ASYNC_SERVER"}, "num_clients": 2, "client_config": {"client_type": "ASYNC_CLIENT", "security_params": null, "payload_config": {"simple_params": {"resp_size": 65536, "req_size": 65536}}, "client_channels": 32, "async_client_threads": 31, "outstanding_rpcs_per_channel": 100, "rpc_type": "UNARY", "load_params": {"closed_loop": {}}, "histogram_params": {"max_possible": 60000000000.0, "resolution": 0.01}}}]}'
}
