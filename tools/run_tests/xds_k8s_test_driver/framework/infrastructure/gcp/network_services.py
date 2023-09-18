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
    def from_response(
        cls, name: str, response: Dict[str, Any]
    ) -> "EndpointPolicy":
        return cls(
            name=name,
            url=response["name"],
            type=response["type"],
            server_tls_policy=response.get("serverTlsPolicy", None),
            traffic_port_selector=response["trafficPortSelector"],
            endpoint_matcher=response["endpointMatcher"],
            http_filters=response.get("httpFilters", None),
            update_time=response["updateTime"],
            create_time=response["createTime"],
        )


@dataclasses.dataclass(frozen=True)
class Mesh:
    name: str
    url: str
    routes: Optional[List[str]]

    @classmethod
    def from_response(cls, name: str, d: Dict[str, Any]) -> "Mesh":
        return cls(
            name=name,
            url=d["name"],
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
        def from_response(cls, d: Dict[str, Any]) -> "GrpcRoute.MethodMatch":
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
        def from_response(cls, d: Dict[str, Any]) -> "GrpcRoute.HeaderMatch":
            return cls(
                type=d.get("type"),
                key=d["key"],
                value=d["value"],
            )

    @dataclasses.dataclass(frozen=True)
    class RouteMatch:
        method: Optional["GrpcRoute.MethodMatch"]
        headers: Tuple["GrpcRoute.HeaderMatch"]

        @classmethod
        def from_response(cls, d: Dict[str, Any]) -> "GrpcRoute.RouteMatch":
            return cls(
                method=GrpcRoute.MethodMatch.from_response(d["method"])
                if "method" in d
                else None,
                headers=tuple(
                    GrpcRoute.HeaderMatch.from_response(h) for h in d["headers"]
                )
                if "headers" in d
                else (),
            )

    @dataclasses.dataclass(frozen=True)
    class Destination:
        service_name: str
        weight: Optional[int]

        @classmethod
        def from_response(cls, d: Dict[str, Any]) -> "GrpcRoute.Destination":
            return cls(
                service_name=d["serviceName"],
                weight=d.get("weight"),
            )

    @dataclasses.dataclass(frozen=True)
    class RouteAction:
        @classmethod
        def from_response(cls, d: Dict[str, Any]) -> "GrpcRoute.RouteAction":
            destinations = (
                [
                    GrpcRoute.Destination.from_response(dest)
                    for dest in d["destinations"]
                ]
                if "destinations" in d
                else []
            )
            return cls(destinations=destinations)

    @dataclasses.dataclass(frozen=True)
    class RouteRule:
        matches: List["GrpcRoute.RouteMatch"]
        action: "GrpcRoute.RouteAction"

        @classmethod
        def from_response(cls, d: Dict[str, Any]) -> "GrpcRoute.RouteRule":
            matches = (
                [GrpcRoute.RouteMatch.from_response(m) for m in d["matches"]]
                if "matches" in d
                else []
            )
            return cls(
                matches=matches,
                action=GrpcRoute.RouteAction.from_response(d["action"]),
            )

    name: str
    url: str
    hostnames: Tuple[str]
    rules: Tuple["GrpcRoute.RouteRule"]
    meshes: Optional[Tuple[str]]

    @classmethod
    def from_response(
        cls, name: str, d: Dict[str, Any]
    ) -> "GrpcRoute.RouteRule":
        return cls(
            name=name,
            url=d["name"],
            hostnames=tuple(d["hostnames"]),
            rules=tuple(d["rules"]),
            meshes=None if d.get("meshes") is None else tuple(d["meshes"]),
        )


@dataclasses.dataclass(frozen=True)
class HttpRoute:
    @dataclasses.dataclass(frozen=True)
    class MethodMatch:
        type: Optional[str]
        http_service: Optional[str]
        http_method: Optional[str]
        case_sensitive: Optional[bool]

        @classmethod
        def from_response(cls, d: Dict[str, Any]) -> "HttpRoute.MethodMatch":
            return cls(
                type=d.get("type"),
                http_service=d.get("httpService"),
                http_method=d.get("httpMethod"),
                case_sensitive=d.get("caseSensitive"),
            )

    @dataclasses.dataclass(frozen=True)
    class HeaderMatch:
        type: Optional[str]
        key: str
        value: str

        @classmethod
        def from_response(cls, d: Dict[str, Any]) -> "HttpRoute.HeaderMatch":
            return cls(
                type=d.get("type"),
                key=d["key"],
                value=d["value"],
            )

    @dataclasses.dataclass(frozen=True)
    class RouteMatch:
        method: Optional["HttpRoute.MethodMatch"]
        headers: Tuple["HttpRoute.HeaderMatch"]

        @classmethod
        def from_response(cls, d: Dict[str, Any]) -> "HttpRoute.RouteMatch":
            return cls(
                method=HttpRoute.MethodMatch.from_response(d["method"])
                if "method" in d
                else None,
                headers=tuple(
                    HttpRoute.HeaderMatch.from_response(h) for h in d["headers"]
                )
                if "headers" in d
                else (),
            )

    @dataclasses.dataclass(frozen=True)
    class Destination:
        service_name: str
        weight: Optional[int]

        @classmethod
        def from_response(cls, d: Dict[str, Any]) -> "HttpRoute.Destination":
            return cls(
                service_name=d["serviceName"],
                weight=d.get("weight"),
            )

    @dataclasses.dataclass(frozen=True)
    class RouteAction:
        destinations: List["HttpRoute.Destination"]
        stateful_session_affinity: Optional["HttpRoute.StatefulSessionAffinity"]

        @classmethod
        def from_response(cls, d: Dict[str, Any]) -> "HttpRoute.RouteAction":
            destinations = (
                [
                    HttpRoute.Destination.from_response(dest)
                    for dest in d["destinations"]
                ]
                if "destinations" in d
                else []
            )
            stateful_session_affinity = (
                HttpRoute.StatefulSessionAffinity.from_response(
                    d["statefulSessionAffinity"]
                )
                if "statefulSessionAffinity" in d
                else None
            )
            return cls(
                destinations=destinations,
                stateful_session_affinity=stateful_session_affinity,
            )

    @dataclasses.dataclass(frozen=True)
    class StatefulSessionAffinity:
        cookie_ttl: Optional[str]

        @classmethod
        def from_response(
            cls, d: Dict[str, Any]
        ) -> "HttpRoute.StatefulSessionAffinity":
            return cls(cookie_ttl=d.get("cookieTtl"))

    @dataclasses.dataclass(frozen=True)
    class RouteRule:
        matches: List["HttpRoute.RouteMatch"]
        action: "HttpRoute.RouteAction"

        @classmethod
        def from_response(cls, d: Dict[str, Any]) -> "HttpRoute.RouteRule":
            matches = (
                [HttpRoute.RouteMatch.from_response(m) for m in d["matches"]]
                if "matches" in d
                else []
            )
            return cls(
                matches=matches,
                action=HttpRoute.RouteAction.from_response(d["action"]),
            )

    name: str
    url: str
    hostnames: Tuple[str]
    rules: Tuple["HttpRoute.RouteRule"]
    meshes: Optional[Tuple[str]]

    @classmethod
    def from_response(cls, name: str, d: Dict[str, Any]) -> "HttpRoute":
        return cls(
            name=name,
            url=d["name"],
            hostnames=tuple(d["hostnames"]),
            rules=tuple(d["rules"]),
            meshes=None if d.get("meshes") is None else tuple(d["meshes"]),
        )


class _NetworkServicesBase(
    gcp.api.GcpStandardCloudApiResource, metaclass=abc.ABCMeta
):
    """Base class for NetworkServices APIs."""

    # TODO(https://github.com/grpc/grpc/issues/29532) remove pylint disable
    # pylint: disable=abstract-method

    def __init__(self, api_manager: gcp.api.GcpApiManager, project: str):
        super().__init__(api_manager.networkservices(self.api_version), project)
        # Shortcut to projects/*/locations/ endpoints
        self._api_locations = self.api.projects().locations()

    @property
    def api_name(self) -> str:
        return "networkservices"

    def _execute(
        self, *args, **kwargs
    ):  # pylint: disable=signature-differs,arguments-differ
        # Workaround TD bug: throttled operations are reported as internal.
        # Ref b/175345578
        retryer = tenacity.Retrying(
            retry=tenacity.retry_if_exception(self._operation_internal_error),
            wait=tenacity.wait_fixed(10),
            stop=tenacity.stop_after_delay(5 * 60),
            before_sleep=tenacity.before_sleep_log(logger, logging.DEBUG),
            reraise=True,
        )
        retryer(super()._execute, *args, **kwargs)

    @staticmethod
    def _operation_internal_error(exception):
        return (
            isinstance(exception, gcp.api.OperationError)
            and exception.error.code == code_pb2.INTERNAL
        )


class NetworkServicesV1Beta1(_NetworkServicesBase):
    """NetworkServices API v1beta1."""

    ENDPOINT_POLICIES = "endpointPolicies"

    @property
    def api_version(self) -> str:
        return "v1beta1"

    def create_endpoint_policy(self, name, body: dict) -> GcpResource:
        return self._create_resource(
            collection=self._api_locations.endpointPolicies(),
            body=body,
            endpointPolicyId=name,
        )

    def get_endpoint_policy(self, name: str) -> EndpointPolicy:
        response = self._get_resource(
            collection=self._api_locations.endpointPolicies(),
            full_name=self.resource_full_name(name, self.ENDPOINT_POLICIES),
        )
        return EndpointPolicy.from_response(name, response)

    def delete_endpoint_policy(self, name: str) -> bool:
        return self._delete_resource(
            collection=self._api_locations.endpointPolicies(),
            full_name=self.resource_full_name(name, self.ENDPOINT_POLICIES),
        )


class NetworkServicesV1Alpha1(NetworkServicesV1Beta1):
    """NetworkServices API v1alpha1.

    Note: extending v1beta1 class presumes that v1beta1 is just a v1alpha1 API
    graduated into a more stable version. This is true in most cases. However,
    v1alpha1 class can always override and reimplement incompatible methods.
    """

    HTTP_ROUTES = "httpRoutes"
    GRPC_ROUTES = "grpcRoutes"
    MESHES = "meshes"

    @property
    def api_version(self) -> str:
        return "v1alpha1"

    def create_mesh(self, name: str, body: dict) -> GcpResource:
        return self._create_resource(
            collection=self._api_locations.meshes(), body=body, meshId=name
        )

    def get_mesh(self, name: str) -> Mesh:
        full_name = self.resource_full_name(name, self.MESHES)
        result = self._get_resource(
            collection=self._api_locations.meshes(), full_name=full_name
        )
        return Mesh.from_response(name, result)

    def delete_mesh(self, name: str) -> bool:
        return self._delete_resource(
            collection=self._api_locations.meshes(),
            full_name=self.resource_full_name(name, self.MESHES),
        )

    def create_grpc_route(self, name: str, body: dict) -> GcpResource:
        return self._create_resource(
            collection=self._api_locations.grpcRoutes(),
            body=body,
            grpcRouteId=name,
        )

    def create_http_route(self, name: str, body: dict) -> GcpResource:
        return self._create_resource(
            collection=self._api_locations.httpRoutes(),
            body=body,
            httpRouteId=name,
        )

    def get_grpc_route(self, name: str) -> GrpcRoute:
        full_name = self.resource_full_name(name, self.GRPC_ROUTES)
        result = self._get_resource(
            collection=self._api_locations.grpcRoutes(), full_name=full_name
        )
        return GrpcRoute.from_response(name, result)

    def get_http_route(self, name: str) -> GrpcRoute:
        full_name = self.resource_full_name(name, self.HTTP_ROUTES)
        result = self._get_resource(
            collection=self._api_locations.httpRoutes(), full_name=full_name
        )
        return HttpRoute.from_response(name, result)

    def delete_grpc_route(self, name: str) -> bool:
        return self._delete_resource(
            collection=self._api_locations.grpcRoutes(),
            full_name=self.resource_full_name(name, self.GRPC_ROUTES),
        )

    def delete_http_route(self, name: str) -> bool:
        return self._delete_resource(
            collection=self._api_locations.httpRoutes(),
            full_name=self.resource_full_name(name, self.HTTP_ROUTES),
        )
