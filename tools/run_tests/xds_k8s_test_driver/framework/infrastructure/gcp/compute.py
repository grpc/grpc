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
import dataclasses
import enum
import logging
from typing import Any, Dict, Optional

from googleapiclient import discovery
import googleapiclient.errors
# TODO(sergiitk): replace with tenacity
import retrying

from framework.infrastructure import gcp

logger = logging.getLogger(__name__)


class ComputeV1(gcp.api.GcpProjectApiResource):
    # TODO(sergiitk): move someplace better
    _WAIT_FOR_BACKEND_SEC = 60 * 10
    _WAIT_FOR_OPERATION_SEC = 60 * 5

    @dataclasses.dataclass(frozen=True)
    class GcpResource:
        name: str
        url: str

    @dataclasses.dataclass(frozen=True)
    class ZonalGcpResource(GcpResource):
        zone: str

    def __init__(self, api_manager: gcp.api.GcpApiManager, project: str):
        super().__init__(api_manager.compute('v1'), project)

    class HealthCheckProtocol(enum.Enum):
        TCP = enum.auto()
        GRPC = enum.auto()

    class BackendServiceProtocol(enum.Enum):
        HTTP2 = enum.auto()
        GRPC = enum.auto()

    def create_health_check(self,
                            name: str,
                            protocol: HealthCheckProtocol,
                            *,
                            port: Optional[int] = None) -> GcpResource:
        if protocol is self.HealthCheckProtocol.TCP:
            health_check_field = 'tcpHealthCheck'
        elif protocol is self.HealthCheckProtocol.GRPC:
            health_check_field = 'grpcHealthCheck'
        else:
            raise TypeError(f'Unexpected Health Check protocol: {protocol}')

        health_check_settings = {}
        if port is None:
            health_check_settings['portSpecification'] = 'USE_SERVING_PORT'
        else:
            health_check_settings['portSpecification'] = 'USE_FIXED_PORT'
            health_check_settings['port'] = port

        return self._insert_resource(
            self.api.healthChecks(), {
                'name': name,
                'type': protocol.name,
                health_check_field: health_check_settings,
            })

    def delete_health_check(self, name):
        self._delete_resource(self.api.healthChecks(), 'healthCheck', name)

    def create_backend_service_traffic_director(
            self,
            name: str,
            health_check: GcpResource,
            protocol: Optional[BackendServiceProtocol] = None) -> GcpResource:
        if not isinstance(protocol, self.BackendServiceProtocol):
            raise TypeError(f'Unexpected Backend Service protocol: {protocol}')
        return self._insert_resource(
            self.api.backendServices(),
            {
                'name': name,
                'loadBalancingScheme':
                    'INTERNAL_SELF_MANAGED',  # Traffic Director
                'healthChecks': [health_check.url],
                'protocol': protocol.name,
            })

    def get_backend_service_traffic_director(self, name: str) -> GcpResource:
        return self._get_resource(self.api.backendServices(),
                                  backendService=name)

    def patch_backend_service(self, backend_service, body, **kwargs):
        self._patch_resource(collection=self.api.backendServices(),
                             backendService=backend_service.name,
                             body=body,
                             **kwargs)

    def backend_service_add_backends(self, backend_service, backends):
        backend_list = [{
            'group': backend.url,
            'balancingMode': 'RATE',
            'maxRatePerEndpoint': 5
        } for backend in backends]

        self._patch_resource(collection=self.api.backendServices(),
                             body={'backends': backend_list},
                             backendService=backend_service.name)

    def backend_service_remove_all_backends(self, backend_service):
        self._patch_resource(collection=self.api.backendServices(),
                             body={'backends': []},
                             backendService=backend_service.name)

    def delete_backend_service(self, name):
        self._delete_resource(self.api.backendServices(), 'backendService',
                              name)

    def create_url_map(
        self,
        name: str,
        matcher_name: str,
        src_hosts,
        dst_default_backend_service: GcpResource,
        dst_host_rule_match_backend_service: Optional[GcpResource] = None,
    ) -> GcpResource:
        if dst_host_rule_match_backend_service is None:
            dst_host_rule_match_backend_service = dst_default_backend_service
        return self._insert_resource(
            self.api.urlMaps(), {
                'name':
                    name,
                'defaultService':
                    dst_default_backend_service.url,
                'hostRules': [{
                    'hosts': src_hosts,
                    'pathMatcher': matcher_name,
                }],
                'pathMatchers': [{
                    'name': matcher_name,
                    'defaultService': dst_host_rule_match_backend_service.url,
                }],
            })

    def delete_url_map(self, name):
        self._delete_resource(self.api.urlMaps(), 'urlMap', name)

    def create_target_grpc_proxy(
        self,
        name: str,
        url_map: GcpResource,
    ) -> GcpResource:
        return self._insert_resource(self.api.targetGrpcProxies(), {
            'name': name,
            'url_map': url_map.url,
            'validate_for_proxyless': True,
        })

    def delete_target_grpc_proxy(self, name):
        self._delete_resource(self.api.targetGrpcProxies(), 'targetGrpcProxy',
                              name)

    def create_target_http_proxy(
        self,
        name: str,
        url_map: GcpResource,
    ) -> GcpResource:
        return self._insert_resource(self.api.targetHttpProxies(), {
            'name': name,
            'url_map': url_map.url,
        })

    def delete_target_http_proxy(self, name):
        self._delete_resource(self.api.targetHttpProxies(), 'targetHttpProxy',
                              name)

    def create_forwarding_rule(
        self,
        name: str,
        src_port: int,
        target_proxy: GcpResource,
        network_url: str,
    ) -> GcpResource:
        return self._insert_resource(
            self.api.globalForwardingRules(),
            {
                'name': name,
                'loadBalancingScheme':
                    'INTERNAL_SELF_MANAGED',  # Traffic Director
                'portRange': src_port,
                'IPAddress': '0.0.0.0',
                'network': network_url,
                'target': target_proxy.url,
            })

    def delete_forwarding_rule(self, name):
        self._delete_resource(self.api.globalForwardingRules(),
                              'forwardingRule', name)

    @staticmethod
    def _network_endpoint_group_not_ready(neg):
        return not neg or neg.get('size', 0) == 0

    def wait_for_network_endpoint_group(self, name, zone):

        @retrying.retry(retry_on_result=self._network_endpoint_group_not_ready,
                        stop_max_delay=60 * 1000,
                        wait_fixed=2 * 1000)
        def _wait_for_network_endpoint_group_ready():
            try:
                neg = self.get_network_endpoint_group(name, zone)
                logger.debug(
                    'Waiting for endpoints: NEG %s in zone %s, '
                    'current count %s', neg['name'], zone, neg.get('size'))
            except googleapiclient.errors.HttpError as error:
                # noinspection PyProtectedMember
                reason = error._get_reason()
                logger.debug('Retrying NEG load, got %s, details %s',
                             error.resp.status, reason)
                raise
            return neg

        network_endpoint_group = _wait_for_network_endpoint_group_ready()
        # TODO(sergiitk): dataclass
        return self.ZonalGcpResource(network_endpoint_group['name'],
                                     network_endpoint_group['selfLink'], zone)

    def get_network_endpoint_group(self, name, zone):
        neg = self.api.networkEndpointGroups().get(project=self.project,
                                                   networkEndpointGroup=name,
                                                   zone=zone).execute()
        # TODO(sergiitk): dataclass
        return neg

    def wait_for_backends_healthy_status(
        self,
        backend_service,
        backends,
        timeout_sec=_WAIT_FOR_BACKEND_SEC,
        wait_sec=4,
    ):
        pending = set(backends)

        @retrying.retry(retry_on_result=lambda result: not result,
                        stop_max_delay=timeout_sec * 1000,
                        wait_fixed=wait_sec * 1000)
        def _retry_backends_health():
            for backend in pending:
                result = self.get_backend_service_backend_health(
                    backend_service, backend)

                if 'healthStatus' not in result:
                    logger.debug('Waiting for instances: backend %s, zone %s',
                                 backend.name, backend.zone)
                    continue

                backend_healthy = True
                for instance in result['healthStatus']:
                    logger.debug(
                        'Backend %s in zone %s: instance %s:%s health: %s',
                        backend.name, backend.zone, instance['ipAddress'],
                        instance['port'], instance['healthState'])
                    if instance['healthState'] != 'HEALTHY':
                        backend_healthy = False

                if backend_healthy:
                    logger.info('Backend %s in zone %s reported healthy',
                                backend.name, backend.zone)
                    pending.remove(backend)

            return not pending

        _retry_backends_health()

    def get_backend_service_backend_health(self, backend_service, backend):
        return self.api.backendServices().getHealth(
            project=self.project,
            backendService=backend_service.name,
            body={
                "group": backend.url
            }).execute()

    def _get_resource(self, collection: discovery.Resource,
                      **kwargs) -> GcpResource:
        resp = collection.get(project=self.project, **kwargs).execute()
        logger.info('Loaded compute resource:\n%s',
                    self._resource_pretty_format(resp))
        return self.GcpResource(resp['name'], resp['selfLink'])

    def _insert_resource(self, collection: discovery.Resource,
                         body: Dict[str, Any]) -> GcpResource:
        logger.info('Creating compute resource:\n%s',
                    self._resource_pretty_format(body))
        resp = self._execute(collection.insert(project=self.project, body=body))
        return self.GcpResource(body['name'], resp['targetLink'])

    def _patch_resource(self, collection, body, **kwargs):
        logger.info('Patching compute resource:\n%s',
                    self._resource_pretty_format(body))
        self._execute(
            collection.patch(project=self.project, body=body, **kwargs))

    def _delete_resource(self, collection: discovery.Resource,
                         resource_type: str, resource_name: str) -> bool:
        try:
            params = {"project": self.project, resource_type: resource_name}
            self._execute(collection.delete(**params))
            return True
        except googleapiclient.errors.HttpError as error:
            if error.resp and error.resp.status == 404:
                logger.info(
                    'Resource %s "%s" not deleted since it does not exist',
                    resource_type, resource_name)
            else:
                logger.warning('Failed to delete %s "%s", %r', resource_type,
                               resource_name, error)
        return False

    @staticmethod
    def _operation_status_done(operation):
        return 'status' in operation and operation['status'] == 'DONE'

    def _execute(self,
                 request,
                 *,
                 test_success_fn=None,
                 timeout_sec=_WAIT_FOR_OPERATION_SEC):
        operation = request.execute(num_retries=self._GCP_API_RETRIES)
        logger.debug('Response %s', operation)

        # TODO(sergiitk) try using wait() here
        # https://googleapis.github.io/google-api-python-client/docs/dyn/compute_v1.globalOperations.html#wait
        operation_request = self.api.globalOperations().get(
            project=self.project, operation=operation['name'])

        if test_success_fn is None:
            test_success_fn = self._operation_status_done

        logger.debug('Waiting for global operation %s, timeout %s sec',
                     operation['name'], timeout_sec)
        response = self.wait_for_operation(operation_request=operation_request,
                                           test_success_fn=test_success_fn,
                                           timeout_sec=timeout_sec)

        if 'error' in response:
            logger.debug('Waiting for global operation failed, response: %r',
                         response)
            raise Exception(f'Operation {operation["name"]} did not complete '
                            f'within {timeout_sec}s, error={response["error"]}')
        return response
