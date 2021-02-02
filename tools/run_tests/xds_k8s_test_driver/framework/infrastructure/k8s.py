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
import functools
import json
import logging
import subprocess
import time
from typing import Optional, List, Tuple

# TODO(sergiitk): replace with tenacity
import retrying
import kubernetes.config
from kubernetes import client
from kubernetes import utils

logger = logging.getLogger(__name__)
# Type aliases
V1Deployment = client.V1Deployment
V1ServiceAccount = client.V1ServiceAccount
V1Pod = client.V1Pod
V1PodList = client.V1PodList
V1Service = client.V1Service
V1Namespace = client.V1Namespace
ApiException = client.ApiException


def simple_resource_get(func):

    def wrap_not_found_return_none(*args, **kwargs):
        try:
            return func(*args, **kwargs)
        except client.ApiException as e:
            if e.status == 404:
                # Ignore 404
                return None
            raise

    return wrap_not_found_return_none


def label_dict_to_selector(labels: dict) -> str:
    return ','.join(f'{k}=={v}' for k, v in labels.items())


class KubernetesApiManager:

    def __init__(self, context):
        self.context = context
        self.client = self._cached_api_client_for_context(context)
        self.apps = client.AppsV1Api(self.client)
        self.core = client.CoreV1Api(self.client)

    def close(self):
        self.client.close()

    @classmethod
    @functools.lru_cache(None)
    def _cached_api_client_for_context(cls, context: str) -> client.ApiClient:
        client_instance = kubernetes.config.new_client_from_config(
            context=context)
        logger.info('Using kubernetes context "%s", active host: %s', context,
                    client_instance.configuration.host)
        return client_instance


class PortForwardingError(Exception):
    """Error forwarding port"""


