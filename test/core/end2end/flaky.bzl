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
"""A list of flaky tests, consumed by generate_tests.bzl to set flaky attrs."""
FLAKY_TESTS = [
    "h2_compress_test@max_connection_age",
    "h2_full+pipe_test@max_connection_age",
    "h2_full_no_retry_test@max_connection_age",
    "h2_full_test@max_connection_age",
    "h2_full_test@max_connection_idle",
    "h2_http_proxy_test@max_connection_age",
    "h2_http_proxy_test@retry_per_attempt_recv_timeout",
    "h2_insecure_test@max_connection_idle",
    "h2_local_abstract_uds_percent_encoded_test@max_connection_idle",
    "h2_local_ipv4_test@max_connection_idle",
    "h2_local_ipv6_test@max_connection_idle",
    "h2_local_uds_percent_encoded_test@max_connection_idle",
    "h2_local_uds_test@max_connection_idle",
    "h2_oauth2_test@max_connection_idle",
    "h2_oauth2_tls12_test@max_connection_idle",
    "h2_proxy_test@cancel_with_status",
    "h2_sockpair_1byte_test@max_connection_age",
    "h2_ssl_cred_reload_test@max_connection_idle",
    "h2_ssl_cred_reload_tls12_test@max_connection_age",
    "h2_ssl_proxy_test@retry_transparent_not_sent_on_wire",
    "h2_ssl_test@max_connection_idle",
    "h2_ssl_test@retry_cancellation",
    "h2_tls_certwatch_async_tls1_3_test@connectivity",
    "h2_tls_certwatch_async_tls1_3_test@max_connection_idle",
    "h2_tls_certwatch_async_tls1_3_test@retry_cancellation",
    "h2_tls_certwatch_async_tls1_3_test@simple_delayed_request",
    "h2_tls_certwatch_sync_tls1_2_test@connectivity",
    "h2_tls_certwatch_sync_tls1_2_test@max_connection_idle",
    "h2_tls_certwatch_sync_tls1_2_test@retry_cancellation",
    "h2_tls_certwatch_sync_tls1_2_test@simple_delayed_request",
    "h2_tls_simple_test@connectivity",
    "h2_tls_simple_test@max_connection_idle",
    "h2_tls_simple_test@simple_delayed_request",
    "h2_tls_static_async_tls1_3_test@connectivity",
    "h2_tls_static_async_tls1_3_test@max_connection_idle",
    "h2_tls_static_async_tls1_3_test@retry_cancellation",
    "h2_tls_static_async_tls1_3_test@simple_delayed_request",
    "h2_tls_test@connectivity",
    "h2_tls_test@hpack_size",
    "h2_tls_test@invoke_large_request",
    "h2_tls_test@max_connection_idle",
    "h2_tls_test@resource_quota_server",
    "h2_tls_test@retry_cancellation",
    "h2_tls_test@simple_delayed_request",
    "h2_uds_abstract_test@max_connection_age",
    "h2_uds_test@max_connection_idle",
]
