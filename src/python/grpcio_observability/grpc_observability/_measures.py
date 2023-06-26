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

from opencensus.stats import measure

# These measure definitions should be kept in sync across opencensus implementations.
# https://github.com/census-instrumentation/opencensus-java/blob/master/contrib/grpc_metrics/src/main/java/io/opencensus/contrib/grpc/metrics/RpcMeasureConstants.java.

# Unit constatns
UNIT_BYTES = "By"
UNIT_MILLISECONDS = "ms"
UNIT_COUNT = "1"

# Client
CLIENT_STARTED_RPCS_MEASURE = measure.MeasureInt(
    "grpc.io/client/started_rpcs",
    "The total number of client RPCs ever opened, including those that have not been completed.",
    UNIT_COUNT,
)

CLIENT_COMPLETED_RPCS_MEASURE = measure.MeasureInt(
    "grpc.io/client/completed_rpcs",
    "The total number of completed client RPCs",
    UNIT_COUNT,
)

CLIENT_ROUNDTRIP_LATENCY_MEASURE = measure.MeasureFloat(
    "grpc.io/client/roundtrip_latency",
    "Time between first byte of request sent to last byte of response received, or terminal error",
    UNIT_MILLISECONDS,
)

CLIENT_API_LATENCY_MEASURE = measure.MeasureInt(
    "grpc.io/client/api_latency",
    "End-to-end time taken to complete an RPC",
    UNIT_MILLISECONDS,
)

CLIENT_SEND_BYTES_PER_RPC_MEASURE = measure.MeasureFloat(
    "grpc.io/client/sent_bytes_per_rpc",
    "Total bytes sent across all request messages per RPC",
    UNIT_BYTES,
)

CLIENT_RECEIVED_BYTES_PER_RPC_MEASURE = measure.MeasureFloat(
    "grpc.io/client/received_bytes_per_rpc",
    "Total bytes received across all response messages per RPC",
    UNIT_BYTES,
)

# Server
SERVER_STARTED_RPCS_MEASURE = measure.MeasureInt(
    "grpc.io/server/started_rpcs",
    "Total bytes sent across all request messages per RPC",
    UNIT_COUNT,
)

SERVER_COMPLETED_RPCS_MEASURE = measure.MeasureInt(
    "grpc.io/server/completed_rpcs",
    "The total number of completed server RPCs",
    UNIT_COUNT,
)

SERVER_SENT_BYTES_PER_RPC_MEASURE = measure.MeasureFloat(
    "grpc.io/server/sent_bytes_per_rpc",
    "Total bytes sent across all messages per RPC",
    UNIT_BYTES,
)

SERVER_RECEIVED_BYTES_PER_RPC_MEASURE = measure.MeasureFloat(
    "grpc.io/server/received_bytes_per_rpc",
    "Total bytes received across all messages per RPC",
    UNIT_BYTES,
)

SERVER_SERVER_LATENCY_MEASURE = measure.MeasureFloat(
    "grpc.io/server/server_latency",
    "Time between first byte of request received to last byte of response sent, or terminal error",
    UNIT_MILLISECONDS,
)
