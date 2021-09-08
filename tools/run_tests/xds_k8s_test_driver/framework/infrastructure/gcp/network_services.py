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

import abc
import dataclasses
import logging
from typing import Any, Dict, List, Optional, Tuple

from google.rpc import code_pb2
import tenacity

from framework.infrastructure import gcp

logger = logging.getLogger(__name__)

# Type aliases
GcpResource = gcp.compute.ComputeV1.GcpResource


@dataclasses.dataclass(frozen=True)
class EndpointPolicy:
    url: str
    name: str
    type: str
    traffic_port_selector: dict
    endpoint_matcher: dict
    update_time: str
    create_time: str
    http_filters: Optional[dict] = None
    server_tls_policy: Optional[str] = None

    @classmethod
    def from_response(cls, name: str, response: Dict[str,
                                                     Any]) -> 'EndpointPolicy':
        return cls(name=name,
                   url=response['name'],
                   type=response['type'],
                   server_tls_policy=response.get('serverTlsPolicy', None),
                   traffic_port_selector=response['trafficPortSelector'],
                   endpoint_matcher=response['endpointMatcher'],
                   http_filters=response.get('httpFilters', None),
                   update_time=response['updateTime'],
                   create_time=response['createTime'])


@dataclasses.dataclass(frozen=True)
class Router:

    name: str
    url: str
    type: str
    network: Optional[str]
    routes: Optional[List[str]]

    @classmethod
    def from_response(cls, name: str, d: Dict[str, Any]) -> 'Router':
        return cls(
            name=name,
            url=d["name"],
            type=d["type"],
            network=d.get("network"),
            routes=list(d["routes"]) if "routes" in d else None,
        )


@dataclasses.dataclass(frozen=True)
class GrpcRoute:

    @dataclasses.dataclass(frozen=True)
    class MethodMatch:
        type: Optional[str]
        grpc_service: Optional[str]
        grpc_method: Optional[str]
        case_sensitive: Optional[bool]

        @classmethod
        def from_response(cls, d: Dict[str, Any]) -> 'MethodMatch':
            return cls(
                type=d.get("type"),
                grpc_service=d.get("grpcService"),
                grpc_method=d.get("grpcMethod"),
                case_sensitive=d.get("caseSensitive"),
            )

    @dataclasses.dataclass(frozen=True)
    class HeaderMatch:
        type: Optional[str]
        key: str
        value: str

        @classmethod
        def from_response(cls, d: Dict[str, Any]) -> 'HeaderMatch':
            return cls(
                type=d.get("type"),
                key=d["key"],
                value=d["value"],
            )

    @dataclasses.dataclass(frozen=True)
    class RouteMatch:
        method: Optional['MethodMatch']
        headers: Tuple['HeaderMatch']

        @classmethod
        def from_response(cls, d: Dict[str, Any]) -> 'RouteMatch':
            return cls(
                method=MethodMatch.from_response(d["method"])
                if "method" in d else None,
                headers=tuple(
                    HeaderMatch.from_response(h) for h in d["headers"])
                if "headers" in d else (),
            )

    @dataclasses.dataclass(frozen=True)
    class Destination:
        service_name: str
        weight: Optional[int]

        @classmethod
        def from_response(cls, d: Dict[str, Any]) -> 'Destination':
            return cls(
                service_name=d["serviceName"],
                weight=d.get("weight"),
            )

    @dataclasses.dataclass(frozen=True)
    class RouteAction:
        destination: Optional['Destination']
        drop: Optional[int]

        @classmethod
        def from_response(cls, d: Dict[str, Any]) -> 'RouteAction':
            return cls(
                destination=Destination.from_response(d["destination"])
                if "destination" in d else None,
                drop=d.get("drop"),
            )

    @dataclasses.dataclass(frozen=True)
    class RouteRule:
        match: Optional['RouteMatch']
        action: 'RouteAction'

        @classmethod
        def from_response(cls, d: Dict[str, Any]) -> 'RouteRule':
            return cls(
                match=RouteMatch.from_response(d["match"])
                if "match" in d else "",
                action=RouteAction.from_response(d["action"]),
            )

    name: str
    url: str
    hostnames: Tuple[str]
    rules: Tuple['RouteRule']

    @classmethod
    def from_response(cls, name: str, d: Dict[str, Any]) -> 'RouteRule':
        return cls(
            name=name,
            url=d["name"],
            hostnames=tuple(d["hostnames"]),
            rules=tuple(d["rules"]),
        )


