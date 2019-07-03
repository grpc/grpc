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
""" QPS Scenarios to run """

_SCENARIOS = {
    'large-message-throughput':
    '{"scenarios":[{"name":"large-message-throughput", "spawn_local_worker_count": -2, "warmup_seconds": 30, "benchmark_seconds": 270, "num_servers": 1, "server_config": {"async_server_threads": 1, "security_params": null, "server_type": "ASYNC_SERVER"}, "num_clients": 1, "client_config": {"client_type": "ASYNC_CLIENT", "security_params": null, "payload_config": {"simple_params": {"resp_size": 1048576, "req_size": 1048576}}, "client_channels": 1, "async_client_threads": 1, "outstanding_rpcs_per_channel": 1, "rpc_type": "UNARY", "load_params": {"closed_loop": {}}, "histogram_params": {"max_possible": 60000000000.0, "resolution": 0.01}}}]}',
    'multi-channel-64-KiB':
    '{"scenarios":[{"name":"multi-channel-64-KiB", "spawn_local_worker_count": -3, "warmup_seconds": 30, "benchmark_seconds": 270, "num_servers": 1, "server_config": {"async_server_threads": 31, "security_params": null, "server_type": "ASYNC_SERVER"}, "num_clients": 2, "client_config": {"client_type": "ASYNC_CLIENT", "security_params": null, "payload_config": {"simple_params": {"resp_size": 65536, "req_size": 65536}}, "client_channels": 32, "async_client_threads": 31, "outstanding_rpcs_per_channel": 100, "rpc_type": "UNARY", "load_params": {"closed_loop": {}}, "histogram_params": {"max_possible": 60000000000.0, "resolution": 0.01}}}]}'
}
