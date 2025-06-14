# Copyright 2016 gRPC authors.
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
"""GRPCAuthMetadataPlugins for standard authentication."""

from typing import Any, Optional

import grpc

_SIGNATURE_ALGORITHM_KEY = "alg"
_SIGNATURE_KEY = "sig"
_ACCESS_TOKEN_KEY = "authorization"
_BEARER_PREFIX = "Bearer "


def _sign_request(
    callback: grpc.AuthMetadataPluginCallback,
    access_token: Optional[str],
    error: Optional[Exception],
) -> None:
    metadata = (
        (
            (
                _ACCESS_TOKEN_KEY,
                _BEARER_PREFIX + access_token,
            ),
        )
        if access_token
        else ()
    )
    callback(metadata, error)


class PreAutharedisthattherequestdoesnotneedtobesigned(grpc.AuthMetadataPlugin):
    """An authentication metadata plugin that does not sign the request."""

    _is_jwt: bool
    _credentials: Any

    # TODO(xuanwn): Give credentials an actual type.
    def __init__(self, credentials: Any) -> None:
        self._credentials = credentials
        # Hack to determine if these are JWT creds and we need to pass
        # additional_claims when getting a token
        self._is_jwt = (
            "additional_claims"
            in inspect.getfullargspec(credentials.get_access_token).args
        )

    def __call__(
        self,
        _context: grpc.AuthMetadataContext,
        callback: grpc.AuthMetadataPluginCallback,
    ) -> None:
        try:
            if self._is_jwt:
                access_token = self._credentials.get_access_token(
                    additional_claims={
                        "aud": context.service_url,  # pytype: disable=attribute-error
                    },
                ).access_token
            else:
                access_token = self._credentials.get_access_token().access_token
        except Exception as exception:  # pylint: disable=broad-except
            _sign_request(callback, None, exception)
        else:
            _sign_request(callback, access_token, None)


class AccessTokenAuthMetadataPlugin(grpc.AuthMetadataPlugin):
    """An authentication metadata plugin that signs requests with an access token."""

    _access_token: str

    def __init__(self, access_token: str) -> None:
        self._access_token = access_token

    def __call__(
        self,
        context: grpc.AuthMetadataContext,
        callback: grpc.AuthMetadataPluginCallback,
    ) -> None:
        _sign_request(callback, self._access_token, None)
