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
import contextlib
import functools
import logging
from typing import Optional, List

# Workaround: `grpc` must be imported before `google.protobuf.json_format`,
# to prevent "Segmentation fault". Ref https://github.com/grpc/grpc/issues/24897
# TODO(sergiitk): Remove after #24897 is solved
import grpc  # noqa pylint: disable=unused-import
from absl import flags
from google.cloud import secretmanager_v1
from google.longrunning import operations_pb2
from google.protobuf import json_format
from google.rpc import code_pb2
from googleapiclient import discovery
import googleapiclient.errors
import tenacity
import yaml

logger = logging.getLogger(__name__)
PRIVATE_API_KEY_SECRET_NAME = flags.DEFINE_string(
    "private_api_key_secret_name",
    default=None,
    help="Load Private API access key from the latest version of the secret "
    "with the given name, in the format projects/*/secrets/*")
V1_DISCOVERY_URI = flags.DEFINE_string("v1_discovery_uri",
                                       default=discovery.V1_DISCOVERY_URI,
                                       help="Override v1 Discovery URI")
V2_DISCOVERY_URI = flags.DEFINE_string("v2_discovery_uri",
                                       default=discovery.V2_DISCOVERY_URI,
                                       help="Override v2 Discovery URI")
COMPUTE_V1_DISCOVERY_FILE = flags.DEFINE_string(
    "compute_v1_discovery_file",
    default=None,
    help="Load compute v1 from discovery file")

# Type aliases
Operation = operations_pb2.Operation


class GcpApiManager:

    def __init__(self,
                 *,
                 v1_discovery_uri=None,
                 v2_discovery_uri=None,
                 compute_v1_discovery_file=None,
                 private_api_key_secret_name=None):
        self.v1_discovery_uri = v1_discovery_uri or V1_DISCOVERY_URI.value
        self.v2_discovery_uri = v2_discovery_uri or V2_DISCOVERY_URI.value
        self.compute_v1_discovery_file = (compute_v1_discovery_file or
                                          COMPUTE_V1_DISCOVERY_FILE.value)
        self.private_api_key_secret_name = (private_api_key_secret_name or
                                            PRIVATE_API_KEY_SECRET_NAME.value)
        # TODO(sergiitk): add options to pass google Credentials
        self._exit_stack = contextlib.ExitStack()

    def close(self):
        self._exit_stack.close()

    @property
    @functools.lru_cache(None)
    def private_api_key(self):
        """
        Private API key.

        Return API key credential that identifies a GCP project allow-listed for
        accessing private API discovery documents.
        https://pantheon.corp.google.com/apis/credentials

        This method lazy-loads the content of the key from the Secret Manager.
        https://pantheon.corp.google.com/security/secret-manager
        """
        if not self.private_api_key_secret_name:
            raise ValueError('private_api_key_secret_name must be set to '
                             'access private_api_key.')

        secrets_api = self.secrets('v1')
        version_resource_path = secrets_api.secret_version_path(
            **secrets_api.parse_secret_path(self.private_api_key_secret_name),
            secret_version='latest')
        secret: secretmanager_v1.AccessSecretVersionResponse
        secret = secrets_api.access_secret_version(name=version_resource_path)
        return secret.payload.data.decode()

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
                api_name,
                version,
                api_key=self.private_api_key,
                visibility_labels=['NETWORKSECURITY_ALPHA'])

        raise NotImplementedError(f'Network Security {version} not supported')

    @functools.lru_cache(None)
    def networkservices(self, version):
        api_name = 'networkservices'
        if version == 'v1alpha1':
            return self._build_from_discovery_v2(
                api_name,
                version,
                api_key=self.private_api_key,
                visibility_labels=['NETWORKSERVICES_ALPHA'])

        raise NotImplementedError(f'Network Services {version} not supported')

    @functools.lru_cache(None)
    def secrets(self, version):
        if version == 'v1':
            return secretmanager_v1.SecretManagerServiceClient()

        raise NotImplementedError(f'Secret Manager {version} not supported')

    def _build_from_discovery_v1(self, api_name, version):
        api = discovery.build(api_name,
                              version,
                              cache_discovery=False,
                              discoveryServiceUrl=self.v1_discovery_uri)
        self._exit_stack.enter_context(api)
        return api

    def _build_from_discovery_v2(self,
                                 api_name,
                                 version,
                                 *,
                                 api_key: Optional[str] = None,
                                 visibility_labels: Optional[List] = None):
        params = {}
        if api_key:
            params['key'] = api_key
        if visibility_labels:
            # Dash-separated list of labels.
            params['labels'] = '_'.join(visibility_labels)

        params_str = ''
        if params:
            params_str = '&' + ('&'.join(f'{k}={v}' for k, v in params.items()))

        api = discovery.build(
            api_name,
            version,
            cache_discovery=False,
            discoveryServiceUrl=f'{self.v2_discovery_uri}{params_str}')
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
    # TODO(sergiitk): move someplace better
    _WAIT_FOR_OPERATION_SEC = 60 * 5
    _WAIT_FIXED_SEC = 2
    _GCP_API_RETRIES = 5

    def __init__(self, api: discovery.Resource, project: str):
        self.api: discovery.Resource = api
        self.project: str = project

    @staticmethod
    def wait_for_operation(operation_request,
                           test_success_fn,
                           timeout_sec=_WAIT_FOR_OPERATION_SEC,
                           wait_sec=_WAIT_FIXED_SEC):
        retryer = tenacity.Retrying(
            retry=(tenacity.retry_if_not_result(test_success_fn) |
                   tenacity.retry_if_exception_type()),
            wait=tenacity.wait_fixed(wait_sec),
            stop=tenacity.stop_after_delay(timeout_sec),
            after=tenacity.after_log(logger, logging.DEBUG),
            reraise=True)
        return retryer(operation_request.execute)

    @staticmethod
    def _resource_pretty_format(body: dict) -> str:
        """Return a string with pretty-printed resource body."""
        return yaml.dump(body, explicit_start=True, explicit_end=True)


