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
import logging

from absl import app
from absl import flags

from framework import xds_flags
from framework import xds_k8s_flags
from framework.infrastructure import k8s
from framework.test_app import client_app

logger = logging.getLogger(__name__)
# Flags
_CMD = flags.DEFINE_enum('cmd',
                         default='run',
                         enum_values=['run', 'cleanup'],
                         help='Command')
_SECURE = flags.DEFINE_bool("secure",
                            default=False,
                            help="Run client in the secure mode")
_QPS = flags.DEFINE_integer('qps', default=25, help='Queries per second')
_PRINT_RESPONSE = flags.DEFINE_bool("print_response",
                                    default=False,
                                    help="Client prints responses")
_REUSE_NAMESPACE = flags.DEFINE_bool("reuse_namespace",
                                     default=True,
                                     help="Use existing namespace if exists")
_CLEANUP_NAMESPACE = flags.DEFINE_bool(
    "cleanup_namespace",
    default=False,
    help="Delete namespace during resource cleanup")
flags.adopt_module_key_flags(xds_flags)
flags.adopt_module_key_flags(xds_k8s_flags)


def main(argv):
    if len(argv) > 1:
        raise app.UsageError('Too many command-line arguments.')

    # Base namespace
    namespace = xds_flags.NAMESPACE.value
    client_namespace = namespace

    runner_kwargs = dict(
        deployment_name=xds_flags.CLIENT_NAME.value,
        image_name=xds_k8s_flags.CLIENT_IMAGE.value,
        gcp_service_account=xds_k8s_flags.GCP_SERVICE_ACCOUNT.value,
        td_bootstrap_image=xds_k8s_flags.TD_BOOTSTRAP_IMAGE.value,
        xds_server_uri=xds_flags.XDS_SERVER_URI.value,
        network=xds_flags.NETWORK.value,
        stats_port=xds_flags.CLIENT_PORT.value,
        reuse_namespace=_REUSE_NAMESPACE.value)

    if _SECURE.value:
        runner_kwargs.update(
            deployment_template='client-secure.deployment.yaml')

    k8s_api_manager = k8s.KubernetesApiManager(xds_k8s_flags.KUBE_CONTEXT.value)
    client_runner = client_app.KubernetesClientRunner(
        k8s.KubernetesNamespace(k8s_api_manager, client_namespace),
        **runner_kwargs)

    # Server target
    server_xds_host = xds_flags.SERVER_XDS_HOST.value
    server_xds_port = xds_flags.SERVER_XDS_PORT.value

    if _CMD.value == 'run':
        logger.info('Run client, secure_mode=%s', _SECURE.value)
        client_runner.run(
            server_target=f'xds:///{server_xds_host}:{server_xds_port}',
            qps=_QPS.value,
            print_response=_PRINT_RESPONSE.value,
            secure_mode=_SECURE.value)

    elif _CMD.value == 'cleanup':
        logger.info('Cleanup client')
        client_runner.cleanup(force=True,
                              force_namespace=_CLEANUP_NAMESPACE.value)


if __name__ == '__main__':
    app.run(main)
