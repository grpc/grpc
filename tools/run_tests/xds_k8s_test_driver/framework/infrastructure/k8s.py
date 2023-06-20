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
# TODO(sergiitk): to k8s/ package, and get rid of k8s_internal, which is only
#   added to get around circular dependencies caused by k8s.py clashing with
#   k8s/__init__.py
import datetime
import json
import logging
import pathlib
import threading
from typing import Any, Callable, List, Optional, Tuple

from kubernetes import client
from kubernetes import utils
import kubernetes.config
import urllib3.exceptions
import yaml

from framework.helpers import retryers
import framework.helpers.highlighter
from framework.infrastructure.k8s_internal import k8s_log_collector
from framework.infrastructure.k8s_internal import k8s_port_forwarder

logger = logging.getLogger(__name__)

# Type aliases
_HighlighterYaml = framework.helpers.highlighter.HighlighterYaml
PodLogCollector = k8s_log_collector.PodLogCollector
PortForwarder = k8s_port_forwarder.PortForwarder
ApiClient = client.ApiClient
V1Deployment = client.V1Deployment
V1ServiceAccount = client.V1ServiceAccount
V1Pod = client.V1Pod
V1PodList = client.V1PodList
V1Service = client.V1Service
V1Namespace = client.V1Namespace

_timedelta = datetime.timedelta
_ApiException = client.ApiException
_FailToCreateError = utils.FailToCreateError

_RETRY_ON_EXCEPTIONS = (
    urllib3.exceptions.HTTPError,
    _ApiException,
    _FailToCreateError,
)


def _server_restart_retryer() -> retryers.Retrying:
    return retryers.exponential_retryer_with_timeout(
        retry_on_exceptions=_RETRY_ON_EXCEPTIONS,
        wait_min=_timedelta(seconds=1),
        wait_max=_timedelta(seconds=10),
        timeout=_timedelta(minutes=3),
    )


def _too_many_requests_retryer() -> retryers.Retrying:
    return retryers.exponential_retryer_with_timeout(
        retry_on_exceptions=_RETRY_ON_EXCEPTIONS,
        wait_min=_timedelta(seconds=10),
        wait_max=_timedelta(seconds=30),
        timeout=_timedelta(minutes=3),
    )


def _quick_recovery_retryer() -> retryers.Retrying:
    return retryers.constant_retryer(
        wait_fixed=_timedelta(seconds=1),
        attempts=3,
        retry_on_exceptions=_RETRY_ON_EXCEPTIONS,
    )


def label_dict_to_selector(labels: dict) -> str:
    return ",".join(f"{k}=={v}" for k, v in labels.items())


class NotFound(Exception):
    """Indicates the resource is not found on the API server."""


class KubernetesApiManager:
    _client: ApiClient
    context: str
    apps: client.AppsV1Api
    core: client.CoreV1Api
    _apis: set

    def __init__(self, context: str):
        self.context = context
        self._client = self._new_client_from_context(context)
        self.apps = client.AppsV1Api(self.client)
        self.core = client.CoreV1Api(self.client)
        self.custom_objects = client.CustomObjectsApi(self.client)
        self._apis = {self.apps, self.core}

    @property
    def client(self) -> ApiClient:
        return self._client

    def close(self):
        self.client.close()

    def reload(self):
        self.close()
        self._client = self._new_client_from_context(self.context)
        # Update default configuration so that modules that initialize
        # ApiClient implicitly (e.g. kubernetes.watch.Watch) get the updates.
        client.Configuration.set_default(self._client.configuration)
        for api in self._apis:
            api.api_client = self._client

    @staticmethod
    def _new_client_from_context(context: str) -> ApiClient:
        client_instance = kubernetes.config.new_client_from_config(
            context=context
        )
        logger.info(
            'Using kubernetes context "%s", active host: %s',
            context,
            client_instance.configuration.host,
        )
        # TODO(sergiitk): fine-tune if we see the total wait unreasonably long.
        client_instance.configuration.retries = 10
        return client_instance


