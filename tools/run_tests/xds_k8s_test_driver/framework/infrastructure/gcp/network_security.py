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
import logging

import dataclasses
from google.rpc import code_pb2
import tenacity

from framework.infrastructure import gcp

logger = logging.getLogger(__name__)


class NetworkSecurityV1Alpha1(gcp.api.GcpStandardCloudApiResource):
    SERVER_TLS_POLICIES = 'serverTlsPolicies'
    CLIENT_TLS_POLICIES = 'clientTlsPolicies'

    @dataclasses.dataclass(frozen=True)
    class ServerTlsPolicy:
        url: str
        name: str
        server_certificate: dict
        mtls_policy: dict
        update_time: str
        create_time: str

    @dataclasses.dataclass(frozen=True)
    class ClientTlsPolicy:
        url: str
        name: str
        client_certificate: dict
        server_validation_ca: list
        update_time: str
        create_time: str

    def __init__(self, api_manager: gcp.api.GcpApiManager, project: str):
        super().__init__(api_manager.networksecurity(self.api_version), project)
        # Shortcut to projects/*/locations/ endpoints
        self._api_locations = self.api.projects().locations()

    @property
    def api_name(self) -> str:
        return 'networksecurity'

    @property
    def api_version(self) -> str:
        return 'v1alpha1'

    def create_server_tls_policy(self, name, body: dict):
        return self._create_resource(self._api_locations.serverTlsPolicies(),
                                     body,
                                     serverTlsPolicyId=name)

    def get_server_tls_policy(self, name: str) -> ServerTlsPolicy:
        result = self._get_resource(
            collection=self._api_locations.serverTlsPolicies(),
            full_name=self.resource_full_name(name, self.SERVER_TLS_POLICIES))

        return self.ServerTlsPolicy(name=name,
                                    url=result['name'],
                                    server_certificate=result.get(
                                        'serverCertificate', {}),
                                    mtls_policy=result.get('mtlsPolicy', {}),
                                    create_time=result['createTime'],
                                    update_time=result['updateTime'])

    def delete_server_tls_policy(self, name):
        return self._delete_resource(
            collection=self._api_locations.serverTlsPolicies(),
            full_name=self.resource_full_name(name, self.SERVER_TLS_POLICIES))

    def create_client_tls_policy(self, name, body: dict):
        return self._create_resource(self._api_locations.clientTlsPolicies(),
                                     body,
                                     clientTlsPolicyId=name)

    def get_client_tls_policy(self, name: str) -> ClientTlsPolicy:
        result = self._get_resource(
            collection=self._api_locations.clientTlsPolicies(),
            full_name=self.resource_full_name(name, self.CLIENT_TLS_POLICIES))

        return self.ClientTlsPolicy(
            name=name,
            url=result['name'],
            client_certificate=result.get('clientCertificate', {}),
            server_validation_ca=result.get('serverValidationCa', []),
            create_time=result['createTime'],
            update_time=result['updateTime'])

    def delete_client_tls_policy(self, name):
        return self._delete_resource(
            collection=self._api_locations.clientTlsPolicies(),
            full_name=self.resource_full_name(name, self.CLIENT_TLS_POLICIES))

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
