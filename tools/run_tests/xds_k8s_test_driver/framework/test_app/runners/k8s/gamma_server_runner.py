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
"""
Run xDS Test Client on Kubernetes using Gamma
"""
import logging
from typing import List, Optional

from framework.infrastructure import gcp
from framework.infrastructure import k8s
from framework.test_app.runners.k8s import k8s_xds_server_runner
from framework.test_app.server_app import XdsTestServer

logger = logging.getLogger(__name__)


KubernetesServerRunner = k8s_xds_server_runner.KubernetesServerRunner


class GammaServerRunner(KubernetesServerRunner):
    # Mutable state.
    mesh: Optional[k8s.GammaMesh] = None
    route: Optional[k8s.GammaGrpcRoute] = None

    # Mesh
    mesh_name: str
    route_name: str

    def __init__(
        self,
        k8s_namespace: k8s.KubernetesNamespace,
        *,
        deployment_name: str,
        image_name: str,
        td_bootstrap_image: str,
        network: str = "default",
        xds_server_uri: Optional[str] = None,
        gcp_api_manager: gcp.api.GcpApiManager,
        gcp_project: str,
        gcp_service_account: str,
        service_account_name: Optional[str] = None,
        service_name: Optional[str] = None,
        mesh_name: Optional[str] = None,
        route_name: Optional[str] = None,
        neg_name: Optional[str] = None,
        deployment_template: str = "server.deployment.yaml",
        service_account_template: str = "service-account.yaml",
        service_template: str = "gamma/service.yaml",
        reuse_service: bool = False,
        reuse_namespace: bool = False,
        namespace_template: Optional[str] = None,
        debug_use_port_forwarding: bool = False,
        enable_workload_identity: bool = True,
    ):
        super().__init__(
            k8s_namespace,
            deployment_name=deployment_name,
            image_name=image_name,
            td_bootstrap_image=td_bootstrap_image,
            network=network,
            xds_server_uri=xds_server_uri,
            gcp_api_manager=gcp_api_manager,
            gcp_project=gcp_project,
            gcp_service_account=gcp_service_account,
            service_account_name=service_account_name,
            service_name=service_name,
            neg_name=neg_name,
            deployment_template=deployment_template,
            service_account_template=service_account_template,
            service_template=service_template,
            reuse_service=reuse_service,
            reuse_namespace=reuse_namespace,
            namespace_template=namespace_template,
            debug_use_port_forwarding=debug_use_port_forwarding,
            enable_workload_identity=enable_workload_identity,
        )

        self.mesh_name = mesh_name or deployment_name
        self.route_name = route_name or deployment_name

    def run(
        self,
        *,
        test_port: int = KubernetesServerRunner.DEFAULT_TEST_PORT,
        maintenance_port: Optional[int] = None,
        secure_mode: bool = False,
        replica_count: int = 1,
        log_to_stdout: bool = False,
    ) -> List[XdsTestServer]:
        if not maintenance_port:
            maintenance_port = self._get_default_maintenance_port(secure_mode)

        logger.info(
            (
                'Deploying GAMMA xDS test server "%s" to k8s namespace %s:'
                " test_port=%s maintenance_port=%s secure_mode=%s"
                " replica_count=%s"
            ),
            self.deployment_name,
            self.k8s_namespace.name,
            test_port,
            maintenance_port,
            False,
            replica_count,
        )
        # super(k8s_base_runner.KubernetesBaseRunner, self).run()

        if self.reuse_namespace:
            self.namespace = self._reuse_namespace()
        if not self.namespace:
            self.namespace = self._create_namespace(
                self.namespace_template, namespace_name=self.k8s_namespace.name
            )

        # Create gamma mesh.
        self.mesh = self._create_gamma_mesh(
            "gamma/tdmesh.yaml",
            mesh_name=self.mesh_name,
            namespace_name=self.k8s_namespace.name,
        )

        # Reuse existing if requested, create a new deployment when missing.
        # Useful for debugging to avoid NEG loosing relation to deleted service.
        if self.reuse_service:
            self.service = self._reuse_service(self.service_name)
        if not self.service:
            self.service = self._create_service(
                self.service_template,
                service_name=self.service_name,
                namespace_name=self.k8s_namespace.name,
                deployment_name=self.deployment_name,
                neg_name=self.gcp_neg_name,
                test_port=test_port,
            )

        # Create route
        self.route = self._create_gamma_route(
            "gamma/route_grpc.yaml",
            route_name=self.route_name,
            mesh_name=self.mesh_name,
            service_name=self.service_name,
            namespace_name=self.k8s_namespace.name,
            test_port=test_port,
        )

        # Todo: wait for mesh?
        self._wait_service_neg(self.service_name, test_port)

        if self.enable_workload_identity:
            # Allow Kubernetes service account to use the GCP service account
            # identity.
            self._grant_workload_identity_user(
                gcp_iam=self.gcp_iam,
                gcp_service_account=self.gcp_service_account,
                service_account_name=self.service_account_name,
            )

            # Create service account
            self.service_account = self._create_service_account(
                self.service_account_template,
                service_account_name=self.service_account_name,
                namespace_name=self.k8s_namespace.name,
                gcp_service_account=self.gcp_service_account,
            )

        # Always create a new deployment
        self.deployment = self._create_deployment(
            self.deployment_template,
            deployment_name=self.deployment_name,
            image_name=self.image_name,
            namespace_name=self.k8s_namespace.name,
            service_account_name=self.service_account_name,
            td_bootstrap_image=self.td_bootstrap_image,
            xds_server_uri=self.xds_server_uri,
            network=self.network,
            replica_count=replica_count,
            test_port=test_port,
            maintenance_port=maintenance_port,
            secure_mode=secure_mode,
        )

        return self._make_servers_for_deployment(
            replica_count,
            test_port=test_port,
            maintenance_port=maintenance_port,
            log_to_stdout=log_to_stdout,
            secure_mode=secure_mode,
        )

    # pylint: disable=arguments-differ
    def cleanup(self, *, force=False, force_namespace=False):
        try:
            if True or self.route or force:
                self._delete_gamma_route(self.route_name)
                self.route = None

            if True or self.mesh or force:
                self._delete_gamma_mesh(self.mesh_name)

            if (self.service and not self.reuse_service) or force:
                self._delete_service(self.service_name)
                self.service = None

            if self.deployment or force:
                self._delete_deployment(self.deployment_name)
                self.deployment = None

            if self.enable_workload_identity and (
                self.service_account or force
            ):
                self._revoke_workload_identity_user(
                    gcp_iam=self.gcp_iam,
                    gcp_service_account=self.gcp_service_account,
                    service_account_name=self.service_account_name,
                )
                self._delete_service_account(self.service_account_name)
                self.service_account = None

            self._cleanup_namespace(force=(force_namespace and force))
        finally:
            self._stop()

    # pylint: enable=arguments-differ
