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
"""Clean up GKE namespaces leaked by the tests."""

from absl import app

from bin.cleanup import cleanup
from framework import xds_flags
from framework import xds_k8s_flags


def main(argv):
    if len(argv) > 1:
        raise app.UsageError("Too many command-line arguments.")
    cleanup.load_keep_config()

    # Must be called before KubernetesApiManager or GcpApiManager init.
    xds_flags.set_socket_default_timeout_from_flag()

    project: str = xds_flags.PROJECT.value
    network: str = xds_flags.NETWORK.value
    gcp_service_account: str = xds_k8s_flags.GCP_SERVICE_ACCOUNT.value
    dry_run: bool = cleanup.DRY_RUN.value

    cleanup.find_and_remove_leaked_k8s_resources(
        dry_run, project, network, gcp_service_account
    )


if __name__ == "__main__":
    app.run(main)
