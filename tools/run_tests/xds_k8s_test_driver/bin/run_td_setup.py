# Copyright 2020 gRPC authors.
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
"""Configure Traffic Director for different GRPC Proxyless.

This is intended as a debugging / local development helper and not executed
as a part of interop test suites.

Typical usage examples:

    # Regular proxyless setup
    python -m bin.run_td_setup --flagfile=config/local-dev.cfg

    # Additional commands: cleanup, backend management, etc.
    python -m bin.run_td_setup --flagfile=config/local-dev.cfg --cmd=cleanup

    # PSM security setup options: mtls, tls, etc.
    python -m bin.run_td_setup --flagfile=config/local-dev.cfg --security=mtls

    # More information and usage options
    python -m bin.run_td_setup --helpful
"""
import logging
import uuid

from absl import app
from absl import flags

from framework import xds_flags
from framework import xds_k8s_flags
from framework.infrastructure import gcp
from framework.infrastructure import k8s
from framework.infrastructure import traffic_director
from framework.test_app import server_app

logger = logging.getLogger(__name__)
# Flags
_CMD = flags.DEFINE_enum('cmd',
                         default='create',
                         enum_values=[
                             'cycle', 'create', 'cleanup', 'backends-add',
                             'backends-cleanup'
                         ],
                         help='Command')
_SECURITY = flags.DEFINE_enum('security',
                              default=None,
                              enum_values=[
                                  'mtls', 'tls', 'plaintext', 'mtls_error',
                                  'server_authz_error'
                              ],
                              help='Configure TD with security')
flags.adopt_module_key_flags(xds_flags)
flags.adopt_module_key_flags(xds_k8s_flags)

_DEFAULT_SECURE_MODE_MAINTENANCE_PORT = \
    server_app.KubernetesServerRunner.DEFAULT_SECURE_MODE_MAINTENANCE_PORT


def main(argv):
    if len(argv) > 1:
        raise app.UsageError('Too many command-line arguments.')

    command = _CMD.value
    security_mode = _SECURITY.value

    project: str = xds_flags.PROJECT.value
    network: str = xds_flags.NETWORK.value
    namespace = xds_flags.NAMESPACE.value

    # Test server
    server_name = xds_flags.SERVER_NAME.value
    server_port = xds_flags.SERVER_PORT.value
    server_maintenance_port = xds_flags.SERVER_MAINTENANCE_PORT.value
    server_xds_host = xds_flags.SERVER_XDS_HOST.value
    server_xds_port = xds_flags.SERVER_XDS_PORT.value

    gcp_api_manager = gcp.api.GcpApiManager()

    if security_mode is None:
        td = traffic_director.TrafficDirectorManager(gcp_api_manager,
                                                     project=project,
                                                     resource_prefix=namespace,
                                                     network=network)
    else:
        td = traffic_director.TrafficDirectorSecureManager(
            gcp_api_manager,
            project=project,
            resource_prefix=namespace,
            network=network)
        if server_maintenance_port is None:
            server_maintenance_port = _DEFAULT_SECURE_MODE_MAINTENANCE_PORT

    try:
        if command in ('create', 'cycle'):
            logger.info('Create mode')
            if security_mode is None:
                logger.info('No security')
                td.setup_for_grpc(server_xds_host,
                                  server_xds_port,
                                  health_check_port=server_maintenance_port)

            elif security_mode == 'mtls':
                logger.info('Setting up mtls')
                td.setup_for_grpc(server_xds_host,
                                  server_xds_port,
                                  health_check_port=server_maintenance_port)
                td.setup_server_security(server_namespace=namespace,
                                         server_name=server_name,
                                         server_port=server_port,
                                         tls=True,
                                         mtls=True)
                td.setup_client_security(server_namespace=namespace,
                                         server_name=server_name,
                                         tls=True,
                                         mtls=True)

            elif security_mode == 'tls':
                logger.info('Setting up tls')
                td.setup_for_grpc(server_xds_host,
                                  server_xds_port,
                                  health_check_port=server_maintenance_port)
                td.setup_server_security(server_namespace=namespace,
                                         server_name=server_name,
                                         server_port=server_port,
                                         tls=True,
                                         mtls=False)
                td.setup_client_security(server_namespace=namespace,
                                         server_name=server_name,
                                         tls=True,
                                         mtls=False)

            elif security_mode == 'plaintext':
                logger.info('Setting up plaintext')
                td.setup_for_grpc(server_xds_host,
                                  server_xds_port,
                                  health_check_port=server_maintenance_port)
                td.setup_server_security(server_namespace=namespace,
                                         server_name=server_name,
                                         server_port=server_port,
                                         tls=False,
                                         mtls=False)
                td.setup_client_security(server_namespace=namespace,
                                         server_name=server_name,
                                         tls=False,
                                         mtls=False)

            elif security_mode == 'mtls_error':
                # Error case: server expects client mTLS cert,
                # but client configured only for TLS
                logger.info('Setting up mtls_error')
                td.setup_for_grpc(server_xds_host,
                                  server_xds_port,
                                  health_check_port=server_maintenance_port)
                td.setup_server_security(server_namespace=namespace,
                                         server_name=server_name,
                                         server_port=server_port,
                                         tls=True,
                                         mtls=True)
                td.setup_client_security(server_namespace=namespace,
                                         server_name=server_name,
                                         tls=True,
                                         mtls=False)

            elif security_mode == 'server_authz_error':
                # Error case: client does not authorize server
                # because of mismatched SAN name.
                logger.info('Setting up mtls_error')
                td.setup_for_grpc(server_xds_host,
                                  server_xds_port,
                                  health_check_port=server_maintenance_port)
                # Regular TLS setup, but with client policy configured using
                # intentionality incorrect server_namespace.
                td.setup_server_security(server_namespace=namespace,
                                         server_name=server_name,
                                         server_port=server_port,
                                         tls=True,
                                         mtls=False)
                incorrect_namespace = f'incorrect-namespace-{uuid.uuid4().hex}'
                td.setup_client_security(server_namespace=incorrect_namespace,
                                         server_name=server_name,
                                         tls=True,
                                         mtls=False)

            logger.info('Works!')
    except Exception:  # noqa pylint: disable=broad-except
        logger.exception('Got error during creation')

    if command in ('cleanup', 'cycle'):
        logger.info('Cleaning up')
        td.cleanup(force=True)

    if command == 'backends-add':
        logger.info('Adding backends')
        k8s_api_manager = k8s.KubernetesApiManager(
            xds_k8s_flags.KUBE_CONTEXT.value)
        k8s_namespace = k8s.KubernetesNamespace(k8s_api_manager, namespace)

        neg_name, neg_zones = k8s_namespace.get_service_neg(
            server_name, server_port)

        td.load_backend_service()
        td.backend_service_add_neg_backends(neg_name, neg_zones)
        td.wait_for_backends_healthy_status()
        # TODO(sergiitk): wait until client reports rpc health
    elif command == 'backends-cleanup':
        td.load_backend_service()
        td.backend_service_remove_all_backends()


if __name__ == '__main__':
    app.run(main)
