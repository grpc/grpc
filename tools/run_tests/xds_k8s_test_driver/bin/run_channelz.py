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
"""Channelz debugging tool for xDS test client/server.

This is intended as a debugging / local development helper and not executed
as a part of interop test suites.

Typical usage examples:

    # Show channel and server socket pair
    python -m bin.run_channelz --flagfile=config/local-dev.cfg

    # Evaluate setup for different security configurations
    python -m bin.run_channelz --flagfile=config/local-dev.cfg --security=tls
    python -m bin.run_channelz --flagfile=config/local-dev.cfg --security=mtls_error

    # More information and usage options
    python -m bin.run_channelz --helpful
"""
import hashlib
import logging

from absl import app
from absl import flags

from framework import xds_flags
from framework import xds_k8s_flags
from framework.infrastructure import k8s
from framework.rpc import grpc_channelz
from framework.test_app import client_app
from framework.test_app import server_app

logger = logging.getLogger(__name__)
# Flags
_SERVER_RPC_HOST = flags.DEFINE_string('server_rpc_host',
                                       default='127.0.0.1',
                                       help='Server RPC host')
_CLIENT_RPC_HOST = flags.DEFINE_string('client_rpc_host',
                                       default='127.0.0.1',
                                       help='Client RPC host')
_SECURITY = flags.DEFINE_enum('security',
                              default=None,
                              enum_values=[
                                  'mtls', 'tls', 'plaintext', 'mtls_error',
                                  'server_authz_error'
                              ],
                              help='Show info for a security setup')
flags.adopt_module_key_flags(xds_flags)
flags.adopt_module_key_flags(xds_k8s_flags)

# Type aliases
_Channel = grpc_channelz.Channel
_Socket = grpc_channelz.Socket
_ChannelState = grpc_channelz.ChannelState
_XdsTestServer = server_app.XdsTestServer
_XdsTestClient = client_app.XdsTestClient


def debug_cert(cert):
    if not cert:
        return '<missing>'
    sha1 = hashlib.sha1(cert)
    return f'sha1={sha1.hexdigest()}, len={len(cert)}'


def debug_sock_tls(tls):
    return (f'local:  {debug_cert(tls.local_certificate)}\n'
            f'remote: {debug_cert(tls.remote_certificate)}')


def get_deployment_pod_ips(k8s_ns, deployment_name):
    deployment = k8s_ns.get_deployment(deployment_name)
    pods = k8s_ns.list_deployment_pods(deployment)
    return [pod.status.pod_ip for pod in pods]


def debug_security_setup_negative(test_client):
    """Debug negative cases: mTLS Error, Server AuthZ error

    1) mTLS Error: Server expects client mTLS cert,
       but client configured only for TLS.
    2) AuthZ error: Client does not authorize server because of mismatched
       SAN name.
    """
    # Client side.
    client_correct_setup = True
    channel: _Channel = test_client.wait_for_server_channel_state(
        state=_ChannelState.TRANSIENT_FAILURE)
    try:
        subchannel, *subchannels = list(
            test_client.channelz.list_channel_subchannels(channel))
    except ValueError:
        print("Client setup fail: subchannel not found. "
              "Common causes: test client didn't connect to TD; "
              "test client exhausted retries, and closed all subchannels.")
        return

    # Client must have exactly one subchannel.
    logger.debug('Found subchannel, %s', subchannel)
    if subchannels:
        client_correct_setup = False
        print(f'Unexpected subchannels {subchannels}')
    subchannel_state: _ChannelState = subchannel.data.state.state
    if subchannel_state is not _ChannelState.TRANSIENT_FAILURE:
        client_correct_setup = False
        print('Subchannel expected to be in '
              'TRANSIENT_FAILURE, same as its channel')

    # Client subchannel must have no sockets.
    sockets = list(test_client.channelz.list_subchannels_sockets(subchannel))
    if sockets:
        client_correct_setup = False
        print(f'Unexpected subchannel sockets {sockets}')

    # Results.
    if client_correct_setup:
        print('Client setup pass: the channel '
              'to the server has exactly one subchannel '
              'in TRANSIENT_FAILURE, and no sockets')


def debug_security_setup_positive(test_client, test_server):
    """Debug positive cases: mTLS, TLS, Plaintext."""
    test_client.wait_for_active_server_channel()
    client_sock: _Socket = test_client.get_active_server_channel_socket()
    server_sock: _Socket = test_server.get_server_socket_matching_client(
        client_sock)

    server_tls = server_sock.security.tls
    client_tls = client_sock.security.tls

    print(f'\nServer certs:\n{debug_sock_tls(server_tls)}')
    print(f'\nClient certs:\n{debug_sock_tls(client_tls)}')
    print()

    if server_tls.local_certificate:
        eq = server_tls.local_certificate == client_tls.remote_certificate
        print(f'(TLS)  Server local matches client remote: {eq}')
    else:
        print('(TLS)  Not detected')

    if server_tls.remote_certificate:
        eq = server_tls.remote_certificate == client_tls.local_certificate
        print(f'(mTLS) Server remote matches client local: {eq}')
    else:
        print('(mTLS) Not detected')


def debug_basic_setup(test_client, test_server):
    """Show channel and server socket pair"""
    test_client.wait_for_active_server_channel()
    client_sock: _Socket = test_client.get_active_server_channel_socket()
    server_sock: _Socket = test_server.get_server_socket_matching_client(
        client_sock)

    print(f'Client socket:\n{client_sock}\n')
    print(f'Matching server:\n{server_sock}\n')


def main(argv):
    if len(argv) > 1:
        raise app.UsageError('Too many command-line arguments.')

    k8s_api_manager = k8s.KubernetesApiManager(xds_k8s_flags.KUBE_CONTEXT.value)

    # Server
    server_name = xds_flags.SERVER_NAME.value
    server_namespace = xds_flags.NAMESPACE.value
    server_k8s_ns = k8s.KubernetesNamespace(k8s_api_manager, server_namespace)
    server_pod_ip = get_deployment_pod_ips(server_k8s_ns, server_name)[0]
    test_server: _XdsTestServer = _XdsTestServer(
        ip=server_pod_ip,
        rpc_port=xds_flags.SERVER_PORT.value,
        xds_host=xds_flags.SERVER_XDS_HOST.value,
        xds_port=xds_flags.SERVER_XDS_PORT.value,
        rpc_host=_SERVER_RPC_HOST.value)

    # Client
    client_name = xds_flags.CLIENT_NAME.value
    client_namespace = xds_flags.NAMESPACE.value
    client_k8s_ns = k8s.KubernetesNamespace(k8s_api_manager, client_namespace)
    client_pod_ip = get_deployment_pod_ips(client_k8s_ns, client_name)[0]
    test_client: _XdsTestClient = _XdsTestClient(
        ip=client_pod_ip,
        server_target=test_server.xds_uri,
        rpc_port=xds_flags.CLIENT_PORT.value,
        rpc_host=_CLIENT_RPC_HOST.value)

    if _SECURITY.value in ('mtls', 'tls', 'plaintext'):
        debug_security_setup_positive(test_client, test_server)
    elif _SECURITY.value == ('mtls_error', 'server_authz_error'):
        debug_security_setup_negative(test_client)
    else:
        debug_basic_setup(test_client, test_server)

    test_client.close()
    test_server.close()


if __name__ == '__main__':
    app.run(main)
