# Copyright 2015-2016, Google Inc.
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

import pkg_resources
import threading

from grpc import _grpcio_metadata
from grpc._cython import cygrpc
from grpc._adapter import _implementations
from grpc._adapter import _types

_ROOT_CERTIFICATES_RESOURCE_PATH = 'credentials/roots.pem'
_USER_AGENT = 'Python-gRPC-{}'.format(_grpcio_metadata.__version__)

ChannelCredentials = cygrpc.ChannelCredentials
CallCredentials = cygrpc.CallCredentials
ServerCredentials = cygrpc.ServerCredentials

channel_credentials_composite = cygrpc.channel_credentials_composite
call_credentials_composite = cygrpc.call_credentials_composite

def server_credentials_ssl(root_credentials, pair_sequence, force_client_auth):
  return cygrpc.server_credentials_ssl(
      root_credentials,
      [cygrpc.SslPemKeyCertPair(key, pem) for key, pem in pair_sequence],
      force_client_auth)

def channel_credentials_ssl(
    root_certificates, private_key, certificate_chain):
  pair = None
  if private_key is not None or certificate_chain is not None:
    pair = cygrpc.SslPemKeyCertPair(private_key, certificate_chain)
  if root_certificates is None:
    root_certificates = pkg_resources.resource_string(
      __name__, _ROOT_CERTIFICATES_RESOURCE_PATH)
  return cygrpc.channel_credentials_ssl(root_certificates, pair)


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
    wrapped_context = _implementations.AuthMetadataContext(context.service_url,
                                                           context.method_name)
    try:
      self.plugin(
          wrapped_context,
          _implementations.AuthMetadataPluginCallback(wrapped_cygrpc_callback))
    except Exception as error:
      wrapped_cygrpc_callback.notify_failure(error)
      raise


def call_credentials_metadata_plugin(plugin, name):
  """
  Args:
    plugin: A callable accepting a _types.AuthMetadataContext
      object and a callback (itself accepting a list of metadata key/value
      2-tuples and a None-able exception value). The callback must be eventually
      called, but need not be called in plugin's invocation.
      plugin's invocation must be non-blocking.
  """
  return cygrpc.call_credentials_metadata_plugin(
      cygrpc.CredentialsMetadataPlugin(_WrappedPlugin(plugin), name))


class CompletionQueue(_types.CompletionQueue):

  def __init__(self):
    self.completion_queue = cygrpc.CompletionQueue()

  def next(self, deadline=float('+inf')):
    raw_event = self.completion_queue.poll(cygrpc.Timespec(deadline))
    if raw_event.type == cygrpc.CompletionType.queue_timeout:
      return None
    event_type = raw_event.type
    event_tag = raw_event.tag
    event_call = Call(raw_event.operation_call)
    if raw_event.request_call_details:
      event_call_details = _types.CallDetails(
          raw_event.request_call_details.method,
          raw_event.request_call_details.host,
          float(raw_event.request_call_details.deadline))
    else:
      event_call_details = None
    event_success = raw_event.success
    event_results = []
    if raw_event.is_new_request:
      event_results.append(_types.OpResult(
          _types.OpType.RECV_INITIAL_METADATA, raw_event.request_metadata,
          None, None, None, None))
    else:
      if raw_event.batch_operations:
        for operation in raw_event.batch_operations:
          result_type = operation.type
          result_initial_metadata = operation.received_metadata_or_none
          result_trailing_metadata = operation.received_metadata_or_none
          result_message = operation.received_message_or_none
          if result_message is not None:
            result_message = result_message.bytes()
          result_cancelled = operation.received_cancelled_or_none
          if operation.has_status:
            result_status = _types.Status(
                operation.received_status_code_or_none,
                operation.received_status_details_or_none)
          else:
            result_status = None
          event_results.append(
              _types.OpResult(result_type, result_initial_metadata,
                              result_trailing_metadata, result_message,
                              result_status, result_cancelled))
    return _types.Event(event_type, event_tag, event_call, event_call_details,
                        event_results, event_success)

  def shutdown(self):
    self.completion_queue.shutdown()


