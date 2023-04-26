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
from typing import List, Optional

from framework.infrastructure import gcp
from framework.infrastructure import k8s
from framework.test_app.runners.k8s import k8s_base_runner
from framework.test_app.server_app import XdsTestServer

logger = logging.getLogger(__name__)


class KubernetesServerRunner(k8s_base_runner.KubernetesBaseRunner):
    DEFAULT_TEST_PORT = 8080
    DEFAULT_MAINTENANCE_PORT = 8080
    DEFAULT_SECURE_MODE_MAINTENANCE_PORT = 8081

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
            service_account_name=None,
            service_name=None,
            neg_name=None,
            xds_server_uri=None,
            network='default',
            deployment_template='server.deployment.yaml',
            service_account_template='service-account.yaml',
            service_template='server.service.yaml',
            reuse_service=False,
            reuse_namespace=False,
            namespace_template=None,
            debug_use_port_forwarding=False,
            enable_workload_identity=True):
        super().__init__(k8s_namespace, namespace_template, reuse_namespace)

        # Settings
        self.deployment_name = deployment_name
        self.image_name = image_name
        self.service_name = service_name or deployment_name
        # xDS bootstrap generator
        self.td_bootstrap_image = td_bootstrap_image
        self.xds_server_uri = xds_server_uri
        # This only works in k8s >= 1.18.10-gke.600
        # https://cloud.google.com/kubernetes-engine/docs/how-to/standalone-neg#naming_negs
        self.neg_name = neg_name or (f'{self.k8s_namespace.name}-'
                                     f'{self.service_name}')
        self.network = network
        self.deployment_template = deployment_template
        self.service_template = service_template
        self.reuse_service = reuse_service
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
        self.service: Optional[k8s.V1Service] = None

    def run(  # pylint: disable=arguments-differ,too-many-branches
            self,
            *,
            test_port: int = DEFAULT_TEST_PORT,
            maintenance_port: Optional[int] = None,
            secure_mode: bool = False,
            replica_count: int = 1,
            log_to_stdout: bool = False) -> List[XdsTestServer]:
        if not maintenance_port:
            maintenance_port = self._get_default_maintenance_port(secure_mode)

        # Implementation detail: in secure mode, maintenance ("backchannel")
        # port must be different from the test port so communication with
        # maintenance services can be reached independently of the security
        # configuration under test.
        if secure_mode and maintenance_port == test_port:
            raise ValueError('port and maintenance_port must be different '
                             'when running test server in secure mode')
        # To avoid bugs with comparing wrong types.
        if not (isinstance(test_port, int) and
                isinstance(maintenance_port, int)):
            raise TypeError('Port numbers must be integer')

        if secure_mode and not self.enable_workload_identity:
            raise ValueError('Secure mode requires Workload Identity enabled.')

        logger.info(
            'Deploying xDS test server "%s" to k8s namespace %s: test_port=%s '
            'maintenance_port=%s secure_mode=%s replica_count=%s',
            self.deployment_name, self.k8s_namespace.name, test_port,
            maintenance_port, secure_mode, replica_count)
        self._logs_explorer_link(deployment_name=self.deployment_name,
                                 namespace_name=self.k8s_namespace.name,
                                 gcp_project=self.gcp_project,
                                 gcp_ui_url=self.gcp_ui_url)

        # Create namespace.
        super().run()

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
                neg_name=self.neg_name,
                test_port=test_port)
        self._wait_service_neg(self.service_name, test_port)

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
            replica_count=replica_count,
            test_port=test_port,
            maintenance_port=maintenance_port,
            secure_mode=secure_mode)

        pod_names = self._wait_deployment_pod_count(self.deployment,
                                                    replica_count)
        pods = []
        for pod_name in pod_names:
            pod = self._wait_pod_started(pod_name)
            pods.append(pod)
            if self.should_collect_logs:
                self._start_logging_pod(pod, log_to_stdout=log_to_stdout)

        # Verify the deployment reports all pods started as well.
        self._wait_deployment_with_available_replicas(self.deployment_name,
                                                      replica_count)
        servers: List[XdsTestServer] = []
        for pod in pods:
            servers.append(
                self._xds_test_server_for_pod(pod,
                                              test_port=test_port,
                                              maintenance_port=maintenance_port,
                                              secure_mode=secure_mode))
        return servers

    def _get_default_maintenance_port(self, secure_mode: bool) -> int:
        if not secure_mode:
            maintenance_port = self.DEFAULT_MAINTENANCE_PORT
        else:
            maintenance_port = self.DEFAULT_SECURE_MODE_MAINTENANCE_PORT
        return maintenance_port

    def _xds_test_server_for_pod(self,
                                 pod: k8s.V1Pod,
                                 *,
                                 test_port: int = DEFAULT_TEST_PORT,
                                 maintenance_port: Optional[int] = None,
                                 secure_mode: bool = False) -> XdsTestServer:
        if maintenance_port is None:
            maintenance_port = self._get_default_maintenance_port(secure_mode)

        if self.debug_use_port_forwarding:
            pf = self._start_port_forwarding_pod(pod, maintenance_port)
            rpc_port, rpc_host = pf.local_port, pf.local_address
        else:
            rpc_port, rpc_host = maintenance_port, None

        return XdsTestServer(ip=pod.status.pod_ip,
                             rpc_port=test_port,
                             hostname=pod.metadata.name,
                             maintenance_port=rpc_port,
                             secure_mode=secure_mode,
                             rpc_host=rpc_host)

    def cleanup(self, *, force=False, force_namespace=False):  # pylint: disable=arguments-differ
        if self.deployment or force:
            self._delete_deployment(self.deployment_name)
            self.deployment = None
        if (self.service and not self.reuse_service) or force:
            self._delete_service(self.service_name)
            self.service = None
        if self.enable_workload_identity and (self.service_account or force):
            self._revoke_workload_identity_user(
                gcp_iam=self.gcp_iam,
                gcp_service_account=self.gcp_service_account,
                service_account_name=self.service_account_name)
            self._delete_service_account(self.service_account_name)
            self.service_account = None
        super().cleanup(force=(force_namespace and force))

    @classmethod
    def make_namespace_name(cls,
                            resource_prefix: str,
                            resource_suffix: str,
                            name: str = 'server') -> str:
        """A helper to make consistent XdsTestServer kubernetes namespace name
        for given resource prefix and suffix.

        Note: the idea is to intentionally produce different namespace name for
        the test server, and the test client, as that closely mimics real-world
        deployments.
        """
        return cls._make_namespace_name(resource_prefix, resource_suffix, name)
