# Copyright 2015 gRPC authors.
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

import collections
import threading

import grpc
from grpc import _common
from grpc._cython import cygrpc


class AuthMetadataContext(
        collections.namedtuple('AuthMetadataContext', (
            'service_url', 'method_name',)), grpc.AuthMetadataContext):
    pass


class AuthMetadataPluginCallback(grpc.AuthMetadataContext):

    def __init__(self, callback):
        self._callback = callback

    def __call__(self, metadata, error):
        self._callback(metadata, error)


class _WrappedCygrpcCallback(object):

    def __init__(self, cygrpc_callback):
        self.is_called = False
        self.error = None
        self.is_called_lock = threading.Lock()
        self.cygrpc_callback = cygrpc_callback

    def _invoke_failure(self, error):
        # TODO(atash) translate different Exception superclasses into different
        # status codes.
        self.cygrpc_callback(_common.EMPTY_METADATA, cygrpc.StatusCode.internal,
                             _common.encode(str(error)))

    def _invoke_success(self, metadata):
        try:
            cygrpc_metadata = _common.to_cygrpc_metadata(metadata)
        except Exception as exception:  # pylint: disable=broad-except
            self._invoke_failure(exception)
            return
        self.cygrpc_callback(cygrpc_metadata, cygrpc.StatusCode.ok, b'')

    def __call__(self, metadata, error):
        with self.is_called_lock:
            if self.is_called:
                raise RuntimeError('callback should only ever be invoked once')
            if self.error:
                self._invoke_failure(self.error)
                return
            self.is_called = True
        if error is None:
            self._invoke_success(metadata)
        else:
            self._invoke_failure(error)

    def notify_failure(self, error):
        with self.is_called_lock:
            if not self.is_called:
                self.error = error


class _WrappedPlugin(object):

    def __init__(self, plugin):
        self.plugin = plugin

    def __call__(self, context, cygrpc_callback):
        wrapped_cygrpc_callback = _WrappedCygrpcCallback(cygrpc_callback)
        wrapped_context = AuthMetadataContext(
            _common.decode(context.service_url),
            _common.decode(context.method_name))
        try:
            self.plugin(wrapped_context,
                        AuthMetadataPluginCallback(wrapped_cygrpc_callback))
        except Exception as error:
            wrapped_cygrpc_callback.notify_failure(error)
            raise


def call_credentials_metadata_plugin(plugin, name):
    """
  Args:
    plugin: A callable accepting a grpc.AuthMetadataContext
      object and a callback (itself accepting a list of metadata key/value
      2-tuples and a None-able exception value). The callback must be eventually
      called, but need not be called in plugin's invocation.
      plugin's invocation must be non-blocking.
  """
    return cygrpc.call_credentials_metadata_plugin(
        cygrpc.CredentialsMetadataPlugin(
            _WrappedPlugin(plugin), _common.encode(name)))
