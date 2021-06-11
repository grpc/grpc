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
import contextlib
import logging
import os
import pathlib
from typing import Optional
import time
import threading

import mako.template
import tenacity
import yaml

from framework.infrastructure import k8s
from kubernetes import client, utils, watch
import kubernetes.client.rest

logger = logging.getLogger(__name__)

if "KOKORO_ARTIFACTS_DIR" in os.environ:
    _TEST_LOG_BASE_DIR = os.environ["KOKORO_ARTIFACTS_DIR"]
else:
    _TEST_LOG_BASE_DIR = os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        '../../../../../../artifacts')

_BACKOFF_SECONDS = 1


class RunnerError(Exception):
    """Error running app"""


class KubernetesBaseRunner:
    TEMPLATE_DIR_NAME = 'kubernetes-manifests'
    TEMPLATE_DIR_RELATIVE_PATH = f'../../{TEMPLATE_DIR_NAME}'

    def __init__(self,
                 k8s_namespace,
                 namespace_template=None,
                 reuse_namespace=False,
                 case_name=None,
                 pod_label_selector=None):
        # Kubernetes namespaced resources manager
        self.k8s_namespace: k8s.KubernetesNamespace = k8s_namespace
        self.reuse_namespace = reuse_namespace
        self.namespace_template = namespace_template or 'namespace.yaml'
        self.case_name: str = case_name or ""
        self.pod_label_selector: str = pod_label_selector or ""

        # Mutable state
        self.namespace: Optional[k8s.V1Namespace] = None
        self._log_stop_event: threading.Event = threading.Event()

    def _await_container_ready(self, core: client.CoreV1Api, pod_name: str,
                               container_name: str, namespace: str) -> None:
        while not self._log_stop_event.is_set():
            try:
                pod_status = core.read_namespaced_pod_status(
                    pod_name, namespace).status
                candidate_containers = [
                    container for container in pod_status.container_statuses
                    if container.name == container_name
                ]
                if not candidate_containers:
                    time.sleep(_BACKOFF_SECONDS)
                    continue
                container = candidate_containers[0]
                if not container.ready:
                    time.sleep(_BACKOFF_SECONDS)
                else:
                    break
            except kubernetes.client.rest.ApiException:
                time.sleep(_BACKOFF_SECONDS)

    def _log_container(self, core: client.CoreV1Api, pod_name: str,
                       container_name: str, namespace: str) -> None:
        query_restarted = False
        logfile = os.path.join(
            _TEST_LOG_BASE_DIR,
            f"{self.case_name}.{pod_name}.{container_name}.sponge_log.log")

        self._await_container_ready(core, pod_name, container_name, namespace)

        with open(logfile, "w") as f:
            while not self._log_stop_event.is_set():
                try:
                    if query_restarted:
                        f.write(
                            "Restarted log fetching. Attempting to read from the beginning, but truncation may have occurred.\n"
                        )
                    w = watch.Watch()
                    for msg in w.stream(core.read_namespaced_pod_log,
                                        name=pod_name,
                                        namespace=namespace,
                                        container=container_name,
                                        follow=True):
                        f.write(msg)
                        f.write("\n")
                except kubernetes.client.rest.ApiException as e:
                    f.write(f"Exception fetching logs: {e}\n")
                    query_restarted = True
                    time.sleep(_BACKOFF_SECONDS)

    def _start_logging_container(self, core: client.CoreV1Api, pod_name: str,
                                 container_name: str, namespace: str) -> None:
        t = threading.Thread(target=self._log_container,
                             args=(core, pod_name, container_name, namespace),
                             daemon=True)
        t.start()

    def _start_logging_pod(self, core: client.CoreV1Api, pod_name: str,
                           namespace: str) -> None:
        retryer = tenacity.Retrying(retry=tenacity.retry_if_exception_type(
            kubernetes.client.rest.ApiException),
                                    wait=tenacity.wait_fixed(_BACKOFF_SECONDS),
                                    reraise=True)
        pod = retryer(core.read_namespaced_pod, pod_name, namespace)
        for container in pod.spec.containers:
            self._start_logging_container(core, pod_name, container.name,
                                          namespace)

    def _start_logging_deployment(self, deployment_name: str):
        core = self.k8s_namespace.api.core
        apps = self.k8s_namespace.api.apps

        pod_names = None
        namespace = self.k8s_namespace.name

        def _get_deployment_pods():
            pods_all_namespaces = core.list_pod_for_all_namespaces(
                label_selector=self.pod_label_selector).items
            pods = [
                pod for pod in pods_all_namespaces
                if pod.metadata.namespace == namespace
            ]
            return [pod.metadata.name for pod in pods]

        retryer = tenacity.Retrying(retry=tenacity.retry_if_exception_type(
            kubernetes.client.rest.ApiException),
                                    wait=tenacity.wait_fixed(_BACKOFF_SECONDS),
                                    reraise=True)

        pod_names = retryer(_get_deployment_pods)

        for pod_name in pod_names:
            self._start_logging_pod(core, pod_name, namespace)

    def run(self, **kwargs):
        if self.reuse_namespace:
            self.namespace = self._reuse_namespace()
        if not self.namespace:
            self.namespace = self._create_namespace(
                self.namespace_template, namespace_name=self.k8s_namespace.name)

    def cleanup(self, *, force=False):
        if (self.namespace and not self.reuse_namespace) or force:
            self._delete_namespace()
            self.namespace = None

    @staticmethod
    def _render_template(template_file, **kwargs):
        template = mako.template.Template(filename=str(template_file))
        return template.render(**kwargs)

    @staticmethod
    def _manifests_from_yaml_file(yaml_file):
        with open(yaml_file) as f:
            with contextlib.closing(yaml.safe_load_all(f)) as yml:
                for manifest in yml:
                    yield manifest

    @staticmethod
    def _manifests_from_str(document):
        with contextlib.closing(yaml.safe_load_all(document)) as yml:
            for manifest in yml:
                yield manifest

    @classmethod
    def _template_file_from_name(cls, template_name):
        templates_path = (pathlib.Path(__file__).parent /
                          cls.TEMPLATE_DIR_RELATIVE_PATH)
        return templates_path.joinpath(template_name).resolve()

    def _create_from_template(self, template_name, **kwargs):
        template_file = self._template_file_from_name(template_name)
        logger.debug("Loading k8s manifest template: %s", template_file)

        yaml_doc = self._render_template(template_file, **kwargs)
        logger.info("Rendered template %s/%s:\n%s", self.TEMPLATE_DIR_NAME,
                    template_name, yaml_doc)

        manifests = self._manifests_from_str(yaml_doc)
        manifest = next(manifests)
        # Error out on multi-document yaml
        if next(manifests, False):
            raise RunnerError('Exactly one document expected in manifest '
                              f'{template_file}')
        k8s_objects = self.k8s_namespace.apply_manifest(manifest)
        if len(k8s_objects) != 1:
            raise RunnerError('Expected exactly one object must created from '
                              f'manifest {template_file}')

        logger.info('%s %s created', k8s_objects[0].kind,
                    k8s_objects[0].metadata.name)
        return k8s_objects[0]

    def _reuse_deployment(self, deployment_name) -> k8s.V1Deployment:
        deployment = self.k8s_namespace.get_deployment(deployment_name)
        # TODO(sergiitk): check if good or must be recreated
        return deployment

    def _reuse_service(self, service_name) -> k8s.V1Service:
        service = self.k8s_namespace.get_service(service_name)
        # TODO(sergiitk): check if good or must be recreated
        return service

    def _reuse_namespace(self) -> k8s.V1Namespace:
        return self.k8s_namespace.get()

    def _create_namespace(self, template, **kwargs) -> k8s.V1Namespace:
        namespace = self._create_from_template(template, **kwargs)
        if not isinstance(namespace, k8s.V1Namespace):
            raise RunnerError('Expected V1Namespace to be created '
                              f'from manifest {template}')
        if namespace.metadata.name != kwargs['namespace_name']:
            raise RunnerError('V1Namespace created with unexpected name: '
                              f'{namespace.metadata.name}')
        logger.debug('V1Namespace %s created at %s',
                     namespace.metadata.self_link,
                     namespace.metadata.creation_timestamp)
        return namespace

    def _create_service_account(self, template,
                                **kwargs) -> k8s.V1ServiceAccount:
        resource = self._create_from_template(template, **kwargs)
        if not isinstance(resource, k8s.V1ServiceAccount):
            raise RunnerError('Expected V1ServiceAccount to be created '
                              f'from manifest {template}')
        if resource.metadata.name != kwargs['service_account_name']:
            raise RunnerError('V1ServiceAccount created with unexpected name: '
                              f'{resource.metadata.name}')
        logger.debug('V1ServiceAccount %s created at %s',
                     resource.metadata.self_link,
                     resource.metadata.creation_timestamp)
        return resource

    def _create_deployment(self, template, **kwargs) -> k8s.V1Deployment:
        deployment = self._create_from_template(template, **kwargs)
        if not isinstance(deployment, k8s.V1Deployment):
            raise RunnerError('Expected V1Deployment to be created '
                              f'from manifest {template}')
        if deployment.metadata.name != kwargs['deployment_name']:
            raise RunnerError('V1Deployment created with unexpected name: '
                              f'{deployment.metadata.name}')
        logger.debug('V1Deployment %s created at %s',
                     deployment.metadata.self_link,
                     deployment.metadata.creation_timestamp)
        self._start_logging_deployment(deployment.metadata.name)
        return deployment

    def _create_service(self, template, **kwargs) -> k8s.V1Service:
        service = self._create_from_template(template, **kwargs)
        if not isinstance(service, k8s.V1Service):
            raise RunnerError('Expected V1Service to be created '
                              f'from manifest {template}')
        if service.metadata.name != kwargs['service_name']:
            raise RunnerError('V1Service created with unexpected name: '
                              f'{service.metadata.name}')
        logger.debug('V1Service %s created at %s', service.metadata.self_link,
                     service.metadata.creation_timestamp)
        return service

    def _delete_deployment(self, name, wait_for_deletion=True):
        logger.info('Deleting deployment %s', name)
        try:
            self.k8s_namespace.delete_deployment(name)
            self._log_stop_event.set()
        except k8s.ApiException as e:
            logger.info('Deployment %s deletion failed, error: %s %s', name,
                        e.status, e.reason)
            return

        if wait_for_deletion:
            self.k8s_namespace.wait_for_deployment_deleted(name)
        logger.debug('Deployment %s deleted', name)

    def _delete_service(self, name, wait_for_deletion=True):
        logger.info('Deleting service %s', name)
        try:
            self.k8s_namespace.delete_service(name)
        except k8s.ApiException as e:
            logger.info('Service %s deletion failed, error: %s %s', name,
                        e.status, e.reason)
            return

        if wait_for_deletion:
            self.k8s_namespace.wait_for_service_deleted(name)
        logger.debug('Service %s deleted', name)

    def _delete_service_account(self, name, wait_for_deletion=True):
        logger.info('Deleting service account %s', name)
        try:
            self.k8s_namespace.delete_service_account(name)
        except k8s.ApiException as e:
            logger.info('Service account %s deletion failed, error: %s %s',
                        name, e.status, e.reason)
            return

        if wait_for_deletion:
            self.k8s_namespace.wait_for_service_account_deleted(name)
        logger.debug('Service account %s deleted', name)

    def _delete_namespace(self, wait_for_deletion=True):
        logger.info('Deleting namespace %s', self.k8s_namespace.name)
        try:
            self.k8s_namespace.delete()
        except k8s.ApiException as e:
            logger.info('Namespace %s deletion failed, error: %s %s',
                        self.k8s_namespace.name, e.status, e.reason)
            return

        if wait_for_deletion:
            self.k8s_namespace.wait_for_namespace_deleted()
        logger.debug('Namespace %s deleted', self.k8s_namespace.name)

    def _wait_deployment_with_available_replicas(self, name, count=1, **kwargs):
        logger.info('Waiting for deployment %s to have %s available replica(s)',
                    name, count)
        self.k8s_namespace.wait_for_deployment_available_replicas(
            name, count, **kwargs)
        deployment = self.k8s_namespace.get_deployment(name)
        logger.info('Deployment %s has %i replicas available',
                    deployment.metadata.name,
                    deployment.status.available_replicas)

    def _wait_pod_started(self, name, **kwargs):
        logger.info('Waiting for pod %s to start', name)
        self.k8s_namespace.wait_for_pod_started(name, **kwargs)
        pod = self.k8s_namespace.get_pod(name)
        logger.info('Pod %s ready, IP: %s', pod.metadata.name,
                    pod.status.pod_ip)

    def _wait_service_neg(self, name, service_port, **kwargs):
        logger.info('Waiting for NEG for service %s', name)
        self.k8s_namespace.wait_for_service_neg(name, **kwargs)
        neg_name, neg_zones = self.k8s_namespace.get_service_neg(
            name, service_port)
        logger.info("Service %s: detected NEG=%s in zones=%s", name, neg_name,
                    neg_zones)
