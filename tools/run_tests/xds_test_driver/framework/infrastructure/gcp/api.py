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
import functools
import logging
import os

# For some reason without `import grpc`, google.protobuf.json_format produces
# "Segmentation fault"
import grpc
from absl import flags
from google.longrunning import operations_pb2
from google.protobuf import json_format
from google.rpc import code_pb2
from googleapiclient import discovery
import googleapiclient.errors
import tenacity

logger = logging.getLogger(__name__)
V1_DISCOVERY_URI = flags.DEFINE_string(
    "v1_discovery_uri", default=discovery.V1_DISCOVERY_URI,
    help="Override v1 Discovery URI")
V2_DISCOVERY_URI = flags.DEFINE_string(
    "v2_discovery_uri", default=discovery.V2_DISCOVERY_URI,
    help="Override v2 Discovery URI")
COMPUTE_V1_DISCOVERY_FILE = flags.DEFINE_string(
    "compute_v1_discovery_file", default=None,
    help="Load compute v1 from discovery file")

# Type aliases
Operation = operations_pb2.Operation


class GcpApiManager:
    def __init__(self, *,
                 v1_discovery_uri=None,
                 v2_discovery_uri=None,
                 compute_v1_discovery_file=None,
                 private_api_key=None):
        self.v1_discovery_uri = v1_discovery_uri or V1_DISCOVERY_URI.value
        self.v2_discovery_uri = v2_discovery_uri or V2_DISCOVERY_URI.value
        self.compute_v1_discovery_file = (compute_v1_discovery_file or
                                          COMPUTE_V1_DISCOVERY_FILE.value)
        self.private_api_key = private_api_key or os.getenv('PRIVATE_API_KEY')
        self._exit_stack = contextlib.ExitStack()

    def close(self):
        self._exit_stack.close()

    @functools.lru_cache(None)
    def compute(self, version):
        api_name = 'compute'
        if version == 'v1':
            if self.compute_v1_discovery_file:
                return self._build_from_file(self.compute_v1_discovery_file)
            else:
                return self._build_from_discovery_v1(api_name, version)

        raise NotImplementedError(f'Compute {version} not supported')

    @functools.lru_cache(None)
    def networksecurity(self, version):
        api_name = 'networksecurity'
        if version == 'v1alpha1':
            return self._build_from_discovery_v2(
                api_name, version, api_key=self.private_api_key)

        raise NotImplementedError(f'Network Security {version} not supported')

    @functools.lru_cache(None)
    def networkservices(self, version):
        api_name = 'networkservices'
        if version == 'v1alpha1':
            return self._build_from_discovery_v2(
                api_name, version, api_key=self.private_api_key)

        raise NotImplementedError(f'Network Services {version} not supported')

    def _build_from_discovery_v1(self, api_name, version):
        api = discovery.build(
            api_name, version, cache_discovery=False,
            discoveryServiceUrl=self.v1_discovery_uri)
        self._exit_stack.enter_context(api)
        return api

    def _build_from_discovery_v2(self, api_name, version, *, api_key=None):
        key_arg = f'&key={api_key}' if api_key else ''
        api = discovery.build(
            api_name, version, cache_discovery=False,
            discoveryServiceUrl=f'{self.v2_discovery_uri}{key_arg}')
        self._exit_stack.enter_context(api)
        return api

    def _build_from_file(self, discovery_file):
        with open(discovery_file, 'r') as f:
            api = discovery.build_from_document(f.read())
        self._exit_stack.enter_context(api)
        return api


class Error(Exception):
    """Base error class for GCP API errors"""


class OperationError(Error):
    """
    Operation was not successful.

    Assuming Operation based on Google API Style Guide:
    https://cloud.google.com/apis/design/design_patterns#long_running_operations
    https://github.com/googleapis/googleapis/blob/master/google/longrunning/operations.proto
    """
    def __init__(self, api_name, operation_response, message=None):
        self.api_name = api_name
        operation = json_format.ParseDict(operation_response, Operation())
        self.name = operation.name or 'unknown'
        self.error = operation.error
        self.code_name = code_pb2.Code.Name(operation.error.code)
        if message is None:
            message = (f'{api_name} operation "{self.name}" failed. Error '
                       f'code: {self.error.code} ({self.code_name}), '
                       f'message: {self.error.message}')
        self.message = message
        super().__init__(message)


class GcpProjectApiResource:
    # todo(sergiitk): move someplace better
    _WAIT_FOR_OPERATION_SEC = 60 * 5
    _WAIT_FIXES_SEC = 2
    _GCP_API_RETRIES = 5

    def __init__(self, api: discovery.Resource, project: str):
        self.api: discovery.Resource = api
        self.project: str = project

    @staticmethod
    def wait_for_operation(operation_request,
                           test_success_fn,
                           timeout_sec=_WAIT_FOR_OPERATION_SEC,
                           wait_sec=_WAIT_FIXES_SEC):
        retryer = tenacity.Retrying(
            retry=(tenacity.retry_if_not_result(test_success_fn) |
                   tenacity.retry_if_exception_type()),
            wait=tenacity.wait_fixed(wait_sec),
            stop=tenacity.stop_after_delay(timeout_sec),
            after=tenacity.after_log(logger, logging.DEBUG),
            reraise=True)
        return retryer(operation_request.execute)


class GcpStandardCloudApiResource(GcpProjectApiResource):
    DEFAULT_GLOBAL = 'global'

    def parent(self, location=None):
        if not location:
            location = self.DEFAULT_GLOBAL
        return f'projects/{self.project}/locations/{location}'

    def resource_full_name(self, name, collection_name):
        return f'{self.parent()}/{collection_name}/{name}'

    def _create_resource(self, collection: discovery.Resource, body: dict,
                         **kwargs):
        logger.debug("Creating %s", body)
        create_req = collection.create(parent=self.parent(),
                                       body=body, **kwargs)
        self._execute(create_req)

    @staticmethod
    def _get_resource(collection: discovery.Resource, full_name):
        resource = collection.get(name=full_name).execute()
        logger.debug("Loaded %r", resource)
        return resource

    def _delete_resource(self, collection: discovery.Resource, full_name: str):
        logger.debug("Deleting %s", full_name)
        try:
            self._execute(collection.delete(name=full_name))
        except googleapiclient.errors.HttpError as error:
            # noinspection PyProtectedMember
            reason = error._get_reason()
            logger.info('Delete failed. Error: %s %s',
                        error.resp.status, reason)

    def _execute(self, request,
                 timeout_sec=GcpProjectApiResource._WAIT_FOR_OPERATION_SEC):
        operation = request.execute(num_retries=self._GCP_API_RETRIES)
        self._wait(operation, timeout_sec)

    def _wait(self, operation,
              timeout_sec=GcpProjectApiResource._WAIT_FOR_OPERATION_SEC):
        op_name = operation['name']
        logger.debug('Waiting for %s operation, timeout %s sec: %s',
                     self.__class__.__name__, timeout_sec, op_name)

        op_request = self.api.projects().locations().operations().get(
            name=op_name)
        operation = self.wait_for_operation(
            operation_request=op_request,
            test_success_fn=lambda result: result['done'],
            timeout_sec=timeout_sec)

        logger.debug('Completed operation: %s', operation)
        if 'error' in operation:
            raise OperationError(self.__class__.__name__, operation)
