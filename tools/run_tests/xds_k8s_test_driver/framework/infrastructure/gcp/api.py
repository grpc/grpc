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
import json
import logging
from typing import Any, Dict, List, Optional

from absl import flags
from google.cloud import secretmanager_v1
from google.longrunning import operations_pb2
from google.protobuf import json_format
from google.rpc import code_pb2
from google.rpc import error_details_pb2
from google.rpc import status_pb2
from googleapiclient import discovery
import googleapiclient.errors
import googleapiclient.http
import tenacity
import yaml

import framework.helpers.highlighter

logger = logging.getLogger(__name__)
PRIVATE_API_KEY_SECRET_NAME = flags.DEFINE_string(
    "private_api_key_secret_name",
    default=None,
    help=(
        "Load Private API access key from the latest version of the secret "
        "with the given name, in the format projects/*/secrets/*"
    ),
)
V1_DISCOVERY_URI = flags.DEFINE_string(
    "v1_discovery_uri",
    default=discovery.V1_DISCOVERY_URI,
    help="Override v1 Discovery URI",
)
V2_DISCOVERY_URI = flags.DEFINE_string(
    "v2_discovery_uri",
    default=discovery.V2_DISCOVERY_URI,
    help="Override v2 Discovery URI",
)
COMPUTE_V1_DISCOVERY_FILE = flags.DEFINE_string(
    "compute_v1_discovery_file",
    default=None,
    help="Load compute v1 from discovery file",
)
GCP_UI_URL = flags.DEFINE_string(
    "gcp_ui_url",
    default="console.cloud.google.com",
    help="Override GCP UI URL.",
)

# Type aliases
_HttpError = googleapiclient.errors.HttpError
_HttpLib2Error = googleapiclient.http.httplib2.HttpLib2Error
_HighlighterYaml = framework.helpers.highlighter.HighlighterYaml
Operation = operations_pb2.Operation
HttpRequest = googleapiclient.http.HttpRequest