class KubernetesNamespace:  # pylint: disable=too-many-public-methods
    _highlighter: framework.helpers.highlighter.Highlighter
    _api: KubernetesApiManager
    _name: str

    NEG_STATUS_META = "cloud.google.com/neg-status"
    DELETE_GRACE_PERIOD_SEC: int = 5
    WAIT_SHORT_TIMEOUT_SEC: int = 60
    WAIT_SHORT_SLEEP_SEC: int = 1
    WAIT_MEDIUM_TIMEOUT_SEC: int = 5 * 60
    WAIT_MEDIUM_SLEEP_SEC: int = 10
    WAIT_LONG_TIMEOUT_SEC: int = 10 * 60
    WAIT_LONG_SLEEP_SEC: int = 30
    WAIT_POD_START_TIMEOUT_SEC: int = 3 * 60

    def __init__(self, api: KubernetesApiManager, name: str):
        self._api = api
        self._name = name
        self._highlighter = _HighlighterYaml()

    @property
    def name(self):
        return self._name

    def _refresh_auth(self):
        logger.info("Reloading k8s api client to refresh the auth.")
        self._api.reload()

    def _apply_manifest(self, manifest):
        return utils.create_from_dict(
            self._api.client, manifest, namespace=self.name
        )

    def _get_resource(self, method: Callable[[Any], object], *args, **kwargs):
        try:
            return self._execute(method, *args, **kwargs)
        except NotFound:
            # Instead of trowing an error when a resource doesn't exist,
            # just return None.
            return None

    def _execute(self, method: Callable[[Any], object], *args, **kwargs):
        # Note: Intentionally leaving return type as unspecified to not confuse
        # pytype for methods that delegate calls to this wrapper.
        try:
            return method(*args, **kwargs)
        except _RETRY_ON_EXCEPTIONS as err:
            retryer = self._handle_exception(err)
            if retryer is not None:
                return retryer(method, *args, **kwargs)
            raise

    def _handle_exception(self, err: Exception) -> Optional[retryers.Retrying]:
        # TODO(sergiitk): replace returns with match/case when we use to py3.10.
        # pylint: disable=too-many-return-statements

        # Unwrap MaxRetryError.
        if isinstance(err, urllib3.exceptions.MaxRetryError):
            return self._handle_exception(err.reason) if err.reason else None

        # We consider all `NewConnectionError`s as caused by a k8s
        # API server restart. `NewConnectionError`s we've seen:
        #   - [Errno 110] Connection timed out
        #   - [Errno 111] Connection refused
        if isinstance(err, urllib3.exceptions.NewConnectionError):
            return _server_restart_retryer()

        # We consider all `ProtocolError`s with "Connection aborted" message
        # as caused by a k8s API server restart.
        # `ProtocolError`s we've seen:
        #   - RemoteDisconnected('Remote end closed connection
        #     without response')
        #   - ConnectionResetError(104, 'Connection reset by peer')
        if isinstance(err, urllib3.exceptions.ProtocolError):
            if "connection aborted" in str(err).lower():
                return _server_restart_retryer()
            else:
                # To cover other cases we didn't account for, and haven't
                # seen in the wild, f.e. "Connection broken"
                return _quick_recovery_retryer()

        # ApiException means the server has received our request and responded
        # with an error we can parse (except a few corner cases, f.e. SSLError).
        if isinstance(err, _ApiException):
            return self._handle_api_exception(err)

        # Unwrap FailToCreateError.
        if isinstance(err, _FailToCreateError):
            # We're always sending a single document, so we expect
            # a single wrapped exception in return.
            if len(err.api_exceptions) == 1:
                return self._handle_exception(err.api_exceptions[0])

        return None

    def _handle_api_exception(
        self, err: _ApiException
    ) -> Optional[retryers.Retrying]:
        # TODO(sergiitk): replace returns with match/case when we use to py3.10.
        # pylint: disable=too-many-return-statements

        # TODO(sergiitk): can I chain the retryers?
        logger.debug(
            "Handling k8s.ApiException: status=%s reason=%s body=%s headers=%s",
            err.status,
            err.reason,
            err.body,
            err.headers,
        )

        code: int = err.status
        body = err.body.lower() if err.body else ""

        # 401 Unauthorized: token might be expired, attempt auth refresh.
        if code == 401:
            self._refresh_auth()
            return _quick_recovery_retryer()

        # 404 Not Found. Make it easier for the caller to handle 404s.
        if code == 404:
            raise NotFound(
                "Kubernetes API returned 404 Not Found: "
                f"{self._status_message_or_body(body)}"
            ) from err

        # 409 Conflict
        # "Operation cannot be fulfilled on resourcequotas "foo": the object
        # has been modified; please apply your changes to the latest version
        # and try again".
        # See https://github.com/kubernetes/kubernetes/issues/67761
        if code == 409:
            return _quick_recovery_retryer()

        # 429 Too Many Requests: "Too many requests, please try again later"
        if code == 429:
            return _too_many_requests_retryer()

        # 500 Internal Server Error
        if code == 500:
            # Observed when using `kubectl proxy`.
            # "dial tcp 127.0.0.1:8080: connect: connection refused"
            if "connection refused" in body:
                return _server_restart_retryer()

            # Known 500 errors that should be treated as 429:
            # - Internal Server Error: "/api/v1/namespaces": the server has
            #   received too many requests and has asked us
            #   to try again later
            # - Internal Server Error: "/api/v1/namespaces/foo/services":
            #   the server is currently unable to handle the request
            if (
                "too many requests" in body
                or "currently unable to handle the request" in body
            ):
                return _too_many_requests_retryer()

            # In other cases, just retry a few times in case the server
            # resumes normal operation.
            return _quick_recovery_retryer()

        # 504 Gateway Timeout:
        # "Timeout: request did not complete within the allotted timeout"
        if code == 504:
            return _server_restart_retryer()

        return None

    @classmethod
    def _status_message_or_body(cls, body: str) -> str:
        try:
            return str(json.loads(body)["message"])
        except (KeyError, ValueError):
            return body

    def create_single_resource(self, manifest):
        return self._execute(self._apply_manifest, manifest)

    def get_service(self, name) -> V1Service:
        return self._get_resource(
            self._api.core.read_namespaced_service, name, self.name
        )

    def get_service_account(self, name) -> V1Service:
        return self._get_resource(
            self._api.core.read_namespaced_service_account, name, self.name
        )

    def delete_service(
        self, name, grace_period_seconds=DELETE_GRACE_PERIOD_SEC
    ):
        self._execute(
            self._api.core.delete_namespaced_service,
            name=name,
            namespace=self.name,
            body=client.V1DeleteOptions(
                propagation_policy="Foreground",
                grace_period_seconds=grace_period_seconds,
            ),
        )

    def delete_service_account(
        self, name, grace_period_seconds=DELETE_GRACE_PERIOD_SEC
    ):
        self._execute(
            self._api.core.delete_namespaced_service_account,
            name=name,
            namespace=self.name,
            body=client.V1DeleteOptions(
                propagation_policy="Foreground",
                grace_period_seconds=grace_period_seconds,
            ),
        )

    def get(self) -> V1Namespace:
        return self._get_resource(self._api.core.read_namespace, self.name)

    def delete(self, grace_period_seconds=DELETE_GRACE_PERIOD_SEC):
        self._execute(
            self._api.core.delete_namespace,
            name=self.name,
            body=client.V1DeleteOptions(
                propagation_policy="Foreground",
                grace_period_seconds=grace_period_seconds,
            ),
        )

    def wait_for_service_deleted(
        self,
        name: str,
        timeout_sec: int = WAIT_SHORT_TIMEOUT_SEC,
        wait_sec: int = WAIT_SHORT_SLEEP_SEC,
    ) -> None:
        retryer = retryers.constant_retryer(
            wait_fixed=_timedelta(seconds=wait_sec),
            timeout=_timedelta(seconds=timeout_sec),
            check_result=lambda service: service is None,
        )
        retryer(self.get_service, name)

    def wait_for_service_account_deleted(
        self,
        name: str,
        timeout_sec: int = WAIT_SHORT_TIMEOUT_SEC,
        wait_sec: int = WAIT_SHORT_SLEEP_SEC,
    ) -> None:
        retryer = retryers.constant_retryer(
            wait_fixed=_timedelta(seconds=wait_sec),
            timeout=_timedelta(seconds=timeout_sec),
            check_result=lambda service_account: service_account is None,
        )
        retryer(self.get_service_account, name)

    def wait_for_namespace_deleted(
        self,
        timeout_sec: int = WAIT_LONG_TIMEOUT_SEC,
        wait_sec: int = WAIT_LONG_SLEEP_SEC,
    ) -> None:
        retryer = retryers.constant_retryer(
            wait_fixed=_timedelta(seconds=wait_sec),
            timeout=_timedelta(seconds=timeout_sec),
            check_result=lambda namespace: namespace is None,
        )
        retryer(self.get)

    def wait_for_service_neg(
        self,
        name: str,
        timeout_sec: int = WAIT_SHORT_TIMEOUT_SEC,
        wait_sec: int = WAIT_SHORT_SLEEP_SEC,
    ) -> None:
        timeout = _timedelta(seconds=timeout_sec)
        retryer = retryers.constant_retryer(
            wait_fixed=_timedelta(seconds=wait_sec),
            timeout=timeout,
            check_result=self._check_service_neg_annotation,
        )
        try:
            retryer(self.get_service, name)
        except retryers.RetryError as e:
            logger.error(
                (
                    "Timeout %s (h:mm:ss) waiting for service %s to report NEG "
                    "status. Last service status:\n%s"
                ),
                timeout,
                name,
                self._pretty_format_status(e.result()),
            )
            raise

    def get_service_neg(
        self, service_name: str, service_port: int
    ) -> Tuple[str, List[str]]:
        service = self.get_service(service_name)
        neg_info: dict = json.loads(
            service.metadata.annotations[self.NEG_STATUS_META]
        )
        neg_name: str = neg_info["network_endpoint_groups"][str(service_port)]
        neg_zones: List[str] = neg_info["zones"]
        return neg_name, neg_zones

    def get_deployment(self, name) -> V1Deployment:
        return self._get_resource(
            self._api.apps.read_namespaced_deployment, name, self.name
        )

    def delete_deployment(
        self, name: str, grace_period_seconds: int = DELETE_GRACE_PERIOD_SEC
    ) -> None:
        self._execute(
            self._api.apps.delete_namespaced_deployment,
            name=name,
            namespace=self.name,
            body=client.V1DeleteOptions(
                propagation_policy="Foreground",
                grace_period_seconds=grace_period_seconds,
            ),
        )

    def list_deployment_pods(self, deployment: V1Deployment) -> List[V1Pod]:
        # V1LabelSelector.match_expressions not supported at the moment
        return self.list_pods_with_labels(deployment.spec.selector.match_labels)

    def wait_for_deployment_available_replicas(
        self,
        name: str,
        count: int = 1,
        timeout_sec: int = WAIT_MEDIUM_TIMEOUT_SEC,
        wait_sec: int = WAIT_SHORT_SLEEP_SEC,
    ) -> None:
        timeout = _timedelta(seconds=timeout_sec)
        retryer = retryers.constant_retryer(
            wait_fixed=_timedelta(seconds=wait_sec),
            timeout=timeout,
            check_result=lambda depl: self._replicas_available(depl, count),
        )
        try:
            retryer(self.get_deployment, name)
        except retryers.RetryError as e:
            logger.error(
                (
                    "Timeout %s (h:mm:ss) waiting for deployment %s to report"
                    " %i replicas available. Last status:\n%s"
                ),
                timeout,
                name,
                count,
                self._pretty_format_status(e.result()),
            )
            raise

    def wait_for_deployment_replica_count(
        self,
        deployment: V1Deployment,
        count: int = 1,
        *,
        timeout_sec: int = WAIT_MEDIUM_TIMEOUT_SEC,
        wait_sec: int = WAIT_SHORT_SLEEP_SEC,
    ) -> None:
        timeout = _timedelta(seconds=timeout_sec)
        retryer = retryers.constant_retryer(
            wait_fixed=_timedelta(seconds=wait_sec),
            timeout=timeout,
            check_result=lambda pods: len(pods) == count,
        )
        try:
            retryer(self.list_deployment_pods, deployment)
        except retryers.RetryError as e:
            result = e.result(default=[])
            logger.error(
                (
                    "Timeout %s (h:mm:ss) waiting for pod count %i, got: %i. "
                    "Pod statuses:\n%s"
                ),
                timeout,
                count,
                len(result),
                self._pretty_format_statuses(result),
            )
            raise

    def wait_for_deployment_deleted(
        self,
        deployment_name: str,
        timeout_sec: int = WAIT_MEDIUM_TIMEOUT_SEC,
        wait_sec: int = WAIT_MEDIUM_SLEEP_SEC,
    ) -> None:
        retryer = retryers.constant_retryer(
            wait_fixed=_timedelta(seconds=wait_sec),
            timeout=_timedelta(seconds=timeout_sec),
            check_result=lambda deployment: deployment is None,
        )
        retryer(self.get_deployment, deployment_name)

    def list_pods_with_labels(self, labels: dict) -> List[V1Pod]:
        pod_list: V1PodList = self._execute(
            self._api.core.list_namespaced_pod,
            self.name,
            label_selector=label_dict_to_selector(labels),
        )
        return pod_list.items

    def get_pod(self, name: str) -> V1Pod:
        return self._get_resource(
            self._api.core.read_namespaced_pod, name, self.name
        )

    def wait_for_pod_started(
        self,
        pod_name: str,
        timeout_sec: int = WAIT_POD_START_TIMEOUT_SEC,
        wait_sec: int = WAIT_SHORT_SLEEP_SEC,
    ) -> None:
        timeout = _timedelta(seconds=timeout_sec)
        retryer = retryers.constant_retryer(
            wait_fixed=_timedelta(seconds=wait_sec),
            timeout=timeout,
            check_result=self._pod_started,
        )
        try:
            retryer(self.get_pod, pod_name)
        except retryers.RetryError as e:
            logger.error(
                (
                    "Timeout %s (h:mm:ss) waiting for pod %s to start. "
                    "Pod status:\n%s"
                ),
                timeout,
                pod_name,
                self._pretty_format_status(e.result()),
            )
            raise

    def port_forward_pod(
        self,
        pod: V1Pod,
        remote_port: int,
        local_port: Optional[int] = None,
        local_address: Optional[str] = None,
    ) -> k8s_port_forwarder.PortForwarder:
        pf = k8s_port_forwarder.PortForwarder(
            self._api.context,
            self.name,
            f"pod/{pod.metadata.name}",
            remote_port,
            local_port,
            local_address,
        )
        pf.connect()
        return pf

    def pod_start_logging(
        self,
        *,
        pod_name: str,
        log_path: pathlib.Path,
        log_stop_event: threading.Event,
        log_to_stdout: bool = False,
        log_timestamps: bool = False,
    ) -> PodLogCollector:
        pod_log_collector = PodLogCollector(
            pod_name=pod_name,
            namespace_name=self.name,
            read_pod_log_fn=self._api.core.read_namespaced_pod_log,
            stop_event=log_stop_event,
            log_path=log_path,
            log_to_stdout=log_to_stdout,
            log_timestamps=log_timestamps,
        )
        pod_log_collector.start()
        return pod_log_collector

    def _pretty_format_statuses(
        self, k8s_objects: List[Optional[object]]
    ) -> str:
        return "\n".join(
            self._pretty_format_status(k8s_object) for k8s_object in k8s_objects
        )

    def _pretty_format_status(self, k8s_object: Optional[object]) -> str:
        if k8s_object is None:
            return "No data"

        # Parse the name if present.
        if hasattr(k8s_object, "metadata") and hasattr(
            k8s_object.metadata, "name"
        ):
            name = k8s_object.metadata.name
        else:
            name = "Can't parse resource name"

        # Pretty-print the status if present.
        if hasattr(k8s_object, "status"):
            try:
                status = self._pretty_format(k8s_object.status.to_dict())
            except Exception as e:  # pylint: disable=broad-except
                # Catching all exceptions because not printing the status
                # isn't as important as the system under test.
                status = f"Can't parse resource status: {e}"
        else:
            status = "Can't parse resource status"

        # Return the name of k8s object, and its pretty-printed status.
        return f"{name}:\n{status}\n"

    def _pretty_format(self, data: dict) -> str:
        """Return a string with pretty-printed yaml data from a python dict."""
        yaml_out: str = yaml.dump(data, explicit_start=True, explicit_end=True)
        return self._highlighter.highlight(yaml_out)

    @classmethod
    def _check_service_neg_annotation(
        cls, service: Optional[V1Service]
    ) -> bool:
        return (
            isinstance(service, V1Service)
            and cls.NEG_STATUS_META in service.metadata.annotations
        )

    @classmethod
    def _pod_started(cls, pod: V1Pod) -> bool:
        return isinstance(pod, V1Pod) and pod.status.phase not in (
            "Pending",
            "Unknown",
        )

    @classmethod
    def _replicas_available(cls, deployment: V1Deployment, count: int) -> bool:
        return (
            isinstance(deployment, V1Deployment)
            and deployment.status.available_replicas is not None
            and deployment.status.available_replicas >= count
        )
