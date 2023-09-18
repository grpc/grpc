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
"""Utilities for stateful session affinity tests.

These utilities must be shared between test environments that configure SSA
via Kubernetes CRDs and environments that configure SSA directly through the
networkservices.googleapis.com API.
"""

import datetime
import logging

from typing import Sequence, Tuple

from framework.helpers import retryers
from framework import xds_k8s_testcase

_XdsKubernetesBaseTestCase = xds_k8s_testcase.XdsKubernetesBaseTestCase
_XdsTestServer = xds_k8s_testcase.XdsTestServer
_XdsTestClient = xds_k8s_testcase.XdsTestClient

_SET_COOKIE_MAX_WAIT_SEC = 300


def get_setcookie_headers(
    metadatas_by_peer: dict[str, "MetadataByPeer"]
) -> dict[str, str]:
    cookies = dict()
    for peer, metadatas in metadatas_by_peer.items():
        for metadatas in metadatas.rpc_metadata:
            for metadata in metadatas.metadata:
                if metadata.key.lower() == "set-cookie":
                    cookies[peer] = metadata.value
    return cookies


def assert_eventually_retrieve_cookie_and_server(
    test: _XdsKubernetesBaseTestCase,
    test_client: _XdsTestClient,
    servers: Sequence[_XdsTestServer],
) -> Tuple[str, _XdsTestServer]:
    """Retrieves the initial cookie and corresponding server.

    Given a test client and set of backends for which SSA is enabled, samples
    a single RPC from the test client to the backends, with metadata collection enabled.
    The "set-cookie" header is retrieved and its contents are returned along with the
    server to which it corresponds.

    Since SSA config is supplied as a separate resource from the Route resource,
    there will be periods of time where the SSA config may not be applied. This is
    therefore an eventually consistent function.
    """

    def _assert_retrieve_cookie_and_server():
        lb_stats = test.assertSuccessfulRpcs(test_client, 1)
        cookies = get_setcookie_headers(lb_stats.metadatas_by_peer)
        test.assertLen(cookies, 1)
        hostname = next(iter(cookies.keys()))
        cookie = cookies[hostname]

        chosen_server_candidates = tuple(
            srv for srv in servers if srv.hostname == hostname
        )
        test.assertLen(chosen_server_candidates, 1)
        chosen_server = chosen_server_candidates[0]
        return cookie, chosen_server

    retryer = retryers.constant_retryer(
        wait_fixed=datetime.timedelta(seconds=10),
        timeout=datetime.timedelta(seconds=_SET_COOKIE_MAX_WAIT_SEC),
        log_level=logging.INFO,
    )
    try:
        return retryer(_assert_retrieve_cookie_and_server)
    except retryers.RetryError as retry_error:
        logger.exception(
            "Rpcs did not go to expected servers before timeout %s",
            _SET_COOKIE_MAX_WAIT_SEC,
        )
        raise retry_error
