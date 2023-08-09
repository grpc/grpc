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

from bin.lib import common
from framework import xds_flags
from framework import xds_k8s_flags
from framework.infrastructure import gcp
from framework.infrastructure import k8s

logger = logging.getLogger(__name__)
# Flags
_CMD = flags.DEFINE_enum(
    "cmd", default="run", enum_values=["run", "cleanup"], help="Command"
)
_SECURE = flags.DEFINE_bool(
    "secure", default=False, help="Run server in the secure mode"
)
_REUSE_NAMESPACE = flags.DEFINE_bool(
    "reuse_namespace", default=True, help="Use existing namespace if exists"
)
_REUSE_SERVICE = flags.DEFINE_bool(
    "reuse_service", default=False, help="Use existing service if exists"
)
_FOLLOW = flags.DEFINE_bool(
    "follow", default=False, help="Follow pod logs. Requires --collect_app_logs"
)
_CLEANUP_NAMESPACE = flags.DEFINE_bool(
    "cleanup_namespace",
    default=False,
    help="Delete namespace during resource cleanup",
)
flags.adopt_module_key_flags(xds_flags)
flags.adopt_module_key_flags(xds_k8s_flags)
# Running outside of a test suite, so require explicit resource_suffix.
flags.mark_flag_as_required("resource_suffix")


def _make_sigint_handler(server_runner: common.KubernetesServerRunner):
    def sigint_handler(sig, frame):
        del sig, frame
        print("Caught Ctrl+C. Shutting down the logs")
        server_runner.stop_pod_dependencies(log_drain_sec=3)

    return sigint_handler


def main(argv):
    if len(argv) > 1:
        raise app.UsageError("Too many command-line arguments.")

    # Must be called before KubernetesApiManager or GcpApiManager init.
    xds_flags.set_socket_default_timeout_from_flag()

    should_follow_logs = _FOLLOW.value and xds_flags.COLLECT_APP_LOGS.value
    should_port_forward = (
        should_follow_logs and xds_k8s_flags.DEBUG_USE_PORT_FORWARDING.value
    )

    # Setup.
    gcp_api_manager = gcp.api.GcpApiManager()
    k8s_api_manager = k8s.KubernetesApiManager(xds_k8s_flags.KUBE_CONTEXT.value)
    server_namespace = common.make_server_namespace(k8s_api_manager)
    server_runner = common.make_server_runner(
        server_namespace,
        gcp_api_manager,
        reuse_namespace=_REUSE_NAMESPACE.value,
        reuse_service=_REUSE_SERVICE.value,
        secure=_SECURE.value,
        port_forwarding=should_port_forward,
    )

    if _CMD.value == "run":
        logger.info("Run server, secure_mode=%s", _SECURE.value)
        server_runner.run(
            test_port=xds_flags.SERVER_PORT.value,
            maintenance_port=xds_flags.SERVER_MAINTENANCE_PORT.value,
            secure_mode=_SECURE.value,
            log_to_stdout=_FOLLOW.value,
        )
        if should_follow_logs:
            print("Following pod logs. Press Ctrl+C top stop")
            signal.signal(signal.SIGINT, _make_sigint_handler(server_runner))
            signal.pause()

    elif _CMD.value == "cleanup":
        logger.info("Cleanup server")
        server_runner.cleanup(
            force=True, force_namespace=_CLEANUP_NAMESPACE.value
        )


if __name__ == "__main__":
    app.run(main)
