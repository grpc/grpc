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
from typing import Optional

import dataclasses
from google.rpc import code_pb2
import tenacity

from framework.infrastructure import gcp

logger = logging.getLogger(__name__)


class NetworkServicesV1Alpha1(gcp.api.GcpStandardCloudApiResource):
    ENDPOINT_CONFIG_SELECTORS = 'endpointConfigSelectors'

    @dataclasses.dataclass(frozen=True)
    class EndpointConfigSelector:
        url: str
        name: str
        type: str
        server_tls_policy: Optional[str]
        traffic_port_selector: dict
        endpoint_matcher: dict
        http_filters: dict
        update_time: str
        create_time: str

    def __init__(self, api_manager: gcp.api.GcpApiManager, project: str):
        super().__init__(api_manager.networkservices(self.api_version), project)
        # Shortcut to projects/*/locations/ endpoints
        self._api_locations = self.api.projects().locations()

    @property
    def api_name(self) -> str:
        return 'networkservices'

    @property
    def api_version(self) -> str:
        return 'v1alpha1'

    def create_endpoint_config_selector(self, name, body: dict):
        return self._create_resource(
            self._api_locations.endpointConfigSelectors(),
            body,
            endpointConfigSelectorId=name)

    def get_endpoint_config_selector(self, name: str) -> EndpointConfigSelector:
        result = self._get_resource(
            collection=self._api_locations.endpointConfigSelectors(),
            full_name=self.resource_full_name(name,
                                              self.ENDPOINT_CONFIG_SELECTORS))
        return self.EndpointConfigSelector(
            name=name,
            url=result['name'],
            type=result['type'],
            server_tls_policy=result.get('serverTlsPolicy', None),
            traffic_port_selector=result['trafficPortSelector'],
            endpoint_matcher=result['endpointMatcher'],
            http_filters=result['httpFilters'],
            update_time=result['updateTime'],
            create_time=result['createTime'])

    def delete_endpoint_config_selector(self, name):
        return self._delete_resource(
            collection=self._api_locations.endpointConfigSelectors(),
            full_name=self.resource_full_name(name,
                                              self.ENDPOINT_CONFIG_SELECTORS))

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
