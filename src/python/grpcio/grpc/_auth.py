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

import inspect
from concurrent import futures

import grpc


def _sign_request(callback, token, error):
    metadata = (('authorization', 'Bearer {}'.format(token)),)
    callback(metadata, error)


def _create_get_token_callback(callback):

    def get_token_callback(future):
        try:
            access_token = future.result().access_token
        except Exception as exception:  # pylint: disable=broad-except
            _sign_request(callback, None, exception)
        else:
            _sign_request(callback, access_token, None)

    return get_token_callback


class GoogleCallCredentials(grpc.AuthMetadataPlugin):
    """Metadata wrapper for GoogleCredentials from the oauth2client library."""

    def __init__(self, credentials):
        self._credentials = credentials
        self._pool = futures.ThreadPoolExecutor(max_workers=1)

        # Hack to determine if these are JWT creds and we need to pass
        # additional_claims when getting a token
        self._is_jwt = 'additional_claims' in inspect.getargspec(
            credentials.get_access_token).args

    def __call__(self, context, callback):
        # MetadataPlugins cannot block (see grpc.beta.interfaces.py)
        if self._is_jwt:
            future = self._pool.submit(
                self._credentials.get_access_token,
                additional_claims={
                    'aud': context.service_url
                })
        else:
            future = self._pool.submit(self._credentials.get_access_token)
        future.add_done_callback(_create_get_token_callback(callback))

    def __del__(self):
        self._pool.shutdown(wait=False)


class AccessTokenAuthMetadataPlugin(grpc.AuthMetadataPlugin):
    """Metadata wrapper for raw access token credentials."""

    def __init__(self, access_token):
        self._access_token = access_token

    def __call__(self, context, callback):
        _sign_request(callback, self._access_token, None)