class _NetworkServicesBase(gcp.api.GcpStandardCloudApiResource,
                           metaclass=abc.ABCMeta):
    """Base class for NetworkServices APIs."""

    def __init__(self, api_manager: gcp.api.GcpApiManager, project: str):
        super().__init__(api_manager.networkservices(self.api_version), project)
        # Shortcut to projects/*/locations/ endpoints
        self._api_locations = self.api.projects().locations()

    @property
    def api_name(self) -> str:
        return 'networkservices'

    def _execute(self, *args, **kwargs):  # pylint: disable=signature-differs
        # Workaround TD bug: throttled operations are reported as internal.
        # Ref b/175345578
        retryer = tenacity.Retrying(
            retry=tenacity.retry_if_exception(self._operation_internal_error),
            wait=tenacity.wait_fixed(10),
            stop=tenacity.stop_after_delay(5 * 60),
            before_sleep=tenacity.before_sleep_log(logger, logging.DEBUG),
            reraise=True)
        retryer(super()._execute, *args, **kwargs)

    @staticmethod
    def _operation_internal_error(exception):
        return (isinstance(exception, gcp.api.OperationError) and
                exception.error.code == code_pb2.INTERNAL)


class NetworkServicesV1Beta1(_NetworkServicesBase):
    """NetworkServices API v1beta1."""
    ENDPOINT_POLICIES = 'endpointPolicies'

    @property
    def api_version(self) -> str:
        return 'v1beta1'

    def create_endpoint_policy(self, name, body: dict) -> GcpResource:
        return self._create_resource(
            collection=self._api_locations.endpointPolicies(),
            body=body,
            endpointPolicyId=name)

    def get_endpoint_policy(self, name: str) -> EndpointPolicy:
        response = self._get_resource(
            collection=self._api_locations.endpointPolicies(),
            full_name=self.resource_full_name(name, self.ENDPOINT_POLICIES))
        return EndpointPolicy.from_response(name, response)

    def delete_endpoint_policy(self, name: str) -> bool:
        return self._delete_resource(
            collection=self._api_locations.endpointPolicies(),
            full_name=self.resource_full_name(name, self.ENDPOINT_POLICIES))


class NetworkServicesV1Alpha1(NetworkServicesV1Beta1):
    """NetworkServices API v1alpha1.

    Note: extending v1beta1 class presumes that v1beta1 is just a v1alpha1 API
    graduated into a more stable version. This is true in most cases. However,
    v1alpha1 class can always override and reimplement incompatible methods.
    """

    GRPC_ROUTES = 'grpcRoutes'
    ROUTERS = 'routers'

    @property
    def api_version(self) -> str:
        return 'v1alpha1'

    def create_router(self, name: str, body: dict) -> GcpResource:
        return self._create_resource(collection=self._api_locations.routers(),
                                     body=body,
                                     routerId=name)

    def get_router(self, name: str) -> Router:
        full_name = self.resource_full_name(name, self.ROUTERS)
        result = self._get_resource(collection=self._api_locations.routers(),
                                    full_name=full_name)
        return Router.from_response(name, result)

    def delete_router(self, name: str) -> bool:
        return self._delete_resource(collection=self._api_locations.routers(),
                                     full_name=self.resource_full_name(
                                         name, self.ROUTERS))

    def create_grpc_route(self, name: str, body: dict) -> GcpResource:
        return self._create_resource(
            collection=self._api_locations.grpcRoutes(),
            body=body,
            grpcRouteId=name)

    def get_grpc_route(self, name: str) -> GrpcRoute:
        full_name = self.resource_full_name(name, self.GRPC_ROUTES)
        result = self._get_resource(collection=self._api_locations.grpcRoutes(),
                                    full_name=full_name)
        return GrpcRoute.from_response(name, result)

    def delete_grpc_route(self, name: str) -> bool:
        return self._delete_resource(
            collection=self._api_locations.grpcRoutes(),
            full_name=self.resource_full_name(name, self.GRPC_ROUTES))