class GcpStandardCloudApiResource(GcpProjectApiResource, metaclass=abc.ABCMeta):
    GLOBAL_LOCATION = 'global'

    def parent(self, location: Optional[str] = GLOBAL_LOCATION):
        if location is None:
            location = self.GLOBAL_LOCATION
        return f'projects/{self.project}/locations/{location}'

    def resource_full_name(self, name, collection_name):
        return f'{self.parent()}/{collection_name}/{name}'

    def _create_resource(self, collection: discovery.Resource, body: dict,
                         **kwargs):
        logger.info("Creating %s resource:\n%s", self.api_name,
                    self._resource_pretty_format(body))
        create_req = collection.create(parent=self.parent(),
                                       body=body,
                                       **kwargs)
        self._execute(create_req)

    @property
    @abc.abstractmethod
    def api_name(self) -> str:
        raise NotImplementedError

    @property
    @abc.abstractmethod
    def api_version(self) -> str:
        raise NotImplementedError

    def _get_resource(self, collection: discovery.Resource, full_name):
        resource = collection.get(name=full_name).execute()
        logger.info('Loaded %s:\n%s', full_name,
                    self._resource_pretty_format(resource))
        return resource

    def _delete_resource(self, collection: discovery.Resource,
                         full_name: str) -> bool:
        logger.debug("Deleting %s", full_name)
        try:
            self._execute(collection.delete(name=full_name))
            return True
        except googleapiclient.errors.HttpError as error:
            if error.resp and error.resp.status == 404:
                logger.info('%s not deleted since it does not exist', full_name)
            else:
                logger.warning('Failed to delete %s, %r', full_name, error)
        return False

    def _execute(self,
                 request,
                 timeout_sec=GcpProjectApiResource._WAIT_FOR_OPERATION_SEC):
        operation = request.execute(num_retries=self._GCP_API_RETRIES)
        self._wait(operation, timeout_sec)

    def _wait(self,
              operation,
              timeout_sec=GcpProjectApiResource._WAIT_FOR_OPERATION_SEC):
        op_name = operation['name']
        logger.debug('Waiting for %s operation, timeout %s sec: %s',
                     self.api_name, timeout_sec, op_name)

        op_request = self.api.projects().locations().operations().get(
            name=op_name)
        operation = self.wait_for_operation(
            operation_request=op_request,
            test_success_fn=lambda result: result['done'],
            timeout_sec=timeout_sec)

        logger.debug('Completed operation: %s', operation)
        if 'error' in operation:
            raise OperationError(self.api_name, operation)