class Call(_types.Call):

  def __init__(self, call):
    self.call = call

  def start_batch(self, ops, tag):
    translated_ops = []
    for op in ops:
      if op.type == _types.OpType.SEND_INITIAL_METADATA:
        translated_op = cygrpc.operation_send_initial_metadata(
            cygrpc.Metadata(
                cygrpc.Metadatum(key, value)
                for key, value in op.initial_metadata))
      elif op.type == _types.OpType.SEND_MESSAGE:
        translated_op = cygrpc.operation_send_message(op.message)
      elif op.type == _types.OpType.SEND_CLOSE_FROM_CLIENT:
        translated_op = cygrpc.operation_send_close_from_client()
      elif op.type == _types.OpType.SEND_STATUS_FROM_SERVER:
        translated_op = cygrpc.operation_send_status_from_server(
            cygrpc.Metadata(
                cygrpc.Metadatum(key, value)
                for key, value in op.trailing_metadata),
            op.status.code,
            op.status.details)
      elif op.type == _types.OpType.RECV_INITIAL_METADATA:
        translated_op = cygrpc.operation_receive_initial_metadata()
      elif op.type == _types.OpType.RECV_MESSAGE:
        translated_op = cygrpc.operation_receive_message()
      elif op.type == _types.OpType.RECV_STATUS_ON_CLIENT:
        translated_op = cygrpc.operation_receive_status_on_client()
      elif op.type == _types.OpType.RECV_CLOSE_ON_SERVER:
        translated_op = cygrpc.operation_receive_close_on_server()
      else:
        raise ValueError('unexpected operation type {}'.format(op.type))
      translated_ops.append(translated_op)
    return self.call.start_batch(cygrpc.Operations(translated_ops), tag)

  def cancel(self, code=None, details=None):
    if code is None and details is None:
      return self.call.cancel()
    else:
      return self.call.cancel(code, details)

  def peer(self):
    return self.call.peer()

  def set_credentials(self, creds):
    return self.call.set_credentials(creds)


class Channel(_types.Channel):

  def __init__(self, target, args, creds=None):
    args = list(args) + [
        (cygrpc.ChannelArgKey.primary_user_agent_string, _USER_AGENT)]
    args = cygrpc.ChannelArgs(
        cygrpc.ChannelArg(key, value) for key, value in args)
    if creds is None:
      self.channel = cygrpc.Channel(target, args)
    else:
      self.channel = cygrpc.Channel(target, args, creds)

  def create_call(self, completion_queue, method, host, deadline=None):
    internal_call = self.channel.create_call(
        None, 0, completion_queue.completion_queue, method, host,
        cygrpc.Timespec(deadline))
    return Call(internal_call)

  def check_connectivity_state(self, try_to_connect):
    return self.channel.check_connectivity_state(try_to_connect)

  def watch_connectivity_state(self, last_observed_state, deadline,
                               completion_queue, tag):
    self.channel.watch_connectivity_state(
        last_observed_state, cygrpc.Timespec(deadline),
        completion_queue.completion_queue, tag)

  def target(self):
    return self.channel.target()


_NO_TAG = object()

class Server(_types.Server):

  def __init__(self, completion_queue, args):
    args = cygrpc.ChannelArgs(
        cygrpc.ChannelArg(key, value) for key, value in args)
    self.server = cygrpc.Server(args)
    self.server.register_completion_queue(completion_queue.completion_queue)
    self.server_queue = completion_queue

  def add_http2_port(self, addr, creds=None):
    if creds is None:
      return self.server.add_http2_port(addr)
    else:
      return self.server.add_http2_port(addr, creds)

  def start(self):
    return self.server.start()

  def shutdown(self, tag=None):
    return self.server.shutdown(self.server_queue.completion_queue, tag)

  def request_call(self, completion_queue, tag):
    return self.server.request_call(completion_queue.completion_queue,
                                    self.server_queue.completion_queue, tag)

  def cancel_all_calls(self):
    return self.server.cancel_all_calls()
