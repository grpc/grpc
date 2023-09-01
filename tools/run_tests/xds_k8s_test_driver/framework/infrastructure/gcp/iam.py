# Copyright 2021 gRPC authors.
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
import datetime
import functools
import logging
from typing import Any, Dict, FrozenSet, Optional

from framework.helpers import retryers
from framework.infrastructure import gcp

logger = logging.getLogger(__name__)

# Type aliases
_timedelta = datetime.timedelta
_HttpRequest = gcp.api.HttpRequest


class EtagConflict(gcp.api.Error):
    """
    Indicates concurrent policy changes.

    https://cloud.google.com/iam/docs/policies#etag
    """


def handle_etag_conflict(func):
    def wrap_retry_on_etag_conflict(*args, **kwargs):
        retryer = retryers.exponential_retryer_with_timeout(
            retry_on_exceptions=(EtagConflict, gcp.api.TransportError),
            wait_min=_timedelta(seconds=1),
            wait_max=_timedelta(seconds=10),
            timeout=_timedelta(minutes=2),
        )
        return retryer(func, *args, **kwargs)

    return wrap_retry_on_etag_conflict


def _replace_binding(
    policy: "Policy", binding: "Policy.Binding", new_binding: "Policy.Binding"
) -> "Policy":
    new_bindings = set(policy.bindings)
    new_bindings.discard(binding)
    new_bindings.add(new_binding)
    # pylint: disable=too-many-function-args # No idea why pylint is like that.
    return dataclasses.replace(policy, bindings=frozenset(new_bindings))


@dataclasses.dataclass(frozen=True)
class ServiceAccount:
    """An IAM service account.

    https://cloud.google.com/iam/docs/reference/rest/v1/projects.serviceAccounts
    Note: "etag" field is skipped because it's deprecated
    """

    name: str
    projectId: str
    uniqueId: str
    email: str
    oauth2ClientId: str
    displayName: str = ""
    description: str = ""
    disabled: bool = False

    @classmethod
    def from_response(cls, response: Dict[str, Any]) -> "ServiceAccount":
        return cls(
            name=response["name"],
            projectId=response["projectId"],
            uniqueId=response["uniqueId"],
            email=response["email"],
            oauth2ClientId=response["oauth2ClientId"],
            description=response.get("description", ""),
            displayName=response.get("displayName", ""),
            disabled=response.get("disabled", False),
        )

    def as_dict(self) -> Dict[str, Any]:
        return dataclasses.asdict(self)


@dataclasses.dataclass(frozen=True)
class Expr:
    """
    Represents a textual expression in the Common Expression Language syntax.

    https://cloud.google.com/iam/docs/reference/rest/v1/Expr
    """

    expression: str
    title: str = ""
    description: str = ""
    location: str = ""

    @classmethod
    def from_response(cls, response: Dict[str, Any]) -> "Expr":
        return cls(**response)

    def as_dict(self) -> Dict[str, Any]:
        return dataclasses.asdict(self)


@dataclasses.dataclass(frozen=True)
class Policy:
    """An Identity and Access Management (IAM) policy, which specifies
    access controls for Google Cloud resources.

    https://cloud.google.com/iam/docs/reference/rest/v1/Policy
    Note: auditConfigs not supported by this implementation.
    """

    @dataclasses.dataclass(frozen=True)
    class Binding:
        """Policy Binding. Associates members with a role.

        https://cloud.google.com/iam/docs/reference/rest/v1/Policy#binding
        """

        role: str
        members: FrozenSet[str]
        condition: Optional[Expr] = None

        @classmethod
        def from_response(cls, response: Dict[str, Any]) -> "Policy.Binding":
            fields = {
                "role": response["role"],
                "members": frozenset(response.get("members", [])),
            }
            if "condition" in response:
                fields["condition"] = Expr.from_response(response["condition"])

            return cls(**fields)

        def as_dict(self) -> Dict[str, Any]:
            result = {
                "role": self.role,
                "members": list(self.members),
            }
            if self.condition is not None:
                result["condition"] = self.condition.as_dict()
            return result

    bindings: FrozenSet[Binding]
    etag: str
    version: Optional[int] = None

    @functools.lru_cache(maxsize=128)
    def find_binding_for_role(
        self, role: str, condition: Optional[Expr] = None
    ) -> Optional["Policy.Binding"]:
        results = (
            binding
            for binding in self.bindings
            if binding.role == role and binding.condition == condition
        )
        return next(results, None)

    @classmethod
    def from_response(cls, response: Dict[str, Any]) -> "Policy":
        bindings = frozenset(
            cls.Binding.from_response(b) for b in response.get("bindings", [])
        )
        return cls(
            bindings=bindings,
            etag=response["etag"],
            version=response.get("version"),
        )

    def as_dict(self) -> Dict[str, Any]:
        result = {
            "bindings": [binding.as_dict() for binding in self.bindings],
            "etag": self.etag,
        }
        if self.version is not None:
            result["version"] = self.version
        return result


