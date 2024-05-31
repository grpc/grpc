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

import collections
from typing import List

from grpc_observability._cyobservability import MetricsName


class Metric(
    collections.namedtuple(
        "Metric",
        ["name", "cyname", "unit", "description"],
    )
):
    pass


CLIENT_ATTEMPT_STARTED = Metric(
    "grpc.client.attempt.started",
    MetricsName.CLIENT_STARTED_RPCS,
    "{attempt}",
    "Number of client call attempts started",
)
CLIENT_ATTEMPT_DURATION = Metric(
    "grpc.client.attempt.duration",
    MetricsName.CLIENT_ROUNDTRIP_LATENCY,
    "s",
    "End-to-end time taken to complete a client call attempt",
)
CLIENT_RPC_DURATION = Metric(
    "grpc.client.call.duration",
    MetricsName.CLIENT_API_LATENCY,
    "s",
    "End-to-end time taken to complete a call from client's perspective",
)
CLIENT_ATTEMPT_SEND_BYTES = Metric(
    "grpc.client.attempt.sent_total_compressed_message_size",
    MetricsName.CLIENT_SEND_BYTES_PER_RPC,
    "By",
    "Compressed message bytes sent per client call attempt",
)
CLIENT_ATTEMPT_RECEIVED_BYTES = Metric(
    "grpc.client.attempt.rcvd_total_compressed_message_size",
    MetricsName.CLIENT_RECEIVED_BYTES_PER_RPC,
    "By",
    "Compressed message bytes received per call attempt",
)
SERVER_STARTED_RPCS = Metric(
    "grpc.server.call.started",
    MetricsName.SERVER_STARTED_RPCS,
    "{call}",
    "Number of server calls started",
)
SERVER_RPC_DURATION = Metric(
    "grpc.server.call.duration",
    MetricsName.SERVER_SERVER_LATENCY,
    "s",
    "End-to-end time taken to complete a call from server transport's perspective",
)
SERVER_RPC_SEND_BYTES = Metric(
    "grpc.server.call.sent_total_compressed_message_size",
    MetricsName.SERVER_SENT_BYTES_PER_RPC,
    "By",
    "Compressed message bytes sent per server call",
)
SERVER_RPC_RECEIVED_BYTES = Metric(
    "grpc.server.call.rcvd_total_compressed_message_size",
    MetricsName.SERVER_RECEIVED_BYTES_PER_RPC,
    "By",
    "Compressed message bytes received per server call",
)


def base_metrics() -> List[Metric]:
    return [
        CLIENT_ATTEMPT_STARTED,
        CLIENT_ATTEMPT_DURATION,
        # CLIENT_RPC_DURATION is not required yet
        # CLIENT_RPC_DURATION,
        CLIENT_ATTEMPT_SEND_BYTES,
        CLIENT_ATTEMPT_RECEIVED_BYTES,
        SERVER_STARTED_RPCS,
        SERVER_RPC_DURATION,
        SERVER_RPC_SEND_BYTES,
        SERVER_RPC_RECEIVED_BYTES,
    ]
