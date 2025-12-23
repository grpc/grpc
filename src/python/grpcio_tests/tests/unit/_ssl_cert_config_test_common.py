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
"""Shared utilities for SSL certificate configuration tests.

This module contains common code used by both server-side and client-side
SSL certificate rotation tests.
"""

import collections
import threading

import grpc

from tests.unit import resources

# Certificate constants used across all SSL cert config tests
CA_1_PEM = resources.cert_hier_1_root_ca_cert()
CA_2_PEM = resources.cert_hier_2_root_ca_cert()

CLIENT_KEY_1_PEM = resources.cert_hier_1_client_1_key()
CLIENT_CERT_CHAIN_1_PEM = (
    resources.cert_hier_1_client_1_cert()
    + resources.cert_hier_1_intermediate_ca_cert()
)

CLIENT_KEY_2_PEM = resources.cert_hier_2_client_1_key()
CLIENT_CERT_CHAIN_2_PEM = (
    resources.cert_hier_2_client_1_cert()
    + resources.cert_hier_2_intermediate_ca_cert()
)

SERVER_KEY_1_PEM = resources.cert_hier_1_server_1_key()
SERVER_CERT_CHAIN_1_PEM = (
    resources.cert_hier_1_server_1_cert()
    + resources.cert_hier_1_intermediate_ca_cert()
)

SERVER_KEY_2_PEM = resources.cert_hier_2_server_1_key()
SERVER_CERT_CHAIN_2_PEM = (
    resources.cert_hier_2_server_1_cert()
    + resources.cert_hier_2_intermediate_ca_cert()
)

# For use with the CertConfigFetcher. Roughly a simple custom mock
# implementation
Call = collections.namedtuple("Call", ["did_raise", "returned_cert_config"])


def create_channel(port, credentials):
    """Create a secure channel to localhost on the given port."""
    return grpc.secure_channel("localhost:{}".format(port), credentials)


def create_client_stub(channel, expect_success):
    """Create a FirstServiceStub from the given channel.

    Args:
        channel: The gRPC channel to use
        expect_success: If True, wait for channel to be ready before returning

    Returns:
        A FirstServiceStub instance
    """
    from tests.testing.proto import services_pb2_grpc

    if expect_success:
        # per Nathaniel: there's some robustness issue if we start
        # using a channel without waiting for it to be actually ready
        grpc.channel_ready_future(channel).result(timeout=10)
    return services_pb2_grpc.FirstServiceStub(channel)


class CertConfigFetcher(object):
    """Thread-safe mock certificate configuration fetcher.

    This class simulates a certificate configuration callback that can be
    configured to either return a certificate configuration or raise an
    exception. It tracks all calls made to it for test verification.
    """

    def __init__(self):
        self._lock = threading.Lock()
        self._calls = []
        self._should_raise = False
        self._cert_config = None

    def reset(self):
        """Clear all recorded calls and reset configuration."""
        with self._lock:
            self._calls = []
            self._should_raise = False
            self._cert_config = None

    def configure(self, should_raise, cert_config):
        """Configure the fetcher's behavior.

        Args:
            should_raise: If True, the fetcher will raise ValueError when called
            cert_config: The certificate configuration to return (if not raising)

        Raises:
            AssertionError: If both should_raise and cert_config are specified
        """
        assert not (should_raise and cert_config), (
            "should not specify both should_raise and a cert_config at the same"
            " time"
        )
        with self._lock:
            self._should_raise = should_raise
            self._cert_config = cert_config

    def getCalls(self):
        """Get a list of all calls made to this fetcher.

        Returns:
            A list of Call namedtuples with (did_raise, returned_cert_config)
        """
        with self._lock:
            return self._calls

    def __call__(self):
        """Invoke the fetcher callback.

        Returns:
            The configured certificate configuration (if not raising)

        Raises:
            ValueError: If configured with should_raise=True
        """
        with self._lock:
            if self._should_raise:
                self._calls.append(Call(True, None))
                raise ValueError("just for fun, should not affect the test")
            else:
                self._calls.append(Call(False, self._cert_config))
                return self._cert_config
