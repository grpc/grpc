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
Common functionality for running xDS Test Client and Server on Kubernetes.
"""
from abc import ABCMeta
import contextlib
import dataclasses
import datetime
import logging
import pathlib
from typing import List, Optional

import mako.template
import yaml

from framework.helpers import retryers
import framework.helpers.datetime
import framework.helpers.highlighter
import framework.helpers.rand
from framework.infrastructure import gcp
from framework.infrastructure import k8s
from framework.test_app.runners import base_runner

logger = logging.getLogger(__name__)

# Type aliases
_RunnerError = base_runner.RunnerError
_HighlighterYaml = framework.helpers.highlighter.HighlighterYaml
_helper_datetime = framework.helpers.datetime
_datetime = datetime.datetime
_timedelta = datetime.timedelta


@dataclasses.dataclass(frozen=True)
class RunHistory:
    deployment_id: str
    time_start_requested: _datetime
    time_start_completed: Optional[_datetime]
    time_stopped: _datetime


class KubernetesBaseRunner(base_runner.BaseRunner, metaclass=ABCMeta):
    # Pylint wants abstract classes to override abstract methods.
    # pylint: disable=abstract-method

    TEMPLATE_DIR_NAME = "kubernetes-manifests"
    TEMPLATE_DIR_RELATIVE_PATH = f"../../../../{TEMPLATE_DIR_NAME}"
    ROLE_WORKLOAD_IDENTITY_USER = "roles/iam.workloadIdentityUser"
    pod_port_forwarders: List[k8s.PortForwarder]
    pod_log_collectors: List[k8s.PodLogCollector]

    # Required fields.
    k8s_namespace: k8s.KubernetesNamespace
    deployment_name: str
    image_name: str
    gcp_project: str
    gcp_service_account: str
    gcp_ui_url: str

    # Fields with default values.
    namespace_template: str = "namespace.yaml"
    reuse_namespace: bool = False

    # Mutable state. Describes the current run.
    namespace: Optional[k8s.V1Namespace] = None
    deployment: Optional[k8s.V1Deployment] = None
    deployment_id: Optional[str] = None
    service_account: Optional[k8s.V1ServiceAccount] = None
    time_start_requested: Optional[_datetime] = None
    time_start_completed: Optional[_datetime] = None
    time_stopped: Optional[_datetime] = None
    # The history of all runs performed by this runner.
    run_history: List[RunHistory]

    def __init__(
        self,
        k8s_namespace: k8s.KubernetesNamespace,
        *,
        deployment_name: str,
        image_name: str,
        gcp_project: str,
        gcp_service_account: str,
        gcp_ui_url: str,
        namespace_template: Optional[str] = "namespace.yaml",
        reuse_namespace: bool = False,
    ):
        super().__init__()

        # Required fields.
        self.deployment_name = deployment_name
        self.image_name = image_name
        self.gcp_project = gcp_project
        # Maps GCP service account to Kubernetes service account
        self.gcp_service_account = gcp_service_account
        self.gcp_ui_url = gcp_ui_url

        # Kubernetes namespace resources manager.
        self.k8s_namespace = k8s_namespace
        if namespace_template:
            self.namespace_template = namespace_template
        self.reuse_namespace = reuse_namespace

        # Mutable state
        self.run_history = []
        self.pod_port_forwarders = []
        self.pod_log_collectors = []

        # Highlighter.
        self._highlighter = _HighlighterYaml()

    def run(self, **kwargs):
        del kwargs
        if not self.time_stopped and self.time_start_requested:
            if self.time_start_completed:
                raise RuntimeError(
                    f"Deployment {self.deployment_name}: has already been"
                    f" started at {self.time_start_completed.isoformat()}"
                )
            else:
                raise RuntimeError(
                    f"Deployment {self.deployment_name}: start has already been"
                    f" requested at {self.time_start_requested.isoformat()}"
                )

        self._reset_state()
        self.time_start_requested = _datetime.now()

        self.logs_explorer_link()
        if self.reuse_namespace:
            self.namespace = self._reuse_namespace()
        if not self.namespace:
            self.namespace = self._create_namespace(
                self.namespace_template, namespace_name=self.k8s_namespace.name
            )

    def _start_completed(self):
        self.time_start_completed = _datetime.now()

    def _stop(self):
        self.time_stopped = _datetime.now()
        if self.time_start_requested and self.deployment_id:
            run_history = RunHistory(
                deployment_id=self.deployment_id,
                time_start_requested=self.time_start_requested,
                time_start_completed=self.time_start_completed,
                time_stopped=self.time_stopped,
            )
            self.run_history.append(run_history)

    def _reset_state(self):
        """Reset the mutable state of the previous run."""
        if self.pod_port_forwarders:
            logger.warning(
                "Port forwarders weren't cleaned up from the past run: %s",
                len(self.pod_port_forwarders),
            )

        if self.pod_log_collectors:
            logger.warning(
                "Pod log collectors weren't cleaned up from the past run: %s",
                len(self.pod_log_collectors),
            )

        self.namespace = None
        self.deployment = None
        self.deployment_id = None
        self.service_account = None
        self.time_start_requested = None
        self.time_start_completed = None
        self.time_stopped = None
        self.pod_port_forwarders = []
        self.pod_log_collectors = []

    def _cleanup_namespace(self, *, force=False):
        if (self.namespace and not self.reuse_namespace) or force:
            self.delete_namespace()
            self.namespace = None

    def stop_pod_dependencies(self, *, log_drain_sec: int = 0):
        # Signal to stop logging early so less drain time needed.
        self.maybe_stop_logging()

        # Stop port forwarders if any.
        for pod_port_forwarder in self.pod_port_forwarders:
            pod_port_forwarder.close()
        self.pod_port_forwarders = []

        for pod_log_collector in self.pod_log_collectors:
            if log_drain_sec > 0 and not pod_log_collector.drain_event.is_set():
                logger.info(
                    "Draining logs for %s, timeout %i sec",
                    pod_log_collector.pod_name,
                    log_drain_sec,
                )
                # The close will happen normally at the next message.
                pod_log_collector.drain_event.wait(timeout=log_drain_sec)
            # Note this will be called from the main thread and may cause
            # a race for the log file. Still, at least it'll flush the buffers.
            pod_log_collector.flush()

        self.pod_log_collectors = []

    def get_pod_restarts(self, deployment: k8s.V1Deployment) -> int:
        if not self.k8s_namespace or not deployment:
            return 0
        total_restart: int = 0
        pods: List[k8s.V1Pod] = self.k8s_namespace.list_deployment_pods(
            deployment
        )
        for pod in pods:
            total_restart += sum(
                status.restart_count for status in pod.status.container_statuses
            )
        return total_restart

    @classmethod
    def _render_template(cls, template_file, **kwargs):
        template = mako.template.Template(filename=str(template_file))
        return template.render(**kwargs)

    @classmethod
    def _manifests_from_yaml_file(cls, yaml_file):
        with open(yaml_file) as f:
            with contextlib.closing(yaml.safe_load_all(f)) as yml:
                for manifest in yml:
                    yield manifest

    @classmethod
    def _manifests_from_str(cls, document):
        with contextlib.closing(yaml.safe_load_all(document)) as yml:
            for manifest in yml:
                yield manifest

    @classmethod
    def _template_file_from_name(cls, template_name):
        templates_path = (
            pathlib.Path(__file__).parent / cls.TEMPLATE_DIR_RELATIVE_PATH
        )
        return templates_path.joinpath(template_name).resolve()

    def _create_from_template(
        self,
        template_name,
        *,
        custom_object: bool = False,
        **kwargs,
    ) -> object:
        template_file = self._template_file_from_name(template_name)
        logger.debug("Loading k8s manifest template: %s", template_file)

        yaml_doc = self._render_template(template_file, **kwargs)
        logger.info(
            "Rendered template %s/%s:\n%s",
            self.TEMPLATE_DIR_NAME,
            template_name,
            self._highlighter.highlight(yaml_doc),
        )

        manifests = self._manifests_from_str(yaml_doc)
        manifest = next(manifests)
        # Error out on multi-document yaml
        if next(manifests, False):
            raise _RunnerError(
                f"Exactly one document expected in manifest {template_file}"
            )

        k8s_object = self.k8s_namespace.create_single_resource(
            manifest,
            custom_object=custom_object,
        )

        logger.info("%s %s created", k8s_object.kind, k8s_object.metadata.name)
        return k8s_object

    def _reuse_deployment(self, deployment_name) -> k8s.V1Deployment:
        deployment = self.k8s_namespace.get_deployment(deployment_name)
        # TODO(sergiitk): check if good or must be recreated
        return deployment

    def _reuse_service(self, service_name) -> k8s.V1Service:
        service = self.k8s_namespace.get_service(service_name)
        logger.info("Reusing service: %s", service_name)
        # TODO(sergiitk): check if good or must be recreated
        return service

    def _reuse_namespace(self) -> k8s.V1Namespace:
        logger.info("Reusing namespace: %s", self.k8s_namespace.name)
        return self.k8s_namespace.get()

    def _create_namespace(self, template, **kwargs) -> k8s.V1Namespace:
        namespace = self._create_from_template(template, **kwargs)
        if not isinstance(namespace, k8s.V1Namespace):
            raise _RunnerError(
                f"Expected V1Namespace to be created from manifest {template}"
            )
        if namespace.metadata.name != kwargs["namespace_name"]:
            raise _RunnerError(
                "V1Namespace created with unexpected name: "
                f"{namespace.metadata.name}"
            )
        logger.debug(
            "V1Namespace %s created at %s",
            namespace.metadata.self_link,
            namespace.metadata.creation_timestamp,
        )
        return namespace

    @classmethod
    def _get_workload_identity_member_name(
        cls, project, namespace_name, service_account_name
    ):
        """
        Returns workload identity member name used to authenticate Kubernetes
        service accounts.

        https://cloud.google.com/kubernetes-engine/docs/how-to/workload-identity
        """
        return (
            f"serviceAccount:{project}.svc.id.goog"
            f"[{namespace_name}/{service_account_name}]"
        )

    def _grant_workload_identity_user(
        self, *, gcp_iam, gcp_service_account, service_account_name
    ):
        workload_identity_member = self._get_workload_identity_member_name(
            gcp_iam.project, self.k8s_namespace.name, service_account_name
        )
        logger.info(
            "Granting %s to %s for GCP Service Account %s",
            self.ROLE_WORKLOAD_IDENTITY_USER,
            workload_identity_member,
            gcp_service_account,
        )

        gcp_iam.add_service_account_iam_policy_binding(
            gcp_service_account,
            self.ROLE_WORKLOAD_IDENTITY_USER,
            workload_identity_member,
        )

    def _revoke_workload_identity_user(
        self, *, gcp_iam, gcp_service_account, service_account_name
    ):
        workload_identity_member = self._get_workload_identity_member_name(
            gcp_iam.project, self.k8s_namespace.name, service_account_name
        )
        logger.info(
            "Revoking %s from %s for GCP Service Account %s",
            self.ROLE_WORKLOAD_IDENTITY_USER,
            workload_identity_member,
            gcp_service_account,
        )
        try:
            gcp_iam.remove_service_account_iam_policy_binding(
                gcp_service_account,
                self.ROLE_WORKLOAD_IDENTITY_USER,
                workload_identity_member,
            )
        except gcp.api.Error as error:
            logger.warning(
                "Failed  %s from %s for Service Account %s: %r",
                self.ROLE_WORKLOAD_IDENTITY_USER,
                workload_identity_member,
                gcp_service_account,
                error,
            )

    def _create_service_account(
        self, template, **kwargs
    ) -> k8s.V1ServiceAccount:
        resource = self._create_from_template(template, **kwargs)
        if not isinstance(resource, k8s.V1ServiceAccount):
            raise _RunnerError(
                "Expected V1ServiceAccount to be created "
                f"from manifest {template}"
            )
        if resource.metadata.name != kwargs["service_account_name"]:
            raise _RunnerError(
                "V1ServiceAccount created with unexpected name: "
                f"{resource.metadata.name}"
            )
        logger.debug(
            "V1ServiceAccount %s created at %s",
            resource.metadata.self_link,
            resource.metadata.creation_timestamp,
        )
        return resource

    def _create_deployment(self, template, **kwargs) -> k8s.V1Deployment:
        # Not making deployment_name an explicit kwarg to be consistent with
        # the rest of the _create_* methods, which pass kwargs as-is
        # to _create_from_template(), so that the kwargs dict is unpacked into
        # template variables and their values.
        if "deployment_name" not in kwargs:
            raise TypeError(
                "Missing required keyword-only argument: deployment_name"
            )

        # Automatically apply random deployment_id to use in the matchLabels
        # to prevent selecting pods in the same namespace belonging to
        # a different deployment.
        if "deployment_id" not in kwargs:
            rand_id: str = framework.helpers.rand.rand_string(lowercase=True)
            # Fun edge case: when rand_string() happen to generate numbers only,
            # yaml interprets deployment_id label value as an integer,
            # but k8s expects label values to be strings. Lol. K8s responds
            # with a barely readable 400 Bad Request error: 'ReadString: expects
            # \" or n, but found 9, error found in #10 byte of ...|ent_id'.
            # Prepending deployment name forces deployment_id into a string,
            # as well as it's just a better description.
            self.deployment_id = f'{kwargs["deployment_name"]}-{rand_id}'
            kwargs["deployment_id"] = self.deployment_id
        else:
            self.deployment_id = kwargs["deployment_id"]

        deployment = self._create_from_template(template, **kwargs)
        if not isinstance(deployment, k8s.V1Deployment):
            raise _RunnerError(
                f"Expected V1Deployment to be created from manifest {template}"
            )
        if deployment.metadata.name != kwargs["deployment_name"]:
            raise _RunnerError(
                "V1Deployment created with unexpected name: "
                f"{deployment.metadata.name}"
            )
        logger.debug(
            "V1Deployment %s created at %s",
            deployment.metadata.self_link,
            deployment.metadata.creation_timestamp,
        )
        return deployment

    def _create_gamma_route(self, template, **kwargs) -> k8s.GammaHttpRoute:
        route = self._create_from_template(
            template,
            custom_object=True,
            **kwargs,
        )
        if not (
            isinstance(route, k8s.GammaHttpRoute) and route.kind == "HTTPRoute"
        ):
            raise _RunnerError(
                f"Expected ResourceInstance[HTTPRoute] to be created from"
                f" manifest {template}"
            )
        if route.metadata.name != kwargs["route_name"]:
            raise _RunnerError(
                "ResourceInstance[HTTPRoute] created with unexpected name: "
                f"{route.metadata.name}"
            )
        logger.debug(
            "ResourceInstance[HTTPRoute] %s created at %s",
            route.metadata.name,
            route.metadata.creation_timestamp,
        )
        return route

    def _create_session_affinity_policy(self, template, **kwargs) -> k8s.GcpSessionAffinityPolicy:
        saPolicy = self._create_from_template(
            template,
            custom_object=True,
            **kwargs,
        )
        if not (
            isinstance(saPolicy, k8s.GcpSessionAffinityPolicy) and saPolicy.kind == "GCPSessionAffinityPolicy"
        ):
            raise _RunnerError(
                f"Expected ResourceInstance[GCPSessionAffinityPolicy] to be"
                f" created from manifest {template}"
            )
        if saPolicy.metadata.name != kwargs["session_affinity_policy_name"]:
            raise _RunnerError(
                "ResourceInstance[GCPSessionAffinityPolicy] created with"
                f" unexpected name: {saPolicy.metadata.name}"
            )
        logger.debug(
            "ResourceInstance[GCPSessionAffinityPolicy] %s created at %s",
            saPolicy.metadata.name,
            saPolicy.metadata.creation_timestamp,
        )
        return saPolicy

    def _create_session_affinity_filter(self, template, **kwargs) -> k8s.GcpSessionAffinityFilter:
        saFilter = self._create_from_template(
            template,
            custom_object=True,
            **kwargs,
        )
        if not (
            isinstance(saFilter, k8s.GcpSessionAffinityFilter) and saFilter.kind == "GCPSessionAffinityFilter"
        ):
            raise _RunnerError(
                f"Expected ResourceInstance[GCPSessionAffinityFilter] to be"
                f" created from manifest {template}"
            )
        if saFilter.metadata.name != kwargs["session_affinity_filter_name"]:
            raise _RunnerError(
                "ResourceInstance[GCPSessionAffinityFilter] created with"
                f" unexpected name: {saFilter.metadata.name}"
            )
        logger.debug(
            "ResourceInstance[GCPSessionAffinityFilter] %s created at %s",
            saFilter.metadata.name,
            saFilter.metadata.creation_timestamp,
        )
        return saFilter

    def _create_backend_policy(self, template, **kwargs) -> k8s.GcpBackendPolicy:
        saPolicy = self._create_from_template(
            template,
            custom_object=True,
            **kwargs,
        )
        if not (
            isinstance(bePolicy, k8s.GcpBackendPolicy) and bePolicy.kind == "GCPBackendPolicy"
        ):
            raise _RunnerError(
                f"Expected ResourceInstance[GCPBackendPolicy] to be"
                f" created from manifest {template}"
            )
        if bePolicy.metadata.name != kwargs["be_policy_name"]:
            raise _RunnerError(
                "ResourceInstance[GCPBackendPolicy] created with"
                f" unexpected name: {bePolicy.metadata.name}"
            )
        logger.debug(
            "ResourceInstance[GCPBackendPolicy] %s created at %s",
            bePolicy.metadata.name,
            bePolicy.metadata.creation_timestamp,
        )
        return bePolicy

    def _create_service(self, template, **kwargs) -> k8s.V1Service:
        service = self._create_from_template(template, **kwargs)
        if not isinstance(service, k8s.V1Service):
            raise _RunnerError(
                f"Expected V1Service to be created from manifest {template}"
            )
        if service.metadata.name != kwargs["service_name"]:
            raise _RunnerError(
                "V1Service created with unexpected name: "
                f"{service.metadata.name}"
            )
        logger.debug(
            "V1Service %s created at %s",
            service.metadata.self_link,
            service.metadata.creation_timestamp,
        )
        return service


    def _delete_gamma_route(self, name, wait_for_deletion=True):
        logger.info("Deleting HTTPRoute %s", name)
        try:
            self.k8s_namespace.delete_gamma_route(name)
        except (retryers.RetryError, k8s.NotFound) as e:
            logger.info("HTTPRoute %s deletion failed: %s", name, e)
            return

        if wait_for_deletion:
            self.k8s_namespace.wait_for_get_gamma_route_deleted(name)
        logger.debug("HTTPRoute %s deleted", name)

    def _delete_session_affinity_policy(self, name, wait_for_deletion=True):
        logger.info("Deleting GCPSessionAffinityPolicy %s", name)
        try:
            self.k8s_namespace.delete_session_affinity_policy(name)
        except (retryers.RetryError, k8s.NotFound) as e:
            logger.info("GCPSessionAffinityPolicy %s deletion failed: %s", name, e)
            return

        if wait_for_deletion:
            self.k8s_namespace.wait_for_get_session_affinity_policy_deleted(name)
        logger.debug("GCPSessionAffinityPolicy %s deleted", name)

    def _delete_session_affinity_filter(self, name, wait_for_deletion=True):
        logger.info("Deleting GCPSessionAffinityFilter %s", name)
        try:
            self.k8s_namespace.delete_session_affinity_filter(name)
        except (retryers.RetryError, k8s.NotFound) as e:
            logger.info("GCPSessionAffinityFilter %s deletion failed: %s", name, e)
            return

        if wait_for_deletion:
            self.k8s_namespace.wait_for_get_session_affinity_filter_deleted(name)
        logger.debug("GCPSessionAffinityFilter %s deleted", name)

    def _delete_backend_policy(self, name, wait_for_deletion=True):
        logger.info("Deleting GCPBackendPolicy %s", name)
        try:
            self.k8s_namespace.delete_backend_policy(name)
        except (retryers.RetryError, k8s.NotFound) as e:
            logger.info("GGCPBackendPolicy %s deletion failed: %s", name, e)
            return

        if wait_for_deletion:
            self.k8s_namespace.wait_for_get_backend_policy_deleted(name)
        logger.debug("GCPBackendPolicy %s deleted", name)

    def _delete_deployment(self, name, wait_for_deletion=True):
        logger.info("Deleting deployment %s", name)
        self.stop_pod_dependencies()
        try:
            self.k8s_namespace.delete_deployment(name)
        except (retryers.RetryError, k8s.NotFound) as e:
            logger.info("Deployment %s deletion failed: %s", name, e)
            return

        if wait_for_deletion:
            self.k8s_namespace.wait_for_deployment_deleted(name)
        logger.debug("Deployment %s deleted", name)

    def _delete_service(self, name, wait_for_deletion=True):
        logger.info("Deleting service %s", name)
        try:
            self.k8s_namespace.delete_service(name)
        except (retryers.RetryError, k8s.NotFound) as e:
            logger.info("Service %s deletion failed: %s", name, e)
            return

        if wait_for_deletion:
            self.k8s_namespace.wait_for_service_deleted(name)

        logger.debug("Service %s deleted", name)

    def _delete_service_account(self, name, wait_for_deletion=True):
        logger.info("Deleting service account %s", name)
        try:
            self.k8s_namespace.delete_service_account(name)
        except (retryers.RetryError, k8s.NotFound) as e:
            logger.info("Service account %s deletion failed: %s", name, e)
            return

        if wait_for_deletion:
            self.k8s_namespace.wait_for_service_account_deleted(name)
        logger.debug("Service account %s deleted", name)

    def delete_namespace(self, wait_for_deletion=True):
        logger.info("Deleting namespace %s", self.k8s_namespace.name)
        try:
            self.k8s_namespace.delete()
        except (retryers.RetryError, k8s.NotFound) as e:
            logger.info(
                "Namespace %s deletion failed: %s", self.k8s_namespace.name, e
            )
            return

        if wait_for_deletion:
            self.k8s_namespace.wait_for_namespace_deleted()
        logger.debug("Namespace %s deleted", self.k8s_namespace.name)

    def _wait_deployment_with_available_replicas(self, name, count=1, **kwargs):
        logger.info(
            "Waiting for deployment %s to report %s available replica(s)",
            name,
            count,
        )
        self.k8s_namespace.wait_for_deployment_available_replicas(
            name, count, **kwargs
        )
        deployment = self.k8s_namespace.get_deployment(name)
        logger.info(
            "Deployment %s has %i replicas available",
            deployment.metadata.name,
            deployment.status.available_replicas,
        )

    def _wait_deployment_pod_count(
        self, deployment: k8s.V1Deployment, count: int = 1, **kwargs
    ) -> List[str]:
        logger.info(
            "Waiting for deployment %s to initialize %s pod(s)",
            deployment.metadata.name,
            count,
        )
        self.k8s_namespace.wait_for_deployment_replica_count(
            deployment, count, **kwargs
        )
        pods = self.k8s_namespace.list_deployment_pods(deployment)
        pod_names = [pod.metadata.name for pod in pods]
        logger.info(
            "Deployment %s initialized %i pod(s): %s",
            deployment.metadata.name,
            count,
            pod_names,
        )
        # Pods may not  be started yet, just return the names.
        return pod_names

    def _wait_pod_started(self, name, **kwargs) -> k8s.V1Pod:
        logger.info("Waiting for pod %s to start", name)
        self.k8s_namespace.wait_for_pod_started(name, **kwargs)
        pod = self.k8s_namespace.get_pod(name)
        logger.info(
            "Pod %s ready, IP: %s", pod.metadata.name, pod.status.pod_ip
        )
        return pod

    def _start_port_forwarding_pod(
        self, pod: k8s.V1Pod, remote_port: int
    ) -> k8s.PortForwarder:
        logger.info(
            "LOCAL DEV MODE: Enabling port forwarding to %s:%s",
            pod.status.pod_ip,
            remote_port,
        )
        port_forwarder = self.k8s_namespace.port_forward_pod(pod, remote_port)
        self.pod_port_forwarders.append(port_forwarder)
        return port_forwarder

    def _start_logging_pod(
        self, pod: k8s.V1Pod, *, log_to_stdout: bool = False
    ) -> k8s.PodLogCollector:
        pod_name = pod.metadata.name
        logfile_name = f"{self.k8s_namespace.name}_{pod_name}.log"
        log_path = self.logs_subdir / logfile_name
        logger.info(
            "Enabling log collection from pod %s to %s",
            pod_name,
            log_path.relative_to(self.logs_subdir.parent.parent),
        )
        pod_log_collector = self.k8s_namespace.pod_start_logging(
            pod_name=pod_name,
            log_path=log_path,
            log_stop_event=self.log_stop_event,
            log_to_stdout=log_to_stdout,
            # Timestamps are enabled because not all language implementations
            # include them.
            # TODO(sergiitk): Make this setting language-specific.
            log_timestamps=True,
        )
        self.pod_log_collectors.append(pod_log_collector)
        return pod_log_collector

    def _wait_service_neg(self, name, service_port, **kwargs):
        logger.info("Waiting for NEG for service %s", name)
        self.k8s_namespace.wait_for_service_neg(name, **kwargs)
        neg_name, neg_zones = self.k8s_namespace.get_service_neg(
            name, service_port
        )
        logger.info(
            "Service %s: detected NEG=%s in zones=%s", name, neg_name, neg_zones
        )

    def logs_explorer_link(self):
        """Prints GCP Logs Explorer link to all runs of the deployment."""
        self._logs_explorer_link(
            deployment_name=self.deployment_name,
            namespace_name=self.k8s_namespace.name,
            gcp_project=self.gcp_project,
            gcp_ui_url=self.gcp_ui_url,
        )

    def logs_explorer_run_history_links(self):
        """Prints a separate GCP Logs Explorer link for each run *completed* by
        the runner.

        This excludes the current run, if it hasn't been completed.
        """
        if not self.run_history:
            logger.info("No completed deployments of %s", self.deployment_name)
            return
        for run in self.run_history:
            self._logs_explorer_link(
                deployment_name=self.deployment_name,
                namespace_name=self.k8s_namespace.name,
                gcp_project=self.gcp_project,
                gcp_ui_url=self.gcp_ui_url,
                deployment_id=run.deployment_id,
                start_time=run.time_start_requested,
                end_time=run.time_stopped,
            )

    @classmethod
    def _logs_explorer_link(
        cls,
        *,
        deployment_name: str,
        namespace_name: str,
        gcp_project: str,
        gcp_ui_url: str,
        deployment_id: Optional[str] = None,
        start_time: Optional[_datetime] = None,
        end_time: Optional[_datetime] = None,
    ):
        """Output the link to test server/client logs in GCP Logs Explorer."""
        if not start_time:
            start_time = _datetime.now()
        if not end_time:
            end_time = start_time + _timedelta(minutes=30)

        logs_start = _helper_datetime.iso8601_utc_time(start_time)
        logs_end = _helper_datetime.iso8601_utc_time(end_time)
        request = {"timeRange": f"{logs_start}/{logs_end}"}
        query = {
            "resource.type": "k8s_container",
            "resource.labels.project_id": gcp_project,
            "resource.labels.container_name": deployment_name,
            "resource.labels.namespace_name": namespace_name,
        }
        if deployment_id:
            query['labels."k8s-pod/deployment_id"'] = deployment_id

        link = cls._logs_explorer_link_from_params(
            gcp_ui_url=gcp_ui_url,
            gcp_project=gcp_project,
            query=query,
            request=request,
        )
        link_to = deployment_id if deployment_id else deployment_name
        # A whitespace at the end to indicate the end of the url.
        logger.info("GCP Logs Explorer link to %s:\n%s ", link_to, link)

    @classmethod
    def _make_namespace_name(
        cls, resource_prefix: str, resource_suffix: str, name: str
    ) -> str:
        """A helper to make consistent test app kubernetes namespace name
        for given resource prefix and suffix."""
        parts = [resource_prefix, name]
        # Avoid trailing dash when the suffix is empty.
        if resource_suffix:
            parts.append(resource_suffix)
        return "-".join(parts)
# Copyright 2022 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
