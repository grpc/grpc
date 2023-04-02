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
import signal

from absl import app
from absl import flags

from framework import xds_flags
from framework import xds_k8s_flags
from framework.infrastructure import gcp
from framework.infrastructure import k8s
from framework.test_app.runners.k8s import k8s_xds_server_runner

logger = logging.getLogger(__name__)
# Flags
_CMD = flags.DEFINE_enum('cmd',
                         default='run',
                         enum_values=['run', 'cleanup'],
                         help='Command')
_SECURE = flags.DEFINE_bool("secure",
                            default=False,
                            help="Run server in the secure mode")
_REUSE_NAMESPACE = flags.DEFINE_bool("reuse_namespace",
                                     default=True,
                                     help="Use existing namespace if exists")
_REUSE_SERVICE = flags.DEFINE_bool("reuse_service",
                                   default=False,
                                   help="Use existing service if exists")
_FOLLOW = flags.DEFINE_bool("follow",
                            default=False,
                            help="Follow pod logs. "
                            "Requires --collect_app_logs")
_CLEANUP_NAMESPACE = flags.DEFINE_bool(
    "cleanup_namespace",
    default=False,
    help="Delete namespace during resource cleanup")
flags.adopt_module_key_flags(xds_flags)
flags.adopt_module_key_flags(xds_k8s_flags)
# Running outside of a test suite, so require explicit resource_suffix.
flags.mark_flag_as_required("resource_suffix")

# Type aliases
_KubernetesServerRunner = k8s_xds_server_runner.KubernetesServerRunner


def make_sigint_handler(server_runner: _KubernetesServerRunner):

    def sigint_handler(sig, frame):
        del sig, frame
        print('Caught Ctrl+C. Shutting down the logs')
        server_runner.stop_pod_dependencies(log_drain_sec=3)

    return sigint_handler


def main(argv):
    if len(argv) > 1:
        raise app.UsageError('Too many command-line arguments.')

    # Must be called before KubernetesApiManager or GcpApiManager init.
    xds_flags.set_socket_default_timeout_from_flag()

    project: str = xds_flags.PROJECT.value
    # GCP Service Account email
    gcp_service_account: str = xds_k8s_flags.GCP_SERVICE_ACCOUNT.value

    # Resource names.
    resource_prefix: str = xds_flags.RESOURCE_PREFIX.value
    resource_suffix: str = xds_flags.RESOURCE_SUFFIX.value

    # KubernetesServerRunner arguments.
    runner_kwargs = dict(
        deployment_name=xds_flags.SERVER_NAME.value,
        image_name=xds_k8s_flags.SERVER_IMAGE.value,
        td_bootstrap_image=xds_k8s_flags.TD_BOOTSTRAP_IMAGE.value,
        gcp_project=project,
        gcp_api_manager=gcp.api.GcpApiManager(),
        gcp_service_account=gcp_service_account,
        network=xds_flags.NETWORK.value,
        reuse_namespace=_REUSE_NAMESPACE.value,
        reuse_service=_REUSE_SERVICE.value)

    if _SECURE.value:
        runner_kwargs.update(
            xds_server_uri=xds_flags.XDS_SERVER_URI.value,
            deployment_template='server-secure.deployment.yaml')

    k8s_api_manager = k8s.KubernetesApiManager(xds_k8s_flags.KUBE_CONTEXT.value)
    server_namespace = _KubernetesServerRunner.make_namespace_name(
        resource_prefix, resource_suffix)
    server_runner = _KubernetesServerRunner(
        k8s.KubernetesNamespace(k8s_api_manager, server_namespace),
        **runner_kwargs)

    if _CMD.value == 'run':
        logger.info('Run server, secure_mode=%s', _SECURE.value)
        server_runner.run(
            test_port=xds_flags.SERVER_PORT.value,
            maintenance_port=xds_flags.SERVER_MAINTENANCE_PORT.value,
            secure_mode=_SECURE.value,
            log_to_stdout=_FOLLOW.value)
        if server_runner.should_collect_logs and _FOLLOW.value:
            print('Following pod logs. Press Ctrl+C top stop')
            signal.signal(signal.SIGINT, make_sigint_handler(server_runner))
            signal.pause()

    elif _CMD.value == 'cleanup':
        logger.info('Cleanup server')
        server_runner.cleanup(force=True,
                              force_namespace=_CLEANUP_NAMESPACE.value)


if __name__ == '__main__':
    app.run(main)
