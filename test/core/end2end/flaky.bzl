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
    "h2_full+pipe_test@retry_per_attempt_recv_timeout",
    "h2_full_no_retry_test@max_connection_age",
    "h2_insecure_test@retry_per_attempt_recv_timeout",
    "h2_local_abstract_uds_percent_encoded_test@resource_quota_server",
    "h2_proxy_test@cancel_with_status",
    "h2_ssl_proxy_test@cancel_after_accept",
    "h2_ssl_proxy_test@retry_transparent_not_sent_on_wire",
    "h2_ssl_tls13_test@cancel_after_invoke",
    "h2_ssl_tls13_test@retry_per_attempt_recv_timeout",
    "h2_uds_abstract_test@resource_quota_server",
]
