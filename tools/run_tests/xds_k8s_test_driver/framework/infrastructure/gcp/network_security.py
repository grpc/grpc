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
from typing import Any, Dict

from google.rpc import code_pb2
import tenacity

from framework.infrastructure import gcp

logger = logging.getLogger(__name__)

# Type aliases
GcpResource = gcp.compute.ComputeV1.GcpResource


@dataclasses.dataclass(frozen=True)
class ServerTlsPolicy:
    url: str
    name: str
    server_certificate: dict
    mtls_policy: dict
    update_time: str
    create_time: str

    @classmethod
    def from_response(
        cls, name: str, response: Dict[str, Any]
    ) -> "ServerTlsPolicy":
        return cls(
            name=name,
            url=response["name"],
            server_certificate=response.get("serverCertificate", {}),
            mtls_policy=response.get("mtlsPolicy", {}),
            create_time=response["createTime"],
            update_time=response["updateTime"],
        )


@dataclasses.dataclass(frozen=True)
class ClientTlsPolicy:
    url: str
    name: str
    client_certificate: dict
    server_validation_ca: list
    update_time: str
    create_time: str

    @classmethod
    def from_response(
        cls, name: str, response: Dict[str, Any]
    ) -> "ClientTlsPolicy":
        return cls(
            name=name,
            url=response["name"],
            client_certificate=response.get("clientCertificate", {}),
            server_validation_ca=response.get("serverValidationCa", []),
            create_time=response["createTime"],
            update_time=response["updateTime"],
        )


@dataclasses.dataclass(frozen=True)
class AuthorizationPolicy:
    url: str
    name: str
    update_time: str
    create_time: str
    action: str
    rules: list

    @classmethod
    def from_response(
        cls, name: str, response: Dict[str, Any]
    ) -> "AuthorizationPolicy":
        return cls(
            name=name,
            url=response["name"],
            create_time=response["createTime"],
            update_time=response["updateTime"],
            action=response["action"],
            rules=response.get("rules", []),
        )


class _NetworkSecurityBase(
    gcp.api.GcpStandardCloudApiResource, metaclass=abc.ABCMeta
):
    """Base class for NetworkSecurity APIs."""

    # TODO(https://github.com/grpc/grpc/issues/29532) remove pylint disable
    # pylint: disable=abstract-method

    def __init__(self, api_manager: gcp.api.GcpApiManager, project: str):
        super().__init__(api_manager.networksecurity(self.api_version), project)
        # Shortcut to projects/*/locations/ endpoints
        self._api_locations = self.api.projects().locations()

    @property
    def api_name(self) -> str:
        return "networksecurity"

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


class NetworkSecurityV1Beta1(_NetworkSecurityBase):
    """NetworkSecurity API v1beta1."""

    SERVER_TLS_POLICIES = "serverTlsPolicies"
    CLIENT_TLS_POLICIES = "clientTlsPolicies"
    AUTHZ_POLICIES = "authorizationPolicies"

    @property
    def api_version(self) -> str:
        return "v1beta1"

    def create_server_tls_policy(self, name: str, body: dict) -> GcpResource:
        return self._create_resource(
            collection=self._api_locations.serverTlsPolicies(),
            body=body,
            serverTlsPolicyId=name,
        )

    def get_server_tls_policy(self, name: str) -> ServerTlsPolicy:
        response = self._get_resource(
            collection=self._api_locations.serverTlsPolicies(),
            full_name=self.resource_full_name(name, self.SERVER_TLS_POLICIES),
        )
        return ServerTlsPolicy.from_response(name, response)

    def delete_server_tls_policy(self, name: str) -> bool:
        return self._delete_resource(
            collection=self._api_locations.serverTlsPolicies(),
            full_name=self.resource_full_name(name, self.SERVER_TLS_POLICIES),
        )

    def create_client_tls_policy(self, name: str, body: dict) -> GcpResource:
        return self._create_resource(
            collection=self._api_locations.clientTlsPolicies(),
            body=body,
            clientTlsPolicyId=name,
        )

    def get_client_tls_policy(self, name: str) -> ClientTlsPolicy:
        response = self._get_resource(
            collection=self._api_locations.clientTlsPolicies(),
            full_name=self.resource_full_name(name, self.CLIENT_TLS_POLICIES),
        )
        return ClientTlsPolicy.from_response(name, response)

    def delete_client_tls_policy(self, name: str) -> bool:
        return self._delete_resource(
            collection=self._api_locations.clientTlsPolicies(),
            full_name=self.resource_full_name(name, self.CLIENT_TLS_POLICIES),
        )

    def create_authz_policy(self, name: str, body: dict) -> GcpResource:
        return self._create_resource(
            collection=self._api_locations.authorizationPolicies(),
            body=body,
            authorizationPolicyId=name,
        )

    def get_authz_policy(self, name: str) -> ClientTlsPolicy:
        response = self._get_resource(
            collection=self._api_locations.authorizationPolicies(),
            full_name=self.resource_full_name(name, self.AUTHZ_POLICIES),
        )
        return ClientTlsPolicy.from_response(name, response)

    def delete_authz_policy(self, name: str) -> bool:
        return self._delete_resource(
            collection=self._api_locations.authorizationPolicies(),
            full_name=self.resource_full_name(name, self.AUTHZ_POLICIES),
        )


class NetworkSecurityV1Alpha1(NetworkSecurityV1Beta1):
    """NetworkSecurity API v1alpha1.

    Note: extending v1beta1 class presumes that v1beta1 is just a v1alpha1 API
    graduated into a more stable version. This is true in most cases. However,
    v1alpha1 class can always override and reimplement incompatible methods.
    """

    @property
    def api_version(self) -> str:
        return "v1alpha1"
