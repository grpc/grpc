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
from framework.infrastructure import k8s
import framework.xds_k8s_testcase as xds_k8s_testcase
import framework.infrastructure.traffic_director_gamma as td_gamma
from framework.test_app.runners.k8s import gamma_server_runner

GammaServerRunner = gamma_server_runner.GammaServerRunner


class GammaXdsKubernetesTestCase(xds_k8s_testcase.RegularXdsKubernetesTestCase):
    td: td_gamma.TrafficDirectorGammaManager
    server_runner: GammaServerRunner

    def initKubernetesServerRunner(self) -> GammaServerRunner:
        return GammaServerRunner(
            k8s.KubernetesNamespace(
                self.k8s_api_manager, self.server_namespace
            ),
            deployment_name=self.server_name,
            image_name=self.server_image,
            td_bootstrap_image=self.td_bootstrap_image,
            gcp_project=self.project,
            gcp_api_manager=self.gcp_api_manager,
            gcp_service_account=self.gcp_service_account,
            xds_server_uri=self.xds_server_uri,
            network=self.network,
            debug_use_port_forwarding=self.debug_use_port_forwarding,
            enable_workload_identity=self.enable_workload_identity,
            reuse_namespace=True,
            reuse_service=True,
        )

    def initTrafficDirectorManager(
        self,
    ) -> td_gamma.TrafficDirectorGammaManager:
        return td_gamma.TrafficDirectorGammaManager(
            self.gcp_api_manager,
            project=self.project,
            resource_prefix=self.resource_prefix,
            resource_suffix=self.resource_suffix,
            network=self.network,
            compute_api_version=self.compute_api_version,
        )