class KubernetesNamespace:
    NEG_STATUS_META = 'cloud.google.com/neg-status'
    PORT_FORWARD_LOCAL_ADDRESS: str = '127.0.0.1'
    DELETE_GRACE_PERIOD_SEC: int = 5
    WAIT_SHORT_TIMEOUT_SEC: int = 60
    WAIT_SHORT_SLEEP_SEC: int = 1
    WAIT_MEDIUM_TIMEOUT_SEC: int = 5 * 60
    WAIT_MEDIUM_SLEEP_SEC: int = 10
    WAIT_LONG_TIMEOUT_SEC: int = 10 * 60
    WAIT_LONG_SLEEP_SEC: int = 30

    def __init__(self, api: KubernetesApiManager, name: str):
        self.name = name
        self.api = api

    def apply_manifest(self, manifest):
        return utils.create_from_dict(self.api.client,
                                      manifest,
                                      namespace=self.name)

    @simple_resource_get
    def get_service(self, name) -> V1Service:
        return self.api.core.read_namespaced_service(name, self.name)

    @simple_resource_get
    def get_service_account(self, name) -> V1Service:
        return self.api.core.read_namespaced_service_account(name, self.name)

    def delete_service(self,
                       name,
                       grace_period_seconds=DELETE_GRACE_PERIOD_SEC):
        self.api.core.delete_namespaced_service(
            name=name,
            namespace=self.name,
            body=client.V1DeleteOptions(
                propagation_policy='Foreground',
                grace_period_seconds=grace_period_seconds))

    def delete_service_account(self,
                               name,
                               grace_period_seconds=DELETE_GRACE_PERIOD_SEC):
        self.api.core.delete_namespaced_service_account(
            name=name,
            namespace=self.name,
            body=client.V1DeleteOptions(
                propagation_policy='Foreground',
                grace_period_seconds=grace_period_seconds))

    @simple_resource_get
    def get(self) -> V1Namespace:
        return self.api.core.read_namespace(self.name)

    def delete(self, grace_period_seconds=DELETE_GRACE_PERIOD_SEC):
        self.api.core.delete_namespace(
            name=self.name,
            body=client.V1DeleteOptions(
                propagation_policy='Foreground',
                grace_period_seconds=grace_period_seconds))

    def wait_for_service_deleted(self,
                                 name: str,
                                 timeout_sec=WAIT_SHORT_TIMEOUT_SEC,
                                 wait_sec=WAIT_SHORT_SLEEP_SEC):

        @retrying.retry(retry_on_result=lambda r: r is not None,
                        stop_max_delay=timeout_sec * 1000,
                        wait_fixed=wait_sec * 1000)
        def _wait_for_deleted_service_with_retry():
            service = self.get_service(name)
            if service is not None:
                logger.debug('Waiting for service %s to be deleted',
                             service.metadata.name)
            return service

        _wait_for_deleted_service_with_retry()

    def wait_for_service_account_deleted(self,
                                         name: str,
                                         timeout_sec=WAIT_SHORT_TIMEOUT_SEC,
                                         wait_sec=WAIT_SHORT_SLEEP_SEC):

        @retrying.retry(retry_on_result=lambda r: r is not None,
                        stop_max_delay=timeout_sec * 1000,
                        wait_fixed=wait_sec * 1000)
        def _wait_for_deleted_service_account_with_retry():
            service_account = self.get_service_account(name)
            if service_account is not None:
                logger.debug('Waiting for service account %s to be deleted',
                             service_account.metadata.name)
            return service_account

        _wait_for_deleted_service_account_with_retry()

    def wait_for_namespace_deleted(self,
                                   timeout_sec=WAIT_LONG_TIMEOUT_SEC,
                                   wait_sec=WAIT_LONG_SLEEP_SEC):

        @retrying.retry(retry_on_result=lambda r: r is not None,
                        stop_max_delay=timeout_sec * 1000,
                        wait_fixed=wait_sec * 1000)
        def _wait_for_deleted_namespace_with_retry():
            namespace = self.get()
            if namespace is not None:
                logger.debug('Waiting for namespace %s to be deleted',
                             namespace.metadata.name)
            return namespace

        _wait_for_deleted_namespace_with_retry()

    def wait_for_service_neg(self,
                             name: str,
                             timeout_sec=WAIT_SHORT_TIMEOUT_SEC,
                             wait_sec=WAIT_SHORT_SLEEP_SEC):

        @retrying.retry(retry_on_result=lambda r: not r,
                        stop_max_delay=timeout_sec * 1000,
                        wait_fixed=wait_sec * 1000)
        def _wait_for_service_neg():
            service = self.get_service(name)
            if self.NEG_STATUS_META not in service.metadata.annotations:
                logger.debug('Waiting for service %s NEG',
                             service.metadata.name)
                return False
            return True

        _wait_for_service_neg()

    def get_service_neg(self, service_name: str,
                        service_port: int) -> Tuple[str, List[str]]:
        service = self.get_service(service_name)
        neg_info: dict = json.loads(
            service.metadata.annotations[self.NEG_STATUS_META])
        neg_name: str = neg_info['network_endpoint_groups'][str(service_port)]
        neg_zones: List[str] = neg_info['zones']
        return neg_name, neg_zones

    @simple_resource_get
    def get_deployment(self, name) -> V1Deployment:
        return self.api.apps.read_namespaced_deployment(name, self.name)

    def delete_deployment(self,
                          name,
                          grace_period_seconds=DELETE_GRACE_PERIOD_SEC):
        self.api.apps.delete_namespaced_deployment(
            name=name,
            namespace=self.name,
            body=client.V1DeleteOptions(
                propagation_policy='Foreground',
                grace_period_seconds=grace_period_seconds))

    def list_deployment_pods(self, deployment: V1Deployment) -> List[V1Pod]:
        # V1LabelSelector.match_expressions not supported at the moment
        return self.list_pods_with_labels(deployment.spec.selector.match_labels)

    def wait_for_deployment_available_replicas(
            self,
            name,
            count=1,
            timeout_sec=WAIT_MEDIUM_TIMEOUT_SEC,
            wait_sec=WAIT_MEDIUM_SLEEP_SEC):

        @retrying.retry(
            retry_on_result=lambda r: not self._replicas_available(r, count),
            stop_max_delay=timeout_sec * 1000,
            wait_fixed=wait_sec * 1000)
        def _wait_for_deployment_available_replicas():
            deployment = self.get_deployment(name)
            logger.debug(
                'Waiting for deployment %s to have %s available '
                'replicas, current count %s', deployment.metadata.name, count,
                deployment.status.available_replicas)
            return deployment

        _wait_for_deployment_available_replicas()

    def wait_for_deployment_deleted(self,
                                    deployment_name: str,
                                    timeout_sec=WAIT_MEDIUM_TIMEOUT_SEC,
                                    wait_sec=WAIT_MEDIUM_SLEEP_SEC):

        @retrying.retry(retry_on_result=lambda r: r is not None,
                        stop_max_delay=timeout_sec * 1000,
                        wait_fixed=wait_sec * 1000)
        def _wait_for_deleted_deployment_with_retry():
            deployment = self.get_deployment(deployment_name)
            if deployment is not None:
                logger.debug(
                    'Waiting for deployment %s to be deleted. '
                    'Non-terminated replicas: %s', deployment.metadata.name,
                    deployment.status.replicas)
            return deployment

        _wait_for_deleted_deployment_with_retry()

    def list_pods_with_labels(self, labels: dict) -> List[V1Pod]:
        pod_list: V1PodList = self.api.core.list_namespaced_pod(
            self.name, label_selector=label_dict_to_selector(labels))
        return pod_list.items

    def get_pod(self, name) -> client.V1Pod:
        return self.api.core.read_namespaced_pod(name, self.name)

    def wait_for_pod_started(self,
                             pod_name,
                             timeout_sec=WAIT_SHORT_TIMEOUT_SEC,
                             wait_sec=WAIT_SHORT_SLEEP_SEC):

        @retrying.retry(retry_on_result=lambda r: not self._pod_started(r),
                        stop_max_delay=timeout_sec * 1000,
                        wait_fixed=wait_sec * 1000)
        def _wait_for_pod_started():
            pod = self.get_pod(pod_name)
            logger.debug('Waiting for pod %s to start, current phase: %s',
                         pod.metadata.name, pod.status.phase)
            return pod

        _wait_for_pod_started()

    def port_forward_pod(
        self,
        pod: V1Pod,
        remote_port: int,
        local_port: Optional[int] = None,
        local_address: Optional[str] = None,
    ) -> subprocess.Popen:
        """Experimental"""
        local_address = local_address or self.PORT_FORWARD_LOCAL_ADDRESS
        local_port = local_port or remote_port
        cmd = [
            "kubectl", "--context", self.api.context, "--namespace", self.name,
            "port-forward", "--address", local_address,
            f"pod/{pod.metadata.name}", f"{local_port}:{remote_port}"
        ]
        pf = subprocess.Popen(cmd,
                              stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT,
                              universal_newlines=True)
        # Wait for stdout line indicating successful start.
        expected = (f"Forwarding from {local_address}:{local_port}"
                    f" -> {remote_port}")
        try:
            while True:
                time.sleep(0.05)
                output = pf.stdout.readline().strip()
                if not output:
                    return_code = pf.poll()
                    if return_code is not None:
                        errors = [error for error in pf.stdout.readlines()]
                        raise PortForwardingError(
                            'Error forwarding port, kubectl return '
                            f'code {return_code}, output {errors}')
                elif output != expected:
                    raise PortForwardingError(
                        f'Error forwarding port, unexpected output {output}')
                else:
                    logger.info(output)
                    break
        except Exception:
            self.port_forward_stop(pf)
            raise

        # TODO(sergiitk): return new PortForwarder object
        return pf

    @staticmethod
    def port_forward_stop(pf):
        logger.info('Shutting down port forwarding, pid %s', pf.pid)
        pf.kill()
        stdout, _stderr = pf.communicate(timeout=5)
        logger.info('Port forwarding stopped')
        logger.debug('Port forwarding remaining stdout: %s', stdout)

    @staticmethod
    def _pod_started(pod: V1Pod):
        return pod.status.phase not in ('Pending', 'Unknown')

    @staticmethod
    def _replicas_available(deployment, count):
        return (deployment is not None and
                deployment.status.available_replicas is not None and
                deployment.status.available_replicas >= count)
