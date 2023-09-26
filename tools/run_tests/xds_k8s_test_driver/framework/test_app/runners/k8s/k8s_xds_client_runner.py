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
"""
Run xDS Test Client on Kubernetes.
"""
import logging
from typing import Optional

from framework.infrastructure import gcp
from framework.infrastructure import k8s
from framework.test_app.client_app import XdsTestClient
from framework.test_app.runners.k8s import k8s_base_runner

logger = logging.getLogger(__name__)


class KubernetesClientRunner(k8s_base_runner.KubernetesBaseRunner):
    # Required fields.
    xds_server_uri: str
    stats_port: int
    deployment_template: str
    enable_workload_identity: bool
    debug_use_port_forwarding: bool
    td_bootstrap_image: str
    network: str

    # Optional fields.
    service_account_name: Optional[str] = None
    service_account_template: Optional[str] = None
    gcp_iam: Optional[gcp.iam.IamV1] = None

    def __init__(  # pylint: disable=too-many-locals
        self,
        k8s_namespace: k8s.KubernetesNamespace,
        *,
        deployment_name: str,
        image_name: str,
        td_bootstrap_image: str,
        network="default",
        xds_server_uri: Optional[str] = None,
        gcp_api_manager: gcp.api.GcpApiManager,
        gcp_project: str,
        gcp_service_account: str,
        service_account_name: Optional[str] = None,
        stats_port: int = 8079,
        deployment_template: str = "client.deployment.yaml",
        service_account_template: str = "service-account.yaml",
        reuse_namespace: bool = False,
        namespace_template: Optional[str] = None,
        debug_use_port_forwarding: bool = False,
        enable_workload_identity: bool = True,
    ):
        super().__init__(
            k8s_namespace,
            deployment_name=deployment_name,
            image_name=image_name,
            gcp_project=gcp_project,
            gcp_service_account=gcp_service_account,
            gcp_ui_url=gcp_api_manager.gcp_ui_url,
            namespace_template=namespace_template,
            reuse_namespace=reuse_namespace,
        )

        # Settings
        self.stats_port = stats_port
        self.deployment_template = deployment_template
        self.enable_workload_identity = enable_workload_identity
        self.debug_use_port_forwarding = debug_use_port_forwarding

        # Used by the TD bootstrap generator.
        self.td_bootstrap_image = td_bootstrap_image
        self.network = network
        self.xds_server_uri = xds_server_uri

        # Workload identity settings:
        if self.enable_workload_identity:
            # Kubernetes service account.
            self.service_account_name = service_account_name or deployment_name
            self.service_account_template = service_account_template
            # GCP IAM API used to grant allow workload service accounts
            # permission to use GCP service account identity.
            self.gcp_iam = gcp.iam.IamV1(gcp_api_manager, gcp_project)

    def run(  # pylint: disable=arguments-differ
        self,
        *,
        server_target,
        rpc="UnaryCall",
        qps=25,
        metadata="",
        secure_mode=False,
        config_mesh=None,
        generate_mesh_id=False,
        print_response=False,
        log_to_stdout: bool = False,
    ) -> XdsTestClient:
        logger.info(
            (
                'Deploying xDS test client "%s" to k8s namespace %s: '
                "server_target=%s rpc=%s qps=%s metadata=%r secure_mode=%s "
                "print_response=%s"
            ),
            self.deployment_name,
            self.k8s_namespace.name,
            server_target,
            rpc,
            qps,
            metadata,
            secure_mode,
            print_response,
        )
        super().run()

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
            stats_port=self.stats_port,
            server_target=server_target,
            rpc=rpc,
            qps=qps,
            metadata=metadata,
            secure_mode=secure_mode,
            config_mesh=config_mesh,
            generate_mesh_id=generate_mesh_id,
            print_response=print_response,
        )

        # Load test client pod. We need only one client at the moment
        pod_name = self._wait_deployment_pod_count(self.deployment)[0]
        pod: k8s.V1Pod = self._wait_pod_started(pod_name)
        if self.should_collect_logs:
            self._start_logging_pod(pod, log_to_stdout=log_to_stdout)

        # Verify the deployment reports all pods started as well.
        self._wait_deployment_with_available_replicas(self.deployment_name)
        self._start_completed()

        return self._xds_test_client_for_pod(pod, server_target=server_target)

    def _xds_test_client_for_pod(
        self, pod: k8s.V1Pod, *, server_target: str
    ) -> XdsTestClient:
        if self.debug_use_port_forwarding:
            pf = self._start_port_forwarding_pod(pod, self.stats_port)
            rpc_port, rpc_host = pf.local_port, pf.local_address
        else:
            rpc_port, rpc_host = self.stats_port, None

        return XdsTestClient(
            ip=pod.status.pod_ip,
            rpc_port=rpc_port,
            server_target=server_target,
            hostname=pod.metadata.name,
            rpc_host=rpc_host,
        )

    # pylint: disable=arguments-differ
    def cleanup(self, *, force=False, force_namespace=False):
        # TODO(sergiitk): rename to stop().
        try:
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
            self._cleanup_namespace(force=force_namespace and force)
        finally:
            self._stop()

    # pylint: enable=arguments-differ

    @classmethod
    def make_namespace_name(
        cls, resource_prefix: str, resource_suffix: str, name: str = "client"
    ) -> str:
        """A helper to make consistent XdsTestClient kubernetes namespace name
        for given resource prefix and suffix.

        Note: the idea is to intentionally produce different namespace name for
        the test server, and the test client, as that closely mimics real-world
        deployments.
        """
        return cls._make_namespace_name(resource_prefix, resource_suffix, name)