class GcpApiManager:
    def __init__(
        self,
        *,
        v1_discovery_uri=None,
        v2_discovery_uri=None,
        compute_v1_discovery_file=None,
        private_api_key_secret_name=None,
        gcp_ui_url=None,
    ):
        self.v1_discovery_uri = v1_discovery_uri or V1_DISCOVERY_URI.value
        self.v2_discovery_uri = v2_discovery_uri or V2_DISCOVERY_URI.value
        self.compute_v1_discovery_file = (
            compute_v1_discovery_file or COMPUTE_V1_DISCOVERY_FILE.value
        )
        self.private_api_key_secret_name = (
            private_api_key_secret_name or PRIVATE_API_KEY_SECRET_NAME.value
        )
        self.gcp_ui_url = gcp_ui_url or GCP_UI_URL.value
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
        https://console.cloud.google.com/apis/credentials

        This method lazy-loads the content of the key from the Secret Manager.
        https://console.cloud.google.com/security/secret-manager
        """
        if not self.private_api_key_secret_name:
            raise ValueError(
                "private_api_key_secret_name must be set to "
                "access private_api_key."
            )

        secrets_api = self.secrets("v1")
        version_resource_path = secrets_api.secret_version_path(
            **secrets_api.parse_secret_path(self.private_api_key_secret_name),
            secret_version="latest",
        )
        secret: secretmanager_v1.AccessSecretVersionResponse
        secret = secrets_api.access_secret_version(name=version_resource_path)
        return secret.payload.data.decode()

    @functools.lru_cache(None)
    def compute(self, version):
        api_name = "compute"
        if version == "v1":
            if self.compute_v1_discovery_file:
                return self._build_from_file(self.compute_v1_discovery_file)
            else:
                return self._build_from_discovery_v1(api_name, version)
        elif version == "v1alpha":
            return self._build_from_discovery_v1(api_name, "alpha")

        raise NotImplementedError(f"Compute {version} not supported")

    @functools.lru_cache(None)
    def networksecurity(self, version):
        api_name = "networksecurity"
        if version == "v1alpha1":
            return self._build_from_discovery_v2(
                api_name,
                version,
                api_key=self.private_api_key,
                visibility_labels=["NETWORKSECURITY_ALPHA"],
            )
        elif version == "v1beta1":
            return self._build_from_discovery_v2(api_name, version)

        raise NotImplementedError(f"Network Security {version} not supported")

    @functools.lru_cache(None)
    def networkservices(self, version):
        api_name = "networkservices"
        if version == "v1alpha1":
            return self._build_from_discovery_v2(
                api_name,
                version,
                api_key=self.private_api_key,
                visibility_labels=["NETWORKSERVICES_ALPHA"],
            )
        elif version == "v1beta1":
            return self._build_from_discovery_v2(api_name, version)

        raise NotImplementedError(f"Network Services {version} not supported")

    @staticmethod
    @functools.lru_cache(None)
    def secrets(version: str):
        if version == "v1":
            return secretmanager_v1.SecretManagerServiceClient()

        raise NotImplementedError(f"Secret Manager {version} not supported")

    @functools.lru_cache(None)
    def iam(self, version: str) -> discovery.Resource:
        """Identity and Access Management (IAM) API.

        https://cloud.google.com/iam/docs/reference/rest
        https://googleapis.github.io/google-api-python-client/docs/dyn/iam_v1.html
        """
        api_name = "iam"
        if version == "v1":
            return self._build_from_discovery_v1(api_name, version)

        raise NotImplementedError(
            f"Identity and Access Management (IAM) {version} not supported"
        )

    def _build_from_discovery_v1(self, api_name, version):
        api = discovery.build(
            api_name,
            version,
            cache_discovery=False,
            discoveryServiceUrl=self.v1_discovery_uri,
        )
        self._exit_stack.enter_context(api)
        return api

    def _build_from_discovery_v2(
        self,
        api_name,
        version,
        *,
        api_key: Optional[str] = None,
        visibility_labels: Optional[List] = None,
    ):
        params = {}
        if api_key:
            params["key"] = api_key
        if visibility_labels:
            # Dash-separated list of labels.
            params["labels"] = "_".join(visibility_labels)

        params_str = ""
        if params:
            params_str = "&" + "&".join(f"{k}={v}" for k, v in params.items())

        api = discovery.build(
            api_name,
            version,
            cache_discovery=False,
            discoveryServiceUrl=f"{self.v2_discovery_uri}{params_str}",
        )
        self._exit_stack.enter_context(api)
        return api

    def _build_from_file(self, discovery_file):
        with open(discovery_file, "r") as f:
            api = discovery.build_from_document(f.read())
        self._exit_stack.enter_context(api)
        return api


class Error(Exception):
    """Base error class for GCP API errors."""


class ResponseError(Error):
    """The response was not a 2xx."""

    reason: str
    uri: str
    error_details: Optional[str]
    status: Optional[int]
    cause: _HttpError

    def __init__(self, cause: _HttpError):
        # TODO(sergiitk): cleanup when we upgrade googleapiclient:
        #  - remove _get_reason()
        #  - remove error_details note
        #  - use status_code()
        self.reason = cause._get_reason().strip()  # noqa
        self.uri = cause.uri
        self.error_details = cause.error_details  # NOTE: Must after _get_reason
        self.status = None
        if cause.resp and cause.resp.status:
            self.status = cause.resp.status
        self.cause = cause
        super().__init__()

    def __repr__(self):
        return (
            f"<ResponseError {self.status} when requesting {self.uri} "
            f'returned "{self.reason}". Details: "{self.error_details}">'
        )


class TransportError(Error):
    """A transport error has occurred."""

    cause: _HttpLib2Error

    def __init__(self, cause: _HttpLib2Error):
        self.cause = cause
        super().__init__()

    def __repr__(self):
        return f"<TransportError cause: {self.cause!r}>"


class OperationError(Error):
    """
    Operation was not successful.

    Assuming Operation based on Google API Style Guide:
    https://cloud.google.com/apis/design/design_patterns#long_running_operations
    https://github.com/googleapis/googleapis/blob/master/google/longrunning/operations.proto
    """

    api_name: str
    name: str
    metadata: Any
    code_name: code_pb2.Code
    error: status_pb2.Status

    def __init__(self, api_name: str, response: dict):
        self.api_name = api_name

        # Operation.metadata field is Any specific to the API. It may not be
        # present in the default descriptor pool, and that's expected.
        # To avoid json_format.ParseError, handle it separately.
        self.metadata = response.pop("metadata", {})

        # Must be after removing metadata field.
        operation: Operation = self._parse_operation_response(response)
        self.name = operation.name or "unknown"
        self.code_name = code_pb2.Code.Name(operation.error.code)
        self.error = operation.error
        super().__init__()

    @staticmethod
    def _parse_operation_response(operation_response: dict) -> Operation:
        try:
            return json_format.ParseDict(
                operation_response,
                Operation(),
                ignore_unknown_fields=True,
                descriptor_pool=error_details_pb2.DESCRIPTOR.pool,
            )
        except (json_format.Error, TypeError) as e:
            # Swallow parsing errors if any. Building correct OperationError()
            # is more important than losing debug information. Details still
            # can be extracted from the warning.
            logger.warning(
                (
                    "Can't parse response while processing OperationError:"
                    " '%r', error %r"
                ),
                operation_response,
                e,
            )
            return Operation()

    def __str__(self):
        indent_l1 = " " * 2
        indent_l2 = indent_l1 * 2

        result = (
            f'{self.api_name} operation "{self.name}" failed.\n'
            f"{indent_l1}code: {self.error.code} ({self.code_name})\n"
            f'{indent_l1}message: "{self.error.message}"'
        )

        if self.error.details:
            result += f"\n{indent_l1}details: [\n"
            for any_error in self.error.details:
                error_str = json_format.MessageToJson(any_error)
                for line in error_str.splitlines():
                    result += indent_l2 + line + "\n"
            result += f"{indent_l1}]"

        if self.metadata:
            result += f"\n  metadata: \n"
            metadata_str = json.dumps(self.metadata, indent=2)
            for line in metadata_str.splitlines():
                result += indent_l2 + line + "\n"
            result = result.rstrip()

        return result


class GcpProjectApiResource:
    # TODO(sergiitk): move someplace better
    _WAIT_FOR_OPERATION_SEC = 60 * 10
    _WAIT_FIXED_SEC = 2
    _GCP_API_RETRIES = 5

    def __init__(self, api: discovery.Resource, project: str):
        self.api: discovery.Resource = api
        self.project: str = project
        self._highlighter = _HighlighterYaml()

    # TODO(sergiitk): in upcoming GCP refactoring, differentiate between
    #   _execute for LRO (Long Running Operations), and immediate operations.
    def _execute(
        self,
        request: HttpRequest,
        *,
        num_retries: Optional[int] = _GCP_API_RETRIES,
    ) -> Dict[str, Any]:
        """Execute the immediate request.

        Returns:
          Unmarshalled response as a dictionary.

        Raises:
          ResponseError if the response was not a 2xx.
          TransportError if a transport error has occurred.
        """
        if num_retries is None:
            num_retries = self._GCP_API_RETRIES
        try:
            return request.execute(num_retries=num_retries)
        except _HttpError as error:
            raise ResponseError(error)
        except _HttpLib2Error as error:
            raise TransportError(error)

    def resource_pretty_format(
        self,
        resource: Any,
        *,
        highlight: bool = True,
    ) -> str:
        """Return a string with pretty-printed resource body."""
        yaml_out: str = yaml.dump(
            resource,
            explicit_start=True,
            explicit_end=True,
        )
        return self._highlighter.highlight(yaml_out) if highlight else yaml_out

    def resources_pretty_format(
        self,
        resources: list[Any],
        *,
        highlight: bool = True,
    ) -> str:
        out = []
        for resource in resources:
            if hasattr(resource, "name"):
                out.append(f"{resource.name}:")
            elif "name" in resource:
                out.append(f"{resource['name']}:")
            out.append(
                self.resource_pretty_format(resource, highlight=highlight)
            )
        return "\n".join(out)

    @staticmethod
    def wait_for_operation(
        operation_request,
        test_success_fn,
        timeout_sec=_WAIT_FOR_OPERATION_SEC,
        wait_sec=_WAIT_FIXED_SEC,
    ):
        retryer = tenacity.Retrying(
            retry=(
                tenacity.retry_if_not_result(test_success_fn)
                | tenacity.retry_if_exception_type()
            ),
            wait=tenacity.wait_fixed(wait_sec),
            stop=tenacity.stop_after_delay(timeout_sec),
            after=tenacity.after_log(logger, logging.DEBUG),
            reraise=True,
        )
        return retryer(operation_request.execute)


class GcpStandardCloudApiResource(GcpProjectApiResource, metaclass=abc.ABCMeta):
    GLOBAL_LOCATION = "global"

    def parent(self, location: Optional[str] = GLOBAL_LOCATION):
        if location is None:
            location = self.GLOBAL_LOCATION
        return f"projects/{self.project}/locations/{location}"

    def resource_full_name(self, name, collection_name):
        return f"{self.parent()}/{collection_name}/{name}"

    def _create_resource(
        self, collection: discovery.Resource, body: dict, **kwargs
    ):
        logger.info(
            "Creating %s resource:\n%s",
            self.api_name,
            self.resource_pretty_format(body),
        )
        create_req = collection.create(
            parent=self.parent(), body=body, **kwargs
        )
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
        logger.info(
            "Loaded %s:\n%s", full_name, self.resource_pretty_format(resource)
        )
        return resource

    def _delete_resource(
        self, collection: discovery.Resource, full_name: str
    ) -> bool:
        logger.debug("Deleting %s", full_name)
        try:
            self._execute(collection.delete(name=full_name))
            return True
        except _HttpError as error:
            if error.resp and error.resp.status == 404:
                logger.info("%s not deleted since it does not exist", full_name)
            else:
                logger.warning("Failed to delete %s, %r", full_name, error)
        return False

    # TODO(sergiitk): Use ResponseError and TransportError
    def _execute(  # pylint: disable=arguments-differ
        self,
        request: HttpRequest,
        timeout_sec: int = GcpProjectApiResource._WAIT_FOR_OPERATION_SEC,
    ):
        operation = request.execute(num_retries=self._GCP_API_RETRIES)
        logger.debug("Operation %s", operation)
        self._wait(operation["name"], timeout_sec)

    def _wait(
        self,
        operation_id: str,
        timeout_sec: int = GcpProjectApiResource._WAIT_FOR_OPERATION_SEC,
    ):
        logger.info(
            "Waiting %s sec for %s operation id: %s",
            timeout_sec,
            self.api_name,
            operation_id,
        )

        op_request = (
            self.api.projects().locations().operations().get(name=operation_id)
        )
        operation = self.wait_for_operation(
            operation_request=op_request,
            test_success_fn=lambda result: result["done"],
            timeout_sec=timeout_sec,
        )

        logger.debug("Completed operation: %s", operation)
        if "error" in operation:
            raise OperationError(self.api_name, operation)
