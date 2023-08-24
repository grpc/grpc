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
from absl import app
from absl import flags
from absl import logging

from bin.lib import common
from framework import xds_flags
from framework import xds_k8s_flags
from framework.helpers import grpc as helpers_grpc
import framework.helpers.highlighter
from framework.infrastructure import gcp
from framework.infrastructure import k8s
from framework.rpc import grpc_channelz
from framework.rpc import grpc_testing
from framework.test_app import client_app
from framework.test_app import server_app

# Flags
_MODE = flags.DEFINE_enum(
    "mode",
    default="default",
    enum_values=["default", "secure", "gamma"],
    help="Select a deployment of the client/server",
)
_NUM_RPCS = flags.DEFINE_integer(
    "num_rpcs",
    default=100,
    lower_bound=1,
    upper_bound=10_000,
    help="The number of RPCs to check.",
)
flags.adopt_module_key_flags(xds_flags)
flags.adopt_module_key_flags(xds_k8s_flags)
# Running outside of a test suite, so require explicit resource_suffix.
flags.mark_flag_as_required(xds_flags.RESOURCE_SUFFIX.name)
flags.register_validator(
    xds_flags.SERVER_XDS_PORT.name,
    lambda val: val > 0,
    message=(
        "Run outside of a test suite, must provide"
        " the exact port value (must be greater than 0)."
    ),
)

logger = logging.get_absl_logger()

# Type aliases
_Channel = grpc_channelz.Channel
_Socket = grpc_channelz.Socket
_ChannelState = grpc_channelz.ChannelState
_XdsTestServer = server_app.XdsTestServer
_XdsTestClient = client_app.XdsTestClient
LoadBalancerStatsResponse = grpc_testing.LoadBalancerStatsResponse


def get_client_rpc_stats(
    test_client: _XdsTestClient, num_rpcs: int
) -> LoadBalancerStatsResponse:
    lb_stats = test_client.get_load_balancer_stats(num_rpcs=num_rpcs)
    hl = framework.helpers.highlighter.HighlighterYaml()
    logger.info(
        "[%s] Received LoadBalancerStatsResponse:\n%s",
        test_client.hostname,
        hl.highlight(helpers_grpc.lb_stats_pretty(lb_stats)),
    )
    return lb_stats


def run_ping_pong(test_client: _XdsTestClient, num_rpcs: int):
    test_client.wait_for_active_server_channel()
    lb_stats = get_client_rpc_stats(test_client, num_rpcs)
    for backend, rpcs_count in lb_stats.rpcs_by_peer.items():
        if int(rpcs_count) < 1:
            raise AssertionError(
                f"Backend {backend} did not receive a single RPC"
            )

    failed = int(lb_stats.num_failures)
    if int(lb_stats.num_failures) > 0:
        raise AssertionError(
            f"Expected all RPCs to succeed: {failed} of {num_rpcs} failed"
        )


def main(argv):
    if len(argv) > 1:
        raise app.UsageError("Too many command-line arguments.")

    # Must be called before KubernetesApiManager or GcpApiManager init.
    xds_flags.set_socket_default_timeout_from_flag()

    # Flags.
    should_port_forward: bool = xds_k8s_flags.DEBUG_USE_PORT_FORWARDING.value
    mode: str = "secure" if is_secure else "default"

    # Setup.
    gcp_api_manager = gcp.api.GcpApiManager()
    k8s_api_manager = k8s.KubernetesApiManager(xds_k8s_flags.KUBE_CONTEXT.value)

    # Server.
    server_namespace = common.make_server_namespace(k8s_api_manager)
    server_runner = common.make_server_runner(
        server_namespace,
        gcp_api_manager,
        port_forwarding=should_port_forward,
        mode=mode,
    )
    # Find server pod.
    server_pod: k8s.V1Pod = common.get_server_pod(
        server_runner, xds_flags.SERVER_NAME.value
    )

    # Client
    client_namespace = common.make_client_namespace(k8s_api_manager)
    client_runner = common.make_client_runner(
        client_namespace,
        gcp_api_manager,
        port_forwarding=should_port_forward,
        mode=mode,
    )
    # Find client pod.
    client_pod: k8s.V1Pod = common.get_client_pod(
        client_runner, xds_flags.CLIENT_NAME.value
    )

    # Ensure port forwarding stopped.
    common.register_graceful_exit(server_runner, client_runner)

    # Create server app for the server pod.
    test_server: _XdsTestServer = common.get_test_server_for_pod(
        server_runner,
        server_pod,
        test_port=xds_flags.SERVER_PORT.value,
        secure_mode=(mode == "secure"),
    )
    test_server.set_xds_address(
        xds_flags.SERVER_XDS_HOST.value, xds_flags.SERVER_XDS_PORT.value
    )

    # Create client app for the client pod.
    test_client: _XdsTestClient = common.get_test_client_for_pod(
        client_runner, client_pod, server_target=test_server.xds_uri
    )

    with test_client, test_server:
        run_ping_pong(test_client, _NUM_RPCS.value)

    logger.info("SUCCESS!")


if __name__ == "__main__":
    app.run(main)
