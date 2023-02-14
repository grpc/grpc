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

from typing import Mapping

from grpc_observability import _measures
from opencensus.stats import aggregation as aggregation_module
from opencensus.stats import view as view_module
from opencensus.tags.tag_key import TagKey

# from grpc_observability import observability


# These measure definitions should be kept in sync across opencensus
# implementations--see
# https://github.com/census-instrumentation/opencensus-java/blob/master/contrib/grpc_metrics/src/main/java/io/opencensus/contrib/grpc/metrics/RpcMeasureConstants.java.
def client_method_tag_key():
    return TagKey("grpc_client_method")


def client_status_tag_key():
    return TagKey("grpc_client_status")


def server_method_tag_key():
    return TagKey("grpc_server_method")


def server_status_tag_key():
    return TagKey("server_status_tag_key")


def count_distribution_aggregation(
) -> aggregation_module.DistributionAggregation:
    exponential_boundaries = _get_exponential_boundaries(17, 1.0, 2.0)
    return aggregation_module.DistributionAggregation(exponential_boundaries)


def bytes_distribution_aggregation(
) -> aggregation_module.DistributionAggregation:
    return aggregation_module.DistributionAggregation([
        1024, 2048, 4096, 16384, 65536, 262144, 1048576, 4194304, 16777216,
        67108864, 268435456, 1073741824, 4294967296
    ])


def millis_distribution_aggregation(
) -> aggregation_module.DistributionAggregation:
    return aggregation_module.DistributionAggregation([
        0.01, 0.05, 0.1, 0.3, 0.6, 0.8, 1, 2, 3, 4, 5, 6, 8, 10, 13, 16, 20, 25,
        30, 40, 50, 65, 80, 100, 130, 160, 200, 250, 300, 400, 500, 650, 800,
        1000, 2000, 5000, 10000, 20000, 50000, 100000
    ])


# Client
# TODO(xuanwn): Change MOCK Descriptions.
def client_started_rpcs(labels: Mapping[str, str]) -> view_module.View:
    view = view_module.View("grpc.io/client/started_rpcs", "MOCK Description",
                            [TagKey(key) for key in labels.keys()] +
                            [client_method_tag_key()],
                            _measures.rpc_client_started_rpcs(),
                            aggregation_module.CountAggregation())
    return view


def client_completed_rpcs(labels: Mapping[str, str]) -> view_module.View:
    view = view_module.View("grpc.io/client/completed_rpcs", "MOCK Description",
                            [TagKey(key) for key in labels.keys()] +
                            [client_method_tag_key(),
                             client_status_tag_key()],
                            _measures.rpc_client_roundtrip_latency(),
                            aggregation_module.CountAggregation())
    return view


def client_roundtrip_latency(labels: Mapping[str, str]) -> view_module.View:
    view = view_module.View(
        "grpc.io/client/roundtrip_latency", "MOCK Description",
        [TagKey(key) for key in labels.keys()] + [client_method_tag_key()],
        _measures.rpc_client_roundtrip_latency(),
        millis_distribution_aggregation())
    return view


def client_api_latency(labels: Mapping[str, str]) -> view_module.View:
    view = view_module.View("grpc.io/client/api_latency", "MOCK Description",
                            [TagKey(key) for key in labels.keys()] +
                            [client_method_tag_key(),
                             client_status_tag_key()],
                            _measures.rpc_client_api_latency(),
                            millis_distribution_aggregation())
    return view


def client_sent_compressed_message_bytes_per_rpc(
        labels: Mapping[str, str]) -> view_module.View:
    view = view_module.View(
        "grpc.io/client/sent_compressed_message_bytes_per_rpc",
        "MOCK Description", [TagKey(key) for key in labels.keys()] +
        [client_method_tag_key(),
         client_status_tag_key()], _measures.rpc_client_send_bytes_per_prc(),
        bytes_distribution_aggregation())
    return view


def client_received_compressed_message_bytes_per_rpc(
        labels: Mapping[str, str]) -> view_module.View:
    view = view_module.View(
        "grpc.io/client/received_compressed_message_bytes_per_rpc",
        "MOCK Description", [TagKey(key) for key in labels.keys()] +
        [client_method_tag_key(),
         client_status_tag_key()],
        _measures.rpc_client_received_bytes_per_rpc(),
        bytes_distribution_aggregation())
    return view


# Server
def server_started_rpcs(labels: Mapping[str, str]) -> view_module.View:
    view = view_module.View("grpc.io/server/started_rpcs", "MOCK Description",
                            [TagKey(key) for key in labels.keys()] +
                            [server_method_tag_key()],
                            _measures.rpc_server_started_rpcs(),
                            aggregation_module.CountAggregation())
    return view


def server_completed_rpcs(labels: Mapping[str, str]) -> view_module.View:
    view = view_module.View("grpc.io/server/completed_rpcs", "MOCK Description",
                            [TagKey(key) for key in labels.keys()] +
                            [server_method_tag_key(),
                             server_status_tag_key()],
                            _measures.rpc_server_server_latency(),
                            aggregation_module.CountAggregation())
    return view


def server_sent_compressed_message_bytes_per_rpc(
        labels: Mapping[str, str]) -> view_module.View:
    view = view_module.View(
        "grpc.io/server/sent_compressed_message_bytes_per_rpc",
        "MOCK Description", [TagKey(key) for key in labels.keys()] +
        [server_method_tag_key(),
         server_status_tag_key()], _measures.rpc_server_sent_bytes_per_rpc(),
        bytes_distribution_aggregation())
    return view


def server_received_compressed_message_bytes_per_rpc(
        labels: Mapping[str, str]) -> view_module.View:
    view = view_module.View(
        "grpc.io/server/received_compressed_message_bytes_per_rpc",
        "MOCK Description", [TagKey(key) for key in labels.keys()] +
        [server_method_tag_key(),
         server_status_tag_key()],
        _measures.rpc_server_received_bytes_per_rpc(),
        bytes_distribution_aggregation())
    return view


def server_server_latency(labels: Mapping[str, str]) -> view_module.View:
    view = view_module.View("grpc.io/server/server_latency", "MOCK Description",
                            [TagKey(key) for key in labels.keys()] +
                            [server_method_tag_key(),
                             server_status_tag_key()],
                            _measures.rpc_server_server_latency(),
                            millis_distribution_aggregation())
    return view


def _get_exponential_boundaries(num_finite_buckets: int, scale: float,
                                grrowth_factor: float) -> list:
    boundaries = []
    upper_bound = scale
    for _ in range(num_finite_buckets):
        boundaries.append(upper_bound)
        upper_bound *= grrowth_factor
    print(
        f"_get_exponential_boundaries num_finite_buckets: {num_finite_buckets} boundaries size: {len(boundaries)}\n"
    )
    return boundaries
