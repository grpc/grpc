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

    def __init__(  # pylint: disable=too-many-locals
            self,
            k8s_namespace,
            *,
            deployment_name,
            image_name,
            td_bootstrap_image,
            gcp_api_manager: gcp.api.GcpApiManager,
            gcp_project: str,
            gcp_service_account: str,
            xds_server_uri=None,
            network='default',
            service_account_name=None,
            stats_port=8079,
            deployment_template='client.deployment.yaml',
            service_account_template='service-account.yaml',
            reuse_namespace=False,
            namespace_template=None,
            debug_use_port_forwarding=False,
            enable_workload_identity=True):
        super().__init__(k8s_namespace, namespace_template, reuse_namespace)

        # Settings
        self.deployment_name = deployment_name
        self.image_name = image_name
        self.stats_port = stats_port
        # xDS bootstrap generator
        self.td_bootstrap_image = td_bootstrap_image
        self.xds_server_uri = xds_server_uri
        self.network = network
        self.deployment_template = deployment_template
        self.debug_use_port_forwarding = debug_use_port_forwarding
        self.enable_workload_identity = enable_workload_identity
        # Service account settings:
        # Kubernetes service account
        if self.enable_workload_identity:
            self.service_account_name = service_account_name or deployment_name
            self.service_account_template = service_account_template
        else:
            self.service_account_name = None
            self.service_account_template = None
        # GCP.
        self.gcp_project = gcp_project
        self.gcp_ui_url = gcp_api_manager.gcp_ui_url
        # GCP service account to map to Kubernetes service account
        self.gcp_service_account = gcp_service_account
        # GCP IAM API used to grant allow workload service accounts permission
        # to use GCP service account identity.
        self.gcp_iam = gcp.iam.IamV1(gcp_api_manager, gcp_project)

        # Mutable state
        self.deployment: Optional[k8s.V1Deployment] = None
        self.service_account: Optional[k8s.V1ServiceAccount] = None

    # TODO(sergiitk): make rpc UnaryCall enum or get it from proto
    def run(  # pylint: disable=arguments-differ
            self,
            *,
            server_target,
            rpc='UnaryCall',
            qps=25,
            metadata='',
            secure_mode=False,
            config_mesh=None,
            print_response=False,
            log_to_stdout: bool = False) -> XdsTestClient:
        logger.info(
            'Deploying xDS test client "%s" to k8s namespace %s: '
            'server_target=%s rpc=%s qps=%s metadata=%r secure_mode=%s '
            'print_response=%s', self.deployment_name, self.k8s_namespace.name,
            server_target, rpc, qps, metadata, secure_mode, print_response)
        self._logs_explorer_link(deployment_name=self.deployment_name,
                                 namespace_name=self.k8s_namespace.name,
                                 gcp_project=self.gcp_project,
                                 gcp_ui_url=self.gcp_ui_url)

        super().run()

        if self.enable_workload_identity:
            # Allow Kubernetes service account to use the GCP service account
            # identity.
            self._grant_workload_identity_user(
                gcp_iam=self.gcp_iam,
                gcp_service_account=self.gcp_service_account,
                service_account_name=self.service_account_name)

            # Create service account
            self.service_account = self._create_service_account(
                self.service_account_template,
                service_account_name=self.service_account_name,
                namespace_name=self.k8s_namespace.name,
                gcp_service_account=self.gcp_service_account)

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
            print_response=print_response)

        # Load test client pod. We need only one client at the moment
        pod_name = self._wait_deployment_pod_count(self.deployment)[0]
        pod: k8s.V1Pod = self._wait_pod_started(pod_name)
        if self.should_collect_logs:
            self._start_logging_pod(pod, log_to_stdout=log_to_stdout)

        # Verify the deployment reports all pods started as well.
        self._wait_deployment_with_available_replicas(self.deployment_name)

        return self._xds_test_client_for_pod(pod, server_target=server_target)

    def _xds_test_client_for_pod(self, pod: k8s.V1Pod, *,
                                 server_target: str) -> XdsTestClient:
        if self.debug_use_port_forwarding:
            pf = self._start_port_forwarding_pod(pod, self.stats_port)
            rpc_port, rpc_host = pf.local_port, pf.local_address
        else:
            rpc_port, rpc_host = self.stats_port, None

        return XdsTestClient(ip=pod.status.pod_ip,
                             rpc_port=rpc_port,
                             server_target=server_target,
                             hostname=pod.metadata.name,
                             rpc_host=rpc_host)

    def cleanup(self, *, force=False, force_namespace=False):  # pylint: disable=arguments-differ
        if self.deployment or force:
            self._delete_deployment(self.deployment_name)
            self.deployment = None
        if self.enable_workload_identity and (self.service_account or force):
            self._revoke_workload_identity_user(
                gcp_iam=self.gcp_iam,
                gcp_service_account=self.gcp_service_account,
                service_account_name=self.service_account_name)
            self._delete_service_account(self.service_account_name)
            self.service_account = None
        super().cleanup(force=force_namespace and force)

    @classmethod
    def make_namespace_name(cls,
                            resource_prefix: str,
                            resource_suffix: str,
                            name: str = 'client') -> str:
        """A helper to make consistent XdsTestClient kubernetes namespace name
        for given resource prefix and suffix.

        Note: the idea is to intentionally produce different namespace name for
        the test server, and the test client, as that closely mimics real-world
        deployments.
        """
        return cls._make_namespace_name(resource_prefix, resource_suffix, name)
