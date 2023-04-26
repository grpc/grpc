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
"""Common functionality for bin/ python helpers."""
import atexit
import signal
import sys

from absl import logging

from framework import xds_flags
from framework import xds_k8s_flags
from framework.infrastructure import gcp
from framework.infrastructure import k8s
from framework.test_app import client_app
from framework.test_app import server_app
from framework.test_app.runners.k8s import k8s_xds_client_runner
from framework.test_app.runners.k8s import k8s_xds_server_runner

logger = logging.get_absl_logger()

# Type aliases
KubernetesClientRunner = k8s_xds_client_runner.KubernetesClientRunner
KubernetesServerRunner = k8s_xds_server_runner.KubernetesServerRunner
_XdsTestServer = server_app.XdsTestServer
_XdsTestClient = client_app.XdsTestClient


def make_client_namespace(
        k8s_api_manager: k8s.KubernetesApiManager) -> k8s.KubernetesNamespace:
    namespace_name: str = KubernetesClientRunner.make_namespace_name(
        xds_flags.RESOURCE_PREFIX.value, xds_flags.RESOURCE_SUFFIX.value)
    return k8s.KubernetesNamespace(k8s_api_manager, namespace_name)


def make_client_runner(namespace: k8s.KubernetesNamespace,
                       gcp_api_manager: gcp.api.GcpApiManager,
                       port_forwarding: bool = False,
                       reuse_namespace: bool = True,
                       secure: bool = False) -> KubernetesClientRunner:
    # KubernetesClientRunner arguments.
    runner_kwargs = dict(
        deployment_name=xds_flags.CLIENT_NAME.value,
        image_name=xds_k8s_flags.CLIENT_IMAGE.value,
        td_bootstrap_image=xds_k8s_flags.TD_BOOTSTRAP_IMAGE.value,
        gcp_project=xds_flags.PROJECT.value,
        gcp_api_manager=gcp_api_manager,
        gcp_service_account=xds_k8s_flags.GCP_SERVICE_ACCOUNT.value,
        xds_server_uri=xds_flags.XDS_SERVER_URI.value,
        network=xds_flags.NETWORK.value,
        stats_port=xds_flags.CLIENT_PORT.value,
        reuse_namespace=reuse_namespace,
        debug_use_port_forwarding=port_forwarding)

    if secure:
        runner_kwargs.update(
            deployment_template='client-secure.deployment.yaml')
    return KubernetesClientRunner(namespace, **runner_kwargs)


def make_server_namespace(
        k8s_api_manager: k8s.KubernetesApiManager) -> k8s.KubernetesNamespace:
    namespace_name: str = KubernetesServerRunner.make_namespace_name(
        xds_flags.RESOURCE_PREFIX.value, xds_flags.RESOURCE_SUFFIX.value)
    return k8s.KubernetesNamespace(k8s_api_manager, namespace_name)


def make_server_runner(namespace: k8s.KubernetesNamespace,
                       gcp_api_manager: gcp.api.GcpApiManager,
                       port_forwarding: bool = False,
                       reuse_namespace: bool = True,
                       reuse_service: bool = False,
                       secure: bool = False) -> KubernetesServerRunner:
    # KubernetesServerRunner arguments.
    runner_kwargs = dict(
        deployment_name=xds_flags.SERVER_NAME.value,
        image_name=xds_k8s_flags.SERVER_IMAGE.value,
        td_bootstrap_image=xds_k8s_flags.TD_BOOTSTRAP_IMAGE.value,
        gcp_project=xds_flags.PROJECT.value,
        gcp_api_manager=gcp_api_manager,
        gcp_service_account=xds_k8s_flags.GCP_SERVICE_ACCOUNT.value,
        network=xds_flags.NETWORK.value,
        reuse_namespace=reuse_namespace,
        reuse_service=reuse_service,
        debug_use_port_forwarding=port_forwarding)

    if secure:
        runner_kwargs.update(
            xds_server_uri=xds_flags.XDS_SERVER_URI.value,
            deployment_template='server-secure.deployment.yaml')

    return KubernetesServerRunner(namespace, **runner_kwargs)


def _ensure_atexit(signum, frame):
    """Needed to handle signals or atexit handler won't be called."""
    del frame

    # Pylint is wrong about "Module 'signal' has no 'Signals' member":
    # https://docs.python.org/3/library/signal.html#signal.Signals
    sig = signal.Signals(signum)  # pylint: disable=no-member
    logger.warning('Caught %r, initiating graceful shutdown...\n', sig)
    sys.exit(1)


def _graceful_exit(server_runner: KubernetesServerRunner,
                   client_runner: KubernetesClientRunner):
    """Stop port forwarding processes."""
    client_runner.stop_pod_dependencies()
    server_runner.stop_pod_dependencies()


def register_graceful_exit(server_runner: KubernetesServerRunner,
                           client_runner: KubernetesClientRunner):
    atexit.register(_graceful_exit, server_runner, client_runner)
    for signum in (signal.SIGTERM, signal.SIGHUP, signal.SIGINT):
        signal.signal(signum, _ensure_atexit)


def get_client_pod(client_runner: KubernetesClientRunner,
                   deployment_name: str) -> k8s.V1Pod:
    client_deployment: k8s.V1Deployment
    client_deployment = client_runner.k8s_namespace.get_deployment(
        deployment_name)
    client_pod_name: str = client_runner._wait_deployment_pod_count(
        client_deployment)[0]
    return client_runner._wait_pod_started(client_pod_name)


def get_server_pod(server_runner: KubernetesServerRunner,
                   deployment_name: str) -> k8s.V1Pod:
    server_deployment: k8s.V1Deployment
    server_deployment = server_runner.k8s_namespace.get_deployment(
        deployment_name)
    server_pod_name: str = server_runner._wait_deployment_pod_count(
        server_deployment)[0]
    return server_runner._wait_pod_started(server_pod_name)


def get_test_server_for_pod(server_runner: KubernetesServerRunner,
                            server_pod: k8s.V1Pod, **kwargs) -> _XdsTestServer:
    return server_runner._xds_test_server_for_pod(server_pod, **kwargs)


def get_test_client_for_pod(client_runner: KubernetesClientRunner,
                            client_pod: k8s.V1Pod, **kwargs) -> _XdsTestClient:
    return client_runner._xds_test_client_for_pod(client_pod, **kwargs)
