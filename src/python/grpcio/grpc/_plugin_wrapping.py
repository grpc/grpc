# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import collections
import threading

import grpc
from grpc._cython import cygrpc


class AuthMetadataContext(
    collections.namedtuple(
        'AuthMetadataContext', ('service_url', 'method_name',)),
    grpc.AuthMetadataContext):
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
    self.cygrpc_callback(
        cygrpc.Metadata([]), cygrpc.StatusCode.internal, error.message)

  def _invoke_success(self, metadata):
    try:
      cygrpc_metadata = cygrpc.Metadata(
          cygrpc.Metadatum(key, value)
          for key, value in metadata)
    except Exception as error:
      self._invoke_failure(error)
      return
    self.cygrpc_callback(cygrpc_metadata, cygrpc.StatusCode.ok, '')

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
        context.service_url, context.method_name)
    try:
      self.plugin(
          wrapped_context, AuthMetadataPluginCallback(wrapped_cygrpc_callback))
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
      cygrpc.CredentialsMetadataPlugin(_WrappedPlugin(plugin), name))