class IamV1(gcp.api.GcpProjectApiResource):
    """
    Identity and Access Management (IAM) API.

    https://cloud.google.com/iam/docs/reference/rest
    """

    _service_accounts: gcp.api.discovery.Resource

    # Operations that affect conditional role bindings must specify version 3.
    # Otherwise conditions are omitted, and role names returned with a suffix,
    # f.e. roles/iam.workloadIdentityUser_withcond_f1ec33c9beb41857dbf0
    # https://cloud.google.com/iam/docs/reference/rest/v1/Policy#FIELDS.version
    POLICY_VERSION: int = 3

    def __init__(self, api_manager: gcp.api.GcpApiManager, project: str):
        super().__init__(api_manager.iam("v1"), project)
        # Shortcut to projects/*/serviceAccounts/ endpoints
        self._service_accounts = self.api.projects().serviceAccounts()

    def service_account_resource_name(self, account) -> str:
        """
        Returns full resource name of the service account.

        The resource name of the service account in the following format:
        projects/{PROJECT_ID}/serviceAccounts/{ACCOUNT}.
        The ACCOUNT value can be the email address or the uniqueId of the
        service account.
        Ref https://cloud.google.com/iam/docs/reference/rest/v1/projects.serviceAccounts/get

        Args:
            account: The ACCOUNT value
        """
        return f"projects/{self.project}/serviceAccounts/{account}"

    def get_service_account(self, account: str) -> ServiceAccount:
        resource_name = self.service_account_resource_name(account)
        request: _HttpRequest = self._service_accounts.get(name=resource_name)
        response: Dict[str, Any] = self._execute(request)
        logger.debug(
            "Loaded Service Account:\n%s", self.resource_pretty_format(response)
        )
        return ServiceAccount.from_response(response)

    def get_service_account_iam_policy(self, account: str) -> Policy:
        resource_name = self.service_account_resource_name(account)
        request: _HttpRequest = self._service_accounts.getIamPolicy(
            resource=resource_name,
            options_requestedPolicyVersion=self.POLICY_VERSION,
        )
        response: Dict[str, Any] = self._execute(request)
        logger.debug(
            "Loaded Service Account Policy:\n%s",
            self.resource_pretty_format(response),
        )
        return Policy.from_response(response)

    def set_service_account_iam_policy(
        self, account: str, policy: Policy
    ) -> Policy:
        """Sets the IAM policy that is attached to a service account.

        https://cloud.google.com/iam/docs/reference/rest/v1/projects.serviceAccounts/setIamPolicy
        """
        resource_name = self.service_account_resource_name(account)
        body = {"policy": policy.as_dict()}
        logger.debug(
            "Updating Service Account %s policy:\n%s",
            account,
            self.resource_pretty_format(body),
        )
        try:
            request: _HttpRequest = self._service_accounts.setIamPolicy(
                resource=resource_name, body=body
            )
            response: Dict[str, Any] = self._execute(request)
            return Policy.from_response(response)
        except gcp.api.ResponseError as error:
            if error.status == 409:
                # https://cloud.google.com/iam/docs/policies#etag
                logger.debug(error)
                raise EtagConflict from error
            raise

    @handle_etag_conflict
    def add_service_account_iam_policy_binding(
        self, account: str, role: str, member: str
    ) -> None:
        """Add an IAM policy binding to an IAM service account.

        See for details on updating policy bindings:
        https://cloud.google.com/iam/docs/reference/rest/v1/projects.serviceAccounts/setIamPolicy
        """
        policy: Policy = self.get_service_account_iam_policy(account)
        binding: Optional[Policy.Binding] = policy.find_binding_for_role(role)
        if binding and member in binding.members:
            logger.debug(
                "Member %s already has role %s for Service Account %s",
                member,
                role,
                account,
            )
            return

        if binding is None:
            updated_binding = Policy.Binding(role, frozenset([member]))
        else:
            updated_members: FrozenSet[str] = binding.members.union({member})
            updated_binding: Policy.Binding = (
                dataclasses.replace(  # pylint: disable=too-many-function-args
                    binding, members=updated_members
                )
            )

        updated_policy: Policy = _replace_binding(
            policy, binding, updated_binding
        )
        self.set_service_account_iam_policy(account, updated_policy)
        logger.debug(
            "Role %s granted to member %s for Service Account %s",
            role,
            member,
            account,
        )

    @handle_etag_conflict
    def remove_service_account_iam_policy_binding(
        self, account: str, role: str, member: str
    ) -> None:
        """Remove an IAM policy binding from the IAM policy of a service
        account.

        See for details on updating policy bindings:
        https://cloud.google.com/iam/docs/reference/rest/v1/projects.serviceAccounts/setIamPolicy
        """
        policy: Policy = self.get_service_account_iam_policy(account)
        binding: Optional[Policy.Binding] = policy.find_binding_for_role(role)

        if binding is None:
            logger.debug(
                "Noop: Service Account %s has no bindings for role %s",
                account,
                role,
            )
            return
        if member not in binding.members:
            logger.debug(
                "Noop: Service Account %s binding for role %s has no member %s",
                account,
                role,
                member,
            )
            return

        updated_members: FrozenSet[str] = binding.members.difference({member})
        updated_binding: Policy.Binding = (
            dataclasses.replace(  # pylint: disable=too-many-function-args
                binding, members=updated_members
            )
        )
        updated_policy: Policy = _replace_binding(
            policy, binding, updated_binding
        )
        self.set_service_account_iam_policy(account, updated_policy)
        logger.debug(
            "Role %s revoked from member %s for Service Account %s",
            role,
            member,
            account,
        )
